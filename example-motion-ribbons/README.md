# Motion Ribbons

A minimal three-dancer motion study: cyan, coral and ivory traces describe the
hands, feet and head, while a fine live skeleton keeps the choreography legible.
The data is the original A/B/C sample from the 2012 Perfume Global Site Project.

Requires **openFrameworks 0.12.1**, a desktop **OpenGL 3.2** context and this entire
repository. The example uses modern `ofApp` lifecycle overrides, GLM positions,
RAII-owned meshes/shaders/framebuffer and a programmable rendering pipeline.

## Run

Place the repository at `openFrameworks/apps/myApps/example-openFrameworks`,
open `example-motion-ribbons.xcodeproj` and select the Release scheme, or:

```sh
make Release OF_ROOT=/path/to/openFrameworks
make RunRelease OF_ROOT=/path/to/openFrameworks
```

Keep `../example-bvh/bin/data/{A,B,C}_test.bvh` in place. Data is shared using a
relative path from this example's `bin/data`, and is never duplicated.

## Three shader stages

1. CPU: sample BVH landmarks into bounded trails and upload colored line segments.
   Each dancer is horizontally recentered to keep the composition stable.
2. `ribbon.vert`: transform line endpoints with OF's camera matrix.
3. `ribbon.geom`: expand each line into a screen-facing four-vertex strip.
   `uWidth` is its pixel width and `uViewport` is the framebuffer size.
4. `ribbon.frag`: soften the transverse edge and fade old samples through vertex alpha.

This is an actual geometry shader, so it is a **desktop GL** example, not an
OpenGL ES/mobile example. It uses no immediate-mode OpenGL or point sprites.

This is a pose/trail study, not a reconstruction of the original stage formation.
Playback starts at 12 seconds, where the sample movement is already active.

## Controls and capture

- Space: pause/resume both pose and trails.
- R: rewind to the start of the study and clear trails.
- Drag / scroll: orbit / zoom the camera.
- H: toggle the bottom control legend.

Launch the executable with `--smoke-test` to render 180 deterministic frames in
a hidden window. `--capture=/absolute/path/frame.png` saves the FBO image and
exits; `--capture-frame=420` chooses another pose. The two modes can be combined.
Validation checks shader linkage, GL errors, visible pixels and actual joint
movement before printing `SMOKE_TEST_OK`. Automated runs never show or activate
a window. Interactive drawing and capture use the same 1440×900 FBO; other
window shapes are letterboxed, and the orbit control area follows the image.

The new source and shaders use [MIT](LICENSE). Shared motion assets and the
ofxBvh dependency retain their existing terms; see the repository license index.
