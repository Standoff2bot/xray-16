#include "stdafx.h"

#include <GLFW/glfw3.h>

#include "renderer/VulkanRenderer.h"

#include "../../../Externals/imgui/imgui.h"
#include "../../../Externals/imgui/imgui_internal.h"
#include "../../../Externals/ozz-animation/samples/framework/mesh.h"

#include "../ExtendedBoneMetadata.h"
#include "../OzzBundle.h"

#include "ozz/animation/runtime/animation.h"
#include "ozz/animation/runtime/local_to_model_job.h"
#include "ozz/animation/runtime/skeleton.h"
#include "ozz/base/containers/vector.h"
#include "ozz/base/io/archive.h"
#include "ozz/base/io/stream.h"
#include "ozz/base/maths/simd_math.h"
#include "ozz/base/maths/soa_transform.h"
#include "ozz/base/span.h"

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <limits>
#include <numeric>
#include <unordered_map>
#include <system_error>
#include <string>
#include <string_view>
#include <vector>

#define Msg(...) printf(__VA_ARGS__), printf("\n")

namespace {

using xray::animation::renderer::SkeletonLinePoint;
using xray::animation::renderer::Camera;
using xray::animation::renderer::VulkanRenderer;

constexpr double kStatusMessageDuration = 6.0;

struct AnimationInterval {
    float start = 0.0f;
    float end = 0.0f;
};

struct AnimationMark {
    std::string name;
    std::vector<AnimationInterval> intervals;
};

struct AnimationBoneMotion {
    uint16_t bone_id = 0;
    uint8_t flags = 0;
    uint8_t translation_format = 0;
    uint32_t rotation_crc = 0;
    uint32_t translation_crc = 0;
    std::vector<CKeyQR> rotation_keys;
    std::vector<CKeyQT8> translation_keys8;
    std::vector<CKeyQT16> translation_keys16;
    Fvector translation_size{};
    Fvector translation_init{};
};

struct AnimationMetadata {
    std::string name;
    uint32_t flags = 0;
    uint16_t bone_or_part = 0;
    uint16_t motion_id = 0;
    float speed = 0.0f;
    float power = 0.0f;
    float accrue = 0.0f;
    float falloff = 0.0f;
    std::vector<AnimationMark> marks;
    uint32_t frame_count = 0;
    std::vector<AnimationBoneMotion> bone_motions;
};

std::string ParseBundleArgument(int argc, const char** argv) {
    const std::string_view prefix = "--bundle=";
    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];
        if (arg.rfind(prefix, 0) == 0) {
            return std::string(arg.substr(prefix.size()));
        }
        if (arg == "--bundle" && i + 1 < argc) {
            return argv[i + 1];
        }
    }
    return {};
}

std::string ParseAnimationArgument(int argc, const char** argv) {
    const std::string_view prefix = "--animation=";
    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];
        if (arg.rfind(prefix, 0) == 0) {
            return std::string(arg.substr(prefix.size()));
        }
        if (arg == "--animation" && i + 1 < argc) {
            return argv[i + 1];
        }
    }
    return {};
}

std::string ReadArchiveString(ozz::io::IArchive& archive) {
    uint32_t length = 0;
    archive >> length;
    std::string value;
    value.resize(length);
    if (length > 0) {
        archive >> ozz::io::MakeArray(value.data(), length);
    }
    return value;
}

std::string BuildAnimationLabel(size_t index, const ozz::animation::Animation& animation,
    const AnimationMetadata* metadata) {
    if (metadata && !metadata->name.empty()) {
        return metadata->name;
    }
    const char* runtime_name = animation.name();
    if (runtime_name && runtime_name[0] != '\0') {
        return runtime_name;
    }
    return "animation_" + std::to_string(index);
}

bool ValidateCount(uint32_t value, uint32_t max_value, const char* context, std::string* error) {
    if (value <= max_value) {
        return true;
    }
    if (error) {
        *error = std::string(context) + " exceeds supported limit (" + std::to_string(value) + " > " +
            std::to_string(max_value) + ")";
    }
    return false;
}

bool ReadMotionMetadataChunk(ozz::io::IArchive& archive, AnimationMetadata* metadata, std::string* error) {
    if (!metadata) {
        if (error) {
            *error = "Metadata output pointer is null";
        }
        return false;
    }

    AnimationMetadata result;
    result.name = ReadArchiveString(archive);
    archive >> result.flags;
    archive >> result.bone_or_part;
    archive >> result.motion_id;
    archive >> result.speed;
    archive >> result.power;
    archive >> result.accrue;
    archive >> result.falloff;

    uint32_t mark_count = 0;
    archive >> mark_count;
    if (!ValidateCount(mark_count, 4096, "Animation mark count", error)) {
        return false;
    }

    result.marks.resize(mark_count);
    for (uint32_t mark_index = 0; mark_index < mark_count; ++mark_index) {
        AnimationMark mark;
        mark.name = ReadArchiveString(archive);
        uint32_t interval_count = 0;
        archive >> interval_count;
        if (!ValidateCount(interval_count, 4096, "Animation mark interval count", error)) {
            return false;
        }

        mark.intervals.resize(interval_count);
        for (uint32_t interval_index = 0; interval_index < interval_count; ++interval_index) {
            AnimationInterval interval{};
            archive >> interval.start;
            archive >> interval.end;
            mark.intervals[interval_index] = interval;
        }

        result.marks[mark_index] = std::move(mark);
    }

    archive >> result.frame_count;
    uint32_t bone_motion_count = 0;
    archive >> bone_motion_count;
    if (!ValidateCount(bone_motion_count, 512, "Bone motion count", error)) {
        return false;
    }

    result.bone_motions.resize(bone_motion_count);
    for (uint32_t bone_index = 0; bone_index < bone_motion_count; ++bone_index) {
        AnimationBoneMotion bone;
        archive >> bone.bone_id;
        archive >> bone.flags;
        archive >> bone.translation_format;
        archive >> bone.rotation_crc;
        archive >> bone.translation_crc;

        uint32_t rotation_key_count = 0;
        archive >> rotation_key_count;
        if (!ValidateCount(rotation_key_count, 65536, "Rotation key count", error)) {
            return false;
        }

        bone.rotation_keys.resize(rotation_key_count);
        for (uint32_t key_index = 0; key_index < rotation_key_count; ++key_index) {
            CKeyQR key{};
            archive >> key.x;
            archive >> key.y;
            archive >> key.z;
            archive >> key.w;
            bone.rotation_keys[key_index] = key;
        }

        uint32_t translation_key_count = 0;
        archive >> translation_key_count;
        if (!ValidateCount(translation_key_count, 65536, "Translation key count", error)) {
            return false;
        }

        switch (bone.translation_format) {
        case 1:
            bone.translation_keys8.resize(translation_key_count);
            for (uint32_t key_index = 0; key_index < translation_key_count; ++key_index) {
                CKeyQT8 key{};
                archive >> key.x1;
                archive >> key.y1;
                archive >> key.z1;
                bone.translation_keys8[key_index] = key;
            }
            break;
        case 2:
            bone.translation_keys16.resize(translation_key_count);
            for (uint32_t key_index = 0; key_index < translation_key_count; ++key_index) {
                CKeyQT16 key{};
                archive >> key.x1;
                archive >> key.y1;
                archive >> key.z1;
                bone.translation_keys16[key_index] = key;
            }
            break;
        default:
            if (translation_key_count != 0) {
                if (error) {
                    *error = "Translation key count mismatch for raw format (expected 0, got " +
                        std::to_string(translation_key_count) + ")";
                }
                return false;
            }
            break;
        }

        archive >> bone.translation_size.x;
        archive >> bone.translation_size.y;
        archive >> bone.translation_size.z;
        archive >> bone.translation_init.x;
        archive >> bone.translation_init.y;
        archive >> bone.translation_init.z;

        result.bone_motions[bone_index] = std::move(bone);
    }

    *metadata = std::move(result);
    return true;
}

