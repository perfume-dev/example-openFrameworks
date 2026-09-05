"""Independently validate complete references before making a graphics package.

Copyright (c) 2026 Daito Manabe. MIT; new code only, not motion/model assets.
"""
import argparse
import hashlib
import json
from pathlib import Path
import xml.etree.ElementTree as ET

import mujoco
import numpy as np

from retarget import GATES, SIDES, mesh_export, prepare_targets, source_arrays, unit, write_json

MODEL_SHA = '3c2616550a31f33e84d3c80b8e913ac5618c8888019b0c9490dae93493e647f3'
MODEL_REV = '8161bba264d7fa7c99ca301e91e7fb44737676ad'
URDF_SHA = 'c0ae739c640c3e2c00d1bdd8810b5d6e59601487bd1a3995859f9543269ee5c8'
URDF_REV = '7d6075f7f58588b189b940130e3edab3c839b2df'
MODEL_MANIFEST_SHA = '2a0904330a305220e7f7aedf5041f6f91192258d03ab01790432db6e653479f8'
SOURCE_SHAS = {
    'A': 'bb53e8e80491830b5ad1ae2a649d44aa7c65e683161d3b456c9279a533ec563e',
    'B': '74811741939eaf3ff7e41336ed1bacc5f287879d58dc61d1d738d1c69df7725c',
    'C': '7d99f23d74a241a7ce2cb3c12004b6b9951866c7e65fa0d1150ee6e39435b469',
}


def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def checked_speed_limits(speeds):
    speeds = np.asarray(speeds, dtype=float)
    if speeds.shape != (29,) or not np.all(np.isfinite(speeds) & (speeds > 0)):
        raise ValueError('Expected 29 finite positive manufacturer velocity limits')
    return speeds


def verify_model_assets(directory, expected_manifest_sha=MODEL_MANIFEST_SHA):
    manifest_path = directory / 'manifest.json'
    if digest(manifest_path) != expected_manifest_sha:
        raise ValueError('Model asset manifest is not the verified pinned manifest')
    manifest = json.loads(manifest_path.read_text())
    hashes = {}
    for item in manifest['files']:
        relative = Path(item['path'])
        if relative.is_absolute() or '..' in relative.parts or relative.as_posix() in hashes:
            raise ValueError('Unsafe or repeated model asset path')
        path = directory / relative
        if not path.resolve().is_relative_to(directory.resolve()):
            raise ValueError('Model asset escapes the model directory')
        if path.stat().st_size != item['bytes'] or digest(path) != item['sha256']:
            raise ValueError('Model asset content differs from the pinned manifest: ' + relative.as_posix())
        hashes[relative.as_posix()] = item['sha256']
    return hashes


def validate_reference(archive, expected_frames, expected_fps, joint_names, body_names):
    """Reject partial clips, reordered/invalid poses, or fabricated time coverage."""
    qpos = np.asarray(archive['qpos'], dtype=float)
    fps = float(archive['fps'])
    indices = np.asarray(archive['source_frame_indices'])
    if qpos.shape != (expected_frames - 1, 36) or not np.all(np.isfinite(qpos)):
        raise ValueError('Expected complete finite frame-1-onward [N,36] poses')
    if not np.isfinite(fps) or abs(fps - expected_fps) > 1e-9:
        raise ValueError('Reference must retain original source sampling rate')
    if not np.array_equal(indices, np.arange(1, expected_frames)):
        raise ValueError('Missing, reordered or repeated original source frame indices')
    if 'time' in archive:
        times = np.asarray(archive['time'], dtype=float)
        if times.shape != indices.shape or not np.all(np.isfinite(times)) or not np.allclose(times, indices / fps, atol=1e-9, rtol=0):
            raise ValueError('Explicit reference timestamps disagree with source frame indices')
    if list(archive['joint_names']) != joint_names or list(archive['body_names']) != body_names:
        raise ValueError('Reference model joint/body order mismatch')
    if np.max(np.abs(np.linalg.norm(qpos[:, 3:7], axis=1) - 1.)) > 1e-6:
        raise ValueError('Root quaternions must be normalized WXYZ')
    return qpos, indices


