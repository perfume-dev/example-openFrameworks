"""Perfume BVH -> pinned Unitree G1: constrained kinematic references, NOT control."""
import argparse
import hashlib
import importlib.metadata
import json
import os
from pathlib import Path
import sys
import time
import xml.etree.ElementTree as ET

import mink
import mujoco
import numpy as np
from scipy.spatial import ConvexHull
from scipy.spatial.transform import Rotation
from scipy.optimize import least_squares

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'preflight'))
from bvh_audit import load_bvh, fk

BASIS = np.array([[0., 0., 1.], [1., 0., 0.], [0., 1., 0.]])
SIDES = ('left', 'right')
# Preregistered gates for a constrained robot reference, not a same-rig avatar.
# G1 elbow/ankle ranges cannot reproduce every human straight-limb pose.
GATES = {'mean_limb_direction_degrees': 18., 'p95_limb_direction_degrees': 40.,
         'max_joint_limit_violation_rad': 1e-5, 'max_joint_velocity_limit_ratio': 1.00001,
         'max_foot_penetration_m': .025, 'max_target_position_error_m': .18}


def unit(v):
    return v / max(float(np.linalg.norm(v)), 1e-9)


def transform(rotation, position):
    xyzw = Rotation.from_matrix(rotation).as_quat()
    return mink.SE3(np.r_[xyzw[3], xyzw[:3], position])


def write_json(path, obj, compact=False):
    path.write_text(json.dumps(obj, indent=None if compact else 2,
                               separators=(',', ':') if compact else None, allow_nan=False) + '\n')


def mesh_export(model, output):
    meshes = []
    for geom in range(model.ngeom):
        if model.geom_type[geom] != mujoco.mjtGeom.mjGEOM_MESH or model.geom_group[geom] != 2:
            continue
        mesh = model.geom_dataid[geom]
        address, count = model.mesh_vertadr[mesh], model.mesh_vertnum[mesh]
        # Quantize before triangulation, so written vertices and the hull's
        # winding/area calculations use exactly the same geometry.
        vertices = np.asarray(model.mesh_vert[address:address + count], dtype=float).round(9)
        # Convex hull is a deliberately simplified visual representation, never a
        # replacement for the original simulator's collision/inertial model.
        hull = ConvexHull(vertices)
        used = sorted(set(hull.simplices.reshape(-1).tolist()))
        remap = {old: new for new, old in enumerate(used)}
        faces = []
        for tri, equation in zip(hull.simplices, hull.equations):
            a, b, c = tri
            if np.dot(np.cross(vertices[b] - vertices[a], vertices[c] - vertices[a]), equation[:3]) < 0:
                b, c = c, b
            faces.extend(remap[int(v)] for v in (a, b, c))
        meshes.append({'body': int(model.geom_bodyid[geom]) - 1,
                       'vertices': vertices[used].ravel().tolist(), 'indices': faces,
                       'position': model.geom_pos[geom].round(7).tolist(),
                       'quaternion': model.geom_quat[geom].round(8).tolist(),
                       'color': (model.mat_rgba[model.geom_matid[geom]] if model.geom_matid[geom] >= 0
                                 else model.geom_rgba[geom]).round(4).tolist()})
    write_json(output / 'g1-model.json', {'schema_version': 1, 'license': 'BSD-3-Clause',
               'representation': 'convex-hull visual study, not collision geometry', 'meshes': meshes}, True)
    return len(meshes)


def source_arrays(path):
    motion = load_bvh(path)
    samples = [fk(motion, row) for row in motion.frames]
    pos = np.asarray([s[0] for s in samples]) @ BASIS.T * .01
    raw_rot = np.asarray([s[1] for s in samples]).reshape(len(samples), len(motion.joints), 3, 3)
    rot = BASIS @ raw_rot @ BASIS.T
    rest, _ = fk(motion, [0.] * motion.channel_count)
    rest = np.asarray(rest) @ BASIS.T * .01
    names = [j.name for j in motion.joints]
    return motion, names, pos, rot, rest


