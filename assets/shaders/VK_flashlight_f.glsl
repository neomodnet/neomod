#version 450

layout(location = 0) out vec4 outColor;

layout(set = 3, binding = 0) uniform FragParams {
    float max_opacity;
    float flashlight_radius;
    vec2 flashlight_center;
} fu;

void main() {
    float dist = distance(fu.flashlight_center, gl_FragCoord.xy);
    float opacity = 1.0 - smoothstep(fu.flashlight_radius, fu.flashlight_radius * 1.4, dist);
    opacity = 1.0 - min(opacity, fu.max_opacity);
    outColor = vec4(0.0, 0.0, 0.0, opacity);
}
