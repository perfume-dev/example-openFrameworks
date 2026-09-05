#version 150
// All coordinates are measured in canvas heights, so circles remain round.
uniform vec2 uResolution;
uniform vec2 uJoints[39]; // 13 sparse BVH landmarks for each of A / B / C.
uniform float uTime;
uniform float uContourDensity;
out vec4 fragColor;

float contour(float value, float density) {
    float phase = value * density;
    float distanceToLine = abs(fract(phase - 0.5) - 0.5);
    float antialias = max(fwidth(phase), 0.004);
    return 1.0 - smoothstep(0.012, 0.012 + antialias, distanceToLine);
}

void main() {
    vec2 p = (gl_FragCoord.xy - 0.5 * uResolution) / uResolution.y;
    vec3 color = vec3(0.024, 0.034, 0.055);
    vec3 palette[3] = vec3[3](vec3(0.30, 0.92, 0.94), vec3(1.0, 0.37, 0.30), vec3(0.93, 0.91, 0.79));
    // The field is a sum of smooth radial kernels. Contours expose equal-potential curves.
    for (int dancer = 0; dancer < 3; ++dancer) {
        float field = 0.0;
        float dots = 0.0;
        for (int joint = 0; joint < 13; ++joint) {
            vec2 delta = p - uJoints[dancer * 13 + joint];
            float radius = length(delta);
            field += 0.23 * exp(-dot(delta, delta) / 0.0045);
            dots += 1.0 - smoothstep(0.0012, 0.0028, radius);
        }
        float mask = smoothstep(0.035, 0.10, field);
        float rings = contour(field, uContourDensity) * mask;
        // A slow moving phase only modulates light; pause freezes this and the pose together.
        float light = 0.86 + 0.14 * sin(uTime * 0.6 + float(dancer));
        color += palette[dancer] * (rings * 0.75 + min(field, 1.0) * 0.065) * light;
        color += vec3(0.75, 0.82, 0.86) * min(dots, 1.0) * 0.55;
    }
    // Restrained coordinate lattice, with a broad vignette leaving generous negative space.
    vec2 cell = abs(fract(p * 18.0 + 0.5) - 0.5);
    float grid = 1.0 - smoothstep(0.008, 0.025, min(cell.x, cell.y));
    color += vec3(0.022, 0.028, 0.04) * grid;
    color *= 1.0 - 0.28 * smoothstep(0.40, 0.95, length(p));
    fragColor = vec4(color, 1.0);
}
