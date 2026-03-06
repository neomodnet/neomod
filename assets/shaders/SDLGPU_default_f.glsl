#version 450

layout(location = 0) in vec4 fragColor;
layout(location = 1) in vec2 fragTexcoord;

layout(location = 0) out vec4 outColor;

layout(set = 2, binding = 0) uniform sampler2D tex0;

layout(set = 3, binding = 0) uniform FragUniforms {
    vec4 misc; // x = texturing enabled, y = color inversion, z/w = unused
    vec4 col;  // global color (m_color)
};

void main() {
    vec4 result;
    if (misc.x > 0.5) {
        result = texture(tex0, fragTexcoord) * col * fragColor;
    } else {
        result = fragColor;
    }

    if (misc.y > 0.5) {
        result.rgb = vec3(1.0) - result.rgb;
    }

    outColor = result;
}
