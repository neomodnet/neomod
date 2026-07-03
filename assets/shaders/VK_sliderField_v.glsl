#version 450

layout(location = 0) in vec3 inPos;
layout(location = 1) in vec4 inCol;
layout(location = 2) in vec2 inTex;

layout(location = 0) out vec2 tex_coord;

layout(set = 1, binding = 0) uniform VertexUniforms {
    mat4 mvp;
};

void main() {
    gl_Position = mvp * vec4(inPos, 1.0);
    tex_coord = inTex;
}