bool LoadAnimationsFromStream(ozz::io::Stream* stream, const std::string& source_label,
    std::vector<ozz::animation::Animation>& animations, std::vector<AnimationMetadata>& metadata,
    std::string* error) {
    animations.clear();
    metadata.clear();

    if (!stream) {
        if (error) {
            *error = "Animation stream is null";
        }
        return false;
    }

    if (stream->Seek(0, ozz::io::Stream::kSet) < 0) {
        if (error) {
            *error = "Failed to seek animation stream: " + source_label;
        }
        return false;
    }

    ozz::io::IArchive archive(stream);

    if (archive.TestTag<ozz::animation::Animation>()) {
        animations.resize(1);
        archive >> animations[0];

        AnimationMetadata clip_metadata;
        if (!ReadMotionMetadataChunk(archive, &clip_metadata, error)) {
            return false;
        }

        metadata.push_back(std::move(clip_metadata));
        return true;
    }

    if (stream->Seek(0, ozz::io::Stream::kSet) < 0) {
        if (error) {
            *error = "Failed to rewind animation stream: " + source_label;
        }
        return false;
    }

    archive = ozz::io::IArchive(stream);

    uint32_t animation_count = 0;
    archive >> animation_count;
    if (!ValidateCount(animation_count, 8192, "Animation count", error) || animation_count == 0) {
        if (error && animation_count == 0) {
            *error = "Animation archive '" + source_label + "' contains zero animations";
        }
        return false;
    }

    animations.resize(animation_count);
    metadata.resize(animation_count);

    for (uint32_t i = 0; i < animation_count; ++i) {
        archive >> animations[i];
        if (!ReadMotionMetadataChunk(archive, &metadata[i], error)) {
            return false;
        }
    }

    return true;
}

bool LoadSkeletonFromBytes(const std::vector<std::uint8_t>& bytes, ozz::animation::Skeleton* skeleton) {
    if (!skeleton) {
        return false;
    }

    ozz::io::MemoryStream stream;
    if (!bytes.empty() && !stream.Write(bytes.data(), bytes.size())) {
        return false;
    }
    stream.Seek(0, ozz::io::Stream::kSet);

    ozz::io::IArchive archive(&stream);
    if (!archive.TestTag<ozz::animation::Skeleton>()) {
        return false;
    }
    archive >> *skeleton;
    return skeleton->num_joints() > 0;
}

bool LoadMeshesFromBytes(const std::vector<std::uint8_t>& bytes, ozz::vector<ozz::sample::Mesh>* meshes) {
    if (!meshes) {
        return false;
    }

    ozz::io::MemoryStream stream;
    if (!bytes.empty() && !stream.Write(bytes.data(), bytes.size())) {
        return false;
    }
    stream.Seek(0, ozz::io::Stream::kSet);

    ozz::io::IArchive archive(&stream);
    meshes->clear();
    while (archive.TestTag<ozz::sample::Mesh>()) {
        meshes->resize(meshes->size() + 1);
        archive >> meshes->back();
    }
    return true;
}

bool LoadAnimationsFromBytes(const std::vector<std::uint8_t>& bytes,
    std::vector<ozz::animation::Animation>& animations,
    std::vector<AnimationMetadata>& metadata,
    std::string* error) {
    animations.clear();
    metadata.clear();

    if (bytes.empty()) {
        if (error) {
            *error = "Embedded animation payload is empty";
        }
        return false;
    }

    ozz::io::MemoryStream stream;
    if (!stream.Write(bytes.data(), bytes.size())) {
        if (error) {
            *error = "Failed to copy embedded animation payload into memory stream";
        }
        return false;
    }

    return LoadAnimationsFromStream(&stream, "<embedded>", animations, metadata, error);
}

bool LoadAnimationsFromPath(const std::filesystem::path& animation_path,
    std::vector<ozz::animation::Animation>& animations,
    std::vector<AnimationMetadata>& metadata,
    std::string* error) {
    animations.clear();
    metadata.clear();

    if (animation_path.empty()) {
        if (error) {
            *error = "Animation path is empty";
        }
        return false;
    }

    std::error_code ec;
    if (!std::filesystem::exists(animation_path, ec)) {
        if (error) {
            *error = "Animation file does not exist: " + animation_path.string();
        }
        return false;
    }

    ozz::io::File file(animation_path.string().c_str(), "rb");
    if (!file.opened()) {
        if (error) {
            *error = "Failed to open animation file for reading: " + animation_path.string();
        }
        return false;
    }

    return LoadAnimationsFromStream(&file, animation_path.string(), animations, metadata, error);
}

std::vector<SkeletonLinePoint> BuildSkeletonLinePoints(const ozz::animation::Skeleton& skeleton) {
    std::vector<SkeletonLinePoint> points;
    const int joint_count = skeleton.num_joints();
    if (joint_count <= 0) {
        return points;
    }

    ozz::vector<ozz::math::Float4x4> models(joint_count);
    ozz::animation::LocalToModelJob job;
    job.skeleton = &skeleton;
    job.input = skeleton.joint_rest_poses();
    job.output = ozz::make_span(models);
    if (!job.Run()) {
        return {};
    }

    const auto parents = skeleton.joint_parents();
    for (int joint = 0; joint < joint_count; ++joint) {
        const int parent = parents[joint];
        if (parent < 0) {
            continue;
        }

        SkeletonLinePoint parent_point{};
        SkeletonLinePoint child_point{};
        ozz::math::Store3PtrU(models[parent].cols[3], &parent_point.x);
        ozz::math::Store3PtrU(models[joint].cols[3], &child_point.x);
        points.push_back(parent_point);
        points.push_back(child_point);
    }

    return points;
}

struct ViewerState {
    GLFWwindow* window = nullptr;
    std::array<char, 512> bundle_path_buffer{};
    std::array<char, 512> animation_path_buffer{};
    std::string loaded_bundle_path;
    std::string loaded_animation_path;
    std::string cli_animation_path;
    bool bundle_loaded = false;

    ozz::animation::Skeleton skeleton;
    ozz::vector<ozz::sample::Mesh> meshes;
    XRay::Animation::ExtendedBoneMetadataCollection bone_metadata;

    std::vector<ozz::animation::Animation> animations;
    std::vector<AnimationMetadata> animation_metadata;
    int current_animation_index = -1;

    std::vector<bool> mesh_visibility;
    bool mesh_dirty = false;
    bool request_load = false;
    bool request_reload = false;

    std::string status_message;
    bool status_is_error = false;
    double status_timestamp = 0.0;

    bool dockspace_initialized = false;
    ImGuiID dockspace_id = 0;

    std::vector<float> frame_time_history;
    size_t frame_history_capacity = 240;
    float frame_time_average_ms = 0.0f;
    float frame_time_peak_ms = 0.0f;
    float frame_time_latest_ms = 0.0f;

    bool show_demo_window = false;
};

void QueueStatus(ViewerState& state, std::string message, bool is_error) {
    state.status_message = std::move(message);
    state.status_is_error = is_error;
    state.status_timestamp = glfwGetTime();
}

void ApplyLoadedAnimations(ViewerState& state, VulkanRenderer& renderer,
    std::vector<ozz::animation::Animation>&& clips,
    std::vector<AnimationMetadata>&& metadata,
    const std::string& source_label,
    bool* animation_loaded_out = nullptr) {
    state.animations = std::move(clips);
    state.animation_metadata = std::move(metadata);
    if (state.animation_metadata.size() != state.animations.size()) {
        state.animation_metadata.resize(state.animations.size());
    }

    state.loaded_animation_path = source_label;
    state.current_animation_index = state.animations.empty() ? -1 : 0;

    const bool has_animation = state.current_animation_index >= 0;
    if (animation_loaded_out) {
        *animation_loaded_out = has_animation;
    }

    if (has_animation) {
        renderer.SetActiveAnimation(&state.animations[static_cast<size_t>(state.current_animation_index)]);
        renderer.SetMeshAnimationTime(0.0f);
    } else {
        renderer.SetActiveAnimation(nullptr);
    }
}

