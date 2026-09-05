"""Strict, dependency-free BVH parsing/FK and a read-only motion preflight.

No retargeting, inverse kinematics, robot control or source rewriting is done.
Column-vector convention: parent @ T(offset) @ declared channel transforms.
All distances remain raw BVH units; no implicit metre conversion is applied.
"""

from __future__ import annotations

import argparse
from dataclasses import asdict, dataclass
import hashlib
import json
import math
from pathlib import Path
import re
from typing import Sequence


class BvhError(ValueError):
    pass


@dataclass(frozen=True)
class Joint:
    name: str
    parent: int | None
    offset: tuple[float, float, float]
    channels: tuple[str, ...]
    channel_start: int
    end_site: bool = False


@dataclass(frozen=True)
class Motion:
    joints: tuple[Joint, ...]
    frames: tuple[tuple[float, ...], ...]
    frame_time: float
    sha256: str
    hierarchy_sha256: str
    units: None = None

    @property
    def channel_count(self):
        return sum(len(j.channels) for j in self.joints)

    def index(self, name):
        return next(i for i, j in enumerate(self.joints) if j.name == name)


def finite(token: str, label: str) -> float:
    try:
        value = float(token)
    except ValueError as error:
        raise BvhError(f"Invalid {label}: {token}") from error
    if not math.isfinite(value):
        raise BvhError(f"Non-finite {label}: {token}")
    return value


def parse_text(text: str, raw: bytes | None = None) -> Motion:
    lines = text.splitlines()
    sections = [i for i, line in enumerate(lines) if line.strip() == 'MOTION']
    if len(sections) != 1:
        raise BvhError('Expected exactly one MOTION section line')
    section = sections[0]
    hierarchy = '\n'.join(lines[:section])
    tokens = re.findall(r'\{|\}|[^\s{}]+', hierarchy)
    cursor = 0
    nodes = []
    names = set()
    channel_count = 0
    allowed = {axis + suffix for axis in 'XYZ' for suffix in ('position', 'rotation')}

    def take(expected=None):
        nonlocal cursor
        if cursor >= len(tokens):
            raise BvhError('Truncated hierarchy')
        token = tokens[cursor]
        cursor += 1
        if expected is not None and token != expected:
            raise BvhError(f'Expected {expected}, got {token}')
        return token

    def parse_joint(kind, parent=None, depth=0):
        nonlocal channel_count
        if depth > 128:
            raise BvhError('Hierarchy exceeds 128 levels')
        take(kind)
        end = kind == 'End'
        if end:
            take('Site')
            name = nodes[parent]['name'] + '_end'
        else:
            name = take()
            if name in ('{', '}', 'OFFSET', 'CHANNELS'):
                raise BvhError('Invalid joint name')
        if name in names:
            raise BvhError(f'Duplicate joint name: {name}')
        names.add(name)
        index = len(nodes)
        nodes.append({'name': name})
        take('{')
        take('OFFSET')
        offset = tuple(finite(take(), 'OFFSET') for _ in range(3))
        channels = ()
        start = channel_count
        if not end:
            take('CHANNELS')
            count_text = take()
            if not re.fullmatch(r'[0-9]+', count_text):
                raise BvhError('Channel count must be an integer')
            count = int(count_text)
            if not 1 <= count <= 6:
                raise BvhError('Channel count must be 1..6')
            channels = tuple(take() for _ in range(count))
            if len(set(channels)) != count or not set(channels) <= allowed:
                raise BvhError('Duplicate or unsupported channel')
            channel_count += count
        nodes[index] = dict(name=name, parent=parent, offset=offset, channels=channels,
                            channel_start=start, end_site=end)
        while cursor < len(tokens) and tokens[cursor] != '}':
            child_kind = tokens[cursor]
            if end or child_kind not in ('JOINT', 'End'):
                raise BvhError(f'Unexpected hierarchy token: {child_kind}')
            parse_joint(child_kind, index, depth + 1)
        take('}')

    take('HIERARCHY')
    parse_joint('ROOT')
    if cursor != len(tokens):
        raise BvhError('Unexpected data after root hierarchy')
    motion_lines = [line.strip() for line in lines[section + 1:] if line.strip()]
    if len(motion_lines) < 3:
        raise BvhError('Incomplete MOTION section')
    frame_header = re.fullmatch(r'Frames:\s*([0-9]+)', motion_lines[0])
    time_header = re.fullmatch(r'Frame\s+Time:\s*(\S+)', motion_lines[1])
    if frame_header is None or time_header is None:
        raise BvhError('Invalid Frames or Frame Time header')
    frame_count = int(frame_header.group(1))
    frame_time = finite(time_header.group(1), 'frame time')
    if frame_count <= 0 or frame_time <= 0:
        raise BvhError('Frame count and frame time must be positive')
    if len(motion_lines) - 2 != frame_count:
        raise BvhError('Declared frame count does not match row count')
    frames = []
    for index, line in enumerate(motion_lines[2:]):
        parts = line.split()
        if len(parts) != channel_count:
            raise BvhError(f'Frame {index} has {len(parts)} values, expected {channel_count}')
        frames.append(tuple(finite(token, f'frame {index}') for token in parts))
    return Motion(tuple(Joint(**node) for node in nodes), tuple(frames), frame_time,
                  hashlib.sha256(raw if raw is not None else text.encode()).hexdigest(),
                  hashlib.sha256(hierarchy.encode()).hexdigest())


