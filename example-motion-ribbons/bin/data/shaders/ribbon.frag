#version 150
in vec4 ribbonColor;
in float acrossRibbon;
out vec4 fragColor;

void main() {
    // An antialiased soft edge without a second blur pass or large overdraw.
    float edge = 1.0 - smoothstep(0.35, 1.0, abs(acrossRibbon));
    fragColor = vec4(ribbonColor.rgb, ribbonColor.a * edge);
}
