#version 450

layout(location = 0) in vec2 tex_coord;

layout(location = 0) out vec4 outColor;

layout(set = 2, binding = 0) uniform sampler2D tex0;

layout(std140, set = 3, binding = 0) uniform FragParams {
    vec2 rect_min;
    vec2 rect_max;
    float edge_softness;
    float _pad0; float _pad1; float _pad2;
    vec4 col;
} fu;

void main() {
    vec2 dist_to_edges = min(gl_FragCoord.xy - fu.rect_min, fu.rect_max - gl_FragCoord.xy);
    float min_dist = min(dist_to_edges.x, dist_to_edges.y);

    float alpha = smoothstep(-fu.edge_softness, 0.0, min_dist);
    vec4 texColor = texture(tex0, tex_coord);
    outColor = vec4(texColor.rgb * fu.col.rgb, texColor.a * fu.col.a * alpha);
}