std::filesystem::path NormalizeBundlePath(const std::string& value) {
    std::filesystem::path path = value;
    std::error_code ec;
    const std::filesystem::path absolute = std::filesystem::absolute(path, ec);
    if (!ec) {
        path = absolute;
    }
    return path.lexically_normal();
}

bool BuildCombinedMesh(const ozz::vector<ozz::sample::Mesh>& meshes,
    const std::vector<bool>& visibility,
    ozz::sample::Mesh& out_mesh) {
    out_mesh.parts.clear();
    out_mesh.triangle_indices.clear();
    out_mesh.joint_remaps.clear();
    out_mesh.inverse_bind_poses.clear();

    if (meshes.empty() || visibility.empty()) {
        return false;
    }

    if (meshes.empty() || visibility.empty()) {
        return false;
    }

    std::unordered_map<uint16_t, uint16_t> joint_to_global;
    ozz::vector<uint16_t> global_remap;
    ozz::vector<ozz::math::Float4x4> global_inverse_bind;

    uint32_t vertex_offset = 0;
    bool any_visible = false;

    auto ensure_global_index = [&](const ozz::sample::Mesh& mesh, uint16_t local_index) -> uint16_t {
        uint16_t joint = local_index;
        if (!mesh.joint_remaps.empty() && local_index < mesh.joint_remaps.size()) {
            joint = mesh.joint_remaps[local_index];
        }
        auto [it, inserted] = joint_to_global.emplace(joint, static_cast<uint16_t>(global_remap.size()));
        if (inserted) {
            global_remap.push_back(joint);
            ozz::math::Float4x4 inverse = ozz::math::Float4x4::identity();
            if (!mesh.inverse_bind_poses.empty() && local_index < mesh.inverse_bind_poses.size()) {
                inverse = mesh.inverse_bind_poses[local_index];
            }
            global_inverse_bind.push_back(inverse);
        }
        return it->second;
    };

    for (size_t mesh_index = 0; mesh_index < meshes.size(); ++mesh_index) {
        if (mesh_index >= visibility.size() || !visibility[mesh_index]) {
            continue;
        }

        const ozz::sample::Mesh& mesh = meshes[mesh_index];

        const size_t palette_count = !mesh.joint_remaps.empty()
            ? mesh.joint_remaps.size()
            : mesh.inverse_bind_poses.size();
        for (size_t palette_index = 0; palette_index < palette_count; ++palette_index) {
            ensure_global_index(mesh, static_cast<uint16_t>(palette_index));
        }

        const uint32_t mesh_vertex_offset = vertex_offset;
        uint32_t mesh_vertex_count = 0;

        for (const ozz::sample::Mesh::Part& part : mesh.parts) {
            ozz::sample::Mesh::Part part_copy = part;
            const int influences = part_copy.influences_count();
            const int vertex_count = part_copy.vertex_count();
            if (!part_copy.joint_indices.empty() && influences > 0 && vertex_count > 0) {
                for (int vertex = 0; vertex < vertex_count; ++vertex) {
                    for (int influence = 0; influence < influences; ++influence) {
                        const size_t idx = static_cast<size_t>(vertex) * influences + influence;
                        if (idx >= part_copy.joint_indices.size()) {
                            break;
                        }
                        const uint16_t local_palette = part_copy.joint_indices[idx];
                        const uint16_t global_index = ensure_global_index(mesh, local_palette);
                        part_copy.joint_indices[idx] = global_index;
                    }
                }
            }
            out_mesh.parts.push_back(std::move(part_copy));
            mesh_vertex_count += static_cast<uint32_t>(part.vertex_count());
        }

        for (uint16_t index_value : mesh.triangle_indices) {
            const uint32_t adjusted = mesh_vertex_offset + static_cast<uint32_t>(index_value);
            out_mesh.triangle_indices.push_back(static_cast<uint16_t>(std::min<uint32_t>(adjusted, std::numeric_limits<uint16_t>::max())));
        }

        if (!any_visible) {
            out_mesh.xray_metadata = mesh.xray_metadata;
        } else {
            out_mesh.xray_metadata.original_vertex_count += mesh.xray_metadata.original_vertex_count;
            out_mesh.xray_metadata.original_face_count += mesh.xray_metadata.original_face_count;
            out_mesh.xray_metadata.progressive_collapse_count += mesh.xray_metadata.progressive_collapse_count;
            out_mesh.xray_metadata.lod_visuals.insert(out_mesh.xray_metadata.lod_visuals.end(),
                mesh.xray_metadata.lod_visuals.begin(), mesh.xray_metadata.lod_visuals.end());
            out_mesh.xray_metadata.lod_data.insert(out_mesh.xray_metadata.lod_data.end(),
                mesh.xray_metadata.lod_data.begin(), mesh.xray_metadata.lod_data.end());
            out_mesh.xray_metadata.child_visual_links.insert(out_mesh.xray_metadata.child_visual_links.end(),
                mesh.xray_metadata.child_visual_links.begin(), mesh.xray_metadata.child_visual_links.end());
        }

        vertex_offset += mesh_vertex_count;
        any_visible = true;
    }

    if (!any_visible) {
        return false;
    }

    if (!global_remap.empty()) {
        std::vector<uint16_t> order(global_remap.size());
        std::iota(order.begin(), order.end(), uint16_t(0));
        std::sort(order.begin(), order.end(), [&](uint16_t a, uint16_t b) {
            return global_remap[a] < global_remap[b];
        });

        std::vector<uint16_t> remap(order.size());
        ozz::vector<uint16_t> sorted_remap;
        sorted_remap.resize(order.size());
        ozz::vector<ozz::math::Float4x4> sorted_inverse;
        sorted_inverse.resize(order.size());
        for (size_t new_index = 0; new_index < order.size(); ++new_index) {
            const uint16_t old_index = order[new_index];
            sorted_remap[new_index] = global_remap[old_index];
            sorted_inverse[new_index] = global_inverse_bind[old_index];
            remap[old_index] = static_cast<uint16_t>(new_index);
        }

        for (auto& part : out_mesh.parts) {
            for (uint16_t& index_value : part.joint_indices) {
                if (index_value < remap.size()) {
                    index_value = remap[index_value];
                }
            }
        }

        out_mesh.joint_remaps = std::move(sorted_remap);
        out_mesh.inverse_bind_poses = std::move(sorted_inverse);
    } else {
        out_mesh.joint_remaps.clear();
        out_mesh.inverse_bind_poses.clear();
    }

    return true;
}

bool UploadVisibleMeshes(ViewerState& state, VulkanRenderer& renderer, std::string* error) {
    if (state.meshes.empty()) {
        renderer.SetShowSkinnedMesh(false);
        renderer.SetShowTriangle(true);
        return true;
    }

    ozz::sample::Mesh combined;
    if (!BuildCombinedMesh(state.meshes, state.mesh_visibility, combined)) {
        renderer.SetShowSkinnedMesh(false);
        renderer.SetShowTriangle(true);
        return true;
    }

    if (!renderer.LoadBundleMesh(combined, state.skeleton)) {
        if (error) {
            *error = "Failed to upload combined mesh";
        }
        renderer.SetShowSkinnedMesh(false);
        renderer.SetShowTriangle(true);
        return false;
    }

    renderer.SetShowSkinnedMesh(true);
    renderer.SetShowTriangle(false);
    renderer.SetMeshAnimationTime(0.0f);
    return true;
}