def load_bvh(path: str | Path) -> Motion:
    raw = Path(path).read_bytes()
    try:
        return parse_text(raw.decode('utf-8'), raw)
    except UnicodeDecodeError as error:
        raise BvhError('BVH is not UTF-8 text') from error


IDENTITY = (1., 0., 0., 0., 1., 0., 0., 0., 1.)


def matmul(a, b):
    return tuple(sum(a[row * 3 + k] * b[k * 3 + col] for k in range(3))
                 for row in range(3) for col in range(3))


def matvec(a, p):
    return tuple(sum(a[row * 3 + k] * p[k] for k in range(3)) for row in range(3))


def add(a, b):
    return tuple(x + y for x, y in zip(a, b))


def distance(a, b):
    return math.sqrt(sum((x - y) ** 2 for x, y in zip(a, b)))


def axis_rotation(axis, degrees):
    c, s = math.cos(math.radians(degrees)), math.sin(math.radians(degrees))
    if axis == 'X':
        return (1., 0., 0., 0., c, -s, 0., s, c)
    if axis == 'Y':
        return (c, 0., s, 0., 1., 0., -s, 0., c)
    if axis == 'Z':
        return (c, -s, 0., s, c, 0., 0., 0., 1.)
    raise BvhError('Unknown rotation axis')


def fk(motion: Motion, values: Sequence[float]):
    """Return global positions and rotation matrices; do not recenter/scale."""
    if len(values) != motion.channel_count or not all(math.isfinite(v) for v in values):
        raise BvhError('Invalid FK channel values')
    positions, rotations = [], []
    for joint in motion.joints:
        local_position, local_rotation = joint.offset, IDENTITY
        for index, channel in enumerate(joint.channels):
            value = values[joint.channel_start + index]
            axis = channel[0]
            if channel.endswith('rotation'):
                local_rotation = matmul(local_rotation, axis_rotation(axis, value))
            else:
                delta = tuple(value if i == 'XYZ'.index(axis) else 0 for i in range(3))
                local_position = add(local_position, matvec(local_rotation, delta))
        if joint.parent is None:
            positions.append(local_position)
            rotations.append(local_rotation)
        else:
            positions.append(add(positions[joint.parent], matvec(rotations[joint.parent], local_position)))
            rotations.append(matmul(rotations[joint.parent], local_rotation))
    return positions, rotations


