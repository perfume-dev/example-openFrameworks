"""Publication-boundary contract tests; run in the remote retarget environment."""
import unittest
import hashlib
import json
from pathlib import Path
import tempfile

import numpy as np

from export_viewer import checked_speed_limits, digest, validate_reference, verify_model_assets


class ReferenceContractTests(unittest.TestCase):
    def setUp(self):
        self.joints = ['joint_' + str(i) for i in range(29)]
        self.bodies = ['body_' + str(i) for i in range(30)]
        q = np.zeros((3, 36))
        q[:, 3] = 1
        self.archive = {'qpos': q, 'fps': np.array(40.), 'source_frame_indices': np.arange(1, 4),
                        'joint_names': np.array(self.joints), 'body_names': np.array(self.bodies)}

    def validate(self):
        return validate_reference(self.archive, 4, 40., self.joints, self.bodies)

    def test_original_rate_and_full_frame1_onward_accepted(self):
        q, frames = self.validate()
        self.assertEqual(q.shape, (3, 36))
        np.testing.assert_array_equal(frames, [1, 2, 3])

    def test_partial_clip_rejected(self):
        self.archive['qpos'] = self.archive['qpos'][:2]
        with self.assertRaises(ValueError): self.validate()

    def test_wrong_or_nan_sampling_rate_rejected(self):
        for value in [4., 50., float('nan')]:
            self.archive['fps'] = value
            with self.assertRaises(ValueError): self.validate()

    def test_calibration_frame_or_time_permutation_rejected(self):
        for indices in [[0, 1, 2], [1, 3, 2], [1, 2, 2]]:
            self.archive['source_frame_indices'] = np.array(indices)
            with self.assertRaises(ValueError): self.validate()

    def test_quaternion_not_silently_normalized(self):
        for value in [0., .5, float('nan')]:
            self.archive['qpos'][0, 3] = value
            with self.assertRaises(ValueError): self.validate()

    def test_joint_and_body_order_are_not_implicit(self):
        for key in ['joint_names', 'body_names']:
            self.archive[key] = self.archive[key][::-1]
            with self.assertRaises(ValueError): self.validate()
            self.archive[key] = self.archive[key][::-1]

    def test_explicit_timestamp_must_match_source_offset(self):
        self.archive['time'] = np.arange(3) / 40
        with self.assertRaises(ValueError): self.validate()
        self.archive['time'] = np.arange(1, 4) / 40
        self.validate()

    def test_velocity_limits_cannot_disable_gate(self):
        for value in [float('inf'), float('nan'), -1., 0.]:
            speeds = np.full(29, 20.)
            speeds[0] = value
            with self.assertRaises(ValueError): checked_speed_limits(speeds)
        with self.assertRaises(ValueError): checked_speed_limits(np.ones(28))
        np.testing.assert_array_equal(checked_speed_limits(np.full(29, 20.)), np.full(29, 20.))

    def test_mesh_tampering_or_changed_manifest_is_rejected(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            asset = root / 'example.asset'
            asset.write_bytes(b'unit test only')
            manifest = root / 'manifest.json'
            manifest.write_text(json.dumps({'files': [{'path': asset.name, 'bytes': asset.stat().st_size,
                                                       'sha256': digest(asset)}]}))
            pinned = digest(manifest)
            self.assertEqual(verify_model_assets(root, pinned)[asset.name], digest(asset))
            asset.write_bytes(b'changed asset!')
            with self.assertRaises(ValueError): verify_model_assets(root, pinned)
            manifest.write_text('{}')
            with self.assertRaises(ValueError): verify_model_assets(root, pinned)


if __name__ == '__main__':
    unittest.main()
