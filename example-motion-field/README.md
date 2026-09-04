# Motion Field

Three dancing fields, drawn from sparse BVH landmarks with a single full-screen
rectangle. Cyan, coral and ivory isocontours merge and separate as hands, feet
and torso move. A subdued coordinate lattice and generous negative space keep
the result simple.

Requires **openFrameworks 0.12.1**, desktop **OpenGL 3.2** and this entire
repository. The original 2012 A/B/C motion samples are shared rather than copied.

## Run

Place the repository at `openFrameworks/apps/myApps/example-openFrameworks`,
open `example-motion-field.xcodeproj` and select the Release scheme, or:

```sh
make Release OF_ROOT=/path/to/openFrameworks
make RunRelease OF_ROOT=/path/to/openFrameworks
```

Keep `../example-bvh/bin/data/{A,B,C}_test.bvh` in place.

## How the shader works

The CPU uploads 13 projected joint positions per dancer as `uJoints[39]`.
Each dancer is horizontally recentered. Positions use canvas-height units, so
radial kernels do not stretch with aspect
ratio. `field.vert` draws one rectangle; `field.frag` sums smooth radial kernels,
extracts equal-potential contours and overlays tiny landmark dots.

- `uResolution`: framebuffer width and height.
- `uJoints`: sparse positions, ordered dancer A then B then C.
- `uContourDensity`: count of contours per unit of field potential.
- `uTime`: the same playback clock as the BVH pose; pause freezes all animation.

`fwidth` gives the contour edges pixel-aware antialiasing. The field has a
fixed-size uniform array and no texture, postprocess chain or additional addon.

This is a pose study, not a reconstruction of the original stage formation.
Playback starts at 12 seconds, where the sample movement is already active.

## Controls and capture

- Space: pause/resume.
- R: rewind to the start of the study.
- Up/down arrows: increase/decrease contour density (3–16).
- H: toggle the bottom control legend.

Use `--smoke-test` for 180 deterministic frames in a hidden window.
`--capture=/absolute/path/frame.png` saves the actual rendered FBO and exits;
`--capture-frame=420` selects a later pose. Both options can be combined.
The program prints `SMOKE_TEST_OK` only after shader/GL, visible-pixel and
joint-motion checks succeed. Automated runs never show or activate a window.
Interactive drawing and capture use the same 1440×900 FBO; other window shapes
are letterboxed to preserve the composition.

The new source and shaders use [MIT](LICENSE). Shared motion assets and the
ofxBvh dependency retain their existing terms; see the repository license index.
