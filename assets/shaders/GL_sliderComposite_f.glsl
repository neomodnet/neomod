#version {RUNTIME_VERSION}

// tex holds the slider's accumulated distance field (see the sliderField shader): the MAX-blend union of
// every primitive's radial gradient, i.e. exactly the radial the old one-pass shaders computed per
// primitive, but resolved to the nearest curve feature. shading it here runs the gradient once per pixel
// no matter how much the body geometry self-overlaps, and radial ramping to 0 keeps the silhouette
// analytically round at any tessellation density.

#ifdef GL_ES
precision highp float;
#endif

uniform sampler2D tex;
uniform int style;
uniform float bodyColorSaturation;
uniform float bodyAlphaMultiplier;
uniform float borderSizeMultiplier;
uniform float borderFeather;
uniform float alphaMultiplier;
uniform vec3 colBorder;
uniform vec3 colBody;

varying vec2 tex_coord;

const float defaultTransitionSize = 0.011;
const float defaultBorderSize = 0.11;
const float outerShadowSize = 0.08;

vec4 getInnerBodyColor(in vec4 bodyColor)
{
	float brightnessMultiplier = 0.25;
	bodyColor.r = min(1.0, bodyColor.r * (1.0 + 0.5 * brightnessMultiplier) + brightnessMultiplier);
	bodyColor.g = min(1.0, bodyColor.g * (1.0 + 0.5 * brightnessMultiplier) + brightnessMultiplier);
	bodyColor.b = min(1.0, bodyColor.b * (1.0 + 0.5 * brightnessMultiplier) + brightnessMultiplier);
	return vec4(bodyColor);
}

vec4 getOuterBodyColor(in vec4 bodyColor)
{
	float darknessMultiplier = 0.1;
	bodyColor.r = min(1.0, bodyColor.r / (1.0 + darknessMultiplier));
	bodyColor.g = min(1.0, bodyColor.g / (1.0 + darknessMultiplier));
	bodyColor.b = min(1.0, bodyColor.b / (1.0 + darknessMultiplier));
	return vec4(bodyColor);
}

void main()
{
	float radial = texture2D(tex, tex_coord).r;

	float borderSize = (defaultBorderSize + borderFeather) * borderSizeMultiplier;
	float transitionSize = defaultTransitionSize + borderFeather;

	vec4 out_color = vec4(0.0);

	vec4 borderColor = vec4(colBorder.x, colBorder.y, colBorder.z, 1.0);
	vec4 bodyColor = vec4(colBody.x, colBody.y, colBody.z, 0.7*bodyAlphaMultiplier);
	vec4 outerShadowColor = vec4(0, 0, 0, 0.25);
	vec4 innerBodyColor = getInnerBodyColor(bodyColor);
	vec4 outerBodyColor = getOuterBodyColor(bodyColor);

	innerBodyColor.rgb *= bodyColorSaturation;
	outerBodyColor.rgb *= bodyColorSaturation;

	if (style == 1)
	{
		outerBodyColor.rgb = bodyColor.rgb * bodyColorSaturation;
		outerBodyColor.a = 1.0*bodyAlphaMultiplier;
		innerBodyColor.rgb = bodyColor.rgb * 0.5 * bodyColorSaturation;
		innerBodyColor.a = 0.0;
	}

	if (borderSizeMultiplier < 0.01)
		borderColor = outerShadowColor;

	if (radial < outerShadowSize - transitionSize) // just shadow
	{
		float delta = radial / (outerShadowSize - transitionSize);
		out_color = mix(vec4(0), outerShadowColor, delta);
	}
	if (radial > outerShadowSize - transitionSize && radial < outerShadowSize + transitionSize) // shadow + border
	{
		float delta = (radial - outerShadowSize + transitionSize) / (2.0*transitionSize);
		out_color = mix(outerShadowColor, borderColor, delta);
	}
	if (radial > outerShadowSize + transitionSize && radial < outerShadowSize + borderSize - transitionSize) // just border
	{
		out_color = borderColor;
	}
	if (radial > outerShadowSize + borderSize - transitionSize && radial < outerShadowSize + borderSize + transitionSize) // border + outer body
	{
		float delta = (radial - outerShadowSize - borderSize + transitionSize) / (2.0*transitionSize);
		out_color = mix(borderColor, outerBodyColor, delta);
	}
	if (radial > outerShadowSize + borderSize + transitionSize) // outer body + inner body
	{
		float size = outerShadowSize + borderSize + transitionSize;
		float delta = ((radial - size) / (1.0-size));
		out_color = mix(outerBodyColor, innerBodyColor, delta);
	}

	// the slider's overall fade percent
	out_color.a *= alphaMultiplier;

	gl_FragColor = out_color;
}
