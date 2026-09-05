# Validation scope

The bundled export is a **kinematic reference**, not an executable robot dance.
All A/B/C clips passed an independent model-based re-evaluation on 2026-09-05.
The checked-in `bin/data/pose_retarget_qa.json` contains exact metrics, thresholds,
source/reference hashes, and calibration assumptions; `provenance.json` pins
the model, manufacturer velocity limits, geometry and generated files.

## Complete source-frame pose checks

Each clip contains 1,299 frames at 40 Hz, retaining original frames 1–1299.
Source frame 0 has a large discontinuity and is excluded only from the derived
dance, not deleted from the recording. The source-time offset is 0.025 s and
the last source timestamp is 32.475 s. Playback is 32.475 s with the final frame
held for one sample; the first-to-last sample span is 32.450 s.

| Clip | Mean limb-direction error | 95th percentile | Maximum target-position error | Maximum sole penetration |
| --- | ---: | ---: | ---: | ---: |
| A | 3.86° | 11.77° | 13.88 cm | 2.88 mm |
| B | 2.44° | 6.15° | 7.85 cm | 0 mm |
| C | 2.59° | 5.96° | 6.18 cm | 0 mm |

All samples remain inside the model's joint-position limits. Adjacent-frame
joint speeds remain within the manufacturer's per-joint velocity limits,
allowing only numerical tolerance (maximum ratio 1.00000000000002).
The unchanged acceptance gates are mean direction ≤18°, 95th percentile ≤40°,
target-position error ≤18 cm, sole penetration ≤25 mm, joint-limit violation
≤0.00001 rad and speed-limit ratio ≤1.00001. These are engineering study
criteria, **not certification or hardware-safety limits**.

Human and robot proportions differ: the poses are adapted, not reproduced
exactly. Units and source axes are explicit assumptions recorded in each
clip's calibration report. The reset from the last frame to the first is a
viewer loop boundary, not a validated continuous robot transition. Recentring
and side-by-side offsets are presentation transforms, not stage formation.

## Physical simulation is not approved

A separate, bounded NVIDIA ProtoMotions/BONES-SEED tracking experiment used the
full B reference for two simulated seconds, under gravity, contacts and finite
actuator forces. It remained upright, but **failed** its 20 mm maximum contact
penetration gate: the measured maximum was **58.43 mm**. Recorded-state
inspection identified hand/thigh and other self-contact, not merely ground
penetration. This exploratory result does not validate A, C, longer tracking,
balance recovery or the real robot. No collision masks or thresholds were
relaxed to obtain a pass.

The experiment used ProtoMotions revision
`607ca7a0bb92e261120bcab8d9f97f28b3130ffc` and a checkpoint with SHA-256
`a59baa3e04a951e5cf0b4cc68f24ebaafa9272714226618b99a5017dfc805b4c`.
Neither the controller nor checkpoint is included in these graphics examples.
There is no robot SDK, DDS endpoint or actuator command path.

## Computation and display

Retargeting ran on an RTX 4090 workstation. The constrained IK and temporal
path search used its CPU; EGL reference rendering used its NVIDIA GPU.
Full B/C solves took about 36.29/29.11 seconds. A needed multi-stage candidate
and temporal-path research; those B/C timings do not describe A or a complete
fresh reproduction run. No paid cloud resources were used.

Rendering, pose checks, and dynamic tracking are distinct tests. The viewer
uses fixed robot offsets and parent-relative quaternion interpolation so rigid
links remain connected between source samples. A successful screenshot or
build cannot override a failed pose report or imply physical feasibility.