def prepare_targets(model, source_names, pos, rot, rest):
    idx = {name: i for i, name in enumerate(source_names)}
    data = mujoco.MjData(model)
    mujoco.mj_forward(model, data)
    rp = {model.body(i).name: data.xpos[i].copy() for i in range(1, model.nbody)}
    # Scale locomotion by the ratio of pelvis-to-ankle chain lengths. Limb
    # segments are then fitted individually to the actual robot dimensions.
    robot_leg = sum(np.linalg.norm(rp[a] - rp[b]) for a, b in
                    [('left_hip_pitch_link', 'left_knee_link'),
                     ('left_knee_link', 'left_ankle_roll_link')])
    human_leg = sum(np.linalg.norm(rest[idx[a]] - rest[idx[b]]) for a, b in
                    [('LeftHip', 'LeftKnee'), ('LeftKnee', 'LeftAnkle')])
    scale = robot_leg / human_leg
    root = pos[:, idx['Hips']] * scale
    root[:, :2] -= root[1, :2]
    roots = rot[:, idx['Hips']]
    torsos = rot[:, idx['Chest4']]
    # Human Hips lies at hip-joint height; the robot pelvis frame is above its
    # hip axes. Account for that frame-origin difference before floor alignment.
    human_hip_offset = (rest[idx['LeftHip']] + rest[idx['RightHip']]) / 2 - rest[idx['Hips']]
    robot_hip_offset = (rp['left_hip_pitch_link'] + rp['right_hip_pitch_link']) / 2 - rp['pelvis']
    pelvis_origin_correction = scale * human_hip_offset - robot_hip_offset
    root += np.einsum('nij,j->ni', roots, pelvis_origin_correction)
    targets = {'pelvis': root, 'torso_link': np.empty_like(root)}
    rotations = {'pelvis': roots, 'torso_link': torsos}
    targets['torso_link'] = root + np.einsum('nij,j->ni', roots, rp['torso_link'] - rp['pelvis'])
    chains = []
    for side in SIDES:
        title = side.title()
        hip, knee, ankle = [f'{side}_{s}_link' for s in ('hip_pitch', 'knee', 'ankle_roll')]
        shoulder, elbow, wrist = [f'{side}_{s}_link' for s in ('shoulder_pitch', 'elbow', 'wrist_yaw')]
        targets[hip] = root + np.einsum('nij,j->ni', roots, rp[hip] - rp['pelvis'])
        targets[shoulder] = targets['torso_link'] + np.einsum('nij,j->ni', torsos, rp[shoulder] - rp['torso_link'])
        for start, end, hs, he in [(hip, knee, 'Hip', 'Knee'), (knee, ankle, 'Knee', 'Ankle'),
                                    (shoulder, elbow, 'Shoulder', 'Elbow'), (elbow, wrist, 'Elbow', 'Wrist')]:
            direction = pos[:, idx[title + he]] - pos[:, idx[title + hs]]
            direction /= np.maximum(np.linalg.norm(direction, axis=1, keepdims=True), 1e-9)
            targets[end] = targets[start] + direction * np.linalg.norm(rp[end] - rp[start])
            chains.append((start, end, title + hs, title + he))
        rotations[ankle] = rot[:, idx[title + 'Ankle']]
    # Single constant height shift, derived from the low-percentile ankle target.
    # Never per-frame root teleportation or hidden floor clamping.
    floor_shift = .04 - np.percentile(np.concatenate([targets[f'{s}_ankle_roll_link'][1:, 2] for s in SIDES]), 5)
    for positions in targets.values():
        positions[:, 2] += floor_shift
    # Project the foot target using its actual four plantar spheres and desired
    # orientation. Preserve the amount of morphology/floor adaptation in QA.
    floor_projection = {}
    for side in SIDES:
        name = f'{side}_ankle_roll_link'
        ids = [g for g in range(model.ngeom) if model.geom_bodyid[g] == model.body(name).id
               and model.geom_type[g] == mujoco.mjtGeom.mjGEOM_SPHERE]
        if len(ids) != 4: raise RuntimeError('Expected four plantar spheres on each foot')
        rotated = np.einsum('nij,kj->nki', rotations[name], model.geom_pos[ids])
        min_z = (rotated[:, :, 2] - model.geom_size[ids, 0]).min(axis=1) + targets[name][:, 2]
        correction = np.maximum(0., .003 - min_z)
        targets[name][:, 2] += correction
        floor_projection[side] = {'max_m': float(correction[1:].max()), 'mean_m': float(correction[1:].mean())}
    source_display = (pos - pos[:, idx['Hips'], None, :]) * scale + root[:, None, :]
    return targets, rotations, source_display, chains, {'source_unit_assumption': 'centimeters',
        'source_axes_assumption': 'Y up, X anatomical left, Z forward', 'basis': BASIS.tolist(),
        'robot_locomotion_scale': float(scale), 'constant_floor_shift_m': float(floor_shift),
        'pelvis_origin_correction_local_m': pelvis_origin_correction.tolist(),
        'foot_target_floor_projection': floor_projection,
        'frame0_use': 'inspection/calibration only, excluded from dance trajectory', 'source_start_frame': 1}


