# Motion as a shader input

The new studies share a small idea: read motion on the CPU, send a compact set
of positions to the GPU, and let the shader decide how that motion looks.
The BVH data remains the source of the animation.

## Motion Ribbons

Open [`example-motion-ribbons`](../example-motion-ribbons) first if you want to
learn the geometry stage.

1. The app samples joint positions over time and builds line segments.
2. The vertex shader transforms each position.
3. The geometry shader expands a segment into a narrow, camera-facing strip.
4. The fragment shader shapes its soft edge and luminous colour.

This keeps the C++ topology simple. Changing ribbon width or edge falloff does
not require a new BVH parser or a dense CPU-generated tube. Read the example's
README for its controls and shader filenames.

## Motion Field

[`example-motion-field`](../example-motion-field) starts with a full-screen
rectangle. The app projects selected joints and sends their positions to the
fragment shader. Each pixel evaluates its relationship to those moving sources,
producing contour lines and overlapping halos.

Start with the contour spacing, line softness, and colour constants. Keep
expensive work bounded: adding more source joints increases the work for every
pixel, while increasing the window size increases the number of pixels.

## openFrameworks and Processing

| Stage | openFrameworks studies | Processing studies |
| --- | --- | --- |
| Motion sampling | C++ and the bundled `ofxBvh` | Java/PDE and the bundled BVH parser |
| Ribbon topology | Geometry shader expands line segments | The sketch creates a triangle strip |
| Vertex shading | GLSL vertex shader | `PShader` vertex shader |
| Colour and soft edges | GLSL fragment shader | `PShader` fragment shader |
| Contour field | Full-screen fragment shader | Full-screen `PShader` |

Processing's standard `PShader` API exposes vertex and fragment shaders. Its
ribbon study therefore generates the strip in the sketch; it does not require
a custom OpenGL wrapper for a geometry stage. The openFrameworks study uses a
desktop OpenGL 3.2 context and GLSL 150, including a real geometry shader.

These examples target the stable releases checked for this update:
[openFrameworks 0.12.1](https://openframeworks.cc/download/) and
[Processing 4.5.6](https://github.com/processing/processing4/releases/tag/processing-1434-4.5.6).

References: [ofShader](https://openframeworks.cc/documentation/gl/ofShader/) and
[Processing PShader](https://processing.org/reference/PShader).
