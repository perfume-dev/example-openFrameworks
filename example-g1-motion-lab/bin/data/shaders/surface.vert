#version 150

uniform mat4 modelViewProjectionMatrix;
uniform mat4 modelViewMatrix;
in vec4 position;
in vec3 normal;
out vec3 viewNormal;
out vec3 viewPosition;

void main() {
    // Body and mesh transforms are rigid: the rotational 3x3 is the normal matrix.
    viewNormal = normalize(mat3(modelViewMatrix) * normal);
    viewPosition = (modelViewMatrix * position).xyz;
    gl_Position = modelViewProjectionMatrix * position;
}
