#version 450

layout(location = 0) in vec3 inPos;
layout(location = 1) in vec4 inCol;
layout(location = 2) in vec2 inTex;

layout(location = 0) out vec2 tex_coord;
layout(location = 1) out float vtx_alpha;

layout(set = 1, binding = 0) uniform VertexUniforms {
    mat4 mvp;
} vu;

void main() {
    gl_Position = vu.mvp * vec4(inPos.xy, 0.0, 1.0);
    tex_coord = inTex;
    vtx_alpha = inPos.z;
}
