#version 450

layout(location = 0) in vec2 fragTexcoord;

layout(location = 0) out vec4 outColor;

layout(set = 2, binding = 0) uniform sampler2D tex0;

layout(std140, set = 3, binding = 0) uniform TextParams {
    vec4 col;         // text color
    vec4 col_shadow;  // shadow color; a=0 disables
    vec4 col_outline; // outline color; a=0 disables
    vec4 params;      // xy = shadow UV offset, zw = outline UV radius
    vec4 params2;     // xy = soft shadow spread UV, z = color glyph flag
} fu;

void main() {
    vec4 texSample = texture(tex0, fragTexcoord);
    float textA = texSample.a;
    bool isColor = fu.params2.z > 0.5;

    // shadow (always uses alpha only; shadow is a solid-color effect)
    float shadowA = 0.0;
    if (fu.col_shadow.a > 0.0) {
        vec2 shadowCenter = fragTexcoord - fu.params.xy;

        if (fu.params2.x > 0.0) {
            // soft shadow: 5-tap diamond kernel
            vec2 spread = fu.params2.xy;
            shadowA  = texture(tex0, shadowCenter).a                        * 0.4;
            shadowA += texture(tex0, shadowCenter + vec2(spread.x, 0.0)).a  * 0.15;
            shadowA += texture(tex0, shadowCenter - vec2(spread.x, 0.0)).a  * 0.15;
            shadowA += texture(tex0, shadowCenter + vec2(0.0, spread.y)).a  * 0.15;
            shadowA += texture(tex0, shadowCenter - vec2(0.0, spread.y)).a  * 0.15;
        } else {
            // hard shadow: single sample
            shadowA = texture(tex0, shadowCenter).a;
        }
    }

    // outline: 8-tap max (alpha only)
    float outlineA = 0.0;
    if (fu.col_outline.a > 0.0) {
        vec2 ox = vec2(fu.params.z, 0.0);
        vec2 oy = vec2(0.0, fu.params.w);
        outlineA = max(outlineA, texture(tex0, fragTexcoord + ox).a);
        outlineA = max(outlineA, texture(tex0, fragTexcoord - ox).a);
        outlineA = max(outlineA, texture(tex0, fragTexcoord + oy).a);
        outlineA = max(outlineA, texture(tex0, fragTexcoord - oy).a);
        outlineA = max(outlineA, texture(tex0, fragTexcoord + ox + oy).a);
        outlineA = max(outlineA, texture(tex0, fragTexcoord + ox - oy).a);
        outlineA = max(outlineA, texture(tex0, fragTexcoord - ox + oy).a);
        outlineA = max(outlineA, texture(tex0, fragTexcoord - ox - oy).a);
    }

    // composite back-to-front: outline -> shadow -> text (premultiplied alpha)
    // for color glyphs (emoji): use texture RGB directly
    // for alpha-only glyphs (text): use uniform color
    vec4 textCol = isColor ? vec4(texSample.rgb, fu.col.a) : fu.col;

    float oA = outlineA * fu.col_outline.a;
    vec4 result = vec4(fu.col_outline.rgb * oA, oA);

    float sA = shadowA * fu.col_shadow.a;
    result = vec4(fu.col_shadow.rgb * sA, sA) + result * (1.0 - sA);

    float tA = textA * textCol.a;
    result = vec4(textCol.rgb * tA, tA) + result * (1.0 - tA);

    // un-premultiply for standard alpha blending
    outColor = (result.a > 0.0) ? vec4(result.rgb / result.a, result.a) : vec4(0.0);
}