void ApplyMeshVisibility(ViewerState& state, VulkanRenderer& renderer) {
    if (!state.mesh_dirty) {
        return;
    }

    state.mesh_dirty = false;
    std::string error;
    if (!UploadVisibleMeshes(state, renderer, &error)) {
        QueueStatus(state, error, true);
        Msg("! %s", error.c_str());
        return;
    }

    renderer.SetShowSkinnedMesh(true);
    renderer.SetShowTriangle(false);
}

bool LoadBundleFromPath(const std::filesystem::path& bundle_path, ViewerState& state, VulkanRenderer& renderer, std::string& error) {
    XRay::Animation::OzzxBundle bundle;
    if (!XRay::Animation::ReadOzzxBundle(bundle_path, bundle)) {
        error = "Failed to read bundle: " + bundle_path.string();
        return false;
    }

    if (!LoadSkeletonFromBytes(bundle.skeleton, &state.skeleton)) {
        error = "Failed to deserialize skeleton from bundle: " + bundle_path.string();
        return false;
    }

    const auto skeleton_lines = BuildSkeletonLinePoints(state.skeleton);
    if (skeleton_lines.empty()) {
        error = "Skeleton contains no line segments for visualization";
        return false;
    }

    if (!renderer.GetSkeletonRenderer().IsInitialized()) {
        error = "Skeleton renderer is not initialized";
        return false;
    }

    if (!renderer.GetSkeletonRenderer().SetSkeletonLines(ozz::make_span(skeleton_lines))) {
        error = "Failed to upload skeleton line data to GPU";
        return false;
    }

    std::array<ozz::math::Float4x4, 1> transforms = {ozz::math::Float4x4::identity()};
    if (!renderer.GetSkeletonRenderer().SetInstanceTransforms(ozz::make_span(transforms))) {
        error = "Failed to upload skeleton transforms";
        return false;
    }

    const bool debug_ready = renderer.SetSkeletonDebugData(state.skeleton, bundle.bone_metadata);
    if (!debug_ready) {
        Msg("! Failed to prepare skeleton debug metadata (bones=%d)", state.skeleton.num_joints());
    }

    state.meshes.clear();
    if (!bundle.mesh.empty() && !LoadMeshesFromBytes(bundle.mesh, &state.meshes)) {
        error = "Failed to deserialize skinned mesh data from bundle";
        return false;
    }

    state.mesh_visibility.assign(state.meshes.size(), true);
    state.mesh_dirty = false;

    std::string mesh_error;
    if (!UploadVisibleMeshes(state, renderer, &mesh_error)) {
        error = std::move(mesh_error);
        return false;
    }

    state.animations.clear();
    state.animation_metadata.clear();
    state.current_animation_index = -1;
    state.loaded_animation_path.clear();

    bool animation_loaded = false;
    std::vector<ozz::animation::Animation> loaded_animations;
    std::vector<AnimationMetadata> loaded_metadata;

    auto set_loaded_animations = [&](std::vector<ozz::animation::Animation>&& clips,
        std::vector<AnimationMetadata>&& metadata_list, const std::string& source_label) {
        ApplyLoadedAnimations(state, renderer, std::move(clips), std::move(metadata_list), source_label, &animation_loaded);
    };

    std::filesystem::path animation_path;
    if (!state.cli_animation_path.empty()) {
        animation_path = NormalizeBundlePath(state.cli_animation_path);
    }
    if (state.animation_path_buffer[0] != '\0') {
        animation_path = NormalizeBundlePath(state.animation_path_buffer.data());
    }

    if (!animation_loaded && !animation_path.empty()) {
        std::string animation_error;
        if (LoadAnimationsFromPath(animation_path, loaded_animations, loaded_metadata, &animation_error)) {
            set_loaded_animations(std::move(loaded_animations), std::move(loaded_metadata), animation_path.string());
        } else {
            std::string message = animation_error.empty()
                ? "Failed to load animation: " + animation_path.string()
                : animation_error;
            Msg("! %s", message.c_str());
            QueueStatus(state, message, true);
        }
    }

    loaded_animations.clear();
    loaded_metadata.clear();

    if (!animation_loaded && !bundle.embedded_animation_data.empty()) {
        std::string animation_error;
        if (LoadAnimationsFromBytes(bundle.embedded_animation_data, loaded_animations, loaded_metadata, &animation_error)) {
            set_loaded_animations(std::move(loaded_animations), std::move(loaded_metadata), bundle_path.string() + " (embedded)");
        } else {
            std::string message = animation_error.empty()
                ? "Failed to decode embedded animations from bundle: " + bundle_path.string()
                : animation_error;
            Msg("! %s", message.c_str());
            QueueStatus(state, message, true);
        }
    }

    if (animation_loaded) {
        std::snprintf(state.animation_path_buffer.data(), state.animation_path_buffer.size(), "%s",
            state.loaded_animation_path.c_str());
        state.animation_path_buffer.back() = '\0';
    } else {
        state.animation_path_buffer[0] = '\0';
        renderer.SetActiveAnimation(nullptr);
    }

    state.bone_metadata = bundle.bone_metadata;
    state.loaded_bundle_path = bundle_path.string();
    state.bundle_loaded = true;

    std::snprintf(state.bundle_path_buffer.data(), state.bundle_path_buffer.size(), "%s", state.loaded_bundle_path.c_str());
    state.bundle_path_buffer.back() = '\0';

    renderer.SetMeshAnimationTime(0.0f);
    renderer.SetShowDebugOverlay(debug_ready);
    renderer.SetShowSkeletonLines(!debug_ready);
    return true;
}

void ProcessLoadRequests(ViewerState& state, VulkanRenderer& renderer) {
    if (!state.request_load && !state.request_reload) {
        return;
    }

    std::string path_string;
    if (state.request_reload) {
        path_string = state.loaded_bundle_path;
    } else {
        path_string = state.bundle_path_buffer.data();
    }

    state.request_load = false;
    state.request_reload = false;

    if (path_string.empty()) {
        QueueStatus(state, "Bundle path is empty", true);
        return;
    }

    const std::filesystem::path bundle_path = NormalizeBundlePath(path_string);

    std::string error;
    if (!LoadBundleFromPath(bundle_path, state, renderer, error)) {
        QueueStatus(state, error, true);
        Msg("! %s", error.c_str());
    } else {
        QueueStatus(state, "Loaded bundle: " + bundle_path.string(), false);
        Msg("* Loaded bundle: %s", bundle_path.string().c_str());
    }
}

void UpdatePerformanceHistory(ViewerState& state, const VulkanRenderer& renderer) {
    const float sample_ms = renderer.GetFrameDeltaMilliseconds();
    if (!std::isfinite(sample_ms) || sample_ms < 0.0f) {
        return;
    }

    state.frame_time_latest_ms = sample_ms;
    state.frame_time_history.push_back(sample_ms);
    if (state.frame_time_history.size() > state.frame_history_capacity) {
        state.frame_time_history.erase(state.frame_time_history.begin());
    }

    if (state.frame_time_history.empty()) {
        state.frame_time_average_ms = 0.0f;
        state.frame_time_peak_ms = 0.0f;
        return;
    }

    const float sum = std::accumulate(state.frame_time_history.begin(), state.frame_time_history.end(), 0.0f);
    state.frame_time_average_ms = sum / static_cast<float>(state.frame_time_history.size());
    state.frame_time_peak_ms = *std::max_element(state.frame_time_history.begin(), state.frame_time_history.end());
}

