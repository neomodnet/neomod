#version {RUNTIME_VERSION}

#ifdef GL_ES

attribute vec3 position;
attribute vec2 uv;

varying vec2 tex_coord;

uniform mat4 mvp;

void main()
{
	gl_Position = mvp * vec4(position, 1.0);
	tex_coord = uv;
}

#else

varying vec2 tex_coord;

void main()
{
	gl_Position = gl_ModelViewProjectionMatrix * gl_Vertex;

	gl_FrontColor = gl_Color;
	tex_coord = gl_MultiTexCoord0.xy;
}

#endif
