import itertools
import unittest

import numpy as np
import global_path

from global_path import deduplicate, disconnected_boundaries, interpolate_original_frames, node_gate_mask, select_beam, select_path, shoulder_mirror_seeds, sweep_orders, transition, validate_cache_provenance


def poses(values):
    q = np.zeros((len(values), 36)); q[:, 3] = 1; q[:, 7] = values
    return q


class GlobalPathTests(unittest.TestCase):
    def test_future_information_can_beat_greedy_choice(self):
        candidates = [(poses([0, 1]), np.array([0., 0.1])),
                      (poses([0, 1]), np.array([0., 0.1])),
                      (poses([1]), np.array([0.]))]
        path, info = select_path(candidates, [0, .1, .2], np.ones(29), rate_weight=0, accel_weight=0)
        np.testing.assert_array_equal(path, [1, 1, 0])
        self.assertGreater(info['forbidden_edges'], 0)

    def test_impossible_velocity_path_is_rejected_not_clipped(self):
        candidates = [(poses([0]), np.array([0.])), (poses([1]), np.array([0.]))]
        with self.assertRaisesRegex(ValueError, 'No velocity-feasible path'):
            select_path(candidates, [0, .02], np.ones(29) * 20)

    def test_disconnected_component_scan(self):
        candidates = [(poses([x]), np.array([0.])) for x in [0, 1, 1.1, 2.1]]
        self.assertEqual(disconnected_boundaries(candidates, [0, .02, .04, .06], np.ones(29) * 20), [1, 3])

    def test_second_order_dp_matches_exhaustive_search(self):
        candidates = [(poses([0., .1]), np.array([0., .02])),
                      (poses([.1, -.1]), np.array([.01, 0.])),
                      (poses([.2, .0]), np.array([0., .005])),
                      (poses([.3, .1]), np.array([0., .02]))]
        times = np.array([0., .1, .23, .35]); limits = np.ones(29) * 20
        kwargs = dict(rate_weight=.5, accel_weight=3., accel_scale=10., root_rate_weight=0)
        actual, report = select_path(candidates, times, limits, **kwargs)
        scores = []
        for path in itertools.product(range(2), repeat=4):
            q = np.array([row[0][idx] for row, idx in zip(candidates, path)])
            v = np.diff(q[:, 7:], axis=0) / np.diff(times)[:, None]
            a = np.diff(v, axis=0) / ((times[2:] - times[:-2]) / 2)[:, None]
            score = sum(row[1][idx] for row, idx in zip(candidates, path))
            score += .5 * np.sum((v / limits) ** 2) + 3 * np.sum((a / 10) ** 2)
            scores.append((score, path))
        expected = min(scores)
        np.testing.assert_array_equal(actual, expected[1])
        self.assertAlmostEqual(report['objective'], expected[0])

    def test_first_order_streaming_dp_matches_exhaustive_search(self):
        candidates = [(poses([0., .1]), np.array([0., .02])),
                      (poses([.1, -.1]), np.array([.01, 0.])),
                      (poses([.2, .0]), np.array([0., .005])),
                      (poses([.3, .1]), np.array([0., .02]))]
        times = np.array([0., .1, .23, .35]); limits = np.ones(29) * 20
        actual, report = select_path(candidates, times, limits, rate_weight=.5, accel_weight=0, root_rate_weight=0)
        scores = []
        for path in itertools.product(range(2), repeat=4):
            q = np.array([row[0][idx] for row, idx in zip(candidates, path)])
            v = np.diff(q[:, 7:], axis=0) / np.diff(times)[:, None]
            score = sum(row[1][idx] for row, idx in zip(candidates, path)) + .5 * np.sum((v / limits) ** 2)
            scores.append((score, path))
        expected = min(scores)
        np.testing.assert_array_equal(actual, expected[1])
        self.assertAlmostEqual(report['objective'], expected[0])

    def test_beam_keeps_reachable_diverse_branches(self):
        q = poses([0, .001, 1, 20, -1])
        beam = select_beam(q, np.array([0., .01, .1, np.inf, .2]), np.ones(29), 4)
        self.assertNotIn(3, beam)
        self.assertIn(0, beam)
        self.assertIn(2, beam)
        self.assertIn(4, beam)

    def test_saturated_velocity_arrival_is_not_near_deduplicated(self):
        q, cost = deduplicate(poses([.2, .201]), np.array([.1, 0.]), tolerance=1e-9)
        _, _, allowed = transition(poses([0]), q, .02, np.ones(29) * 10, 0, 0)
        self.assertEqual(len(q), 2)
        self.assertEqual(np.count_nonzero(allowed), 1)

    def test_old_api_costs_rejected_except_as_reoptimized_warm_seeds(self):
        current = dict(source_sha256='s', model_sha256='m', urdf_sha256='u', api_sha256='new')
        old = dict(current, api_sha256='old')
        with self.assertRaisesRegex(ValueError, 'API mismatch'):
            validate_cache_provenance(old, current)
        validate_cache_provenance(old, current, seeds_only=True)
        with self.assertRaisesRegex(ValueError, 'mismatch'):
            validate_cache_provenance(dict(old, model_sha256='wrong'), current, seeds_only=True)

    def test_invalid_cached_cost_and_quaternion_rejected(self):
        with self.assertRaisesRegex(ValueError, 'negative'):
            select_path([(poses([0]), np.array([-1.]))], [0], np.ones(29))
        q = poses([0]); q[:, 3] = 2
        with self.assertRaisesRegex(ValueError, 'quaternion'):
            select_path([(q, np.array([0.]))], [0], np.ones(29))

    def test_middle_anchor_sweeps_cover_both_directions(self):
        self.assertEqual(sweep_orders(5, 2), [(1, [2, 3, 4]), (-1, [2, 1, 0])])
        self.assertEqual(sweep_orders(3), [(1, [0, 1, 2]), (-1, [2, 1, 0])])
        with self.assertRaisesRegex(ValueError, 'anchor'):
            sweep_orders(3, 3)

    def test_node_max_gates_do_not_trade_quality_for_cost(self):
        metrics = {'max_target_position_error_m': [.18, .180001, .1, .1],
                   'max_foot_penetration_m': [0, 0, .025001, 0],
                   'max_joint_limit_violation_rad': [0, 0, 0, .00001001],
                   'mean_limb_direction_degrees': [100, 0, 0, 0]}
        limits = {'max_target_position_error_m': .18, 'max_foot_penetration_m': .025,
                  'max_joint_limit_violation_rad': .00001, 'mean_limb_direction_degrees': 18}
        np.testing.assert_array_equal(node_gate_mask(metrics, limits), [True, False, False, False])

    def test_bounded_multistart_keeps_all_raw_candidates_and_removes_each_proximal_cost(self):
        class Solver:
            speed_limits = np.ones(29) * 10
            def solve(self, frame, previous, extra_seeds=None):
                q = poses([.1, -.1])
                self.last_candidates = [(q[i], 1 + i + .5 * np.sum(((q[i, 7:] - previous[7:]) * .02) ** 2))
                                        for i in range(2)]
                return q[0]
        before = global_path._worker
        global_path._worker = (Solver(), None, None)
        try:
            _, q, costs, _ = global_path.generate_bridge_frame((1, np.empty((0, 36)), np.empty(0),
                [(poses([0])[0], .02)], 1e-9, [poses([0])[0]]))
            np.testing.assert_allclose(q[:, 7], [.1, -.1])
            np.testing.assert_allclose(costs, [1, 2])
        finally:
            global_path._worker = before

    def test_bilateral_bounds_enforce_future_and_past_without_relaxing_manufacturer(self):
        bounds = np.tile([-2., 2.], (29, 1))
        lo, hi = global_path.bilateral_bounds(poses([-1.5])[0], poses([1.5])[0], .1, .1, np.ones(29) * 20, bounds)
        self.assertAlmostEqual(lo[0], -.5)
        self.assertAlmostEqual(hi[0], .5)
        self.assertTrue(np.all(lo >= bounds[:, 0]))
        self.assertTrue(np.all(hi <= bounds[:, 1]))

    def test_bilateral_infeasible_endpoints_are_detectable(self):
        bounds = np.tile([-3., 3.], (29, 1))
        lo, hi = global_path.bilateral_bounds(poses([-2.])[0], poses([2.])[0], .02, .02, np.ones(29) * 20, bounds)
        self.assertGreater(lo[0], hi[0])

    def test_bilateral_worker_restores_manufacturer_ranges_when_optimizer_raises(self):
        class Model:
            jnt_range = np.vstack([[0, 0], np.tile([-2., 2.], (29, 1))])
        class Solver:
            model = Model()
            speed_limits = np.ones(29) * 20
            def solve(self, frame, previous, extra_seeds=None):
                raise RuntimeError('synthetic optimizer failure')
        solver = Solver()
        original = solver.model.jnt_range.copy()
        before = global_path._worker
        global_path._worker = (solver, None, None)
        try:
            with self.assertRaisesRegex(RuntimeError, 'synthetic'):
                global_path.generate_pair_bridge_frame((1, poses([0]), np.array([0.]),
                    [(poses([-1.5])[0], poses([1.5])[0], .1, .1)], False))
            np.testing.assert_array_equal(solver.model.jnt_range, original)
        finally:
            global_path._worker = before

    def test_dedup_retains_lower_cost_and_distinct_branch(self):
        q, cost = deduplicate(poses([0., .001, 1]), np.array([1., .5, 2.]))
        np.testing.assert_allclose(q[:, 7], [.001, 1])
        np.testing.assert_allclose(cost, [.5, 2.])

    def test_interpolation_preserves_source_offset_and_all_frames(self):
        q = poses([0., 1.]); q[:, 0] = [1., 2.]
        full, indices = interpolate_original_frames(q, [1, 11], .025)
        np.testing.assert_array_equal(indices, np.arange(1, 12))
        np.testing.assert_allclose(full[:, 7], np.linspace(0, 1, 11))
        np.testing.assert_allclose(full[:, 0], np.linspace(1, 2, 11))
        np.testing.assert_allclose(np.linalg.norm(full[:, 3:7], axis=1), 1)

    def test_no_revolute_angle_wrapping(self):
        _, _, allowed = transition(poses([-3]), poses([3]), .02, np.ones(29) * 37, 0, 0)
        self.assertFalse(allowed[0, 0])

    def test_mirror_seeds_are_initial_guesses_with_equivalent_euler_rotation(self):
        from scipy.spatial.transform import Rotation
        names = [f'other{i}' for i in range(29)]
        for side, offset in [('left', 0), ('right', 3)]:
            for index, axis in enumerate(['pitch', 'roll', 'yaw']):
                names[offset + index] = f'{side}_shoulder_{axis}_joint'
        q = poses([0])[0]
        q[7:13] = [-2.2, .8, -.5, -2.1, -.9, 1.1]
        original = q.copy()
        mirrors = shoulder_mirror_seeds(q, names)
        np.testing.assert_array_equal(q, original)
        for seed in mirrors:
            for offset in [7, 10]:
                a = Rotation.from_euler('YXZ', q[offset:offset + 3]).as_matrix()
                b = Rotation.from_euler('YXZ', seed[offset:offset + 3]).as_matrix()
                np.testing.assert_allclose(a, b, atol=1e-12)


if __name__ == '__main__':
    unittest.main()