void RenderDockspace(ViewerState& state) {
    ImGuiIO& io = ImGui::GetIO();
    if (!(io.ConfigFlags & ImGuiConfigFlags_DockingEnable)) {
        return;
    }

    const ImGuiViewport* viewport = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(viewport->WorkPos);
    ImGui::SetNextWindowSize(viewport->WorkSize);
    ImGui::SetNextWindowViewport(viewport->ID);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));

    const ImGuiWindowFlags host_flags = ImGuiWindowFlags_NoDocking |
        ImGuiWindowFlags_NoTitleBar |
        ImGuiWindowFlags_NoCollapse |
        ImGuiWindowFlags_NoResize |
        ImGuiWindowFlags_NoMove |
        ImGuiWindowFlags_NoBringToFrontOnFocus |
        ImGuiWindowFlags_NoNavFocus |
        ImGuiWindowFlags_NoBackground;

    if (ImGui::Begin("##ViewerDockHost", nullptr, host_flags)) {
        state.dockspace_id = ImGui::GetID("ViewerDockSpace");
        const ImGuiDockNodeFlags dock_flags = ImGuiDockNodeFlags_PassthruCentralNode | ImGuiDockNodeFlags_NoDockingInCentralNode;
        ImGui::DockSpace(state.dockspace_id, ImVec2(0.f, 0.f), dock_flags);

        if (!state.dockspace_initialized) {
            state.dockspace_initialized = true;
            ImGui::DockBuilderRemoveNode(state.dockspace_id);
            ImGui::DockBuilderAddNode(state.dockspace_id, ImGuiDockNodeFlags_DockSpace);
            ImGui::DockBuilderSetNodeSize(state.dockspace_id, viewport->WorkSize);

            ImGuiID dock_main_id = state.dockspace_id;
            ImGuiID dock_right_id = ImGui::DockBuilderSplitNode(dock_main_id, ImGuiDir_Right, 0.30f, nullptr, &dock_main_id);
            ImGuiID dock_left_id = ImGui::DockBuilderSplitNode(dock_main_id, ImGuiDir_Left, 0.24f, nullptr, &dock_main_id);
            ImGuiID dock_right_bottom_id = ImGui::DockBuilderSplitNode(dock_right_id, ImGuiDir_Down, 0.45f, nullptr, &dock_right_id);

            ImGui::DockBuilderDockWindow("Bundle Inspector", dock_left_id);
            ImGui::DockBuilderDockWindow("Rendering", dock_right_id);
            ImGui::DockBuilderDockWindow("Animation Controls", dock_right_id);
            ImGui::DockBuilderDockWindow("Performance", dock_right_bottom_id);

            ImGui::DockBuilderFinish(state.dockspace_id);
        }
    }
    ImGui::End();
    ImGui::PopStyleVar(3);
}

void DrawPerformancePanel(const ViewerState& state) {
    if (!ImGui::Begin("Performance")) {
        ImGui::End();
        return;
    }

    if (state.frame_time_history.empty()) {
        ImGui::TextUnformatted("Frame timing data will appear after a few frames.");
        ImGui::End();
        return;
    }

    const float fps = state.frame_time_average_ms > 0.0f ? 1000.0f / state.frame_time_average_ms : 0.0f;
    ImGui::Text("Average: %.2f ms (%.0f FPS)", state.frame_time_average_ms, fps);
    ImGui::Text("Latest: %.2f ms    Peak: %.2f ms", state.frame_time_latest_ms, state.frame_time_peak_ms);

    const float plot_max = std::max(state.frame_time_peak_ms * 1.1f, 1.0f);
    ImGui::PlotLines("Frame Time (ms)", state.frame_time_history.data(), static_cast<int>(state.frame_time_history.size()),
        0, nullptr, 0.0f, plot_max, ImVec2(-1.0f, 100.0f));

    ImGui::End();
}

void DrawAnimationPanel(ViewerState& state, VulkanRenderer& renderer) {
    if (!ImGui::Begin("Animation Controls")) {
        ImGui::End();
        return;
    }

    const char* source_label = state.loaded_animation_path.empty() ? "embedded clip" : state.loaded_animation_path.c_str();
    ImGui::Text("Source: %s", source_label);

    if (!state.animations.empty()) {
        if (state.current_animation_index < 0 || state.current_animation_index >= static_cast<int>(state.animations.size())) {
            state.current_animation_index = 0;
            renderer.SetActiveAnimation(&state.animations.front());
        }

        const size_t active_index = static_cast<size_t>(std::clamp(state.current_animation_index, 0, static_cast<int>(state.animations.size() - 1)));
        const AnimationMetadata* active_metadata = (active_index < state.animation_metadata.size()) ?
            &state.animation_metadata[active_index] : nullptr;
        const std::string active_label = BuildAnimationLabel(active_index, state.animations[active_index], active_metadata);

        if (ImGui::BeginCombo("Active animation", active_label.c_str())) {
            for (size_t i = 0; i < state.animations.size(); ++i) {
                const AnimationMetadata* metadata = (i < state.animation_metadata.size()) ? &state.animation_metadata[i] : nullptr;
                const std::string item_label = BuildAnimationLabel(i, state.animations[i], metadata);
                const bool selected = static_cast<int>(i) == state.current_animation_index;
                if (ImGui::Selectable(item_label.c_str(), selected)) {
                    state.current_animation_index = static_cast<int>(i);
                    renderer.SetActiveAnimation(&state.animations[i]);
                }
                if (selected) {
                    ImGui::SetItemDefaultFocus();
                }
            }
            ImGui::EndCombo();
        }

        const ozz::animation::Animation& active_animation = state.animations[active_index];
        ImGui::Text("Duration: %.2f s", active_animation.duration());
        ImGui::Text("Tracks: %d", active_animation.num_tracks());
        ImGui::Text("Soa tracks: %d", active_animation.num_soa_tracks());
    } else {
        ImGui::TextUnformatted("Active animation: <bind pose>");
    }
    ImGui::Separator();

    // Bind Pose / Play Animation toggle
    bool show_bind_pose = renderer.GetShowBindPose();
    const char* button_label = show_bind_pose ? "Play Animation" : "Show Bind Pose";
    if (ImGui::Button(button_label, ImVec2(140, 0))) {
        renderer.SetShowBindPose(!show_bind_pose);
    }

    ImGui::Separator();

    bool animate_mesh = renderer.GetAnimateMesh();
    if (ImGui::Checkbox("Auto-rotate mesh", &animate_mesh)) {
        renderer.SetAnimateMesh(animate_mesh);
    }

    float rotation_speed = renderer.GetMeshRotationSpeed();
    if (ImGui::SliderFloat("Rotation speed (rad/s)", &rotation_speed, -6.0f, 6.0f, "%.2f")) {
        renderer.SetMeshRotationSpeed(rotation_speed);
    }

    if (!renderer.GetAnimateMesh()) {
        float playback_time = renderer.GetMeshAnimationTime();
        if (ImGui::SliderFloat("Manual animation time", &playback_time, 0.0f, 120.0f, "%.2f")) {
            renderer.SetMeshAnimationTime(playback_time);
        }
    }

    ImGui::Separator();
    ImGui::Text("Frame %.2f ms (%.1f FPS)", renderer.GetFrameDeltaMilliseconds(), renderer.GetFrameRate());

    ImGui::End();
}

void DrawRenderingPanel(VulkanRenderer& renderer) {
    if (!ImGui::Begin("Rendering")) {
        ImGui::End();
        return;
    }

    bool show_triangle = renderer.GetShowTriangle();
    if (ImGui::Checkbox("Show debug triangle", &show_triangle)) {
        renderer.SetShowTriangle(show_triangle);
    }

    bool show_skeleton = renderer.GetShowSkeletonLines();
    if (ImGui::Checkbox("Show skeleton lines", &show_skeleton)) {
        renderer.SetShowSkeletonLines(show_skeleton);
    }

    bool show_mesh = renderer.GetShowSkinnedMesh();
    if (ImGui::Checkbox("Show skinned mesh", &show_mesh)) {
        renderer.SetShowSkinnedMesh(show_mesh);
    }

    bool show_debug_overlay = renderer.GetShowDebugOverlay();
    if (ImGui::Checkbox("Show debug overlay", &show_debug_overlay)) {
        renderer.SetShowDebugOverlay(show_debug_overlay);
    }

    ImGui::Separator();

    float clear_r = 0.0f;
    float clear_g = 0.0f;
    float clear_b = 0.0f;
    renderer.GetClearColor(clear_r, clear_g, clear_b);
    float clear_color[3] = {clear_r, clear_g, clear_b};
    if (ImGui::ColorEdit3("Clear color", clear_color, ImGuiColorEditFlags_NoInputs)) {
        renderer.SetClearColor(clear_color[0], clear_color[1], clear_color[2]);
    }

    ImGui::End();
}

