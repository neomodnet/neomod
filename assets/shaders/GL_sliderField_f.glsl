#version {RUNTIME_VERSION}

// tex_coord is the fragment's offset from the nearest curve feature (segment or cap center), normalized
// by the body radius, so 1.0 - length(tex_coord) is that feature's exact radial gradient (1 at the
// centerline, 0 at the edge). the MAX blend equation unions overlapping primitives into the field of the
// nearest feature; the sliderComposite shader then shades the accumulated field once per pixel.

#ifdef GL_ES
precision highp float;
#endif

varying vec2 tex_coord;

void main()
{
	gl_FragColor = vec4(max(0.0, 1.0 - length(tex_coord)));
}
