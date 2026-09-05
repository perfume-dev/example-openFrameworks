# G1 Motion Lab

A quiet, inspectable **kinematic reference viewer** for the Perfume A / B / C
motion studies retargeted to the Unitree G1. The actual G1 body geometry is drawn
using lightweight convex hulls derived from the official robot model, not a
generic stick figure. White, ink, and short cyan hand trails keep the robot and
its motion easy to read.

**This example does not contain a physics engine or a learned controller.** It
does not establish dynamic balance, contact feasibility, torque feasibility, or
real-robot safety. The warning remains visible even when help is hidden.

If `validation.pose_status` is anything other than `pass`, a second persistent
warning reads **POSE QA FAILED — DIAGNOSTIC ONLY**. A successful render check of
such an export validates the graphics path only; it does not approve that
motion for publication or physical tracking. Missing pose status is not a pass.

The bundled A/B/C references pass the independent pose gates at all 1,299
retained samples per clip, at the original 40 Hz. This is a constrained
kinematic result only; physical tracking remains unvalidated. See the supplied
`bin/data/pose_retarget_qa.json` and `bin/data/provenance.json` for the measured
criteria, original-data hashes, model revision, and limitations.

## Run

Use openFrameworks **0.12.1**, a desktop OpenGL 3.2 context, and the repository's
local `ofxBvh` addon (required by the shared `ExampleRuntime.h` include). Place
the repository at `OF_ROOT/apps/myApps/example-openFrameworks` to use the included
Xcode project, or pass an explicit `OF_ROOT` to Make:

```sh
make Release -j4 OF_ROOT=/absolute/path/to/openFrameworks
make RunRelease OF_ROOT=/absolute/path/to/openFrameworks
```

The viewer requires real exported `bin/data/g1-motion.json` and
`bin/data/g1-model.json`. Missing or malformed data causes an explicit error and
a nonzero exit. There is no synthetic fallback or successful dummy capture.

## Controls

| Control | Action |
| --- | --- |
| A / B / C or 1 / 2 / 3 | Inspect one clip |
| 0 or 4 | Show all three side by side |
| Space | Pause / resume |
| R | Reset playback and trails; remains paused if already paused |
| S | Toggle the original BVH skeleton overlay |
| H | Toggle help, not the validation warning |
| Drag / scroll | Orbit / zoom |
| V | Reset camera framing |

Each robot is recentered horizontally at its root for presentation. In the
three-robot view, the columns are display offsets, **not the original stage
formation**. The same display transform is applied to its original BVH overlay.
The exported vertical coordinate is preserved: floor penetration and floating
are not corrected or hidden. The floor is a reference grid, not a collider.

Only the robot root's world translation is linearly interpolated. Rotations
are converted to parent-relative quaternions, interpolated with slerp, then
composed through the hierarchy with the model's fixed body offsets. This keeps
rigid links attached between samples; independently interpolating every body's
world position would shorten or separate links during fast rotations. Original
key poses are retained within the export's rounding precision. The source BVH
comparison overlay uses linear position interpolation.

Trails reset at clip boundaries and view switches. Time is calculated from the
exported FPS and sample count; it is not solver time. Original reference frame 0
is excluded, so the viewer's first sample corresponds to source time 0.025 s.

## Render checks

On macOS, from this example directory:

```sh
bin/example-g1-motion-lab.app/Contents/MacOS/example-g1-motion-lab \
  --smoke-test --capture=/absolute/path/g1-all.png --capture-frame=480
bin/example-g1-motion-lab.app/Contents/MacOS/example-g1-motion-lab \
  --smoke-test --clip=A --source-overlay \
  --capture /absolute/path/g1-source.png --capture-frame 960
bin/example-g1-motion-lab.app/Contents/MacOS/example-g1-motion-lab \
  --smoke-test --clip=A --source-overlay \
  --capture /absolute/path/g1-source-hard.png --capture-frame 860
```

On Linux the executable is `bin/example-g1-motion-lab`; use an actual GL context
(Mesa / Xvfb is suitable for software rendering). `--frames=N` aliases
`--capture-frame=N`. Any capture run is deterministic and uses a hidden GLFW
window without taking focus. The shared runtime requires nonempty geometry,
non-flat and changed rendered content, no GL error, a hidden unfocused window,
and a successfully saved capture before reporting `SMOKE_TEST_OK`. Pause,
paused reset, clip selection, source overlay, and help controls are also checked.
Every retained keyframe is checked against the exported world pose. Every
adjacent pair is sampled at one-third, one-half and two-thirds to check rigid
anchor consistency. These checks validate interpolation geometry, not a
different retarget or a physical controller.

These are **render checks, not physics validation**. The smoke test does not
evaluate retargeting quality or infer balance from an attractive image.
From the repository root, `scripts/smoke-all.sh /absolute/path/captures --g1-only`
runs four G1 views plus all input-rejection checks without rerunning the other
examples. Omitting `--g1-only` preserves the full nine-example suite.

## Data and drawing pipeline

`g1-motion.json` schema 1 contains `fps`, robot `body_names` / `body_parents`,
and `body_offsets` (one finite XYZ vector per body, from the model's fixed
parent-relative body positions). Body 0 must be the root and each remaining
parent must precede its children. The root's stored rest offset is not added
to its independently exported world position. The model offsets must agree
with every exported key pose within 0.1 mm, otherwise loading fails. It also contains
`source_names` / `source_parents`, and exactly three clips named A, B, C. Each
frame has flattened arrays `positions` (world metres, Z-up), `rotations` (world
quaternions, **wxyz**), and `source_positions` (original BVH transformed into the
same robot axes and metres). The required validation label is
`KINEMATIC REFERENCE — PHYSICS NOT VALIDATED`.

`g1-model.json` contains indexed triangle meshes. Each mesh identifies a `body`
and stores local `vertices`, `indices`, body-local `position` / `quaternion`
(wxyz), and RGBA `color` in 0..1. Drawing composes the body's world transform
with this local mesh transform. It does not integrate forces or rewrite the
exported motion. The vertex shader transforms normals into view space; the
fragment shader adds restrained key / fill / rim lighting. Both use GLSL 150.

## Rights

New viewer source and shaders: MIT, Copyright (c) 2026 Daito Manabe.
The original motion data retains its original terms. Robot model geometry and
derived hulls retain the upstream Unitree model license and notices supplied
with the export. This example's MIT license does not relicense either asset.