void DrawMenuBar(ViewerState& state, VulkanRenderer& renderer) {
    ImGuiIO& io = ImGui::GetIO();
    const bool ctrl_reload = io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_R, false);

    if (ctrl_reload) {
        state.request_reload = true;
    }

    if (!ImGui::BeginMainMenuBar()) {
        return;
    }

    if (ImGui::BeginMenu("File")) {
        if (ImGui::MenuItem("Reload Bundle", "Ctrl+R", false, state.bundle_loaded)) {
            state.request_reload = true;
        }

        if (ImGui::MenuItem("Quit")) {
            if (state.window) {
                glfwSetWindowShouldClose(state.window, GLFW_TRUE);
            }
        }
        ImGui::EndMenu();
    }

    if (ImGui::BeginMenu("View")) {
        ImGui::MenuItem("Dear ImGui Demo", nullptr, &state.show_demo_window);
        ImGui::EndMenu();
    }

    if (ImGui::BeginMenu("Render")) {
        bool show_triangle = renderer.GetShowTriangle();
        if (ImGui::MenuItem("Show Debug Triangle", nullptr, &show_triangle)) {
            renderer.SetShowTriangle(show_triangle);
        }

        bool show_skeleton = renderer.GetShowSkeletonLines();
        if (ImGui::MenuItem("Show Skeleton Lines", nullptr, &show_skeleton, renderer.HasSkeletonLoaded())) {
            renderer.SetShowSkeletonLines(show_skeleton);
        }

        bool show_mesh = renderer.GetShowSkinnedMesh();
        if (ImGui::MenuItem("Show Skinned Mesh", nullptr, &show_mesh, renderer.HasMeshLoaded())) {
            renderer.SetShowSkinnedMesh(show_mesh);
        }

        bool show_debug = renderer.GetShowDebugOverlay();
        if (ImGui::MenuItem("Show Debug Overlay", nullptr, &show_debug, renderer.HasSkeletonLoaded())) {
            renderer.SetShowDebugOverlay(show_debug);
        }
        ImGui::EndMenu();
    }

    ImGui::EndMainMenuBar();
}

