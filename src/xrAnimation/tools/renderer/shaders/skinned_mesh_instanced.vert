#version 450

layout(location = 0) in vec3 in_position;
layout(location = 1) in vec3 in_normal;
layout(location = 2) in vec2 in_uv;
layout(location = 3) in vec4 in_bone_weights;
layout(location = 4) in uvec4 in_bone_indices;

layout(location = 5) in vec4 in_instance_transform_col0;
layout(location = 6) in vec4 in_instance_transform_col1;
layout(location = 7) in vec4 in_instance_transform_col2;
layout(location = 8) in vec4 in_instance_transform_col3;

layout(location = 9) in uint in_bone_matrix_offset;

layout(set = 0, binding = 0) uniform CameraUBO {
    mat4 view_proj;
} camera;

layout(std430, set = 0, binding = 1) readonly buffer BoneMatrices {
    mat4 matrices[];
} bones;

layout(location = 0) out vec3 out_world_normal;
layout(location = 1) out vec2 out_uv;
layout(location = 2) out float out_world_z;
layout(location = 3) out float out_view_z;
layout(location = 4) out float out_clip_z;
layout(location = 5) out float out_clip_w;

mat4 accumulate_skinning(uint base_index) {
    mat4 skin_matrix = mat4(0.0);

    if (in_bone_weights.x > 0.0) {
        skin_matrix += bones.matrices[base_index + in_bone_indices.x] * in_bone_weights.x;
    }
    if (in_bone_weights.y > 0.0) {
        skin_matrix += bones.matrices[base_index + in_bone_indices.y] * in_bone_weights.y;
    }
    if (in_bone_weights.z > 0.0) {
        skin_matrix += bones.matrices[base_index + in_bone_indices.z] * in_bone_weights.z;
    }
    if (in_bone_weights.w > 0.0) {
        skin_matrix += bones.matrices[base_index + in_bone_indices.w] * in_bone_weights.w;
    }

    float weight_sum = in_bone_weights.x + in_bone_weights.y + in_bone_weights.z + in_bone_weights.w;
    if (weight_sum <= 0.0) {
        skin_matrix = mat4(1.0);
    }

    return skin_matrix;
}

void main() {
    mat4 instance_transform = mat4(
        in_instance_transform_col0,
        in_instance_transform_col1,
        in_instance_transform_col2,
        in_instance_transform_col3
    );

    mat4 skin_matrix = accumulate_skinning(in_bone_matrix_offset);

    vec4 local_position = skin_matrix * vec4(in_position, 1.0);
    vec3 local_normal = mat3(skin_matrix) * in_normal;

    vec4 world_position = instance_transform * local_position;
    vec4 clip_position = camera.view_proj * world_position;
    gl_Position = clip_position;

    mat3 normal_matrix = mat3(instance_transform);
    out_world_normal = normalize(normal_matrix * local_normal);
    out_uv = in_uv;

    out_world_z = world_position.z;
    out_view_z = clip_position.z / max(clip_position.w, 1e-6);
    out_clip_z = clip_position.z;
    out_clip_w = clip_position.w;
}
