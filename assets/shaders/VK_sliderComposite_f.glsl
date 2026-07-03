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
    float alphaMultiplier;
    float _pad0; float _pad1;
    vec3 colBorder;
    float _pad3;
    vec3 colBody;
    float _pad4;
};

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
    // tex0 holds the slider's accumulated distance field (see the sliderField shader): the MAX-blend union of
    // every primitive's radial gradient, i.e. exactly the radial the old one-pass shaders computed per
    // primitive, but resolved to the nearest curve feature. shading it here runs the gradient once per pixel
    // no matter how much the body geometry self-overlaps, and radial ramping to 0 keeps the silhouette
    // analytically round at any tessellation density.
    float radial = texture(tex0, tex_coord).r;

    float borderSize = (defaultBorderSize + borderFeather) * borderSizeMultiplier;
    float transitionSize = defaultTransitionSize + borderFeather;

    // output var
    vec4 out_color = vec4(0.0);

    // dynamic color calculations
    vec4 borderColor = vec4(colBorder.x, colBorder.y, colBorder.z, 1.0);
    vec4 bodyColor = vec4(colBody.x, colBody.y, colBody.z, 0.7 * bodyAlphaMultiplier);
    vec4 outerShadowColor = vec4(0, 0, 0, 0.25);
    vec4 innerBodyColor = getInnerBodyColor(bodyColor);
    vec4 outerBodyColor = getOuterBodyColor(bodyColor);

    innerBodyColor.rgb *= bodyColorSaturation;
    outerBodyColor.rgb *= bodyColorSaturation;

    // osu!next style color modifications
    if (style == 1) {
        outerBodyColor.rgb = bodyColor.rgb * bodyColorSaturation;
        outerBodyColor.a = 1.0 * bodyAlphaMultiplier;
        innerBodyColor.rgb = bodyColor.rgb * 0.5 * bodyColorSaturation;
        innerBodyColor.a = 0.0;
    }

    // a bit of a hack, but better than rough edges
    if (borderSizeMultiplier < 0.01)
        borderColor = outerShadowColor;

    // conditional variant

    if (radial < outerShadowSize - transitionSize) // just shadow
    {
        float delta = radial / (outerShadowSize - transitionSize);
        out_color = mix(vec4(0), outerShadowColor, delta);
    }
    if (radial > outerShadowSize - transitionSize && radial < outerShadowSize + transitionSize) // shadow + border
    {
        float delta = (radial - outerShadowSize + transitionSize) / (2.0 * transitionSize);
        out_color = mix(outerShadowColor, borderColor, delta);
    }
    if (radial > outerShadowSize + transitionSize && radial < outerShadowSize + borderSize - transitionSize) // just border
    {
        out_color = borderColor;
    }
    if (radial > outerShadowSize + borderSize - transitionSize && radial < outerShadowSize + borderSize + transitionSize) // border + outer body
    {
        float delta = (radial - outerShadowSize - borderSize + transitionSize) / (2.0 * transitionSize);
        out_color = mix(borderColor, outerBodyColor, delta);
    }
    if (radial > outerShadowSize + borderSize + transitionSize) // outer body + inner body
    {
        float size = outerShadowSize + borderSize + transitionSize;
        float delta = ((radial - size) / (1.0 - size));
        out_color = mix(outerBodyColor, innerBodyColor, delta);
    }

    // the slider's overall fade percent
    out_color.a *= alphaMultiplier;

    outColor = out_color;
}
