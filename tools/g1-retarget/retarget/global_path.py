"""Independent IK candidates + velocity-constrained second-order Viterbi.

Prototype kinematic reference only. No clipped outputs, pose-gate changes,
hardware transport, or physics. Retarget API is imported from a pinned snapshot.
"""
from __future__ import annotations

import argparse
from concurrent.futures import ProcessPoolExecutor, as_completed
import hashlib
import importlib.util
import json
import multiprocessing
import os
from pathlib import Path
import sys
import time
import xml.etree.ElementTree as ET

import numpy as np


def require(condition, message):
    if not condition:
        raise ValueError(message)


def sha256(path):
    return hashlib.sha256(Path(path).read_bytes()).hexdigest()


def write_json(path, value):
    Path(path).write_text(json.dumps(value, indent=2, allow_nan=False) + '\n')


def load_api(path):
    spec = importlib.util.spec_from_file_location('global_path_retarget_api', path)
    module = importlib.util.module_from_spec(spec)
    sys.modules[spec.name] = module
    spec.loader.exec_module(module)
    return module


def deduplicate(qpos, costs, tolerance=0.015):
    """Keep the lowest-loss representative of each numerically repeated solution."""
    qpos, costs = np.asarray(qpos), np.asarray(costs)
    require(qpos.ndim == 2 and qpos.shape[1] == 36 and costs.shape == (len(qpos),), 'Invalid candidate dimensions')
    require(np.all(np.isfinite(qpos)) and np.all(np.isfinite(costs)) and np.all(costs >= 0), 'Nonfinite/negative candidate')
    selected = []
    for index in np.argsort(costs, kind='stable'):
        if all(np.max(np.abs(qpos[index, 7:] - qpos[old, 7:])) > tolerance
               or np.linalg.norm(qpos[index, :3] - qpos[old, :3]) > 0.002 for old in selected):
            selected.append(int(index))
    return qpos[selected], costs[selected]


def validate_lattice(candidates, times, speed_limits):
    times, speed_limits = np.asarray(times, dtype=float), np.asarray(speed_limits, dtype=float)
    require(len(candidates) >= 1 and times.shape == (len(candidates),), 'Time/candidate count mismatch')
    require(np.all(np.isfinite(times)) and np.all(np.diff(times) > 0), 'Times must be finite and strictly increasing')
    require(speed_limits.shape == (29,) and np.all(np.isfinite(speed_limits)) and np.all(speed_limits > 0),
            'Expected 29 finite positive manufacturer velocity limits')
    for q, cost in candidates:
        require(q.ndim == 2 and q.shape[1] == 36 and len(q) > 0 and cost.shape == (len(q),), 'Empty/invalid candidate row')
        require(np.all(np.isfinite(q)) and np.all(np.isfinite(cost)) and np.all(cost >= 0), 'Nonfinite/negative candidates')
        require(np.all(np.abs(np.linalg.norm(q[:, 3:7], axis=1) - 1) <= 1e-6), 'Candidate base quaternion is not unit length')
    return times, speed_limits


def transition(a, b, dt, speed_limits, rate_weight, root_rate_weight):
    """No angle wrapping: these are manufacturer-bounded scalar revolute joints."""
    velocity = (b[None, :, 7:] - a[:, None, 7:]) / dt
    allowed = np.all(np.abs(velocity) <= speed_limits + 1e-9, axis=-1)
    root_velocity = (b[None, :, :3] - a[:, None, :3]) / dt
    cost = rate_weight * np.sum((velocity / speed_limits) ** 2, axis=-1)
    cost += root_rate_weight * np.sum(root_velocity ** 2, axis=-1)
    return np.where(allowed, cost, np.inf), velocity, allowed


def select_path(candidates, times, speed_limits, *, rate_weight=0.1, accel_weight=0.02,
                accel_scale=100.0, root_rate_weight=0.01):
    """Exact second-order DP over finite candidates, with hard velocity edges.

    State is (previous candidate, current candidate). Acceleration compares
    consecutive interval velocities divided by the distance between midpoints.
    100 rad/s² is a *soft objective normalization*, never a manufacturer limit.
    """
    times, speed_limits = validate_lattice(candidates, times, speed_limits)
    require(all(np.isfinite(x) and x >= 0 for x in [rate_weight, accel_weight, root_rate_weight])
            and np.isfinite(accel_scale) and accel_scale > 0, 'Invalid objective weights')
    if len(candidates) == 1:
        chosen = int(np.argmin(candidates[0][1]))
        return np.array([chosen]), {'objective': float(candidates[0][1][chosen]), 'forbidden_edges': 0}
    if accel_weight == 0:
        # Exact first-order special case avoids K^3 work when acceleration is
        # intentionally diagnostic-only for a larger repaired candidate pool.
        # Stream edges: retaining all 29-dimensional velocity lattices can use
        # gigabytes even though first-order DP needs only one neighboring pair.
        scores = candidates[0][1].copy()
        back, forbidden = [], 0
        for i, dt in enumerate(np.diff(times)):
            edge, _, allowed = transition(candidates[i][0], candidates[i + 1][0], dt,
                                           speed_limits, rate_weight, root_rate_weight)
            forbidden += int(np.count_nonzero(~allowed))
            total = scores[:, None] + edge
            back.append(np.argmin(total, axis=0))
            scores = np.min(total, axis=0) + candidates[i + 1][1]
            require(np.any(np.isfinite(scores)), f'No velocity-feasible path into frame index {i + 1}')
        path = np.empty(len(candidates), dtype=int)
        path[-1] = np.argmin(scores)
        for i in range(len(candidates) - 2, -1, -1):
            path[i] = back[i][path[i + 1]]
        q = np.array([row[0][choice] for row, choice in zip(candidates, path)])
        ratio = np.abs(np.diff(q[:, 7:], axis=0) / np.diff(times)[:, None]) / speed_limits
        require(ratio.max() <= 1 + 1e-9, 'Internal error: selected forbidden edge')
        return path, {'objective': float(scores.min()), 'forbidden_edges': forbidden,
                      'max_selected_velocity_limit_ratio': float(ratio.max()), 'state_order': 1,
                      'rate_weight': rate_weight, 'acceleration_weight': 0,
                      'root_rate_weight': root_rate_weight, 'acceleration_diagnostic_only': True}
    edges, velocities, forbidden = [], [], 0
    for i, dt in enumerate(np.diff(times)):
        edge, velocity, allowed = transition(candidates[i][0], candidates[i + 1][0], dt,
                                             speed_limits, rate_weight, root_rate_weight)
        edges.append(edge); velocities.append(velocity)
        forbidden += int(np.count_nonzero(~allowed))
    scores = candidates[0][1][:, None] + edges[0] + candidates[1][1][None, :]
    require(np.any(np.isfinite(scores)), 'No velocity-feasible path into frame index 1')
    back = [None, None]
    for i in range(2, len(candidates)):
        # a,b,c axes: previous-previous, previous, current candidate.
        midpoint_dt = (times[i] - times[i - 2]) / 2
        acceleration = (velocities[i - 1][None] - velocities[i - 2][:, :, None]) / midpoint_dt
        acceleration_cost = accel_weight * np.sum((acceleration / accel_scale) ** 2, axis=-1)
        total = scores[:, :, None] + acceleration_cost
        predecessor = np.argmin(total, axis=0)
        scores = np.min(total, axis=0) + edges[i - 1] + candidates[i][1][None, :]
        require(np.any(np.isfinite(scores)), f'No velocity-feasible path into frame index {i}')
        back.append(predecessor)
    last = np.unravel_index(np.argmin(scores), scores.shape)
    path = np.empty(len(candidates), dtype=int)
    path[-2:] = last
    for i in range(len(candidates) - 1, 1, -1):
        path[i - 2] = back[i][path[i - 1], path[i]]
    selected = np.asarray([row[0][index] for row, index in zip(candidates, path)])
    ratio = np.abs(np.diff(selected[:, 7:], axis=0) / np.diff(times)[:, None]) / speed_limits
    require(np.max(ratio) <= 1 + 1e-9, 'Internal error: DP selected forbidden velocity edge')
    return path, {'objective': float(np.min(scores)), 'forbidden_edges': forbidden,
                  'max_selected_velocity_limit_ratio': float(ratio.max()),
                  'state_order': 2, 'rate_weight': rate_weight, 'acceleration_weight': accel_weight,
                  'acceleration_scale_rad_s2': accel_scale, 'root_rate_weight': root_rate_weight,
                  'acceleration_is_soft_cost_not_a_hardware_limit': True}


