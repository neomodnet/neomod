#version 450

layout(location = 0) in vec3 inPos;
layout(location = 1) in vec4 inCol;
layout(location = 2) in vec2 inTex;

layout(set = 1, binding = 0) uniform VertexUniforms {
    mat4 mvp;
} vu;

void main() {
    gl_Position = vu.mvp * vec4(inPos.xy, 0.0, 1.0);
}