def evaluate(model, source, reference, speed_limits):
    if source.stem[0] not in SOURCE_SHAS or digest(source) != SOURCE_SHAS[source.stem[0]]:
        raise ValueError('Source is not the pinned original A/B/C recording')
    motion, names, pos, rot, rest = source_arrays(source)
    if len(motion.frames) != 1300 or abs(motion.frame_time - .025) > 1e-12:
        raise ValueError('Original recording must contain 1300 frames at 40 Hz')
    speed_limits = checked_speed_limits(speed_limits)
    target, _, source_display, chains, calibration = prepare_targets(model, names, pos, rot, rest)
    joints = [model.joint(i).name for i in range(1, model.njnt)]
    bodies = [model.body(i).name for i in range(1, model.nbody)]
    with np.load(reference, allow_pickle=False) as archive:
        qpos, indices = validate_reference(archive, len(motion.frames), 1 / motion.frame_time, joints, bodies)
    data = mujoco.MjData(model)
    ids = {n: model.body(n).id for n in target}
    source_ids = {n: i for i, n in enumerate(names)}
    checked_targets = [n for n in target if 'hip_pitch' not in n and 'shoulder_pitch' not in n]
    soles = [g for g in range(model.ngeom) if model.geom_type[g] == mujoco.mjtGeom.mjGEOM_SPHERE
             and model.body(model.geom_bodyid[g]).name in [f'{s}_ankle_roll_link' for s in SIDES]]
    angles, errors, penetrations, frames = [], [], [], []
    for q, f in zip(qpos, indices):
        data.qpos[:] = q
        mujoco.mj_forward(model, data)
        angles.append([float(np.degrees(np.arccos(np.clip(np.dot(
            unit(data.xpos[ids[b]] - data.xpos[ids[a]]),
            unit(pos[f, source_ids[sb]] - pos[f, source_ids[sa]])), -1., 1.))))
            for a, b, sa, sb in chains])
        errors.append([float(np.linalg.norm(data.xpos[ids[n]] - target[n][f])) for n in checked_targets])
        penetrations.append(max(0., -float(np.min(data.geom_xpos[soles, 2] - model.geom_size[soles, 0]))))
        frames.append({'positions': data.xpos[1:].round(5).ravel().tolist(),
                       'rotations': data.xquat[1:].round(6).ravel().tolist(),
                       'source_positions': source_display[f].round(5).ravel().tolist()})
    angles, errors = np.asarray(angles), np.asarray(errors)
    bounds = model.jnt_range[1:]
    metrics = {
        'mean_limb_direction_degrees': float(angles.mean()),
        'p95_limb_direction_degrees': float(np.percentile(angles, 95)),
        'max_joint_limit_violation_rad': max(0., float(np.max(bounds[:, 0] - qpos[:, 7:])),
                                              float(np.max(qpos[:, 7:] - bounds[:, 1]))),
        'max_joint_velocity_limit_ratio': float(np.max(np.abs(np.diff(qpos[:, 7:], axis=0))
                                                      / motion.frame_time / speed_limits)),
        'max_foot_penetration_m': max(penetrations),
        'max_target_position_error_m': float(errors.max()),
    }
    failed = [key for key, value in metrics.items() if value > GATES[key]]
    report = {'status': 'fail' if failed else 'pass', 'thresholds': GATES, 'metrics': metrics,
              'failed_gates': failed, 'frames': len(qpos), 'fps': 1 / motion.frame_time,
              'source_frame_range_inclusive': [int(indices[0]), int(indices[-1])],
              'source_time_offset_s': motion.frame_time, 'last_source_time_s': int(indices[-1]) * motion.frame_time,
              'playback_sample_span_s': (len(qpos) - 1) * motion.frame_time,
              'source_sha256': motion.sha256, 'reference_sha256': digest(reference),
              'per_chain_p95_degrees': dict(zip([f'{sa}->{sb}' for _, _, sa, sb in chains],
                                              np.percentile(angles, 95, axis=0).tolist())),
              'per_target_max_position_error_m': dict(zip(checked_targets, errors.max(axis=0).tolist())),
              'calibration': calibration, 'physical_tracking_validated': False,
              'limitations': ['Pose gates are engineering reference criteria, not hardware safety',
                              'Human motion is adapted to robot morphology, not exactly reproduced',
                              'Self-collision, dynamic balance and finite-force tracking are not approved']}
    return {'name': source.stem[0], 'frames': frames}, names, [j.parent if j.parent is not None else -1 for j in motion.joints], report


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--model', required=True, type=Path)
    parser.add_argument('--source', required=True, type=Path)
    for letter in 'abc':
        parser.add_argument('--' + letter, required=True, type=Path)
    parser.add_argument('--output', required=True, type=Path)
    args = parser.parse_args()
    if digest(args.model / 'g1.xml') != MODEL_SHA:
        raise ValueError('Model is not the pinned G1 revision')
    if digest(args.model / 'g1_29dof_rev_1_0.urdf') != URDF_SHA:
        raise ValueError('Manufacturer velocity-limit URDF is not the pinned revision')
    model_asset_hashes = verify_model_assets(args.model)
    args.output.mkdir(parents=True, exist_ok=False)
    model = mujoco.MjModel.from_xml_path(str(args.model / 'g1.xml'))
    urdf = ET.parse(args.model / 'g1_29dof_rev_1_0.urdf')
    velocity = {j.get('name'): float(j.find('limit').get('velocity')) for j in urdf.findall('joint')
                if j.get('type') == 'revolute' and j.find('limit') is not None}
    speeds = checked_speed_limits([velocity[model.joint(i).name] for i in range(1, model.njnt)])
    clips, reports = [], {}
    canonical_names, canonical_parents = None, None
    for letter in 'ABC':
        clip, names, parents, report = evaluate(model, args.source / (letter + '_test.bvh'),
                                               getattr(args, letter.lower()), speeds)
        clips.append(clip)
        if canonical_names is not None and (names != canonical_names or parents != canonical_parents):
            raise ValueError('Source hierarchies differ across A/B/C')
        canonical_names, canonical_parents = names, parents
        reports[letter] = report
        print(json.dumps({'clip': letter, 'status': report['status'], 'metrics': report['metrics']}), flush=True)
    passed = all(r['status'] == 'pass' for r in reports.values())
    write_json(args.output / 'pose_retarget_qa.json', {'status': 'pass' if passed else 'fail',
               'physical_tracking_validated': False, 'clips': reports})
    if not passed:
        raise SystemExit('Pose gates failed: no viewer motion/model package emitted')
    mesh_export(model, args.output)
    (args.output / 'MODEL-LICENSE').write_bytes((args.model / 'LICENSE').read_bytes())
    write_json(args.output / 'g1-motion.json', {'schema_version': 1, 'fps': reports['A']['fps'],
        'body_names': [model.body(i).name for i in range(1, model.nbody)],
        'body_parents': [int(model.body_parentid[i]) - 1 for i in range(1, model.nbody)],
        'body_offsets': model.body_pos[1:].round(9).tolist(),
        'source_names': names, 'source_parents': parents, 'clips': clips,
        'source_start_frame': 1, 'source_time_offset_s': 1 / reports['A']['fps'],
        'validation': {'pose_status': 'pass', 'label': 'KINEMATIC REFERENCE — PHYSICS NOT VALIDATED'}}, True)
    write_json(args.output / 'provenance.json', {'model_repository': 'google-deepmind/mujoco_menagerie',
        'model_commit': MODEL_REV, 'model_path': 'unitree_g1/g1.xml', 'model_sha256': MODEL_SHA,
        'velocity_limit_urdf_repository': 'unitreerobotics/unitree_ros',
        'velocity_limit_urdf_commit': URDF_REV, 'velocity_limit_urdf_sha256': URDF_SHA,
        'model_asset_manifest_sha256': MODEL_MANIFEST_SHA, 'model_assets_sha256': model_asset_hashes,
        'model_license': 'BSD-3-Clause; Unitree; see MODEL-LICENSE',
        'motion_rights': 'Original Perfume performance data terms remain unchanged; not covered by the code MIT license',
        'source_repository': 'perfume-dev/example-openFrameworks', 'source_path': 'example-bvh/bin/data',
        'source_sha256': {k: v['source_sha256'] for k, v in reports.items()},
        'reference_sha256': {k: v['reference_sha256'] for k, v in reports.items()},
        'files_sha256': {p.name: digest(p) for p in args.output.iterdir() if p.is_file()},
        'coordinate_convention': 'Metres; Z up; robot forward +X; body world quaternions WXYZ',
        'viewer_interpolation': 'Root world interpolation; parent-relative quaternion interpolation with fixed model body offsets',
        'source_frame0_excluded': True, 'source_time_offset_s': .025,
        'validation_scope': 'Complete 40 Hz constrained kinematic reference; NOT physical robot motion'})


if __name__ == '__main__':
    main()
