#version 150

uniform vec3 uBaseColor;
in vec3 viewNormal;
in vec3 viewPosition;
out vec4 fragColor;

void main() {
    vec3 n = normalize(viewNormal);
    if (!gl_FrontFacing) n = -n;
    vec3 view = normalize(-viewPosition);
    vec3 key = normalize(vec3(-0.45, 0.65, 0.75));
    vec3 fill = normalize(vec3(0.75, 0.15, 0.35));
    float diffuse = 0.40 + 0.46 * max(dot(n, key), 0.0)
        + 0.18 * max(dot(n, fill), 0.0);
    float specular = pow(max(dot(n, normalize(key + view)), 0.0), 42.0) * 0.16;
    float rim = pow(1.0 - max(dot(n, view), 0.0), 3.0) * 0.04;
    vec3 color = uBaseColor * diffuse + vec3(specular) + vec3(0.35, 0.58, 0.60) * rim;
    fragColor = vec4(clamp(color, 0.0, 1.0), 1.0);
}