def make_tasks(model, targets):
    tasks = {}
    for name in targets:
        if 'hip_pitch' in name or 'shoulder_pitch' in name:
            continue
        pc, oc = 3., 0.
        if name == 'pelvis': pc, oc = 6., 3.
        elif name == 'torso_link': pc, oc = 2., 2.
        elif 'ankle' in name: pc, oc = 12., 1.5
        elif 'knee' in name: pc = 4.
        elif 'wrist' in name: pc = 6.
        elif 'elbow' in name: pc = 4.
        tasks[name] = mink.FrameTask(name, 'body', position_cost=pc, orientation_cost=oc, lm_damping=.05)
    return tasks


class DirectionSolver:
    """Bounded nonlinear limb-direction fitting, replacing position-only local IK.

    The base orientation is the measured pelvis orientation, not a floating
    target used to compensate for an incorrectly fitted arm. Translation can
    adjust by 10 cm. Joint rates and manufacturer position limits are bounds.
    """
    def __init__(self, model, names, pos, targets, orientations, chains, dt, speed_limits):
        self.model, self.data = model, mujoco.MjData(model)
        self.ids = {n: model.body(n).id for n in targets}
        self.names = {n: i for i, n in enumerate(names)}
        self.pos, self.targets, self.orientations = pos, targets, orientations
        self.chains, self.dt = chains, dt
        self.previous = None
        self.speed_limits = speed_limits
        self.soles = [g for g in range(model.ngeom) if model.geom_type[g] == mujoco.mjtGeom.mjGEOM_SPHERE
                      and model.body(model.geom_bodyid[g]).name in [f'{s}_ankle_roll_link' for s in SIDES]]
        self.joint_index = {model.joint(j).name: int(model.jnt_qposadr[j]) - 4 for j in range(1, model.njnt)}
        self.convergence = []

    def solve(self, f, initial, extra_seeds=None):
        root = self.targets['pelvis'][f]
        quat = Rotation.from_matrix(self.orientations['pelvis'][f]).as_quat()[[3, 0, 1, 2]]
        directions = [(self.ids[a], self.ids[b], unit(self.pos[f, self.names[sb]] - self.pos[f, self.names[sa]]))
                      for a, b, sa, sb in self.chains]
        ankles = [self.ids[f'{s}_ankle_roll_link'] for s in SIDES]
        foot_targets = [self.targets[f'{s}_ankle_roll_link'][f].copy() for s in SIDES]
        lo = np.r_[root - .1, self.model.jnt_range[1:, 0] + 1e-7]
        hi = np.r_[root + .1, self.model.jnt_range[1:, 1] - 1e-7]
        if self.previous is not None:
            lo[3:] = np.maximum(lo[3:], self.previous[3:] - self.speed_limits * self.dt)
            hi[3:] = np.minimum(hi[3:], self.previous[3:] + self.speed_limits * self.dt)
        x = np.r_[initial[:3], initial[7:]] if self.previous is None else self.previous.copy()
        x = np.clip(x, lo + 1e-9, hi - 1e-9)
        torso_target = self.orientations['torso_link'][f]

        def residual(value):
            self.data.qpos[:] = np.r_[value[:3], quat, value[3:]]
            mujoco.mj_kinematics(self.model, self.data)
            p = self.data.xpos
            rows = [unit(p[b] - p[a]) - direction for a, b, direction in directions]
            rows.extend((p[body] - target) * 12. for body, target in zip(ankles, foot_targets))
            for side in SIDES:
                body = self.ids[f'{side}_ankle_roll_link']
                foot = self.data.xmat[body].reshape(3, 3)
                desired = self.orientations[f'{side}_ankle_roll_link'][f]
                rows.append(Rotation.from_matrix(desired.T @ foot).as_rotvec() * .7)
            # Direction-only fits can translate the whole arm chain while keeping
            # the angles correct. Anchor elbows and wrists to the morphology-fit
            # targets as well, so positional error cannot disappear from the loss.
            for side in SIDES:
                for segment, weight in [('knee', 2.), ('elbow', 5.), ('wrist_yaw', 5.)]:
                    name = f'{side}_{segment}_link'
                    rows.append((p[self.ids[name]] - self.targets[name][f]) * weight)
            torso = self.data.xmat[self.ids['torso_link']].reshape(3, 3)
            rows.append(Rotation.from_matrix(torso_target.T @ torso).as_rotvec() * 1.2)
            rows.append((value[:3] - root) * 6.)
            # Explicit plantar-height penalty, not a post-hoc root teleport.
            rows.append(np.minimum(0., self.data.geom_xpos[self.soles, 2] - self.model.geom_size[self.soles, 0] - .003) * 35.)
            rows.append(value[3:] * .008)
            if self.previous is not None: rows.append((value[3:] - self.previous[3:]) * .02)
            return np.concatenate(rows)

        seeds = [x]
        # Optional branch hypotheses are starting points only. They are fitted
        # with exactly the same objective and manufacturer bounds, never copied
        # or clipped into the resulting motion.
        for proposed in extra_seeds or []:
            proposed = np.asarray(proposed, dtype=float)
            if proposed.shape != (36,) or not np.all(np.isfinite(proposed)):
                raise ValueError('An extra IK seed must be a finite qpos[36]')
            seeds.append(np.clip(np.r_[proposed[:3], proposed[7:]], lo + 1e-8, hi - 1e-8))
        if self.previous is None:
            analytical = x.copy()
            for side in SIDES:
                title = side.title()
                upper = unit(self.pos[f, self.names[title + 'Elbow']] - self.pos[f, self.names[title + 'Shoulder']])
                fore = unit(self.pos[f, self.names[title + 'Wrist']] - self.pos[f, self.names[title + 'Elbow']])
                local = self.orientations['torso_link'][f].T @ upper
                analytical[self.joint_index[f'{side}_shoulder_pitch_joint']] = np.arctan2(-local[0], -local[2])
                analytical[self.joint_index[f'{side}_shoulder_roll_joint']] = np.arcsin(np.clip(local[1], -1, 1))
                analytical[self.joint_index[f'{side}_elbow_joint']] = np.pi / 2 - np.arccos(np.clip(np.dot(upper, fore), -1, 1))
            for left_yaw in [0., np.pi / 2, -np.pi / 2]:
                for right_yaw in [0., np.pi / 2, -np.pi / 2]:
                    seed = analytical.copy()
                    seed[self.joint_index['left_shoulder_yaw_joint']] = left_yaw
                    seed[self.joint_index['right_shoulder_yaw_joint']] = right_yaw
                    seeds.append(np.clip(seed, lo + 1e-8, hi - 1e-8))
        candidates = [least_squares(residual, seed, bounds=(lo, hi), max_nfev=90 if self.previous is None else 30,
                               ftol=1e-5, xtol=1e-5, gtol=1e-5)
                      for seed in seeds]
        result = min(candidates, key=lambda r: float(np.dot(r.fun, r.fun)))
        self.last_candidates = [(np.r_[r.x[:3], quat, r.x[3:]], float(r.cost)) for r in candidates]
        self.convergence.append({'source_frame': f, 'status': int(result.status), 'evaluations': result.nfev,
                                 'optimality': float(result.optimality), 'cost': float(result.cost)})
        self.previous = result.x.copy()
        return np.r_[result.x[:3], quat, result.x[3:]]