def percentile(values, quantile):
    ordered = sorted(values)
    if not ordered:
        return None
    point = (len(ordered) - 1) * quantile
    low = math.floor(point)
    high = math.ceil(point)
    return ordered[low] + (ordered[high] - ordered[low]) * (point - low)


def stats(values):
    values = list(values)
    return {key: percentile(values, q) for key, q in [('min', 0), ('p05', .05), ('median', .5), ('p95', .95), ('max', 1)]}


def angle_difference(a, b):
    # trace(A^T B) is the Frobenius inner product of A and B.
    cosine = (sum(x * y for x, y in zip(a, b)) - 1) / 2
    return math.degrees(math.acos(max(-1., min(1., cosine))))


def audit(motion):
    poses = [fk(motion, frame) for frame in motion.frames]
    points = [pose[0] for pose in poses]
    rotations = [pose[1] for pose in poses]
    zero, _ = fk(motion, [0.] * motion.channel_count)
    frame0_nonzero = [(j.name, ch, motion.frames[0][j.channel_start + k])
                      for j in motion.joints for k, ch in enumerate(j.channels)
                      if ch.endswith('rotation') and abs(motion.frames[0][j.channel_start + k]) > 1e-9]
    rest_min = [min(p[axis] for p in zero) for axis in range(3)]
    rest_max = [max(p[axis] for p in zero) for axis in range(3)]
    report = {
        'sha256': motion.sha256, 'hierarchy_sha256': motion.hierarchy_sha256,
        'frames': len(motion.frames), 'channel_count': motion.channel_count,
        'animated_joints': sum(bool(j.channels) for j in motion.joints),
        'end_sites': sum(j.end_site for j in motion.joints), 'frame_time_seconds': motion.frame_time,
        'fps': 1 / motion.frame_time, 'sample_span_seconds': (len(motion.frames) - 1) * motion.frame_time,
        'playback_period_seconds': len(motion.frames) * motion.frame_time,
        'declared_units': None, 'declared_global_axis': None,
        'hierarchy': [asdict(j) for j in motion.joints],
        'offset_lengths': {j.name: distance(j.offset, (0, 0, 0)) for j in motion.joints},
        'zero_channel_bounds': {'min': rest_min, 'max': rest_max},
        'zero_channel_height': rest_max[1] - rest_min[1], 'zero_channel_armspan': rest_max[0] - rest_min[0],
        'frame0_nonzero_rotation_channels': frame0_nonzero,
        'frame0_is_zero_channel_rest': not bool(frame0_nonzero),
        'frame0_root_xyz_yxz': list(motion.frames[0][:6]),
        'first_motion_root_xyz_yxz': list(motion.frames[1][:6]) if len(poses) > 1 else None,
        'warnings': ['Distance units and global axes are not declared in BVH.',
                     'Zero-channel pose and first motion frame are separate concepts.',
                     'Do not use the visual examples root recentering or display scale for retarget input.',
                     'No ground reaction forces, heel markers, or contact labels are provided.']}
    names = {j.name: i for i, j in enumerate(motion.joints)}
    pairs = []
    for name, index in names.items():
        if name.startswith('Left') and 'Right' + name[4:] in names:
            partner = names['Right' + name[4:]]
            left, right = motion.joints[index].offset, motion.joints[partner].offset
            pairs.append({'left': name, 'right': motion.joints[partner].name,
                          'offset_mirror_error': distance((-left[0], left[1], left[2]), right)})
    report['left_right_pairs'] = pairs
    if len(poses) < 3:
        return report
    report.update({
        'frame0_to_1_root_rotation_degrees': angle_difference(rotations[0][0], rotations[1][0]),
        'frame0_to_1_root_distance': distance(points[0][0], points[1][0]),
        'frame0_to_1_max_joint_distance': max(distance(a, b) for a, b in zip(points[0], points[1])),
        'last_to_frame0_root_rotation_degrees': angle_difference(rotations[-1][0], rotations[0][0]),
        'last_to_frame1_root_rotation_degrees': angle_difference(rotations[-1][0], rotations[1][0]),
        'last_to_frame1_root_distance': distance(points[-1][0], points[1][0]),
        'root_range_excluding_frame0': {axis: stats(p[0][k] for p in points[1:]) for k, axis in enumerate('XYZ')},
        'root_speed_excluding_frame0_transition': stats(distance(a[0], b[0]) / motion.frame_time for a, b in zip(points[1:], points[2:])),
        'joint_world_bounds_excluding_frame0': {
            'min': [min(p[k] for pose in points[1:] for p in pose) for k in range(3)],
            'max': [max(p[k] for pose in points[1:] for p in pose) for k in range(3)]},
        'rotation_channel_ranges_excluding_frame0': {
            j.name: {ch: [min(frame[j.channel_start + k] for frame in motion.frames[1:]),
                          max(frame[j.channel_start + k] for frame in motion.frames[1:])]
                     for k, ch in enumerate(j.channels) if ch.endswith('rotation')}
            for j in motion.joints if j.channels},
    })
    if all(name in names for name in ['LeftToe_end', 'RightToe_end', 'LeftAnkle', 'RightAnkle']):
        ground = percentile([pose[names[side + 'Toe_end']][1] for side in ['Left', 'Right'] for pose in points[1:]], .05)
        contact = {'method': 'toe endpoint Y <= pooled p05 + 3 raw units; horizontal and vertical speed < 10 raw units/s',
                   'frame0_and_transition_excluded': True, 'pooled_ground_p05': ground, 'not_contact_truth': True}
        masks = []
        for side in ['Left', 'Right']:
            tips = [pose[names[side + 'Toe_end']] for pose in points[1:]]
            horizontal = [math.hypot(b[0] - a[0], b[2] - a[2]) / motion.frame_time for a, b in zip(tips, tips[1:])]
            vertical = [abs(b[1] - a[1]) / motion.frame_time for a, b in zip(tips, tips[1:])]
            mask = [p[1] <= ground + 3 and h < 10 and v < 10 for p, h, v in zip(tips[1:], horizontal, vertical)]
            masks.append(mask)
            contact[side] = {'fraction': sum(mask) / len(mask), 'tip_height': stats(p[1] for p in tips),
                             'ankle_height': stats(pose[names[side + 'Ankle']][1] for pose in points[1:]),
                             'horizontal_speed': stats(horizontal), 'vertical_speed': stats(vertical)}
        contact['both_fraction'] = sum(a and b for a, b in zip(*masks)) / len(masks[0])
        contact['neither_fraction'] = sum(not a and not b for a, b in zip(*masks)) / len(masks[0])
        report['contact_heuristic'] = contact
    return report


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--processing-data', type=Path, required=True)
    parser.add_argument('--of-data', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    results, initial_frames, hierarchy_hashes = {}, [], []
    for name in ['A_test.bvh', 'B_test.bvh', 'C_test.bvh']:
        motion = load_bvh(args.processing_data / name)
        result = audit(motion)
        result['processing_equals_of_bytes'] = (args.processing_data / name).read_bytes() == (args.of_data / name).read_bytes()
        results[name] = result
        initial_frames.append(motion.frames[0])
        hierarchy_hashes.append(motion.hierarchy_sha256)
    output = {'schema_version': 1, 'distance_units': 'raw BVH units; no scale applied',
              'fk_convention': 'column vectors; parent * T(offset) * channel transforms in declared order',
              'all_three_hierarchies_identical': len(set(hierarchy_hashes)) == 1,
              'all_three_frame0_values_identical': len(set(initial_frames)) == 1,
              'retargeting_executed': False, 'motions': results}
    args.output.write_text(json.dumps(output, indent=2) + '\n', encoding='utf-8')
    print(json.dumps({'output': str(args.output), 'source_hashes': {name: r['sha256'] for name, r in results.items()}}, indent=2))


if __name__ == '__main__':
    main()