void DrawBundleInspector(ViewerState& state, VulkanRenderer& renderer, double now_seconds) {
    if (!ImGui::Begin("Bundle Inspector")) {
        ImGui::End();
        return;
    }

    ImGui::InputText("Bundle Path", state.bundle_path_buffer.data(), state.bundle_path_buffer.size());

    if (ImGui::Button("Load Bundle")) {
        state.request_load = true;
    }
    ImGui::SameLine();
    if (ImGui::Button("Reload") && state.bundle_loaded) {
        state.request_reload = true;
    }

    ImGui::Separator();
    ImGui::InputText("Animation Path", state.animation_path_buffer.data(), state.animation_path_buffer.size());
    ImGui::SameLine();
    if (ImGui::Button("Load Animation") && state.animation_path_buffer[0] != '\0') {
        const std::filesystem::path animation_path = NormalizeBundlePath(state.animation_path_buffer.data());
        std::vector<ozz::animation::Animation> manual_animations;
        std::vector<AnimationMetadata> manual_metadata;
        std::string animation_error;
        if (LoadAnimationsFromPath(animation_path, manual_animations, manual_metadata, &animation_error)) {
            ApplyLoadedAnimations(state, renderer, std::move(manual_animations), std::move(manual_metadata), animation_path.string());
            state.cli_animation_path = animation_path.string();
            state.animation_path_buffer.back() = '\0';
            QueueStatus(state, "Loaded animation: " + animation_path.string(), false);
        } else {
            const std::string message = animation_error.empty()
                ? "Failed to load animation: " + animation_path.string()
                : animation_error;
            QueueStatus(state, message, true);
            Msg("! %s", message.c_str());
        }
    }
    ImGui::SameLine();
    if (ImGui::Button("Clear Animation")) {
        state.animation_path_buffer[0] = '\0';
        state.cli_animation_path.clear();
        state.animations.clear();
        state.animation_metadata.clear();
        state.current_animation_index = -1;
        renderer.SetActiveAnimation(nullptr);
        QueueStatus(state, "Cleared loaded animations", false);
    }

    if (!state.bundle_loaded) {
        ImGui::Separator();
        ImGui::TextDisabled("No bundle loaded.");
    } else {
        ImGui::Separator();
        ImGui::Text("Active bundle:");
        ImGui::TextWrapped("%s", state.loaded_bundle_path.c_str());

        const int joint_count = state.skeleton.num_joints();
        ImGui::Text("Skeleton joints: %d", joint_count);
        ImGui::Text("Mesh count: %zu", state.meshes.size());
        ImGui::Text("Extended bone metadata entries: %zu", state.bone_metadata.size());

        if (joint_count > 0 && ImGui::CollapsingHeader("Joint Names", ImGuiTreeNodeFlags_DefaultOpen)) {
            ImGui::BeginChild("##JointNames", ImVec2(0.f, 180.f), true, ImGuiWindowFlags_HorizontalScrollbar);
            const auto names = state.skeleton.joint_names();
            for (int i = 0; i < joint_count; ++i) {
                ImGui::Text("[%03d] %s", i, names[i]);
            }
            ImGui::EndChild();
        }

        if (!state.meshes.empty() && ImGui::CollapsingHeader("Meshes", ImGuiTreeNodeFlags_DefaultOpen)) {
            if (ImGui::BeginTable("MeshTable", 5, ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_Resizable)) {
                ImGui::TableSetupColumn("Index", ImGuiTableColumnFlags_WidthFixed, 60.f);
                ImGui::TableSetupColumn("Vertices");
                ImGui::TableSetupColumn("Triangles");
                ImGui::TableSetupColumn("Bones");
                ImGui::TableSetupColumn("Original Faces");
                ImGui::TableHeadersRow();

                for (size_t i = 0; i < state.meshes.size(); ++i) {
                    const auto& mesh = state.meshes[i];
                    ImGui::TableNextRow();
                    ImGui::TableNextColumn();
                    ImGui::Text("%zu", i);
                    ImGui::TableNextColumn();
                    ImGui::Text("%d", mesh.vertex_count());
                    ImGui::TableNextColumn();
                    ImGui::Text("%zu", mesh.triangle_indices.size() / 3);
                    ImGui::TableNextColumn();
                    ImGui::Text("%zu", mesh.joint_remaps.size());
                    ImGui::TableNextColumn();
                    ImGui::Text("%u", mesh.xray_metadata.original_face_count);
                }

                ImGui::EndTable();
            }

            if (state.mesh_visibility.size() != state.meshes.size()) {
                state.mesh_visibility.assign(state.meshes.size(), true);
                state.mesh_dirty = true;
            }

            if (ImGui::Button("Show All Meshes")) {
                state.mesh_visibility.assign(state.meshes.size(), true);
                state.mesh_dirty = true;
            }
            ImGui::SameLine();
            if (ImGui::Button("Hide All Meshes")) {
                state.mesh_visibility.assign(state.meshes.size(), false);
                state.mesh_dirty = true;
            }

            for (size_t i = 0; i < state.meshes.size(); ++i) {
                bool visible = i < state.mesh_visibility.size() ? state.mesh_visibility[i] : true;
                std::string label = "##visible_mesh_" + std::to_string(i);
                ImGui::PushID(static_cast<int>(i));
                if (ImGui::Checkbox(label.c_str(), &visible)) {
                    if (i < state.mesh_visibility.size()) {
                        state.mesh_visibility[i] = visible;
                        state.mesh_dirty = true;
                    }
                }
                ImGui::SameLine();
                ImGui::Text("Mesh %zu", i);
                ImGui::PopID();

                if (ImGui::IsItemHovered()) {
                    ImGui::BeginTooltip();
                    const auto& mesh = state.meshes[i];
                    ImGui::Text("Vertices: %d", mesh.vertex_count());
                    ImGui::Text("Triangles: %zu", mesh.triangle_indices.size() / 3);
                    ImGui::Text("Bones: %zu", mesh.joint_remaps.size());
                    ImGui::EndTooltip();
                }
            }
        }

        if (!state.bone_metadata.empty() && ImGui::CollapsingHeader("Bone Metadata")) {
            ImGui::BeginChild("##BoneMetadata", ImVec2(0.f, 160.f), true, ImGuiWindowFlags_HorizontalScrollbar);
            for (size_t bone = 0; bone < state.bone_metadata.size(); ++bone) {
                const auto& metadata = state.bone_metadata[bone];
                ImGui::Text("[%03zu] rest=%.3f mass=%.3f ground=%s weapon=%s",
                    bone,
                    metadata.rest_length,
                    metadata.mass,
                    metadata.ground_contact_candidate ? "yes" : "no",
                    metadata.weapon_anchor_candidate ? "yes" : "no");
            }
            ImGui::EndChild();
        }

        if (!state.animation_metadata.empty() && ImGui::CollapsingHeader("Animation Metadata", ImGuiTreeNodeFlags_DefaultOpen)) {
            ImGui::BeginChild("##AnimationMetadata", ImVec2(0.f, 220.f), true, ImGuiWindowFlags_HorizontalScrollbar);
            for (size_t anim_index = 0; anim_index < state.animation_metadata.size(); ++anim_index) {
                ImGui::PushID(static_cast<int>(anim_index));
                const AnimationMetadata& metadata = state.animation_metadata[anim_index];
                const AnimationMetadata* metadata_ptr = &metadata;
                std::string clip_label;
                if (anim_index < state.animations.size()) {
                    clip_label = BuildAnimationLabel(anim_index, state.animations[anim_index], metadata_ptr);
                } else if (metadata.name.empty()) {
                    clip_label = "animation_" + std::to_string(anim_index);
                } else {
                    clip_label = metadata.name;
                }
                std::string tree_label = "[" + std::to_string(anim_index) + "] " + clip_label;
                if (ImGui::TreeNode(tree_label.c_str())) {
                    ImGui::Text("Flags: %u (0x%08X)", metadata.flags, metadata.flags);
                    ImGui::Text("Bone/Part: %u", metadata.bone_or_part);
                    ImGui::Text("Motion ID: %u", metadata.motion_id);
                    ImGui::Text("Speed: %.3f  Power: %.3f", metadata.speed, metadata.power);
                    ImGui::Text("Accrue: %.3f  Falloff: %.3f", metadata.accrue, metadata.falloff);
                    ImGui::Text("Frame Count: %u", metadata.frame_count);

                    if (!metadata.marks.empty()) {
                        if (ImGui::TreeNode("Marks")) {
                            if (ImGui::BeginTable("MarksTable", 3, ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_Resizable)) {
                                ImGui::TableSetupColumn("Index", ImGuiTableColumnFlags_WidthFixed, 60.f);
                                ImGui::TableSetupColumn("Name", ImGuiTableColumnFlags_WidthStretch);
                                ImGui::TableSetupColumn("Intervals", ImGuiTableColumnFlags_WidthStretch);
                                ImGui::TableHeadersRow();
                                for (size_t mark_index = 0; mark_index < metadata.marks.size(); ++mark_index) {
                                    const AnimationMark& mark = metadata.marks[mark_index];
                                    ImGui::TableNextRow();
                                    ImGui::TableNextColumn();
                                    ImGui::Text("%zu", mark_index);
                                    ImGui::TableNextColumn();
                                    ImGui::TextUnformatted(mark.name.c_str());
                                    ImGui::TableNextColumn();
                                    std::string intervals_text;
                                    for (size_t interval_index = 0; interval_index < mark.intervals.size(); ++interval_index) {
                                        const AnimationInterval& interval = mark.intervals[interval_index];
                                        intervals_text += "[" + std::to_string(interval.start) + ", " + std::to_string(interval.end) + "]";
                                        if (interval_index + 1 < mark.intervals.size()) {
                                            intervals_text += "; ";
                                        }
                                    }
                                    ImGui::TextUnformatted(intervals_text.c_str());
                                }
                                ImGui::EndTable();
                            }
                            ImGui::TreePop();
                        }
                    } else {
                        ImGui::TextUnformatted("Marks: none");
                    }

                    if (!metadata.bone_motions.empty()) {
                        if (ImGui::TreeNode("Bone Motions")) {
                            for (size_t bone_index = 0; bone_index < metadata.bone_motions.size(); ++bone_index) {
                                ImGui::PushID(static_cast<int>(bone_index));
                                const AnimationBoneMotion& bone = metadata.bone_motions[bone_index];
                                std::string bone_label = "Bone " + std::to_string(bone.bone_id) + " (index " + std::to_string(bone_index) + ")";
                                if (ImGui::TreeNode(bone_label.c_str())) {
                                    ImGui::Text("Flags: %u (0x%02X)", bone.flags, bone.flags);
                                    ImGui::Text("Rotation CRC: 0x%08X", bone.rotation_crc);
                                    ImGui::Text("Translation CRC: 0x%08X", bone.translation_crc);
                                    ImGui::Text("Translation Format: %u", bone.translation_format);
                                    ImGui::Text("Rotation Keys: %zu", bone.rotation_keys.size());
                                    ImGui::Text("Translation Keys (8-bit): %zu", bone.translation_keys8.size());
                                    ImGui::Text("Translation Keys (16-bit): %zu", bone.translation_keys16.size());
                                    ImGui::Text("Translation Size: [%.6f, %.6f, %.6f]", bone.translation_size.x, bone.translation_size.y, bone.translation_size.z);
                                    ImGui::Text("Translation Init: [%.6f, %.6f, %.6f]", bone.translation_init.x, bone.translation_init.y, bone.translation_init.z);

                                    if (!bone.rotation_keys.empty() && ImGui::TreeNode("Rotation Keys")) {
                                        if (ImGui::BeginTable("RotationKeyTable", 5, ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_Resizable)) {
                                            ImGui::TableSetupColumn("Index", ImGuiTableColumnFlags_WidthFixed, 60.f);
                                            ImGui::TableSetupColumn("x");
                                            ImGui::TableSetupColumn("y");
                                            ImGui::TableSetupColumn("z");
                                            ImGui::TableSetupColumn("w");
                                            ImGui::TableHeadersRow();
                                            for (size_t key_index = 0; key_index < bone.rotation_keys.size(); ++key_index) {
                                                const CKeyQR& key = bone.rotation_keys[key_index];
                                                ImGui::TableNextRow();
                                                ImGui::TableNextColumn();
                                                ImGui::Text("%zu", key_index);
                                                ImGui::TableNextColumn();
                                                ImGui::Text("%d", key.x);
                                                ImGui::TableNextColumn();
                                                ImGui::Text("%d", key.y);
                                                ImGui::TableNextColumn();
                                                ImGui::Text("%d", key.z);
                                                ImGui::TableNextColumn();
                                                ImGui::Text("%d", key.w);
                                            }
                                            ImGui::EndTable();
                                        }
                                        ImGui::TreePop();
                                    }

                                    if (!bone.translation_keys8.empty() && ImGui::TreeNode("Translation Keys (8-bit)")) {
                                        if (ImGui::BeginTable("TranslationKey8Table", 4, ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_Resizable)) {
                                            ImGui::TableSetupColumn("Index", ImGuiTableColumnFlags_WidthFixed, 60.f);
                                            ImGui::TableSetupColumn("x1");
                                            ImGui::TableSetupColumn("y1");
                                            ImGui::TableSetupColumn("z1");
                                            ImGui::TableHeadersRow();
                                            for (size_t key_index = 0; key_index < bone.translation_keys8.size(); ++key_index) {
                                                const CKeyQT8& key = bone.translation_keys8[key_index];
                                                ImGui::TableNextRow();
                                                ImGui::TableNextColumn();
                                                ImGui::Text("%zu", key_index);
                                                ImGui::TableNextColumn();
                                                ImGui::Text("%d", key.x1);
                                                ImGui::TableNextColumn();
                                                ImGui::Text("%d", key.y1);
                                                ImGui::TableNextColumn();
                                                ImGui::Text("%d", key.z1);
                                            }
                                            ImGui::EndTable();
                                        }
                                        ImGui::TreePop();
                                    }

                                    if (!bone.translation_keys16.empty() && ImGui::TreeNode("Translation Keys (16-bit)")) {
                                        if (ImGui::BeginTable("TranslationKey16Table", 4, ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_Resizable)) {
                                            ImGui::TableSetupColumn("Index", ImGuiTableColumnFlags_WidthFixed, 60.f);
                                            ImGui::TableSetupColumn("x1");
                                            ImGui::TableSetupColumn("y1");
                                            ImGui::TableSetupColumn("z1");
                                            ImGui::TableHeadersRow();
                                            for (size_t key_index = 0; key_index < bone.translation_keys16.size(); ++key_index) {
                                                const CKeyQT16& key = bone.translation_keys16[key_index];
                                                ImGui::TableNextRow();
                                                ImGui::TableNextColumn();
                                                ImGui::Text("%zu", key_index);
                                                ImGui::TableNextColumn();
                                                ImGui::Text("%d", key.x1);
                                                ImGui::TableNextColumn();
                                                ImGui::Text("%d", key.y1);
                                                ImGui::TableNextColumn();
                                                ImGui::Text("%d", key.z1);
                                            }
                                            ImGui::EndTable();
                                        }
                                        ImGui::TreePop();
                                    }

                                    ImGui::TreePop();
                                }
                                ImGui::PopID();
                            }
                            ImGui::TreePop();
                        }
                    } else {
                        ImGui::TextUnformatted("Bone motions: none");
                    }

                    ImGui::TreePop();
                }
                ImGui::PopID();
            }
            ImGui::EndChild();
        }
    }

    if (!state.status_message.empty()) {
        const double elapsed = now_seconds - state.status_timestamp;
        if (elapsed < kStatusMessageDuration) {
            const ImVec4 color = state.status_is_error ? ImVec4(0.9f, 0.3f, 0.3f, 1.0f) : ImVec4(0.4f, 0.8f, 0.4f, 1.0f);
            ImGui::Spacing();
            ImGui::TextColored(color, "%s", state.status_message.c_str());
        } else {
            state.status_message.clear();
        }
    }

    ImGui::End();
}

