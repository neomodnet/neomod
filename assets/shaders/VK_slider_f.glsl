#version 450

layout(location = 0) in vec2 tex_coord;

layout(location = 0) out vec4 outColor;

layout(set = 2, binding = 0) uniform sampler2D tex0;

layout(std140, set = 3, binding = 0) uniform FragParams {
    int style;
    float bodyColorSaturation;
    float bodyAlphaMultiplier;
    float borderSizeMultiplier;
    float borderFeather;
    float _pad0; float _pad1; float _pad2;
    vec3 colBorder;
    float _pad3;
    vec3 colBody;
    float _pad4;
} fu;

const float defaultTransitionSize = 0.011;
const float defaultBorderSize = 0.11;
const float outerShadowSize = 0.08;

vec4 getInnerBodyColor(in vec4 bodyColor) {
    float brightnessMultiplier = 0.25;
    bodyColor.r = min(1.0, bodyColor.r * (1.0 + 0.5 * brightnessMultiplier) + brightnessMultiplier);
    bodyColor.g = min(1.0, bodyColor.g * (1.0 + 0.5 * brightnessMultiplier) + brightnessMultiplier);
    bodyColor.b = min(1.0, bodyColor.b * (1.0 + 0.5 * brightnessMultiplier) + brightnessMultiplier);
    return vec4(bodyColor);
}

vec4 getOuterBodyColor(in vec4 bodyColor) {
    float darknessMultiplier = 0.1;
    bodyColor.r = min(1.0, bodyColor.r / (1.0 + darknessMultiplier));
    bodyColor.g = min(1.0, bodyColor.g / (1.0 + darknessMultiplier));
    bodyColor.b = min(1.0, bodyColor.b / (1.0 + darknessMultiplier));
    return vec4(bodyColor);
}

void main() {
    float borderSize = (defaultBorderSize + fu.borderFeather) * fu.borderSizeMultiplier;
    float transitionSize = defaultTransitionSize + fu.borderFeather;

    // output var
    vec4 out_color = vec4(0.0);

    // dynamic color calculations
    vec4 borderColor = vec4(fu.colBorder.x, fu.colBorder.y, fu.colBorder.z, 1.0);
    vec4 bodyColor = vec4(fu.colBody.x, fu.colBody.y, fu.colBody.z, 0.7 * fu.bodyAlphaMultiplier);
    vec4 outerShadowColor = vec4(0, 0, 0, 0.25);
    vec4 innerBodyColor = getInnerBodyColor(bodyColor);
    vec4 outerBodyColor = getOuterBodyColor(bodyColor);

    innerBodyColor.rgb *= fu.bodyColorSaturation;
    outerBodyColor.rgb *= fu.bodyColorSaturation;

    // osu!next style color modifications
    if (fu.style == 1) {
        outerBodyColor.rgb = bodyColor.rgb * fu.bodyColorSaturation;
        outerBodyColor.a = 1.0 * fu.bodyAlphaMultiplier;
        innerBodyColor.rgb = bodyColor.rgb * 0.5 * fu.bodyColorSaturation;
        innerBodyColor.a = 0.0;
    }

    // a bit of a hack, but better than rough edges
    if (fu.borderSizeMultiplier < 0.01)
        borderColor = outerShadowColor;

    // conditional variant

    if (tex_coord.x < outerShadowSize - transitionSize) // just shadow
    {
        float delta = tex_coord.x / (outerShadowSize - transitionSize);
        out_color = mix(vec4(0), outerShadowColor, delta);
    }
    if (tex_coord.x > outerShadowSize - transitionSize && tex_coord.x < outerShadowSize + transitionSize) // shadow + border
    {
        float delta = (tex_coord.x - outerShadowSize + transitionSize) / (2.0 * transitionSize);
        out_color = mix(outerShadowColor, borderColor, delta);
    }
    if (tex_coord.x > outerShadowSize + transitionSize && tex_coord.x < outerShadowSize + borderSize - transitionSize) // just border
    {
        out_color = borderColor;
    }
    if (tex_coord.x > outerShadowSize + borderSize - transitionSize && tex_coord.x < outerShadowSize + borderSize + transitionSize) // border + outer body
    {
        float delta = (tex_coord.x - outerShadowSize - borderSize + transitionSize) / (2.0 * transitionSize);
        out_color = mix(borderColor, outerBodyColor, delta);
    }
    if (tex_coord.x > outerShadowSize + borderSize + transitionSize) // outer body + inner body
    {
        float size = outerShadowSize + borderSize + transitionSize;
        float delta = ((tex_coord.x - size) / (1.0 - size));
        out_color = mix(outerBodyColor, innerBodyColor, delta);
    }

    outColor = out_color;
}
