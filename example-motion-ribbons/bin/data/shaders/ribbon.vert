#version 150
// OF supplies these default mesh attributes and the camera transform.
uniform mat4 modelViewProjectionMatrix;
in vec4 position;
in vec4 color;
out vec4 vertexColor;

void main() {
    gl_Position = modelViewProjectionMatrix * position;
    vertexColor = color;
}
