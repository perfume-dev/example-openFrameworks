#version 150
// A world-space line becomes a constant-pixel-width screen-facing ribbon.
layout(lines) in;
layout(triangle_strip, max_vertices = 4) out;
uniform vec2 uViewport;
uniform float uWidth;
in vec4 vertexColor[];
out vec4 ribbonColor;
out float acrossRibbon;

void emitCorner(int endpoint, float side, vec2 offset) {
    vec4 clip = gl_in[endpoint].gl_Position;
    clip.xy += offset * side * clip.w;
    gl_Position = clip;
    ribbonColor = vertexColor[endpoint];
    acrossRibbon = side;
    EmitVertex();
}

void main() {
    vec4 a = gl_in[0].gl_Position;
    vec4 b = gl_in[1].gl_Position;
    // Do not divide by a point behind/on the camera plane.
    if (a.w <= 0.001 || b.w <= 0.001) return;
    vec2 direction = (b.xy / b.w - a.xy / a.w) * uViewport;
    float magnitude = length(direction);
    if (magnitude < 0.001) return;
    vec2 normal = vec2(-direction.y, direction.x) / magnitude;
    vec2 offset = normal * uWidth / uViewport;
    emitCorner(0, -1.0, offset);
    emitCorner(0,  1.0, offset);
    emitCorner(1, -1.0, offset);
    emitCorner(1,  1.0, offset);
    EndPrimitive();
}