def disconnected_boundaries(candidates, times, speed_limits):
    """Find every disconnected component boundary, resetting reachability after one."""
    times, speed_limits = validate_lattice(candidates, times, speed_limits)
    reachable = np.ones(len(candidates[0][0]), dtype=bool)
    broken = []
    for i in range(1, len(candidates)):
        _, _, allowed = transition(candidates[i - 1][0], candidates[i][0], times[i] - times[i - 1], speed_limits, 0, 0)
        reachable = np.any(allowed & reachable[:, None], axis=0)
        if not reachable.any():
            broken.append(i)
            reachable = np.ones(len(candidates[i][0]), dtype=bool)
    return broken


_worker = None


def shoulder_mirror_seeds(qpos, joint_names):
    """Initial guesses for the second Ry-Rx-Rz branch, never output transforms."""
    result = []
    columns = {name: i + 7 for i, name in enumerate(joint_names)}
    wrap = lambda angle: (angle + np.pi) % (2 * np.pi) - np.pi
    for mask in [1, 2, 3]:
        seed = qpos.copy()
        for side, bit in [('left', 1), ('right', 2)]:
            if not mask & bit:
                continue
            p, r, y = [columns[f'{side}_shoulder_{axis}_joint'] for axis in ['pitch', 'roll', 'yaw']]
            seed[p] = wrap(seed[p] + np.pi)
            seed[r] = wrap(np.pi - seed[r])
            seed[y] = wrap(seed[y] + np.pi)
        result.append(seed)
    return result


def warm_seeds_from_cache(path, indices, joint_names):
    """Old solutions are initial guesses only; every new output is optimized anew."""
    with np.load(path, allow_pickle=False) as archive:
        frames, qpos, costs, offsets = [archive[key] for key in ['source_frame_indices', 'qpos', 'costs', 'offsets']]
        lookup = {int(frame): i for i, frame in enumerate(frames)}
        result = []
        for frame in indices:
            require(frame in lookup, f'Warm seed cache missing source frame {frame}')
            i = lookup[frame]
            row = qpos[offsets[i]:offsets[i + 1]]
            cost = costs[offsets[i]:offsets[i + 1]]
            best = row[np.argmin(cost)].copy()
            seeds = [best, *shoulder_mirror_seeds(best, joint_names)]
            for neighbor in [i - 1, i + 1]:
                if 0 <= neighbor < len(frames):
                    lo, hi = offsets[neighbor:neighbor + 2]
                    seeds.append(qpos[lo:hi][np.argmin(costs[lo:hi])].copy())
            result.append(seeds)
        return result


def initialize_worker(api_path, model_path, source_path, speed_limits, dt):
    import mujoco
    rt = load_api(api_path)
    model = mujoco.MjModel.from_xml_path(model_path)
    motion, names, pos, rot, rest = rt.source_arrays(Path(source_path))
    targets, orientations, _, chains, _ = rt.prepare_targets(model, names, pos, rot, rest)
    solver = rt.DirectionSolver(model, names, pos, targets, orientations, chains, dt, np.asarray(speed_limits))
    initial = model.key_qpos[model.key('stand').id].copy()
    global _worker
    _worker = (solver, initial, targets)


def generate_frame(request):
    frame, extra_seeds = request if isinstance(request, tuple) else (request, None)
    solver, initial, targets = _worker
    begin = time.perf_counter()
    solver.previous = None  # independent multistart; no inherited greedy branch or velocity box
    initial = initial.copy(); initial[:3] = targets['pelvis'][frame]
    if extra_seeds is None:
        solver.solve(frame, initial)
    else:
        solver.solve(frame, initial, extra_seeds=extra_seeds)
    require(hasattr(solver, 'last_candidates'), 'Retarget API lacks last_candidates')
    raw = solver.last_candidates
    q, cost = deduplicate(np.array([x[0] for x in raw]), np.array([x[1] for x in raw]))
    return frame, q, cost, {'source_frame': frame, 'raw_count': len(raw), 'unique_count': len(q),
                            'minimum_cost': float(cost.min()), 'wall_seconds': time.perf_counter() - begin,
                            'best_solver': solver.convergence[-1]}


def generate_bridge_frame(request):
    """Add independently bounded optimizations arriving from both time directions."""
    frame, current_q, current_cost, arrivals, *options = request
    solver, _, _ = _worker
    begin = time.perf_counter()
    qpos, costs = list(current_q), list(current_cost)
    extra_seeds = options[1] if len(options) > 1 else None
    for previous, interval in arrivals:
        solver.dt = interval
        solver.previous = np.r_[previous[:3], previous[7:]]
        if extra_seeds is None:
            solver.solve(frame, previous)
        else:
            solver.solve(frame, previous, extra_seeds=extra_seeds)
        for q, total_cost in solver.last_candidates:
            require(np.all(np.abs(q[7:] - previous[7:]) <= solver.speed_limits * interval + 1e-8),
                    'Bounded bridge violated manufacturer velocity limits')
            # The owner's weak previous-pose penalty is not part of independent
            # fit; remove it separately for every bounded multistart solution.
            proximal = .5 * np.sum(((q[7:] - previous[7:]) * .02) ** 2)
            fit = float(total_cost - proximal)
            require(fit >= -1e-10, 'Unexpected bridge objective decomposition')
            qpos.append(q); costs.append(max(0.0, fit))
    qpos, costs = deduplicate(np.asarray(qpos), np.asarray(costs),
                              tolerance=options[0] if options else 0.015)
    return frame, qpos, costs, {'source_frame': frame, 'incoming_optimizations': len(arrivals),
                               'unique_count': len(qpos), 'minimum_cost': float(costs.min()),
                               'wall_seconds': time.perf_counter() - begin}