def solve_clip(model, path, output, max_frames, stride, iterations, method, speed_limits, start_frame, reverse_solve):
    begin = time.perf_counter()
    motion, names, pos, rot, rest = source_arrays(path)
    targets, orientations, source_display, chains, calibration = prepare_targets(model, names, pos, rot, rest)
    configuration = mink.Configuration(model)
    configuration.update_from_keyframe('stand')
    tasks = make_tasks(model, targets)
    posture = mink.PostureTask(model, cost=.03)
    posture.set_target_from_configuration(configuration)
    joint_names = [model.joint(i).name for i in range(1, model.njnt)]
    limits = [mink.ConfigurationLimit(model), mink.VelocityLimit(model, dict(zip(joint_names, speed_limits)))]
    frame_indices = list(range(start_frame, len(motion.frames), stride))
    if max_frames: frame_indices = frame_indices[:max_frames]
    if reverse_solve: frame_indices.reverse()
    dt = motion.frame_time * stride
    qpos, all_positions, frames, errors, angles, penetrations, collision_frames = [], [], [], [], [], [], []
    body_ids = {name: model.body(name).id for name in targets}
    source_ids = {name: i for i, name in enumerate(names)}
    startq = configuration.q.copy()
    startq[:3] = targets['pelvis'][1]
    rq = Rotation.from_matrix(orientations['pelvis'][1]).as_quat()
    startq[3:7] = rq[[3, 0, 1, 2]]
    configuration.update(startq)
    directional = DirectionSolver(model, names, pos, targets, orientations, chains, dt, speed_limits)
    for sequence, f in enumerate(frame_indices):
        for name, task in tasks.items():
            task.set_target(transform(orientations.get(name, np.broadcast_to(np.eye(3), (len(pos), 3, 3)))[f], targets[name][f]))
        if method == 'direction':
            configuration.update(directional.solve(f, configuration.q))
        else:
            count = 100 if sequence == 0 else iterations
            subdt = dt if sequence == 0 else dt / iterations
            for _ in range(count):
                velocity = mink.solve_ik(configuration, list(tasks.values()) + [posture], subdt,
                                         solver='daqp', damping=1e-3, limits=limits)
                configuration.integrate_inplace(velocity, subdt)
        data = configuration.data
        mujoco.mj_forward(model, data)
        qpos.append(configuration.q.copy())
        all_positions.append(data.xpos[1:].copy())
        frames.append({'positions': data.xpos[1:].round(5).ravel().tolist(),
                       'rotations': data.xquat[1:].round(6).ravel().tolist(),
                       'source_positions': source_display[f].round(5).ravel().tolist()})
        errors.append({name: float(np.linalg.norm(data.xpos[body_ids[name]] - targets[name][f])) for name in tasks})
        angle = {}
        for a, b, sa, sb in chains:
            robot = unit(data.xpos[body_ids[b]] - data.xpos[body_ids[a]])
            human = unit(pos[f, source_ids[sb]] - pos[f, source_ids[sa]])
            angle[f'{sa}->{sb}'] = float(np.degrees(np.arccos(np.clip(np.dot(robot, human), -1, 1))))
        angles.append(angle)
        lowest = np.min(data.geom_xpos[directional.soles, 2] - model.geom_size[directional.soles, 0])
        penetrations.append(max(0., -float(lowest)))
        colliding = [(int(c.geom1), int(c.geom2), float(c.dist)) for c in data.contact[:data.ncon] if c.dist < -.005]
        if colliding: collision_frames.append({'frame': sequence, 'contacts': colliding})
        if sequence % 100 == 0:
            print(json.dumps({'clip': path.stem, 'solved': sequence + 1, 'total': len(frame_indices),
                              'seconds': round(time.perf_counter() - begin, 2)}), flush=True)
    if reverse_solve:
        for values in [frame_indices, qpos, all_positions, frames, errors, angles, penetrations]: values.reverse()
        for c in collision_frames: c['frame'] = len(frame_indices) - 1 - c['frame']
    qpos = np.asarray(qpos)
    angle_values = np.array([list(a.values()) for a in angles])
    error_values = np.array([list(e.values()) for e in errors])
    joint = qpos[:, 7:]
    bounds = model.jnt_range[1:]
    violation = max(0., float(np.max(bounds[:, 0] - joint)), float(np.max(joint - bounds[:, 1])))
    speed = float(np.max(np.abs(np.diff(joint, axis=0) / dt))) if len(joint) > 1 else 0.
    speed_ratio = float(np.max(np.abs(np.diff(joint, axis=0) / dt) / speed_limits)) if len(joint) > 1 else 0.
    metrics = {'mean_limb_direction_degrees': float(angle_values.mean()),
               'p95_limb_direction_degrees': float(np.percentile(angle_values, 95)),
               'max_joint_limit_violation_rad': violation, 'max_joint_velocity_limit_ratio': speed_ratio,
               'max_foot_penetration_m': max(penetrations),
               'max_target_position_error_m': float(error_values.max())}
    failed = [name for name, value in metrics.items() if value > GATES[name]]
    report = {'status': 'fail' if failed else 'pass', 'owner': 'retarget_math' if failed else None,
              'kind': 'constrained kinematic reference; not physical tracking', 'method': method, 'thresholds': GATES,
              'metrics': metrics, 'failed_gates': failed, 'calibration': calibration,
              'source_sha256': motion.sha256, 'frames': len(qpos), 'fps': 1. / dt,
              'protocol': 'v2: manufacturer URDF velocity limits; real plantar spheres; explicit origin/floor mapping',
              'solve_direction': 'reverse temporal disambiguation' if reverse_solve else 'forward',
              'max_joint_velocity_rad_s': speed, 'velocity_limits_rad_s': dict(zip(joint_names, speed_limits.tolist())),
              'source_frame_indices': frame_indices, 'wall_seconds': time.perf_counter() - begin,
              'per_chain_mean_degrees': dict(zip(angles[0], angle_values.mean(axis=0).tolist())),
              'per_chain_p95_degrees': dict(zip(angles[0], np.percentile(angle_values, 95, axis=0).tolist())),
              'per_target_max_position_error_m': dict(zip(errors[0], error_values.max(axis=0).tolist())),
              'per_target_worst_source_frame': dict(zip(errors[0], [frame_indices[i] for i in error_values.argmax(axis=0)])),
              'solver_convergence': directional.convergence,
              'collision_check': {'penetrating_frames': len(collision_frames), 'contacts': collision_frames,
                                  'limitation': 'model collision masks only; not exhaustive self-collision safety'},
              'physical_tracking_validated': False}
    letter = path.stem[0]
    write_json(output / f'{letter}-pose_retarget_qa.json', report)
    np.savez_compressed(output / f'{letter}.npz', qpos=qpos, fps=1. / dt, joint_names=np.array(joint_names),
                        body_names=np.array([model.body(i).name for i in range(1, model.nbody)]),
                        source_frame_indices=np.array(frame_indices))
    print(json.dumps({'clip': letter, 'status': report['status'], 'metrics': metrics, 'seconds': report['wall_seconds']}), flush=True)
    return {'name': letter, 'frames': frames}, names, [j.parent if j.parent is not None else -1 for j in motion.joints], report


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--model', type=Path, required=True)
    parser.add_argument('--source', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--clips', default='ABC')
    parser.add_argument('--max-frames', type=int, default=0)
    parser.add_argument('--start-frame', type=int, default=1)
    parser.add_argument('--reverse-solve', action='store_true')
    parser.add_argument('--stride', type=int, default=1)
    parser.add_argument('--iterations', type=int, default=6)
    parser.add_argument('--method', choices=['position', 'direction'], default='direction')
    args = parser.parse_args()
    args.output.mkdir(parents=True, exist_ok=True)
    model = mujoco.MjModel.from_xml_path(str(args.model / 'g1.xml'))
    urdf = ET.parse(args.model / 'g1_29dof_rev_1_0.urdf')
    declared = {j.get('name'): float(j.find('limit').get('velocity')) for j in urdf.findall('joint')
                if j.get('type') == 'revolute' and j.find('limit') is not None}
    speed_limits = np.array([declared[model.joint(i).name] for i in range(1, model.njnt)])
    if not np.all(np.isfinite(speed_limits) & (speed_limits > 0)): raise RuntimeError('Invalid URDF speed limits')
    if model.nq != 36 or model.nv != 35: raise RuntimeError('Not the selected 29-DOF model')
    mesh_count = mesh_export(model, args.output)
    write_json(args.output / 'mesh_state_summary.json', {'overall': {'status': 'pass'},
        'kind': 'rigid-link MJCF, no skin weights or FBX', 'nq': model.nq, 'nv': model.nv,
        'model_load_verified': True, 'visual_hulls': mesh_count,
        'model_sha256': hashlib.sha256((args.model / 'g1.xml').read_bytes()).hexdigest(),
        'limitations': ['asset load and joint schema gate only', 'visual convex hulls are not physics colliders']})
    clips, reports = [], []
    for letter in args.clips:
        clip, names, parents, report = solve_clip(model, args.source / f'{letter}_test.bvh', args.output,
                                                   args.max_frames, args.stride, args.iterations, args.method, speed_limits, args.start_frame, args.reverse_solve)
        clips.append(clip)
        reports.append(report)
    summary = {'status': 'pass' if all(r['status'] == 'pass' for r in reports) else 'fail',
               'physical_tracking_validated': False, 'clips': reports,
               'dependencies': {p: importlib.metadata.version(p) for p in ('mujoco', 'mink', 'numpy', 'scipy', 'daqp')}}
    write_json(args.output / 'pose_retarget_qa.json', summary)
    write_json(args.output / 'g1-motion.json', {'schema_version': 1, 'fps': reports[0]['fps'],
        'body_names': [model.body(i).name for i in range(1, model.nbody)],
        'body_parents': [int(model.body_parentid[i]) - 1 for i in range(1, model.nbody)],
        'source_names': names, 'source_parents': parents, 'clips': clips,
        'validation': {'pose_status': summary['status'], 'label': 'KINEMATIC REFERENCE — PHYSICS NOT VALIDATED'}}, True)
    raise SystemExit(0 if summary['status'] == 'pass' else 2)


if __name__ == '__main__':
    main()
