#version 450

layout(location = 0) in vec3 in_world_normal;
layout(location = 1) in vec2 in_uv;
layout(location = 2) in float in_world_z;
layout(location = 3) in float in_view_z;
layout(location = 4) in float in_clip_z;
layout(location = 5) in float in_clip_w;

layout(location = 0) out vec4 out_color;

void main() {
    float hardware_depth = gl_FragCoord.z;
    float clip_w = in_clip_w;
    float ndc_z = in_clip_z / max(abs(clip_w), 1e-6);
    float computed_depth = clamp(ndc_z * 0.5 + 0.5, 0.0, 1.0);
    float depth_diff = abs(hardware_depth - computed_depth);

    float clip_w_visual = clamp(clip_w / 8.0, 0.0, 1.0);
    float diff_visual = clamp(depth_diff * 64.0, 0.0, 1.0);

    out_color = vec4(hardware_depth, computed_depth, diff_visual, clip_w_visual);
}
