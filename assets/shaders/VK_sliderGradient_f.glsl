#version 450

layout(location = 0) in vec2 tex_coord;

layout(location = 0) out vec4 outColor;

layout(set = 2, binding = 0) uniform sampler2D tex0;  // the accumulated distance fields (see sliderComposite)
layout(set = 2, binding = 1) uniform sampler2D tex1;  // the skin's slidergradient.png

layout(std140, set = 3, binding = 0) uniform FragParams {
    vec4 channel;  // which of tex0's channels holds this slider's field
    float colorRGBMultiplier;
    float alphaMultiplier;
} fu;

void main() {
    // the field value picks the gradient's column, from its left edge at the rim to its right edge at the centerline
    // (the texcoords of the old renderer's cones)
    float radial = dot(texture(tex0, tex_coord), fu.channel);
    vec4 color = texture(tex1, vec2(radial, 0.0));

    // the composite covers the body's whole bounding box, which has to stay untouched outside of the body
    float inside = radial > 0.0 ? 1.0 : 0.0;
    outColor = vec4(color.rgb * fu.colorRGBMultiplier, color.a * fu.alphaMultiplier) * inside;
}
