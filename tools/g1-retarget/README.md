# G1 kinematic reference reproduction kit

**Moved:** the maintained reproduction kit now lives in
[`perfume-dev/g1-motion-lab/tools/g1-retarget`](https://github.com/perfume-dev/g1-motion-lab/tree/main/tools/g1-retarget).
This copy and its instructions remain a historical snapshot for existing
checkouts. Use the standalone repository for new G1 development and issues.

This code adapts the original Perfume A/B/C BVH recordings to the pinned
29-DOF Unitree G1 model, evaluates the resulting poses, and exports the data
used by [G1 Motion Lab](../../example-g1-motion-lab/).

It is **kinematic reference preparation, not a trained controller or physical
simulation approval**. It does not establish balance, contact feasibility,
self-collision safety, finite-force tracking, or hardware safety. There are no
policy weights or hardware-control commands in this kit.

## Snapshot status

The Python modules retain their original sibling `preflight/` and `retarget/`
layout and are copied without changes. [SOURCE_SNAPSHOT.json](SOURCE_SNAPSHOT.json)
records their SHA-256 values. The complete A/B/C recordings were validated with
this solver family: A uses the six-stage candidate-path recipe below, while
B/C use the full-rate sequential solver. The final A reference contains all
1,299 original dance frames and passes the unchanged pose gates. This is a
source-specific, tested kinematic workflow, not a general guarantee that any
motion or modified model will pass. Always run the independent exporter.

No models, generated motions, diagnostic probes, caches, screenshots, or machine
credentials are bundled in this directory. Generated outputs can contain paths
from the operator's environment; inspect them before sharing them publicly.

## Environment and execution boundary

Use a workstation you already control, with a separate Python 3.12 environment.
The core versions in `requirements.txt` were tested on Linux with Python 3.12;
they are not a complete lock of every transitive package. Record the resolved
environment for each run. Do not install into a system Python environment.

Run lengthy IK, full-clip QA, and mesh export on the remote workstation, not on
the interactive laptop. The IK and path-search work here is CPU work; owning a
GPU does not make this solver GPU-accelerated. GPU rendering belongs to the
separate viewer/rendering workflow, and no CUDA, training, or policy deployment
is performed by these commands. They do not provision a machine, connect to a
robot, rent a cloud instance, or authorize a paid service.

If using SSH, choose your own existing host explicitly. `GPU_HOST` is just a
user-supplied alias, not a bundled destination or a requirement for GPU IK:

```sh
GPU_HOST="your-ssh-alias"
ssh "$GPU_HOST" hostname
ssh "$GPU_HOST" uptime
ssh "$GPU_HOST" 'df -h .'
# Inspect existing jobs and available resources before entering the host.
ssh "$GPU_HOST"
```

Use an existing checkout on that workstation. Keep other users' processes and
services running; choose another time or host if resources are occupied.

From the openFrameworks repository on that workstation:

```sh
cd tools/g1-retarget
G1_REPO_ROOT="$(git rev-parse --show-toplevel)"
python3.12 -m venv .venv
. .venv/bin/activate
python -m pip install -r requirements.txt
python -m pip check
mkdir -p output
python -m pip freeze > output/environment.txt

# Keep one numerical-library thread per worker; run only one job at a time.
export OMP_NUM_THREADS=1 OPENBLAS_NUM_THREADS=1 MKL_NUM_THREADS=1
export NUMEXPR_NUM_THREADS=1 VECLIB_MAXIMUM_THREADS=1
```

`global_path.py` permits 1–4 workers and defaults to 4. Do not run several
four-worker jobs concurrently. The A/B/C loop below is sequential.

## Verify code and the original recordings

The parser tests need only the Python standard library. The other unit tests
need the isolated environment above; they do not fetch a model or solve a dance.

```sh
python -m unittest discover -s preflight -p 'test_*.py' -v
python -m unittest discover -s retarget -p 'test_*.py' -v
```

The final eight-module snapshot passed all 37 unit tests on the pinned Linux
environment: 9 BVH tests, 19 path/refinement tests and 9 export-contract tests.
These lightweight tests do not replace the full-motion pose/export gates.

The guarded exporter accepts the byte-identical original recordings in
`example-bvh/bin/data`, with 1,300 frames and a 0.025-second interval (40 Hz).
Line-ending changes also change their hashes.

| File | SHA-256 |
| --- | --- |
| `A_test.bvh` | `bb53e8e80491830b5ad1ae2a649d44aa7c65e683161d3b456c9279a533ec563e` |
| `B_test.bvh` | `74811741939eaf3ff7e41336ed1bacc5f287879d58dc61d1d738d1c69df7725c` |
| `C_test.bvh` | `7d99f23d74a241a7ce2cb3c12004b6b9951866c7e65fa0d1150ee6e39435b469` |

To audit and compare an independently checked-out Processing copy, set its
actual data directory explicitly; do not compare the openFrameworks directory
to itself and call that an independent match:

```sh
G1_PROCESSING_DATA="/path/to/example-processing/p5f_sample/data"
python preflight/bvh_audit.py \
  --processing-data "$G1_PROCESSING_DATA" \
  --of-data "$G1_REPO_ROOT/example-bvh/bin/data" \
  --output output/bvh-audit.json
```

The audit reports byte equality, hierarchy, declared channel ordering and
frame-zero differences. It does not silently correct or rescale the recordings.

### Coordinates, morphology, and time

BVH does not declare global axes or distance units. The source-specific
retarget convention explicitly assumes centimetres, source +Y up and +Z
forward; the basis maps `(x, y, z)` to robot `(z, x, y)` and scales by `0.01`.
Robot output is metres, +Z up, +X forward, with world quaternions in WXYZ order.
Forward kinematics respects each joint's declared channel order.

Human-to-robot morphology is not an exact same-rig transfer: locomotion scale
uses a leg-chain ratio, individual segment lengths use the robot model, and
pelvis-origin and foot-floor adaptations are recorded in calibration reports.
The source overlay receives the same global origin, orientation and leg scale,
while human proportion differences remain visible. The viewer may additionally
recenter each robot/source pair in XY for a three-column composition; that is
not the original stage formation. No inferred contact heuristic is contact truth.

Frame 0 is treated as an initialization/reference sample and excluded from
the dance. Accepted references must retain **all original frames 1–1299**,
their original 40 Hz rate, and their source indices. The first retained sample
is at source time 0.025 s, the last at 32.475 s; the first-to-last retained
sample span is 32.450 s. A viewer clock starting at zero therefore has a
0.025-second source-time offset. Do not pad with frame 0, reorder time, or call
a sparse diagnostic span the full dance.

## Fetch and verify the pinned model

Run the following only when you intend to download the public model assets,
using a new model directory. The fetcher is not a preservation workflow for
edited local models. It is not a model-training or cloud-spending step:

```sh
python retarget/fetch_model.py --output models/unitree_g1
```

The fetcher uses commit-pinned URLs for
[MuJoCo Menagerie G1](https://github.com/google-deepmind/mujoco_menagerie/tree/8161bba264d7fa7c99ca301e91e7fb44737676ad/unitree_g1)
and the manufacturer's
[29-DOF URDF](https://github.com/unitreerobotics/unitree_ros/blob/7d6075f7f58588b189b940130e3edab3c839b2df/robots/g1_description/g1_29dof_rev_1_0.urdf).
The export boundary verifies the pinned XML, URDF, the exact 40-file manifest,
and every listed asset's bytes, length and SHA-256. It also rejects paths that
escape the model directory. A modified manifest is not a way to approve
modified geometry. Keep the upstream `LICENSE` with downloaded or derived
model assets.

## Generate the final A reference

Use fresh output directories: the global path command refuses existing ones.
`--max-frames 0` is required below to cover the entire clip; the default limit
of 100 selected keyframes is for research spans, not publication.

The successful A workflow first solves 650 independent 20 Hz keyframes, adds
alternative shoulder branches, propagates multiple states forward/backward,
and repairs quality-and-velocity connectivity around the remaining breaks.
The anchor at **source frame 581** is specific to the pinned A recording.
It is not a timestamp shift, skipped interval, or pose copied into the output.

All candidate stages below use the same solver API and pinned inputs. An old
API cache may be used as newly optimized warm seeds, but not mixed into a
bridge or final selection with old objective values. Cache provenance is
checked before reuse.

```sh
g1_a() {
  python retarget/global_path.py \
    --model models/unitree_g1 \
    --source "$G1_REPO_ROOT/example-bvh/bin/data/A_test.bvh" \
    --start-frame 1 --stride 2 --max-frames 0 --workers 4 \
    --accel-weight 0 "$@"
}

(
  set -e

  # 1. Independent multistart candidates at every selected source keyframe.
  g1_a --output output/A-01-independent --candidates-only

  # 2. Refit mirrored-shoulder and neighboring-key initial guesses.
  g1_a --output output/A-02-warm \
    --warm-seed-cache output/A-01-independent/candidates.npz \
    --candidates-only

  # 3. Multiple reachable states propagated from both clip endpoints.
  g1_a --output output/A-03-sweeps \
    --bridge-cache output/A-02-warm/candidates.npz \
    --sweep-width 16 --candidates-only

  # 4. Propagate the middle-clip branch outward in both time directions.
  g1_a --output output/A-04-anchor \
    --bridge-cache output/A-03-sweeps/candidates.npz \
    --sweep-width 16 --sweep-anchor-frame 581 --candidates-only

  # 5. One bounded, multi-initialization repair near quality/velocity breaks.
  g1_a --output output/A-05-bridges \
    --bridge-cache output/A-04-anchor/candidates.npz \
    --repair-passes 1 --repair-window 20 --bridge-multistart \
    --candidates-only

  # 6. Exact finite-candidate path selection and all-original-frame QA.
  g1_a --output output/A-06-validated \
    --candidate-cache output/A-05-bridges/candidates.npz \
    --refine-interpolation
)
```

`--candidates-only` deliberately returns success after saving a preparation
cache, with `pose_status=not_evaluated` and `entire_clip_validated=false`.
It does **not** accept a trajectory. This permits a normal stop-on-error shell
workflow without disguising an intermediate infeasible path as a valid dance.

The final path uses hard manufacturer velocity edges and a first-order dynamic
program over the saved candidates. Its objective includes fit and rate costs.
`--accel-weight 0` makes acceleration diagnostic-only; no acceleration or torque
safety limit is claimed. Candidate generation uses finite beams and local
optimization, so exact path selection is not a proof of a continuous global
optimum. Framewise maximum-error gates exclude unacceptable candidate nodes;
mean and p95 direction gates remain full-trajectory checks.

Every original 40 Hz frame is evaluated after interpolation. In the recorded
A run, only source frames 546 and 624 failed the interpolated position gate.
`--refine-interpolation` reoptimized those midpoints inside the intersection
of both fixed neighboring keyframes' velocity boxes and the manufacturer
joint ranges, then rechecked all 1,299 frames. No keyframe, timestamp or output
joint value was clipped or moved afterward to hide a failure. If a midpoint
cannot be repaired within those bounds, the full reference remains rejected.

The final directory contains `A.npz`, explicit source indices/timestamps,
provenance, separate sparse/full QA, the pre-refinement QA, and
`interpolation_refinement.json`. The verified A run measured mean direction
error 3.861°, p95 11.774°, maximum target-position error 0.138822 m and plantar
penetration 0.002879 m, with no joint-range violation and manufacturer speed
ratio 1.00000000000002 (floating-point tolerance, below the fixed 1.00001 gate).
These are recorded results, not prefilled acceptance criteria.

Exit code 2 means the selected span failed its pose/path criteria; stop and
inspect the reports. Invalid inputs or unexpected execution errors also fail.
Do not suppress a final failure, weaken thresholds, or publish diagnostics as
an accepted reference. Full A/B/C references are accepted for the viewer only
after the independent exporter below passes.

## Generate B and C sequentially at the original rate

B/C passed the underlying bounded sequential solver at all 1,299 original
40 Hz dance frames. The command processes B and then C, not simultaneous jobs:

```sh
python retarget/retarget.py \
  --model models/unitree_g1 \
  --source "$G1_REPO_ROOT/example-bvh/bin/data" \
  --clips BC --method direction \
  --start-frame 1 --stride 1 --max-frames 0 \
  --output output/BC-sequential
```

Use a new output directory for this step too. `B.npz` and `C.npz` retain source
indices and the original frame rate. Direct graphical output from this solver
is diagnostic, even when its pose report passes; do not copy it directly into
the bundled viewer. The independent exporter is the publication boundary.

### Time and resource costs

The scripts use local CPU, memory and disk on the selected workstation; they
do not call a paid API, rent cloud compute, or download policy weights. Model
and Python dependency downloads still consume bandwidth and storage. A GPU is
useful for the separate viewers/rendering, not for this IK or path solver.

In the recorded four-worker diagnostic runs, the endpoint sweep took about
4 min 19 s, the midpoint-anchor sweep 2 min 12 s, and the bounded multistart
repair 7 min 18 s. Final cached selection, two midpoint refinements and full
QA took about 4 s. The initial two candidate-generation stages add further
minutes. B and C each took roughly half a minute with the sequential solver.
These are machine- and input-specific observations, not benchmarks or a fixed
completion time; the candidate-only recipe omits intermediate trajectory QA.
Keep the generated caches to resume safely instead of rerunning solved stages.

## Independently gate and export A/B/C

The output directory must not already exist. The exporter rechecks the complete
reference arrays, original source hashes, model assets and the actual poses;
it does not trust a solver's earlier `pass` label.

```sh
python retarget/export_viewer.py \
  --model models/unitree_g1 \
  --source "$G1_REPO_ROOT/example-bvh/bin/data" \
  --a output/A-06-validated/A.npz \
  --b output/BC-sequential/B.npz \
  --c output/BC-sequential/C.npz \
  --output output/viewer-validated
```

Only if every gate passes for all three complete recordings are
`g1-motion.json`, `g1-model.json`, `MODEL-LICENSE` and provenance emitted. If
pose gates fail, `pose_retarget_qa.json` records the failure and no viewer
motion/model package is emitted. Invalid inputs fail before acceptance.
The accepted motion includes fixed parent-relative `body_offsets`; viewers
reconstruct child positions from those offsets and interpolate local rotations
so limb lengths do not collapse between samples. This is display interpolation,
not a physical contact or control model.

The fixed gates are mean limb-direction error ≤18°, p95 ≤40°, joint-limit
violation ≤0.00001 rad, manufacturer joint-speed ratio ≤1.00001, actual plantar
sphere penetration ≤0.025 m, and maximum target-position error ≤0.18 m.
Passing these is still **not** dynamic balance or hardware approval.

Review the QA and provenance before an explicit update of viewer assets.
Convex hulls are lightweight visual representations only; they do not replace
the simulator's original collision, mass or inertia model. No command above
automatically installs generated files into the example or publishes them.

## Rights

[MIT](LICENSE), Copyright (c) 2026 Daito Manabe, applies to this new code and
documentation only. It does not grant rights to Perfume performances, names,
music, recordings, motion data, third-party models or dependency packages.
Original Perfume data terms remain unchanged; see the repository's
[rights and attribution information](../../README.md) and [license index](../../LICENSES.md).
The pinned Unitree model retains its upstream BSD-3-Clause notice; downloaded
files and derived geometry must retain the applicable upstream notices.
