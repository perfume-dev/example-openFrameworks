import math
import unittest

from bvh_audit import BvhError, audit, fk, parse_text


def fixture(channels='Xposition Yposition Zposition Yrotation Xrotation Zrotation', values='0 2 0 0 0 0'):
    count = len(channels.split())
    return f'''HIERARCHY
ROOT Hips
{{
OFFSET 0 0 0
CHANNELS {count} {channels}
JOINT LeftHip
{{
OFFSET 1 0 0
CHANNELS 3 Yrotation Xrotation Zrotation
End Site
{{ OFFSET 0 -2 0 }}
}}
JOINT RightHip
{{
OFFSET -1 0 0
CHANNELS 3 Yrotation Xrotation Zrotation
End Site
{{ OFFSET 0 -2 0 }}
}}
}}
MOTION
Frames: 1
Frame Time: 0.025
{values} 0 0 0 0 0 0
'''


class BvhTests(unittest.TestCase):
    def test_hierarchy_and_left_right(self):
        motion = parse_text(fixture())
        self.assertEqual(len(motion.joints), 5)
        self.assertEqual(motion.channel_count, 12)
        self.assertEqual(motion.joints[1].name, 'LeftHip')
        self.assertEqual(motion.joints[2].parent, 1)
        points, _ = fk(motion, motion.frames[0])
        self.assertEqual(points[1], (1, 2, 0))
        self.assertEqual(points[2], (1, 0, 0))
        self.assertEqual(points[4], (-1, 0, 0))
        self.assertTrue(all(pair['offset_mirror_error'] == 0 for pair in audit(motion)['left_right_pairs']))

    def test_declared_rotation_order(self):
        # Rz(90) Rx(90) * (1,0,0) = (0,1,0); reversed gives (0,0,1).
        motion = parse_text(fixture('Zrotation Xrotation Yrotation', '90 90 0'))
        points, _ = fk(motion, motion.frames[0])
        for actual, expected in zip(points[1], (0, 1, 0)):
            self.assertAlmostEqual(actual, expected)

    def test_interleaved_translation_order(self):
        motion = parse_text(fixture('Zrotation Xposition', '90 2'))
        points, _ = fk(motion, motion.frames[0])
        self.assertAlmostEqual(points[0][0], 0)
        self.assertAlmostEqual(points[0][1], 2)

    def test_first_frame_is_not_assumed_rest(self):
        motion = parse_text(fixture(values='0 2 0 45 0 0'))
        result = audit(motion)
        self.assertFalse(result['frame0_is_zero_channel_rest'])
        self.assertIsNone(result['declared_units'])
        self.assertIsNone(result['declared_global_axis'])

    def test_hash_uses_exact_bytes(self):
        normal = fixture()
        crlf = normal.replace('\n', '\r\n')
        self.assertNotEqual(parse_text(normal).sha256, parse_text(crlf).sha256)

    def test_invalid_hierarchy(self):
        cases = {
            'truncated': fixture().replace('}\nMOTION', 'MOTION'),
            'trailing': fixture().replace('\nMOTION', '\nUNKNOWN\nMOTION'),
            'duplicate': fixture().replace('RightHip', 'LeftHip'),
            'bad_root': fixture().replace('ROOT Hips', 'JOINT Hips'),
            'wrong_offset': fixture().replace('OFFSET 0 0 0', 'OFFSET 0 0'),
            'unknown_channel': fixture().replace('Yrotation', 'Wrotation', 1),
            'duplicate_channel': fixture().replace('Yrotation Xrotation', 'Yrotation Yrotation', 1),
            'bad_count': fixture().replace('CHANNELS 6', 'CHANNELS 7'),
            'no_hierarchy': fixture().replace('HIERARCHY', 'HIERARCHIES'),
            'end_channels': fixture().replace('OFFSET 0 -2 0 }', 'OFFSET 0 -2 0 CHANNELS 1 Xrotation }', 1),
        }
        for name, data in cases.items():
            with self.subTest(name=name), self.assertRaises(BvhError):
                parse_text(data)

    def test_invalid_motion(self):
        cases = {
            'wrong_frames': fixture().replace('Frames: 1', 'Frames: 2'),
            'zero_frames': fixture().replace('Frames: 1', 'Frames: 0'),
            'wrong_time': fixture().replace('Frame Time:', 'Frame Times:'),
            'zero_time': fixture().replace('0.025', '0'),
            'negative_time': fixture().replace('0.025', '-0.1'),
            'missing_value': fixture().rsplit(' 0', 1)[0] + '\n',
            'extra_row': fixture() + '0 0 0\n',
            'duplicate_motion': fixture() + '\nMOTION\n',
        }
        for name, data in cases.items():
            with self.subTest(name=name), self.assertRaises(BvhError):
                parse_text(data)

    def test_non_finite_values_rejected(self):
        for value in ('nan', 'inf', '-inf', '1e999'):
            for where in ('offset', 'time', 'frame'):
                with self.subTest(value=value, where=where), self.assertRaises(BvhError):
                    data = fixture()
                    if where == 'offset':
                        data = data.replace('OFFSET 0 0 0', f'OFFSET {value} 0 0')
                    elif where == 'time':
                        data = data.replace('0.025', value)
                    else:
                        data = fixture(values=f'{value} 2 0 0 0 0')
                    parse_text(data)

    def test_fk_rejects_bad_row(self):
        motion = parse_text(fixture())
        with self.assertRaises(BvhError):
            fk(motion, [])
        with self.assertRaises(BvhError):
            fk(motion, [math.nan] * motion.channel_count)


if __name__ == '__main__':
    unittest.main(verbosity=2)
