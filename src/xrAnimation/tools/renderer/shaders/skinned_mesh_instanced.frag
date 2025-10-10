#version 450

layout(location = 0) in vec3 in_world_normal;
layout(location = 1) in vec2 in_uv;

layout(location = 0) out vec4 out_color;

void main() {
    // DIAGNOSTIC: Show raw gl_FragCoord.w (clip space W coordinate)
    // If W is 0 or negative, that's why depth = 1.0!
    float w = gl_FragCoord.w;

    // Also show depth for comparison
    float depth = gl_FragCoord.z;

    // R = W coordinate (should be positive and varying)
    // G = depth (currently all white = 1.0)
    // B = 0
    out_color = vec4(w * 0.1, depth, 0.0, 1.0);

    // Expected: If W is correct, you'll see red+green = yellow/orange gradient
    // If W is 0 or tiny, you'll see green only (white)
}