def select_beam(qpos, scores, speed_limits, width):
    """Keep multiple reachable states: half low-cost, half joint-space diversity.

    This is candidate generation, not the final path optimizer. Unreachable
    states are never promoted. The final exact first-order DP sees every saved
    candidate from both sweep directions.
    """
    require(width >= 2, 'Beam needs at least two independent states')
    finite = np.flatnonzero(np.isfinite(scores))
    require(len(finite) > 0, 'No reachable beam states')
    ranked = finite[np.argsort(scores[finite], kind='stable')]
    selected = ranked[:min(len(ranked), max(1, width // 2))].tolist()
    while len(selected) < min(width, len(ranked)):
        distance = np.min(np.sum(((qpos[ranked, None, 7:] - qpos[np.asarray(selected)][None, :, 7:])
                                  / speed_limits) ** 2, axis=-1), axis=1)
        distance[np.isin(ranked, selected)] = -1
        selected.append(int(ranked[np.argmax(distance)]))
    return np.asarray(selected, dtype=int)


def sweep_orders(count, anchor_index=None):
    require(count >= 2 and (anchor_index is None or 0 <= anchor_index < count), 'Invalid sweep anchor')
    return [(1, list(range(0 if anchor_index is None else anchor_index, count))),
            (-1, list(range(count - 1 if anchor_index is None else anchor_index, -1, -1)))]


def generate_sweeps(api, model_path, source, indices, candidates, speed_limits, frame_time,
                    workers, width, rate_weight, root_rate_weight, anchor_index=None):
    """Finite-horizon bilateral multi-state bounded continuation.

    Each sweep immediately propagates newly optimized reachable states to the
    next frame, unlike Jacobi-style repairs whose information travels one edge
    per pass. The sweep beam is approximate; final DP over the accumulated
    lattice is exact. Bounds remain in the IK optimization, never output clips.
    """
    candidates = [(q.copy(), c.copy()) for q, c in candidates]
    reports = []
    with ProcessPoolExecutor(max_workers=workers, mp_context=multiprocessing.get_context('spawn'),
                             initializer=initialize_worker,
                             initargs=(str(api), str(model_path), str(source), speed_limits.tolist(), frame_time)) as pool:
        for direction, order in sweep_orders(len(indices), anchor_index):
            first = order[0]
            beam = select_beam(*candidates[first], speed_limits, width)
            previous_q, previous_scores = candidates[first][0][beam], candidates[first][1][beam]
            for step, i in enumerate(order[1:], start=1):
                interval = abs(indices[i] - indices[order[step - 1]]) * frame_time
                arrivals = [(q, interval) for q in previous_q]
                chunks = [arrivals[j::workers] for j in range(workers) if arrivals[j::workers]]
                futures = [pool.submit(generate_bridge_frame,
                            (indices[i], np.empty((0, 36)), np.empty(0), chunk, 1e-9)) for chunk in chunks]
                results = [future.result() for future in futures]
                qpos = np.concatenate([candidates[i][0], *[result[1] for result in results]])
                costs = np.concatenate([candidates[i][1], *[result[2] for result in results]])
                # A nearly-identical lower-loss candidate can lie just outside
                # a saturated velocity edge. Preserve such distinct states.
                qpos, costs = deduplicate(qpos, costs, tolerance=1e-9)
                candidates[i] = qpos, costs
                edge, _, _ = transition(previous_q, qpos, interval, speed_limits, rate_weight, root_rate_weight)
                scores = np.min(previous_scores[:, None] + edge, axis=0) + costs
                beam = select_beam(qpos, scores, speed_limits, width)
                previous_q, previous_scores = qpos[beam], scores[beam]
                reports.append({'source_frame': indices[i], 'direction': direction,
                                'incoming_states': len(arrivals), 'unique_candidates': len(qpos),
                                'reachable_states': int(np.count_nonzero(np.isfinite(scores))),
                                'beam_states': len(beam), 'minimum_accumulated_objective': float(scores.min())})
                if step <= 3 or step % 25 == 0 or step == len(order) - 1:
                    print(json.dumps({'stage': 'beam_sweep', 'direction': direction, 'source_frame': indices[i],
                                      'complete': step, 'total': len(order) - 1,
                                      'beam_states': len(beam), 'candidates': len(qpos)}), flush=True)
    return candidates, reports


def bridge_hypotheses(qpos, costs, joint_names):
    """Target-fit and shoulder-pole guesses, reoptimized inside each velocity box."""
    best = qpos[np.argmin(costs)].copy()
    seeds = [best, *shoulder_mirror_seeds(best, joint_names)]
    columns = {name: i + 7 for i, name in enumerate(joint_names)}
    for pitch in [-np.pi / 2, 0., np.pi / 2]:
        seed = best.copy()
        for side, sign in [('left', 1), ('right', -1)]:
            seed[columns[f'{side}_shoulder_pitch_joint']] = pitch
            seed[columns[f'{side}_shoulder_roll_joint']] = sign * np.pi / 2
        seeds.append(seed)
    return seeds


def bilateral_bounds(left, right, left_dt, right_dt, speed_limits, manufacturer_bounds):
    require(left_dt > 0 and right_dt > 0, 'Invalid bilateral intervals')
    lo = np.maximum.reduce([manufacturer_bounds[:, 0], left[7:] - speed_limits * left_dt,
                            right[7:] - speed_limits * right_dt])
    hi = np.minimum.reduce([manufacturer_bounds[:, 1], left[7:] + speed_limits * left_dt,
                            right[7:] + speed_limits * right_dt])
    return lo, hi


def pair_arrivals(left, right, left_dt, right_dt, speed_limits):
    """Bounded representative pairs for candidate generation, not final pruning."""
    li = select_beam(*left, speed_limits, 24)
    ri = select_beam(*right, speed_limits, 24)
    _, _, allowed = transition(left[0][li], right[0][ri], left_dt + right_dt, speed_limits, 0, 0)
    a, b = np.nonzero(allowed)
    if not len(a):
        return []
    a, b = li[a], ri[b]
    scores = left[1][a] + right[1][b]
    ranked = np.argsort(scores, kind='stable')
    selected = ranked[:min(8, len(ranked))].tolist()
    descriptor = np.c_[left[0][a, 7:] / speed_limits, right[0][b, 7:] / speed_limits]
    while len(selected) < min(32, len(ranked)):
        distance = np.min(np.sum((descriptor[:, None] - descriptor[np.asarray(selected)][None]) ** 2, axis=-1), axis=1)
        distance[selected] = -1
        selected.append(int(np.argmax(distance)))
    return [(left[0][a[i]], right[0][b[i]], left_dt, right_dt) for i in selected]


def generate_pair_bridge_frame(request):
    """Fit a center frame inside BOTH neighboring manufacturer velocity boxes.

    These future constraints produce transition poses that need not be local
    minima of one-sided frame fitting. The worker's in-memory optimization range
    is temporarily tightened and restored; the model file is never modified.
    """
    frame, current_q, current_cost, pairs, *options = request
    solver, _, _ = _worker
    manufacturer = solver.model.jnt_range[1:].copy()
    retain_current = options[0] if options else True
    qpos, costs = (list(current_q), list(current_cost)) if retain_current else ([], [])
    best = current_q[np.argmin(current_cost)]
    solved = 0
    try:
        for left, right, left_dt, right_dt in pairs:
            lo, hi = bilateral_bounds(left, right, left_dt, right_dt, solver.speed_limits, manufacturer)
            if np.any(hi - lo <= 1e-6):
                continue
            solver.model.jnt_range[1:, 0] = lo
            solver.model.jnt_range[1:, 1] = hi
            solver.previous = np.r_[left[:3], left[7:]]
            solver.dt = left_dt
            middle = best.copy()
            fraction = left_dt / (left_dt + right_dt)
            middle[:3] = (1 - fraction) * left[:3] + fraction * right[:3]
            middle[7:] = (1 - fraction) * left[7:] + fraction * right[7:]
            solver.solve(frame, left, extra_seeds=[middle, best])
            for q, total_cost in solver.last_candidates:
                require(np.all(np.abs(q[7:] - left[7:]) <= solver.speed_limits * left_dt + 1e-8)
                        and np.all(np.abs(q[7:] - right[7:]) <= solver.speed_limits * right_dt + 1e-8),
                        'Bilateral bridge violated a neighboring velocity constraint')
                fit = float(total_cost - .5 * np.sum(((q[7:] - left[7:]) * .02) ** 2))
                require(fit >= -1e-10, 'Unexpected bilateral objective decomposition')
                qpos.append(q); costs.append(max(0., fit))
            solved += 1
    finally:
        solver.model.jnt_range[1:] = manufacturer
    qpos, costs = (deduplicate(np.asarray(qpos), np.asarray(costs), tolerance=1e-9) if len(qpos)
                   else (np.empty((0, 36)), np.empty(0)))
    return frame, qpos, costs, {'source_frame': frame, 'neighbor_pairs': len(pairs),
                               'solved_pairs': solved, 'unique_count': len(qpos)}


def generate_bridges(api, model_path, source, indices, candidates, speed_limits, frame_time, workers,
                     active=None, multistart=False, joint_names=None, bilateral=False, eligible=None):
    active = set(range(len(indices))) if active is None else set(active)
    result = {f: (*candidates[i], {'source_frame': f, 'unchanged': True})
              for i, f in enumerate(indices) if i not in active}
    with ProcessPoolExecutor(max_workers=workers, mp_context=multiprocessing.get_context('spawn'),
                             initializer=initialize_worker,
                             initargs=(str(api), str(model_path), str(source), speed_limits.tolist(), frame_time)) as pool:
        futures = []
        for i, frame in enumerate(indices):
            if i not in active:
                continue
            if bilateral:
                pairs = []
                if 0 < i < len(indices) - 1 and len(eligible[i - 1][0]) and len(eligible[i + 1][0]):
                    pairs = pair_arrivals(eligible[i - 1], eligible[i + 1],
                        (frame - indices[i - 1]) * frame_time, (indices[i + 1] - frame) * frame_time, speed_limits)
                futures.append(pool.submit(generate_pair_bridge_frame, (frame, *candidates[i], pairs)))
                continue
            arrivals = []
            for neighbor in [i - 1, i + 1]:
                if 0 <= neighbor < len(indices):
                    interval = abs(indices[neighbor] - frame) * frame_time
                    q, cost = candidates[neighbor]
                    # Keep distinct low-loss and distant joint-space alternatives
                    # when iterative repair produces a very large local pool.
                    selected = [int(np.argmin(cost))]
                    while len(selected) < min(16 if multistart else 64, len(q)):
                        distance = np.min(np.sum(((q[:, None, 7:] - q[np.asarray(selected)][None, :, 7:])
                                                  / speed_limits) ** 2, axis=-1), axis=1)
                        distance[selected] = -1
                        selected.append(int(np.argmax(distance)))
                    arrivals.extend((q[s], interval) for s in selected)
            hypotheses = bridge_hypotheses(*candidates[i], joint_names) if multistart else None
            futures.append(pool.submit(generate_bridge_frame, (frame, *candidates[i], arrivals, 1e-9, hypotheses)))
        for future in as_completed(futures):
            frame, q, cost, report = future.result()
            result[frame] = (q, cost, report)
            if len(result) <= workers or len(result) % 25 == 0 or len(result) == len(indices):
                print(json.dumps({'stage': 'bridges', 'source_frame': frame, 'complete': len(result),
                                  'total': len(indices), 'candidates': len(q)}), flush=True)
    return [(result[f][0], result[f][1]) for f in indices], [result[f][2] for f in indices]


def generate_candidates(api, model_path, source, indices, speed_limits, dt, workers, warm_seeds=None):
    results = {}
    ctx = multiprocessing.get_context('spawn')
    with ProcessPoolExecutor(max_workers=workers, mp_context=ctx, initializer=initialize_worker,
                             initargs=(str(api), str(model_path), str(source), speed_limits.tolist(), dt)) as pool:
        futures = {pool.submit(generate_frame, (f, warm_seeds[i]) if warm_seeds is not None else f): f
                   for i, f in enumerate(indices)}
        for future in as_completed(futures):
            frame, q, cost, detail = future.result()
            results[frame] = (q, cost, detail)
            if len(results) <= workers or len(results) % 25 == 0 or len(results) == len(indices):
                print(json.dumps({'stage': 'candidates', 'source_frame': frame, 'complete': len(results),
                                  'total': len(indices), 'candidates': len(q), 'min_cost': float(cost.min())}), flush=True)
    return [(results[f][0], results[f][1]) for f in indices], [results[f][2] for f in indices]


def interpolate_original_frames(qpos, selected_frames, frame_time):
    from scipy.spatial.transform import Rotation, Slerp
    full_indices = np.arange(selected_frames[0], selected_frames[-1] + 1)
    times = np.asarray(selected_frames) * frame_time
    full_times = full_indices * frame_time
    full = np.empty((len(full_indices), 36))
    for column in [0, 1, 2, *range(7, 36)]:
        full[:, column] = np.interp(full_times, times, qpos[:, column])
    rotations = Rotation.from_quat(qpos[:, [4, 5, 6, 3]])
    full[:, 3:7] = Slerp(times, rotations)(full_times).as_quat()[:, [3, 0, 1, 2]]
    return full, full_indices


NODE_GATES = ('max_target_position_error_m', 'max_foot_penetration_m', 'max_joint_limit_violation_rad')


def node_gate_mask(metrics, thresholds):
    """Only genuine framewise MAX gates; never reinterpret aggregate mean/p95."""
    arrays = [np.asarray(metrics[key]) for key in NODE_GATES]
    require(all(a.ndim == 1 and a.shape == arrays[0].shape and np.all(np.isfinite(a)) for a in arrays),
            'Invalid candidate node metrics')
    require(all(np.isfinite(thresholds[key]) and thresholds[key] >= 0 for key in NODE_GATES), 'Invalid node thresholds')
    return np.logical_and.reduce([value <= thresholds[key] for key, value in zip(NODE_GATES, arrays)])


def filter_pose_nodes(rt, model, source_data, candidates, indices):
    """Prevent soft fit/rate tradeoffs from selecting known hard MAX-gate failures.

    Unfiltered preparation caches remain unchanged. Original intermediate source
    frames still need full QA; passing endpoints cannot certify interpolation.
    """
    import mujoco
    _, names, pos, rot, rest = source_data
    targets, _, _, _, _ = rt.prepare_targets(model, names, pos, rot, rest)
    tasks = rt.make_tasks(model, targets)
    bodies = [model.body(name).id for name in tasks]
    data = mujoco.MjData(model)
    soles = [g for g in range(model.ngeom) if model.geom_type[g] == mujoco.mjtGeom.mjGEOM_SPHERE
             and model.body(model.geom_bodyid[g]).name in ['left_ankle_roll_link', 'right_ankle_roll_link']]
    require(len(soles) == 8, 'Expected original eight plantar spheres')
    bounds = model.jnt_range[1:]
    filtered, mappings, rows = [], [], []
    for frame, (qpos, costs) in zip(indices, candidates):
        errors, penetration = [], []
        desired = np.asarray([targets[name][frame] for name in tasks])
        for q in qpos:
            data.qpos[:] = q
            mujoco.mj_kinematics(model, data)
            errors.append(float(np.max(np.linalg.norm(data.xpos[bodies] - desired, axis=1))))
            penetration.append(max(0., -float(np.min(data.geom_xpos[soles, 2] - model.geom_size[soles, 0]))))
        violation = np.maximum(0., np.maximum(np.max(bounds[:, 0] - qpos[:, 7:], axis=1),
                                             np.max(qpos[:, 7:] - bounds[:, 1], axis=1)))
        metrics = dict(zip(NODE_GATES, [np.asarray(errors), np.asarray(penetration), violation]))
        mask = node_gate_mask(metrics, rt.GATES)
        mappings.append(np.flatnonzero(mask))
        filtered.append((qpos[mask], costs[mask]))
        rows.append({'source_frame': int(frame), 'total': len(qpos), 'allowed': int(np.count_nonzero(mask)),
                     'best_target_position_error_m': min(errors)})
    return filtered, mappings, {'thresholds': {key: rt.GATES[key] for key in NODE_GATES},
                                'frames': rows, 'rejected_candidates': sum(row['total'] - row['allowed'] for row in rows),
                                'empty_source_frames': [row['source_frame'] for row in rows if not row['allowed']],
                                'aggregate_direction_gates_remain_in_full_trajectory_qa': True}


def refine_interpolation(rt, api, model, model_path, source, source_data, full, full_indices,
                         key_indices, speed_limits, workers, before_qa):
    """Refit only failing original midpoint frames, keeping both keys fixed.

    With stride two the midpoint intervals are disjoint. Every replacement is
    constrained against both unchanged original neighboring keys. No qpos clip,
    keyframe move, timestamp change, or endpoint-only acceptance is involved.
    """
    require(np.all(np.diff(key_indices) == 2), 'Midpoint refinement requires stride two')
    key_set = set(key_indices)
    bad = [row['source_frame'] for row in before_qa['per_frame']
           if (row['max_position_error_m'] > rt.GATES['max_target_position_error_m']
               or row['foot_penetration_m'] > rt.GATES['max_foot_penetration_m'])
           and row['source_frame'] not in key_set]
    if not bad:
        return full, {'attempted_frames': [], 'replaced_frames': [], 'failed_frames': []}
    lookup = {int(f): i for i, f in enumerate(full_indices)}
    dt = source_data[0].frame_time
    output, reports = full.copy(), []
    with ProcessPoolExecutor(max_workers=workers, mp_context=multiprocessing.get_context('spawn'),
                             initializer=initialize_worker,
                             initargs=(str(api), str(model_path), str(source), speed_limits.tolist(), dt)) as pool:
        futures = []
        for frame in bad:
            i = lookup[frame]
            require(0 < i < len(full) - 1 and frame - 1 in key_set and frame + 1 in key_set,
                    'Refinement frame is not between fixed neighboring original keys')
            futures.append(pool.submit(generate_pair_bridge_frame,
                (frame, full[i:i + 1], np.array([0.]), [(full[i - 1], full[i + 1], dt, dt)], False)))
        for future in as_completed(futures):
            frame, q, costs, detail = future.result()
            if len(q):
                eligible, _, _ = filter_pose_nodes(rt, model, source_data, [(q, costs)], [frame])
                q, costs = eligible[0]
            replaced = bool(len(q))
            if replaced:
                output[lookup[frame]] = q[np.argmin(costs)]
            reports.append(dict(detail, replaced=replaced))
    return output, {'attempted_frames': bad,
                    'replaced_frames': sorted(r['source_frame'] for r in reports if r['replaced']),
                    'failed_frames': sorted(r['source_frame'] for r in reports if not r['replaced']),
                    'details': reports, 'all_original_frames_must_be_rechecked': True}


def evaluate_path(rt, model, source_data, qpos, indices, speed_limits, *, label):
    """Same GATES/targets/plantar geometry as the owner API, on every provided row."""
    import mujoco
    motion, source_names, pos, rot, rest = source_data
    targets, orientations, _, chains, calibration = rt.prepare_targets(model, source_names, pos, rot, rest)
    tasks = rt.make_tasks(model, targets)
    data = mujoco.MjData(model)
    body_ids = {n: model.body(n).id for n in targets}
    source_ids = {n: i for i, n in enumerate(source_names)}
    soles = [g for g in range(model.ngeom) if model.geom_type[g] == mujoco.mjtGeom.mjGEOM_SPHERE
             and model.body(model.geom_bodyid[g]).name in ['left_ankle_roll_link', 'right_ankle_roll_link']]
    require(len(soles) == 8, 'Expected original eight plantar spheres')
    errors, angles, penetration, per_frame = [], [], [], []
    collisions = 0
    for q, frame in zip(qpos, indices):
        data.qpos[:] = q
        mujoco.mj_forward(model, data)
        err = {name: float(np.linalg.norm(data.xpos[body_ids[name]] - targets[name][frame])) for name in tasks}
        angle = {}
        for a, b, sa, sb in chains:
            robot = rt.unit(data.xpos[body_ids[b]] - data.xpos[body_ids[a]])
            human = rt.unit(pos[frame, source_ids[sb]] - pos[frame, source_ids[sa]])
            angle[f'{sa}->{sb}'] = float(np.degrees(np.arccos(np.clip(np.dot(robot, human), -1, 1))))
        lowest = np.min(data.geom_xpos[soles, 2] - model.geom_size[soles, 0])
        pen = max(0.0, -float(lowest))
        penetration.append(pen); errors.append(list(err.values())); angles.append(list(angle.values()))
        collisions += int(any(c.dist < -0.005 for c in data.contact[:data.ncon]))
        per_frame.append({'source_frame': int(frame), 'source_time_s': float(frame * motion.frame_time),
                          'mean_direction_degrees': float(np.mean(list(angle.values()))),
                          'max_position_error_m': max(err.values()), 'target_errors_m': err,
                          'foot_penetration_m': pen})
    angles, errors = np.array(angles), np.array(errors)
    bounds, joints = model.jnt_range[1:], qpos[:, 7:]
    violation = max(0.0, float(np.max(bounds[:, 0] - joints)), float(np.max(joints - bounds[:, 1])))
    dt = np.diff(indices) * motion.frame_time
    velocity = np.diff(joints, axis=0) / dt[:, None] if len(qpos) > 1 else np.zeros((1, 29))
    ratio = float(np.max(np.abs(velocity) / speed_limits))
    metrics = {'mean_limb_direction_degrees': float(angles.mean()),
               'p95_limb_direction_degrees': float(np.percentile(angles, 95)),
               'max_joint_limit_violation_rad': violation, 'max_joint_velocity_limit_ratio': ratio,
               'max_foot_penetration_m': max(penetration), 'max_target_position_error_m': float(errors.max())}
    failed = [name for name, value in metrics.items() if value > rt.GATES[name]]
    acceleration = np.diff(velocity, axis=0) / ((dt[1:] + dt[:-1]) / 2)[:, None] if len(qpos) > 2 else np.zeros((1, 29))
    return {'status': 'fail' if failed else 'pass', 'label': label, 'metrics': metrics,
            'thresholds': dict(rt.GATES), 'failed_gates': failed, 'frames': len(qpos),
            'source_sha256': motion.sha256, 'fps': float(1 / motion.frame_time),
            'sampling': 'original source indices; fps describes the source, sparse rows require explicit time',
            'source_frame_indices': np.asarray(indices).tolist(),
            'complete_source_motion': bool(indices[0] == 1 and indices[-1] == len(motion.frames) - 1
                                           and len(indices) == len(motion.frames) - 1),
            'source_start_timestamp_s': float(indices[0] * motion.frame_time),
            'source_end_timestamp_s': float(indices[-1] * motion.frame_time),
            'original_source_end_timestamp_s': float((len(motion.frames) - 1) * motion.frame_time),
            'calibration': calibration, 'per_frame': per_frame,
            'per_chain_mean_degrees': dict(zip(angle, angles.mean(axis=0).tolist())),
            'per_target_max_position_error_m': dict(zip(err, errors.max(axis=0).tolist())),
            'per_target_worst_source_frame': dict(zip(err, [int(indices[i]) for i in errors.argmax(axis=0)])),
            'max_joint_acceleration_rad_s2': float(np.max(np.abs(acceleration))),
            'penetrating_model_contact_frames': collisions,
            'collision_diagnostic_limitation': 'Original model collision masks only; includes floor, not exhaustive self-collision validation',
            'physical_tracking_validated': False}


def save_candidates(output, candidates, indices):
    counts = [len(q) for q, _ in candidates]
    np.savez_compressed(output / 'candidates.npz', qpos=np.concatenate([q for q, _ in candidates]),
                        costs=np.concatenate([c for _, c in candidates]),
                        offsets=np.r_[0, np.cumsum(counts)], source_frame_indices=np.asarray(indices))


def read_candidates(path, indices):
    with np.load(path, allow_pickle=False) as archive:
        require(np.array_equal(archive['source_frame_indices'], indices), 'Candidate cache frames do not match')
        q, cost, offsets = archive['qpos'], archive['costs'], archive['offsets']
        require(offsets.shape == (len(indices) + 1,) and offsets[0] == 0 and offsets[-1] == len(q)
                and np.all(np.diff(offsets) > 0), 'Malformed candidate offsets')
        return [(q[offsets[i]:offsets[i + 1]], cost[offsets[i]:offsets[i + 1]]) for i in range(len(indices))]


def validate_cache_provenance(cached, current, *, seeds_only=False):
    keys = ['source_sha256', 'model_sha256', 'urdf_sha256']
    if not seeds_only:
        keys.append('api_sha256')
    require(all(cached.get(key) == current[key] for key in keys),
            'Candidate cache input/model/API mismatch; old API candidates may be used only as reoptimized warm seeds')


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--api', type=Path, default=Path(__file__).with_name('retarget.py'))
    parser.add_argument('--model', type=Path, required=True, help='Pinned model directory')
    parser.add_argument('--source', type=Path, required=True, help='One source BVH')
    parser.add_argument('--output', type=Path, required=True, help='New result directory')
    parser.add_argument('--start-frame', type=int, default=1)
    parser.add_argument('--stride', type=int, default=10)
    parser.add_argument('--max-frames', type=int, default=100)
    parser.add_argument('--workers', type=int, default=4)
    parser.add_argument('--candidate-cache', type=Path)
    parser.add_argument('--candidates-only', action='store_true', help='Save preparation cache successfully without selecting or accepting a pose trajectory')
    parser.add_argument('--warm-seed-cache', type=Path, help='Candidate cache used only as newly optimized mirror/neighbor starting guesses')
    parser.add_argument('--bridge-cache', type=Path, help='Add velocity-bounded arriving candidates to a matching cache lattice')
    parser.add_argument('--repair-passes', type=int, default=0, help='Repair disconnected boundary neighborhoods; zero bridges every row once')
    parser.add_argument('--repair-window', type=int, default=3)
    parser.add_argument('--bridge-multistart', action='store_true', help='Try target-fit, mirrored and shoulder-pole seeds inside each bridge velocity box')
    parser.add_argument('--bilateral-bridges', action='store_true', help='Optimize middle poses in the intersection of both neighboring velocity boxes')
    parser.add_argument('--sweep-width', type=int, default=0, help='With bridge-cache, bilateral finite-horizon beam sweeps instead of local repair')
    parser.add_argument('--sweep-anchor-frame', type=int, help='Start both outward sweeps from this source keyframe, preserving multiple middle-clip branches')
    parser.add_argument('--baseline', type=Path)
    parser.add_argument('--rate-weight', type=float, default=0.1)
    parser.add_argument('--accel-weight', type=float, default=0.02)
    parser.add_argument('--root-rate-weight', type=float, default=0.01)
    parser.add_argument('--refine-interpolation', action='store_true', help='Refit failing original midpoints inside BOTH fixed neighboring key velocity boxes; stride two only')
    args = parser.parse_args()
    require(1 <= args.workers <= 4 and args.start_frame >= 1 and args.stride >= 1 and args.max_frames >= 0,
            'Invalid bounds; frame0 is explicitly excluded')
    require(args.sweep_width == 0 or (2 <= args.sweep_width <= 64 and args.bridge_cache and not args.repair_passes),
            'Sweep width must be 2..64 with bridge-cache and no repair-passes')
    require(args.sweep_anchor_frame is None or args.sweep_width, 'An anchor needs beam sweep mode')
    require(not (args.bridge_multistart or args.bilateral_bridges) or (args.bridge_cache and not args.sweep_width),
            'Special bridge generation needs bridge-cache and cannot be combined with sweep mode')
    require(not (args.bridge_multistart and args.bilateral_bridges), 'Select one bridge generation method')
    require(not args.refine_interpolation or args.stride == 2, 'Interpolation refinement requires stride two')
    args.output.mkdir(parents=True, exist_ok=False)
    begin = time.perf_counter()
    import mujoco
    rt = load_api(args.api)
    model = mujoco.MjModel.from_xml_path(str(args.model / 'g1.xml'))
    source_data = rt.source_arrays(args.source)
    motion = source_data[0]
    require(args.start_frame < len(motion.frames), 'Start frame is beyond source coverage')
    indices = list(range(args.start_frame, len(motion.frames), args.stride))
    if args.max_frames:
        indices = indices[:args.max_frames]
    else:
        if indices[-1] != len(motion.frames) - 1:
            indices.append(len(motion.frames) - 1)
    require(len(indices) >= 2, 'Need at least two selected frames')
    require(args.sweep_anchor_frame is None or args.sweep_anchor_frame in indices, 'Sweep anchor is not a selected source keyframe')
    urdf_path = args.model / 'g1_29dof_rev_1_0.urdf'
    urdf = ET.parse(urdf_path)
    limits = {j.get('name'): float(j.find('limit').get('velocity')) for j in urdf.findall('joint')
              if j.get('type') == 'revolute' and j.find('limit') is not None}
    joint_names = [model.joint(i).name for i in range(1, model.njnt)]
    speed_limits = np.array([limits[n] for n in joint_names])
    provenance = {'api_sha256': sha256(args.api), 'source_sha256': sha256(args.source),
                  'model_sha256': sha256(args.model / 'g1.xml'), 'urdf_sha256': sha256(urdf_path),
                  'global_path_sha256': sha256(__file__), 'source_frame_indices': indices,
                  'manufacturer_velocity_limits': dict(zip(joint_names, speed_limits.tolist())),
                  'source_frame_time': motion.frame_time, 'frame0_excluded': True,
                  'source_original_end_timestamp_s': (len(motion.frames) - 1) * motion.frame_time,
                  'workers': args.workers, 'all_original_frames_between_keys_evaluated': False,
                  'full_original_frame_qa_required_for_acceptance': True,
                  'sweep_width': args.sweep_width, 'repair_passes': args.repair_passes,
                  'sweep_anchor_source_frame': args.sweep_anchor_frame,
                  'bridge_multistart': args.bridge_multistart,
                  'bilateral_bridges': args.bilateral_bridges,
                  'refine_interpolation': args.refine_interpolation,
                  'repair_window': args.repair_window, 'rate_weight': args.rate_weight,
                  'acceleration_weight': args.accel_weight, 'root_rate_weight': args.root_rate_weight}
    write_json(args.output / 'provenance.json', provenance)
    warm_seeds = None
    if args.warm_seed_cache:
        cached = json.loads((args.warm_seed_cache.parent / 'provenance.json').read_text())
        validate_cache_provenance(cached, provenance, seeds_only=True)
        require(args.candidate_cache is None, 'Use either final candidate reuse or new warm-seed optimization, not both')
        warm_seeds = warm_seeds_from_cache(args.warm_seed_cache, indices, joint_names)
        provenance['warm_seed_cache_sha256'] = sha256(args.warm_seed_cache)
        provenance['warm_seed_api_sha256'] = cached['api_sha256']
        provenance['warm_seed_generator_sha256'] = cached['global_path_sha256']
        provenance['warm_seed_semantics'] = 'Only initial guesses from old cache; all output candidates optimized using current API'
        write_json(args.output / 'provenance.json', provenance)
    if args.bridge_cache:
        require(args.candidate_cache is None and args.warm_seed_cache is None, 'Bridge mode is separate from other cache modes')
        cached = json.loads((args.bridge_cache.parent / 'provenance.json').read_text())
        validate_cache_provenance(cached, provenance)
        base = read_candidates(args.bridge_cache, indices)
        provenance['bridge_cache_sha256'] = sha256(args.bridge_cache)
        provenance['bridge_cache_api_sha256'] = cached['api_sha256']
        provenance['bridge_cache_generator_sha256'] = cached['global_path_sha256']
        provenance['bridge_semantics'] = 'Original candidates plus bilateral local constrained optimization; no output clipping'
        write_json(args.output / 'provenance.json', provenance)
        candidates = base
        candidate_report = []
        require(0 <= args.repair_passes <= 12 and 1 <= args.repair_window <= 20, 'Invalid repair bounds')
        if args.sweep_width:
            candidates, candidate_report = generate_sweeps(args.api, args.model / 'g1.xml', args.source, indices,
                candidates, speed_limits, motion.frame_time, args.workers, args.sweep_width,
                args.rate_weight, args.root_rate_weight,
                None if args.sweep_anchor_frame is None else indices.index(args.sweep_anchor_frame))
        for attempt in range(0 if args.sweep_width else max(1, args.repair_passes)):
            eligible, _, eligibility = filter_pose_nodes(rt, model, source_data, candidates, indices)
            broken = ([indices.index(f) for f in eligibility['empty_source_frames']]
                      if eligibility['empty_source_frames'] else
                      disconnected_boundaries(eligible, np.asarray(indices) * motion.frame_time, speed_limits))
            if args.repair_passes and not broken:
                break
            active = None if not args.repair_passes else {i for boundary in broken
                for i in range(max(0, boundary - args.repair_window), min(len(indices), boundary + args.repair_window + 1))}
            print(json.dumps({'stage': 'repair', 'attempt': attempt + 1,
                              'disconnected_source_frames': [indices[i] for i in broken],
                              'active_frames': len(indices) if active is None else len(active)}), flush=True)
            candidates, details = generate_bridges(args.api, args.model / 'g1.xml', args.source, indices,
                                                     candidates, speed_limits, motion.frame_time, args.workers, active,
                                                     args.bridge_multistart, joint_names, args.bilateral_bridges, eligible)
            candidate_report.append({'attempt': attempt + 1, 'before_disconnected_source_frames': [indices[i] for i in broken],
                                     'frames': details})
    elif args.candidate_cache:
        cached_provenance = json.loads((args.candidate_cache.parent / 'provenance.json').read_text())
        validate_cache_provenance(cached_provenance, provenance)
        candidates = read_candidates(args.candidate_cache, indices)
        candidate_report = {'cache_sha256': sha256(args.candidate_cache)}
        provenance['candidate_cache_sha256'] = candidate_report['cache_sha256']
        provenance['candidate_cache_generator_sha256'] = cached_provenance['global_path_sha256']
        write_json(args.output / 'provenance.json', provenance)
    else:
        candidates, candidate_report = generate_candidates(args.api, args.model / 'g1.xml', args.source, indices,
                                                            speed_limits, motion.frame_time * args.stride, args.workers, warm_seeds)
    validate_lattice(candidates, np.asarray(indices) * motion.frame_time, speed_limits)
    save_candidates(args.output, candidates, indices)
    write_json(args.output / 'candidate_generation.json', candidate_report)
    if args.candidates_only:
        status = {'status': 'candidates_only', 'pose_status': 'not_evaluated',
                  'selected_keyframes': len(indices), 'candidate_count': sum(len(q) for q, _ in candidates),
                  'entire_clip_validated': False, 'physical_tracking_validated': False,
                  'wall_seconds': time.perf_counter() - begin}
        write_json(args.output / 'candidate_status.json', status)
        print(json.dumps(status), flush=True)
        return 0
    candidates, candidate_mappings, filter_report = filter_pose_nodes(rt, model, source_data, candidates, indices)
    write_json(args.output / 'candidate_pose_filter.json', filter_report)
    if filter_report['empty_source_frames']:
        failure = {'status': 'fail', 'stage': 'candidate_pose_filter',
                   'empty_source_frames': filter_report['empty_source_frames'],
                   'entire_clip_validated': False, 'physical_tracking_validated': False}
        write_json(args.output / 'path_failure.json', failure)
        print(json.dumps(failure), flush=True)
        return 2
    try:
        path, objective = select_path(candidates, np.array(indices) * motion.frame_time, speed_limits,
                                      rate_weight=args.rate_weight, accel_weight=args.accel_weight,
                                      root_rate_weight=args.root_rate_weight)
    except ValueError as error:
        broken = disconnected_boundaries(candidates, np.asarray(indices) * motion.frame_time, speed_limits)
        failure = {'status': 'fail', 'stage': 'candidate_path_selection', 'message': str(error),
                   'disconnected_source_frames': [indices[i] for i in broken],
                   'entire_clip_validated': False, 'physical_tracking_validated': False,
                   'wall_seconds': time.perf_counter() - begin}
        write_json(args.output / 'path_failure.json', failure)
        print(json.dumps(failure), flush=True)
        return 2
    sparse = np.array([candidates[i][0][choice] for i, choice in enumerate(path)])
    full, full_indices = interpolate_original_frames(sparse, indices, motion.frame_time)
    sparse_qa = evaluate_path(rt, model, source_data, sparse, indices, speed_limits, label='selected sparse keyframes only')
    full_qa = evaluate_path(rt, model, source_data, full, full_indices, speed_limits, label='all original frames in selected span')
    if args.refine_interpolation:
        write_json(args.output / 'pre-refinement-pose_retarget_qa.json', full_qa)
        full, refinement = refine_interpolation(rt, args.api, model, args.model / 'g1.xml', args.source,
            source_data, full, full_indices, indices, speed_limits, args.workers, full_qa)
        write_json(args.output / 'interpolation_refinement.json', refinement)
        full_qa = evaluate_path(rt, model, source_data, full, full_indices, speed_limits,
                               label='all original frames; failed interpolated midpoints refitted with bilateral velocity bounds')
    provenance['all_original_frames_between_keys_evaluated'] = True
    write_json(args.output / 'provenance.json', provenance)
    for report in [sparse_qa, full_qa]:
        report.update({key: provenance[key] for key in ['model_sha256', 'urdf_sha256', 'api_sha256', 'global_path_sha256']})
    write_json(args.output / 'sparse-pose_retarget_qa.json', sparse_qa)
    write_json(args.output / 'pose_retarget_qa.json', full_qa)
    write_json(args.output / f'{args.source.stem[0]}-pose_retarget_qa.json', full_qa)
    body_names = [model.body(i).name for i in range(1, model.nbody)]
    np.savez_compressed(args.output / 'sparse.npz', qpos=sparse, source_frame_indices=indices,
                        time=np.asarray(indices) * motion.frame_time, joint_names=joint_names, body_names=body_names)
    np.savez_compressed(args.output / f'{args.source.stem[0]}.npz', qpos=full, source_frame_indices=full_indices,
                        time=full_indices * motion.frame_time, fps=1 / motion.frame_time,
                        joint_names=joint_names, body_names=body_names)
    comparison = {'sparse': sparse_qa['metrics'], 'all_original_frames': full_qa['metrics'],
                  'path': [int(mapping[choice]) for mapping, choice in zip(candidate_mappings, path)],
                  'path_index_semantics': 'Indices refer to original unfiltered candidates.npz rows',
                  'objective': objective,
                  'prototype_span_pass': sparse_qa['status'] == full_qa['status'] == 'pass',
                  'entire_clip_validated': full_qa['complete_source_motion'] and full_qa['status'] == 'pass',
                  'wall_seconds': time.perf_counter() - begin,
                  'source_frame_581': next((row for row in full_qa['per_frame'] if row['source_frame'] == 581), None),
                  'physical_tracking_validated': False}
    if args.baseline:
        with np.load(args.baseline, allow_pickle=False) as archive:
            baseline = archive['qpos']; baseline_indices = archive['source_frame_indices']
            require(archive['joint_names'].astype(str).tolist() == joint_names, 'Baseline joint order differs')
        require(np.array_equal(baseline_indices, indices), 'Baseline frame coverage differs')
        baseline_qa = evaluate_path(rt, model, source_data, baseline, indices, speed_limits, label='existing greedy baseline')
        write_json(args.output / 'baseline-pose_retarget_qa.json', baseline_qa)
        comparison['baseline'] = baseline_qa['metrics']
        comparison['baseline_frame_581'] = next((r for r in baseline_qa['per_frame'] if r['source_frame'] == 581), None)
    write_json(args.output / 'comparison.json', comparison)
    print(json.dumps(comparison), flush=True)
    return 0 if comparison['prototype_span_pass'] else 2


if __name__ == '__main__':
    raise SystemExit(main())