Camera* GetCameraForWindow(GLFWwindow* window) {
    if (!window) {
        return nullptr;
    }
    auto* renderer = static_cast<VulkanRenderer*>(glfwGetWindowUserPointer(window));
    if (!renderer) {
        return nullptr;
    }
    if (renderer->IsImGuiInitialized()) {
        ImGui::SetCurrentContext(renderer->GetImGuiContext());
        if (ImGui::GetIO().WantCaptureMouse) {
            return nullptr;
        }
    }
    return &renderer->GetCamera();
}

void OnGlfwMouseButton(GLFWwindow* window, int button, int action, int mods) {
    Camera* camera = GetCameraForWindow(window);
    if (!camera) {
        return;
    }
    double xpos = 0.0;
    double ypos = 0.0;
    glfwGetCursorPos(window, &xpos, &ypos);
    if (button == GLFW_MOUSE_BUTTON_RIGHT && action == GLFW_PRESS && (mods & GLFW_MOD_SHIFT)) {
        camera->SetPivotFromScreen(xpos, ypos);
    }
    camera->OnMouseButton(button, action, mods, xpos, ypos);
}

void OnGlfwCursorPos(GLFWwindow* window, double xpos, double ypos) {
    Camera* camera = GetCameraForWindow(window);
    if (!camera) {
        return;
    }
    camera->OnMouseMove(xpos, ypos);
}

void OnGlfwScroll(GLFWwindow* window, double xoffset, double yoffset) {
    Camera* camera = GetCameraForWindow(window);
    if (!camera) {
        return;
    }
    camera->OnMouseScroll(xoffset, yoffset);
}

} // namespace

int main(int argc, const char** argv) {
    Msg("* Starting Vulkan viewer...");

    if (!glfwInit()) {
        Msg("! Failed to initialize GLFW");
        return -1;
    }

    glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
    glfwWindowHint(GLFW_RESIZABLE, GLFW_TRUE);

    GLFWwindow* window = glfwCreateWindow(1600, 900, "ozz Animation Viewer (Vulkan)", nullptr, nullptr);
    if (!window) {
        Msg("! Failed to create GLFW window");
        glfwTerminate();
        return -1;
    }

    VulkanRenderer renderer;
    glfwSetWindowUserPointer(window, &renderer);
    glfwSetMouseButtonCallback(window, OnGlfwMouseButton);
    glfwSetCursorPosCallback(window, OnGlfwCursorPos);
    glfwSetScrollCallback(window, OnGlfwScroll);
    if (!renderer.Initialize(window)) {
        Msg("! Failed to initialize Vulkan renderer");
        glfwDestroyWindow(window);
        glfwTerminate();
        return -1;
    }

    ViewerState state;
    state.window = window;

    const std::string bundle_argument = ParseBundleArgument(argc, argv);
    const std::string animation_argument = ParseAnimationArgument(argc, argv);
    state.cli_animation_path = animation_argument;
    if (!animation_argument.empty()) {
        std::snprintf(state.animation_path_buffer.data(), state.animation_path_buffer.size(), "%s", animation_argument.c_str());
        state.animation_path_buffer.back() = '\0';
    } else {
        state.animation_path_buffer[0] = '\0';
    }
    if (!bundle_argument.empty()) {
        const std::filesystem::path bundle_path = NormalizeBundlePath(bundle_argument);
        std::string error;
        if (!LoadBundleFromPath(bundle_path, state, renderer, error)) {
            QueueStatus(state, error, true);
            Msg("! %s", error.c_str());
        } else {
            QueueStatus(state, "Loaded bundle: " + bundle_path.string(), false);
            Msg("* Loaded bundle from CLI: %s", bundle_path.string().c_str());
        }
    }

    while (!glfwWindowShouldClose(window)) {
        glfwPollEvents();

        ProcessLoadRequests(state, renderer);
        ApplyMeshVisibility(state, renderer);

        renderer.BeginFrame();
        UpdatePerformanceHistory(state, renderer);

        if (renderer.IsImGuiInitialized()) {
            ImGui::SetCurrentContext(renderer.GetImGuiContext());
            RenderDockspace(state);
            const double now_seconds = glfwGetTime();
            DrawMenuBar(state, renderer);
            DrawBundleInspector(state, renderer, now_seconds);
            DrawRenderingPanel(renderer);
            DrawAnimationPanel(state, renderer);
            DrawPerformancePanel(state);
            if (state.show_demo_window) {
                ImGui::ShowDemoWindow(&state.show_demo_window);
            }
        }

        renderer.RenderScene();
        renderer.EndFrame();
    }

    Msg("* Shutting down viewer...");
    renderer.Shutdown();
    glfwDestroyWindow(window);
    glfwTerminate();
    return 0;
}
