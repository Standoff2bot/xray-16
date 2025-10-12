#include "stdafx.h"
#include "InstancedMeshRenderer.h"

#include "VulkanDevice.h"
#include "framework/mesh.h"
#include <ozz/base/maths/simd_math.h>

#include <algorithm>
#include <array>
#include <vector>
#include <filesystem>
#include <limits>
#include <cmath>

#define Msg(...) printf(__VA_ARGS__), printf("\n")

namespace xray {
namespace animation {
namespace renderer {

namespace {

struct CameraUBO {
    float view_proj[16];
};

struct Influence {
    uint32_t palette = 0;
    float weight = 0.0f;
};

void StoreMatrix(const ozz::math::Float4x4& source, float* destination) {
    // Store as column-major for GLSL (std140/std430 layout)
    // This matches what ozz::math::StorePtrU does for each column
    for (int col = 0; col < 4; ++col) {
        ozz::math::StorePtrU(source.cols[col], destination + col * 4);
    }
}

} // namespace

bool InstancedMeshRenderer::Initialize(VulkanDevice* device) {
    device_ = device;
    if (!device_) {
        Msg("! InstancedMeshRenderer::Initialize requires a valid device");
        return false;
    }

    if (!CreateDescriptorSetLayout()) return false;
    if (!CreatePipeline()) return false;
    if (!CreateUniformBuffer()) return false;
    if (!EnsureInstanceBuffer(1)) return false;
    if (!EnsureBoneBuffer(1)) return false;
    if (!CreateDescriptorPool()) return false;

    initialized_ = true;
    return true;
}

void InstancedMeshRenderer::Shutdown() {
    if (!device_) {
        clip_debug_buffer_.Destroy();
        clip_debug_capacity_ = 0;
        vertex_buffer_.Destroy();
        index_buffer_.Destroy();
        instance_buffer_.Destroy();
        bone_matrix_buffer_.Destroy();
        uniform_buffer_.Destroy();
        debug_uniform_buffer_.Destroy();
        pipeline_.Destroy();
        descriptor_set_layout_ = VK_NULL_HANDLE;
        descriptor_pool_ = VK_NULL_HANDLE;
        descriptor_set_ = VK_NULL_HANDLE;
        initialized_ = false;
        mesh_uploaded_ = false;
        bones_per_instance_ = 0;
        instance_capacity_ = 0;
        bone_matrix_capacity_ = 0;
        instance_count_ = 0;
        return;
    }

    VkDevice vk_device = device_->GetDevice();

    if (descriptor_pool_ != VK_NULL_HANDLE) {
        vkDestroyDescriptorPool(vk_device, descriptor_pool_, nullptr);
        descriptor_pool_ = VK_NULL_HANDLE;
    }

    if (descriptor_set_layout_ != VK_NULL_HANDLE) {
        vkDestroyDescriptorSetLayout(vk_device, descriptor_set_layout_, nullptr);
        descriptor_set_layout_ = VK_NULL_HANDLE;
    }

    pipeline_.Destroy();
    uniform_buffer_.Destroy();
    debug_uniform_buffer_.Destroy();
    bone_matrix_buffer_.Destroy();
    clip_debug_buffer_.Destroy();
    clip_debug_capacity_ = 0;
    frag_debug_buffer_.Destroy();
    frag_debug_capacity_ = 0;
    instance_buffer_.Destroy();
    index_buffer_.Destroy();
    vertex_buffer_.Destroy();

    descriptor_set_ = VK_NULL_HANDLE;
    initialized_ = false;
    mesh_uploaded_ = false;
    bones_per_instance_ = 0;
    instance_capacity_ = 0;
    bone_matrix_capacity_ = 0;
    instance_count_ = 0;
    device_ = nullptr;
}

bool InstancedMeshRenderer::UploadMesh(const ozz::sample::Mesh& mesh) {
    if (!initialized_) {
        Msg("! InstancedMeshRenderer::UploadMesh called before Initialize");
        return false;
    }

    const size_t total_vertices = static_cast<size_t>(mesh.vertex_count());
    const size_t total_indices = mesh.triangle_indices.size();

    if (total_vertices == 0 || total_indices == 0) {
        Msg("! Mesh has no geometry to upload");
        return false;
    }

    std::vector<Vertex> vertices(total_vertices);
    size_t vertex_cursor = 0;

    std::vector<Influence> influences_buffer;
    influences_buffer.reserve(8);
    size_t max_palette_index = 0;
    size_t max_requested_palette_index = 0;
    bool reported_invalid_joint_index = false;

    const size_t palette_capacity = !mesh.inverse_bind_poses.empty()
        ? mesh.inverse_bind_poses.size()
        : mesh.joint_remaps.size();

    for (const auto& part : mesh.parts) {
        const int vertex_count = part.vertex_count();
        const int influences_per_vertex = part.influences_count();

        const bool has_normals = !part.normals.empty();
        const bool has_uvs = !part.uvs.empty();
        const bool has_weights = influences_per_vertex > 0;

        for (int v = 0; v < vertex_count; ++v) {
            if (vertex_cursor >= vertices.size()) {
                Msg("! Mesh vertex cursor exceeded allocated buffer");
                return false;
            }

            Vertex& dst = vertices[vertex_cursor++];

            // Positions
            const int position_idx = v * ozz::sample::Mesh::Part::kPositionsCpnts;
            dst.position[0] = part.positions[position_idx + 0];
            dst.position[1] = part.positions[position_idx + 1];
            dst.position[2] = part.positions[position_idx + 2];

            // Normals
            if (has_normals) {
                const int normal_idx = v * ozz::sample::Mesh::Part::kNormalsCpnts;
                dst.normal[0] = part.normals[normal_idx + 0];
                dst.normal[1] = part.normals[normal_idx + 1];
                dst.normal[2] = part.normals[normal_idx + 2];
            } else {
                dst.normal[0] = 0.0f;
                dst.normal[1] = 1.0f;
                dst.normal[2] = 0.0f;
            }

            // UVs
            if (has_uvs) {
                const int uv_idx = v * ozz::sample::Mesh::Part::kUVsCpnts;
                dst.uv[0] = part.uvs[uv_idx + 0];
                dst.uv[1] = part.uvs[uv_idx + 1];
            } else {
                dst.uv[0] = 0.0f;
                dst.uv[1] = 0.0f;
            }

            // Skinning influences
            std::fill(std::begin(dst.bone_weights), std::end(dst.bone_weights), 0.0f);
            std::fill(std::begin(dst.bone_indices), std::end(dst.bone_indices), 0u);

            influences_buffer.clear();

            if (has_weights) {
                influences_buffer.reserve(static_cast<size_t>(influences_per_vertex));

                float accum_weight = 0.0f;
                for (int i = 0; i < influences_per_vertex; ++i) {
                    const uint16_t raw_joint_index = part.joint_indices[v * influences_per_vertex + i];
                    uint32_t palette_index = 0;
                    max_requested_palette_index = std::max<size_t>(max_requested_palette_index, raw_joint_index);

                    if (palette_capacity > 0) {
                        if (raw_joint_index >= palette_capacity) {
                            palette_index = static_cast<uint32_t>(palette_capacity - 1);
                            if (!reported_invalid_joint_index) {
                                Msg("! Mesh joint index %u exceeds palette size %zu; clamping to %u",
                                    static_cast<unsigned>(raw_joint_index),
                                    palette_capacity,
                                    palette_index);
                                reported_invalid_joint_index = true;
                            }
                        } else {
                            palette_index = raw_joint_index;
                        }
                    } else {
                        palette_index = 0;
                    }

                    float weight = 0.0f;
                    if (i < influences_per_vertex - 1) {
                        weight = part.joint_weights[v * (influences_per_vertex - 1) + i];
                        accum_weight += weight;
                    } else {
                        weight = std::max(0.0f, 1.0f - accum_weight);
                    }

                    if (weight > 0.0f) {
                        influences_buffer.push_back({palette_index, weight});
                        max_palette_index = std::max<size_t>(max_palette_index, palette_index);
                    }
                }

                if (!influences_buffer.empty()) {
                    std::sort(influences_buffer.begin(), influences_buffer.end(),
                              [](const Influence& a, const Influence& b) {
                                  return a.weight > b.weight;
                              });

                    const size_t copy_count = std::min<size_t>(influences_buffer.size(), 4);
                    float total_weight = 0.0f;

                    for (size_t i = 0; i < copy_count; ++i) {
                        dst.bone_indices[i] = influences_buffer[i].palette;
                        dst.bone_weights[i] = influences_buffer[i].weight;
                        total_weight += influences_buffer[i].weight;
                    }

                    if (total_weight > 0.0f) {
                        const float inv_total = 1.0f / total_weight;
                        for (size_t i = 0; i < copy_count; ++i) {
                            dst.bone_weights[i] *= inv_total;
                        }
                    } else {
                        dst.bone_weights[0] = 1.0f;
                    }
                } else {
                    dst.bone_weights[0] = 1.0f;
                }
            } else {
                dst.bone_weights[0] = 1.0f;
            }
        }
    }

    if (palette_capacity > 0 && max_requested_palette_index >= palette_capacity) {
        Msg("! Mesh requested palette index %zu but capacity is %zu", max_requested_palette_index, palette_capacity);
    }

    if (vertex_cursor != vertices.size()) {
        Msg("! Vertex conversion mismatch (expected %zu, processed %zu)", vertices.size(), vertex_cursor);
        return false;
    }

    vertex_count_ = static_cast<uint32_t>(vertices.size());
    if (!EnsureVertexBuffer(vertices.size())) {
        return false;
    }
    vertex_buffer_.Upload(vertices.data(), sizeof(Vertex) * vertices.size());

    index_count_ = static_cast<uint32_t>(total_indices);
    if (!EnsureIndexBuffer(total_indices)) {
        return false;
    }

    // Convert uint16_t indices to uint32_t for VK_INDEX_TYPE_UINT32
    std::vector<uint32_t> indices_uint32(mesh.triangle_indices.begin(), mesh.triangle_indices.end());
    index_buffer_.Upload(indices_uint32.data(), sizeof(uint32_t) * indices_uint32.size());

    bones_per_instance_ = static_cast<uint32_t>(mesh.num_joints());
    debug_vertices_ = vertices;
    mesh_uploaded_ = true;
    return true;
}

bool InstancedMeshRenderer::EnsureClipDebugBuffer(size_t count) {
    if (!device_) {
        return false;
    }

    if (clip_debug_buffer_.GetBuffer() != VK_NULL_HANDLE && clip_debug_capacity_ >= count) {
        return true;
    }

    clip_debug_buffer_.Destroy();
    clip_debug_capacity_ = static_cast<uint32_t>(count);

    if (clip_debug_capacity_ == 0) {
        UpdateDescriptorSet();
        return true;
    }

    const VkDeviceSize size_bytes = sizeof(ClipDebug) * clip_debug_capacity_;
    clip_debug_buffer_.Create(device_->GetDevice(), device_->GetAllocator(), size_bytes,
        VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, VMA_MEMORY_USAGE_GPU_TO_CPU);

    if (clip_debug_buffer_.GetBuffer() == VK_NULL_HANDLE) {
        clip_debug_capacity_ = 0;
        Msg("! Failed to create clip debug buffer");
        UpdateDescriptorSet();
        return false;
    }

    std::vector<ClipDebug> zeros(clip_debug_capacity_);
    clip_debug_buffer_.Upload(zeros.data(), size_bytes);
    UpdateDescriptorSet();
    return true;
}

void InstancedMeshRenderer::DumpClipDebugBuffer() {
    if (clip_debug_buffer_.GetBuffer() == VK_NULL_HANDLE || clip_debug_capacity_ == 0) {
        return;
    }

    ClipDebug* mapped = static_cast<ClipDebug*>(clip_debug_buffer_.Map());
    if (!mapped) {
        return;
    }

    static int dump_counter = 0;
    const uint32_t capture_count = std::min<uint32_t>(clip_debug_capacity_, 4u);
    bool any_output = false;
    for (uint32_t i = 0; i < capture_count; ++i) {
        clip_debug_cpu_[i] = mapped[i];
        const float w = clip_debug_cpu_[i].clip_w;
        const float z = clip_debug_cpu_[i].clip_z;
        if (dump_counter < 5 && (std::fabs(w) > 1e-6f || std::fabs(z) > 1e-6f)) {
            Msg("[ClipDebugSSBO] v%u clip=(%.6f, %.6f, %.6f, %.6f)", i,
                clip_debug_cpu_[i].clip_x,
                clip_debug_cpu_[i].clip_y,
                clip_debug_cpu_[i].clip_z,
                clip_debug_cpu_[i].clip_w);
            any_output = true;
        }
        mapped[i].clip_x = mapped[i].clip_y = mapped[i].clip_z = mapped[i].clip_w = 0.0f;
    }

    clip_debug_buffer_.Unmap();

    if (dump_counter < 5 && !any_output) {
        Msg("[ClipDebugSSBO] no clip outputs captured (all zeros)");
    }
    ++dump_counter;
}

void InstancedMeshRenderer::Render(VkCommandBuffer cmd,
                                   const ozz::math::Float4x4& view_proj,
                                   const std::vector<MeshInstanceData>& instances,
                                   const std::vector<ozz::math::Float4x4>& bone_matrices) {
    if (!initialized_ || !mesh_uploaded_) {
        return;
    }

    if (index_count_ == 0 || vertex_count_ == 0) {
        return;
    }

    if (!UpdateInstanceBufferData(instances)) {
        return;
    }

    const size_t required_bones = static_cast<size_t>(bones_per_instance_) * instances.size();
    if (bones_per_instance_ > 0 && bone_matrices.size() < required_bones) {
        Msg("! Bone matrix buffer smaller than expected (%zu < %zu)", bone_matrices.size(), required_bones);
        return;
    }

    if (!UpdateBoneBufferData(bone_matrices)) {
        return;
    }

    EnsureClipDebugBuffer(4);
    EnsureFragDebugBuffer(4);  // Ensure fragment debug buffer exists

    // Set debug mode in fragment SSBO
    if (frag_debug_buffer_.GetBuffer() != VK_NULL_HANDLE) {
        FragDebugEntry* mapped = static_cast<FragDebugEntry*>(frag_debug_buffer_.Map());
        if (mapped) {
            mapped[0].data[0] = static_cast<float>(debug_mode_);
            frag_debug_buffer_.Unmap();
        }
    }

    device_->WaitIdle();
    DumpClipDebugBuffer();

    UpdateUniforms(view_proj);

    static int debug_frames = 0;
    if (debug_frames < 3 && !debug_vertices_.empty() && !instances.empty() && !bone_matrices.empty()) {
        const MeshInstanceData& instance = instances.front();
        const uint32_t base_offset = instance.bone_matrix_offset;

        auto skin_vertex = [&](const Vertex& vert) -> ozz::math::SimdFloat4 {
            const float local_raw[4] = {vert.position[0], vert.position[1], vert.position[2], 1.0f};
            const ozz::math::SimdFloat4 local = ozz::math::simd_float4::LoadPtrU(local_raw);
            const float weights[4] = {vert.bone_weights[0], vert.bone_weights[1], vert.bone_weights[2], vert.bone_weights[3]};
            const uint32_t indices[4] = {vert.bone_indices[0], vert.bone_indices[1], vert.bone_indices[2], vert.bone_indices[3]};
            const float total = weights[0] + weights[1] + weights[2] + weights[3];
            if (total <= 0.0f) {
                return local;
            }
            float accum[4] = {0.0f, 0.0f, 0.0f, 0.0f};
            for (int i = 0; i < 4; ++i) {
                const float weight = weights[i];
                if (weight <= 0.0f) {
                    continue;
                }
                const size_t palette_index = static_cast<size_t>(base_offset) + static_cast<size_t>(indices[i]);
                if (palette_index >= bone_matrices.size()) {
                    continue;
                }
                const ozz::math::Float4x4& bone = bone_matrices[palette_index];
                const ozz::math::SimdFloat4 transformed = ozz::math::TransformPoint(bone, local);
                accum[0] += ozz::math::GetX(transformed) * weight;
                accum[1] += ozz::math::GetY(transformed) * weight;
                accum[2] += ozz::math::GetZ(transformed) * weight;
                accum[3] += ozz::math::GetW(transformed) * weight;
            }
            return ozz::math::simd_float4::LoadPtrU(accum);
        };

        const Vertex& v = debug_vertices_.front();
        const ozz::math::SimdFloat4 skinned = skin_vertex(v);
        const ozz::math::SimdFloat4 world = ozz::math::TransformPoint(instance.transform, skinned);
        const ozz::math::SimdFloat4 clip = ozz::math::TransformPoint(view_proj, world);

        const float clip_x = ozz::math::GetX(clip);
        const float clip_y = ozz::math::GetY(clip);
        const float clip_z = ozz::math::GetZ(clip);
        const float clip_w = ozz::math::GetW(clip);
        const float ndc_z = clip_z / clip_w;
        const float depth = 0.5f * ndc_z + 0.5f;

        const float world_x = ozz::math::GetX(world);
        const float world_y = ozz::math::GetY(world);
        const float world_z = ozz::math::GetZ(world);

        Msg("[DepthDebug] v0 world=(%.3f, %.3f, %.3f) clip=(%.3f, %.3f, %.3f, %.3f) ndc_z=%.6f depth=%.6f",
            world_x, world_y, world_z, clip_x, clip_y, clip_z, clip_w, ndc_z, depth);

        if (debug_frames == 0) {
            float min_depth = std::numeric_limits<float>::max();
            float max_depth = std::numeric_limits<float>::lowest();
            float min_ndc = std::numeric_limits<float>::max();
            float max_ndc = std::numeric_limits<float>::lowest();
            size_t sample_count = 0;
            for (const Vertex& vert : debug_vertices_) {
                const ozz::math::SimdFloat4 skinned_vert = skin_vertex(vert);
                const ozz::math::SimdFloat4 world_vert = ozz::math::TransformPoint(instance.transform, skinned_vert);
                const ozz::math::SimdFloat4 clip_vert = ozz::math::TransformPoint(view_proj, world_vert);
                const float w = ozz::math::GetW(clip_vert);
                if (std::abs(w) < 1e-6f) {
                    continue;
                }
                const float ndc = ozz::math::GetZ(clip_vert) / w;
                const float depth_val = ndc * 0.5f + 0.5f;
                min_depth = std::min(min_depth, depth_val);
                max_depth = std::max(max_depth, depth_val);
                min_ndc = std::min(min_ndc, ndc);
                max_ndc = std::max(max_ndc, ndc);
                ++sample_count;
            }
            Msg("[DepthDebug] sampled %zu vertices: depth range [%.6f, %.6f], ndc_z range [%.6f, %.6f]",
                sample_count, min_depth, max_depth, min_ndc, max_ndc);
        }

        ++debug_frames;
    }

    pipeline_.Bind(cmd);

    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline_.GetLayout(), 0, 1, &descriptor_set_, 0, nullptr);

    VkBuffer vertex_buffers[] = {vertex_buffer_.GetBuffer(), instance_buffer_.GetBuffer()};
    VkDeviceSize offsets[] = {0, 0};
    vkCmdBindVertexBuffers(cmd, 0, 2, vertex_buffers, offsets);
    vkCmdBindIndexBuffer(cmd, index_buffer_.GetBuffer(), 0, VK_INDEX_TYPE_UINT32);

    vkCmdDrawIndexed(cmd, index_count_, instance_count_, 0, 0, 0);
}

bool InstancedMeshRenderer::EnsureFragDebugBuffer(size_t count) {
    if (!device_) {
        return false;
    }

    if (frag_debug_buffer_.GetBuffer() != VK_NULL_HANDLE && frag_debug_capacity_ >= count) {
        return true;
    }

    frag_debug_buffer_.Destroy();
    frag_debug_capacity_ = static_cast<uint32_t>(count);

    if (frag_debug_capacity_ == 0) {
        UpdateDescriptorSet();
        return true;
    }

    const VkDeviceSize size_bytes = sizeof(FragDebugEntry) * frag_debug_capacity_;
    frag_debug_buffer_.Create(device_->GetDevice(), device_->GetAllocator(), size_bytes,
        VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, VMA_MEMORY_USAGE_GPU_TO_CPU);

    if (frag_debug_buffer_.GetBuffer() == VK_NULL_HANDLE) {
        frag_debug_capacity_ = 0;
        Msg("! Failed to create fragment debug buffer");
        UpdateDescriptorSet();
        return false;
    }

    std::vector<FragDebugEntry> zeros(frag_debug_capacity_);
    frag_debug_buffer_.Upload(zeros.data(), size_bytes);
    UpdateDescriptorSet();
    return true;
}

void InstancedMeshRenderer::ResetFragDebugBuffer(uint32_t count) {
    if (frag_debug_buffer_.GetBuffer() == VK_NULL_HANDLE || frag_debug_capacity_ == 0) {
        return;
    }

    FragDebugEntry* mapped = static_cast<FragDebugEntry*>(frag_debug_buffer_.Map());
    if (!mapped) {
        return;
    }

    const uint32_t limit = std::min<uint32_t>(frag_debug_capacity_, count);
    for (uint32_t i = 0; i < limit; ++i) {
        mapped[i].recorded = 0u;
        std::fill(std::begin(mapped[i].data), std::end(mapped[i].data), 0.0f);
    }

    frag_debug_buffer_.Unmap();
}

void InstancedMeshRenderer::DumpFragDebugBuffer() {
    if (frag_debug_buffer_.GetBuffer() == VK_NULL_HANDLE || frag_debug_capacity_ == 0) {
        return;
    }

    FragDebugEntry* mapped = static_cast<FragDebugEntry*>(frag_debug_buffer_.Map());
    if (!mapped) {
        return;
    }

    static int dump_counter = 0;
    const uint32_t capture_count = std::min<uint32_t>(frag_debug_capacity_, 4u);
    bool any_output = false;
    for (uint32_t i = 0; i < capture_count; ++i) {
        frag_debug_cpu_[i] = mapped[i];
        if (dump_counter < 5 && frag_debug_cpu_[i].recorded != 0u) {
            Msg("[FragDebugSSBO] entry %u data[0]=%.6f data[1]=%.6f data[2]=%.6f data[3]=%.6f",
                i,
                frag_debug_cpu_[i].data[0],
                frag_debug_cpu_[i].data[1],
                frag_debug_cpu_[i].data[2],
                frag_debug_cpu_[i].data[3]);
            any_output = true;
        }
        mapped[i].recorded = 0u;
        std::fill(std::begin(mapped[i].data), std::end(mapped[i].data), 0.0f);
    }

    frag_debug_buffer_.Unmap();

    if (dump_counter < 5 && !any_output) {
        Msg("[FragDebugSSBO] no fragment depth captured");
    }
    ++dump_counter;
}

bool InstancedMeshRenderer::CreateDescriptorSetLayout() {
    VkDescriptorSetLayoutBinding camera_binding = {};
    camera_binding.binding = 0;
    camera_binding.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    camera_binding.descriptorCount = 1;
    camera_binding.stageFlags = VK_SHADER_STAGE_VERTEX_BIT;

    VkDescriptorSetLayoutBinding bones_binding = {};
    bones_binding.binding = 1;
    bones_binding.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    bones_binding.descriptorCount = 1;
    bones_binding.stageFlags = VK_SHADER_STAGE_VERTEX_BIT;

    VkDescriptorSetLayoutBinding clip_binding = {};
    clip_binding.binding = 2;
    clip_binding.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    clip_binding.descriptorCount = 1;
    clip_binding.stageFlags = VK_SHADER_STAGE_VERTEX_BIT;

    VkDescriptorSetLayoutBinding frag_debug_binding = {};
    frag_debug_binding.binding = 3;
    frag_debug_binding.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;  // Changed to SSBO
    frag_debug_binding.descriptorCount = 1;
    frag_debug_binding.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;

    std::array<VkDescriptorSetLayoutBinding, 4> bindings = {camera_binding, bones_binding, clip_binding, frag_debug_binding};

    VkDescriptorSetLayoutCreateInfo layout_info = {};
    layout_info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    layout_info.bindingCount = static_cast<uint32_t>(bindings.size());
    layout_info.pBindings = bindings.data();

    VkResult result = vkCreateDescriptorSetLayout(device_->GetDevice(), &layout_info, nullptr, &descriptor_set_layout_);
    if (result != VK_SUCCESS) {
        Msg("! Failed to create mesh descriptor set layout (error: %d)", result);
        descriptor_set_layout_ = VK_NULL_HANDLE;
        return false;
    }

    return true;
}

bool InstancedMeshRenderer::CreatePipeline() {
    PipelineConfig config;

#ifdef OZZ_SHADER_BINARY_DIR
    const std::filesystem::path shader_root{OZZ_SHADER_BINARY_DIR};
    config.vertex_shader_path = (shader_root / "skinned_mesh_instanced.vert.spv").string();
    config.fragment_shader_path = (shader_root / "skinned_mesh_instanced.frag.spv").string();
#else
    config.vertex_shader_path = "src/xrAnimation/tools/shaders/skinned_mesh_instanced.vert.spv";
    config.fragment_shader_path = "src/xrAnimation/tools/shaders/skinned_mesh_instanced.frag.spv";
#endif

    VkVertexInputBindingDescription vertex_binding = {};
    vertex_binding.binding = 0;
    vertex_binding.stride = sizeof(Vertex);
    vertex_binding.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;

    VkVertexInputBindingDescription instance_binding = {};
    instance_binding.binding = 1;
    instance_binding.stride = sizeof(InstanceGpuData);
    instance_binding.inputRate = VK_VERTEX_INPUT_RATE_INSTANCE;

    config.vertex_bindings = {vertex_binding, instance_binding};

    VkVertexInputAttributeDescription position_attr = {};
    position_attr.binding = 0;
    position_attr.location = 0;
    position_attr.format = VK_FORMAT_R32G32B32_SFLOAT;
    position_attr.offset = offsetof(Vertex, position);

    VkVertexInputAttributeDescription normal_attr = {};
    normal_attr.binding = 0;
    normal_attr.location = 1;
    normal_attr.format = VK_FORMAT_R32G32B32_SFLOAT;
    normal_attr.offset = offsetof(Vertex, normal);

    VkVertexInputAttributeDescription uv_attr = {};
    uv_attr.binding = 0;
    uv_attr.location = 2;
    uv_attr.format = VK_FORMAT_R32G32_SFLOAT;
    uv_attr.offset = offsetof(Vertex, uv);

    VkVertexInputAttributeDescription weight_attr = {};
    weight_attr.binding = 0;
    weight_attr.location = 3;
    weight_attr.format = VK_FORMAT_R32G32B32A32_SFLOAT;
    weight_attr.offset = offsetof(Vertex, bone_weights);

    VkVertexInputAttributeDescription index_attr = {};
    index_attr.binding = 0;
    index_attr.location = 4;
    index_attr.format = VK_FORMAT_R32G32B32A32_UINT;
    index_attr.offset = offsetof(Vertex, bone_indices);

    VkVertexInputAttributeDescription instance_attr0 = {};
    instance_attr0.binding = 1;
    instance_attr0.location = 5;
    instance_attr0.format = VK_FORMAT_R32G32B32A32_SFLOAT;
    instance_attr0.offset = 0;

    VkVertexInputAttributeDescription instance_attr1 = instance_attr0;
    instance_attr1.location = 6;
    instance_attr1.offset = sizeof(float) * 4;

    VkVertexInputAttributeDescription instance_attr2 = instance_attr0;
    instance_attr2.location = 7;
    instance_attr2.offset = sizeof(float) * 8;

    VkVertexInputAttributeDescription instance_attr3 = instance_attr0;
    instance_attr3.location = 8;
    instance_attr3.offset = sizeof(float) * 12;

    VkVertexInputAttributeDescription instance_attr4 = {};
    instance_attr4.binding = 1;
    instance_attr4.location = 9;
    instance_attr4.format = VK_FORMAT_R32_UINT;
    instance_attr4.offset = sizeof(float) * 16;

    config.vertex_attributes = {
        position_attr,
        normal_attr,
        uv_attr,
        weight_attr,
        index_attr,
        instance_attr0,
        instance_attr1,
        instance_attr2,
        instance_attr3,
        instance_attr4
    };

    config.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
    config.cull_mode = VK_CULL_MODE_BACK_BIT;  // Enable backface culling
    config.front_face = VK_FRONT_FACE_COUNTER_CLOCKWISE;  // Coordinate reflection reverses winding
    config.depth_test_enable = true;   // ENABLE depth testing
    config.depth_write_enable = true;  // ENABLE depth writes
    config.depth_compare_op = VK_COMPARE_OP_LESS;  // Must use LESS with inverted projection matrix
    config.blend_enable = false;
    config.render_pass = device_->GetRenderPass();
    config.subpass = 0;
    config.descriptor_set_layouts = {descriptor_set_layout_};

    Msg("* InstancedMeshRenderer pipeline config: depth_test=%d, depth_write=%d, depth_compare=%d",
        config.depth_test_enable, config.depth_write_enable, config.depth_compare_op);

    if (!pipeline_.Create(device_->GetDevice(), config)) {
        return false;
    }

    return true;
}

bool InstancedMeshRenderer::CreateDescriptorPool() {
    VkDescriptorPoolSize pool_sizes[2] = {};
    pool_sizes[0].type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    pool_sizes[0].descriptorCount = 1;  // camera only
    pool_sizes[1].type = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    pool_sizes[1].descriptorCount = 3;  // bones + clip_debug + frag_debug

    VkDescriptorPoolCreateInfo pool_info = {};
    pool_info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    pool_info.poolSizeCount = 2;
    pool_info.pPoolSizes = pool_sizes;
    pool_info.maxSets = 1;

    VkResult result = vkCreateDescriptorPool(device_->GetDevice(), &pool_info, nullptr, &descriptor_pool_);
    if (result != VK_SUCCESS) {
        Msg("! Failed to create mesh descriptor pool (error: %d)", result);
        descriptor_pool_ = VK_NULL_HANDLE;
        return false;
    }

    VkDescriptorSetAllocateInfo alloc_info = {};
    alloc_info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    alloc_info.descriptorPool = descriptor_pool_;
    alloc_info.descriptorSetCount = 1;
    alloc_info.pSetLayouts = &descriptor_set_layout_;

    result = vkAllocateDescriptorSets(device_->GetDevice(), &alloc_info, &descriptor_set_);
    if (result != VK_SUCCESS) {
        Msg("! Failed to allocate mesh descriptor set (error: %d)", result);
        descriptor_set_ = VK_NULL_HANDLE;
        return false;
    }

    UpdateDescriptorSet();
    return true;
}

bool InstancedMeshRenderer::CreateUniformBuffer() {
    const VkDeviceSize uniform_size = sizeof(CameraUBO);
    uniform_buffer_.Create(device_->GetDevice(), device_->GetAllocator(), uniform_size,
        VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT, VMA_MEMORY_USAGE_CPU_TO_GPU);

    CameraUBO identity{};
    for (int i = 0; i < 16; ++i) {
        identity.view_proj[i] = (i % 5 == 0) ? 1.0f : 0.0f;
    }

    uniform_buffer_.Upload(&identity, sizeof(CameraUBO));

    const VkDeviceSize debug_size = sizeof(DebugSettings);
    debug_uniform_buffer_.Create(device_->GetDevice(), device_->GetAllocator(), debug_size,
        VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT, VMA_MEMORY_USAGE_CPU_TO_GPU);

    debug_uniform_buffer_.Upload(&debug_settings_, sizeof(DebugSettings));

    return uniform_buffer_.GetBuffer() != VK_NULL_HANDLE && debug_uniform_buffer_.GetBuffer() != VK_NULL_HANDLE;
}

bool InstancedMeshRenderer::EnsureVertexBuffer(size_t vertex_count) {
    const size_t count = std::max<size_t>(vertex_count, 1);
    const VkDeviceSize size_bytes = sizeof(Vertex) * count;
    vertex_buffer_.Create(device_->GetDevice(), device_->GetAllocator(), size_bytes,
        VK_BUFFER_USAGE_VERTEX_BUFFER_BIT, VMA_MEMORY_USAGE_CPU_TO_GPU);
    return vertex_buffer_.GetBuffer() != VK_NULL_HANDLE;
}

bool InstancedMeshRenderer::EnsureIndexBuffer(size_t index_count) {
    const size_t count = std::max<size_t>(index_count, 1);
    const VkDeviceSize size_bytes = sizeof(uint32_t) * count;
    index_buffer_.Create(device_->GetDevice(), device_->GetAllocator(), size_bytes,
        VK_BUFFER_USAGE_INDEX_BUFFER_BIT, VMA_MEMORY_USAGE_CPU_TO_GPU);
    return index_buffer_.GetBuffer() != VK_NULL_HANDLE;
}

bool InstancedMeshRenderer::EnsureInstanceBuffer(size_t instance_count) {
    const size_t required = std::max<size_t>(instance_count, 1);
    if (instance_buffer_.GetBuffer() != VK_NULL_HANDLE && required <= instance_capacity_) {
        return true;
    }

    instance_capacity_ = static_cast<uint32_t>(required);
    const VkDeviceSize size_bytes = sizeof(InstanceGpuData) * instance_capacity_;
    instance_buffer_.Create(device_->GetDevice(), device_->GetAllocator(), size_bytes,
        VK_BUFFER_USAGE_VERTEX_BUFFER_BIT, VMA_MEMORY_USAGE_CPU_TO_GPU);
    return instance_buffer_.GetBuffer() != VK_NULL_HANDLE;
}

bool InstancedMeshRenderer::EnsureBoneBuffer(size_t matrix_count) {
    const size_t required = std::max<size_t>(matrix_count, 1);
    if (bone_matrix_buffer_.GetBuffer() != VK_NULL_HANDLE && required <= bone_matrix_capacity_) {
        return true;
    }

    bone_matrix_capacity_ = static_cast<uint32_t>(required);
    const VkDeviceSize size_bytes = sizeof(float) * 16 * bone_matrix_capacity_;
    bone_matrix_buffer_.Create(device_->GetDevice(), device_->GetAllocator(), size_bytes,
        VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, VMA_MEMORY_USAGE_CPU_TO_GPU);

    if (descriptor_set_ != VK_NULL_HANDLE) {
        UpdateDescriptorSet();
    }

    return bone_matrix_buffer_.GetBuffer() != VK_NULL_HANDLE;
}

void InstancedMeshRenderer::UpdateUniforms(const ozz::math::Float4x4& view_proj) {
    CameraUBO ubo{};
    StoreMatrix(view_proj, ubo.view_proj);

    // DEBUG: Print view_proj matrix AND what's in the UBO buffer
    static int frame_count = 0;
    if (frame_count++ < 3) {  // Only print first 3 frames
        Msg("=== View-Proj Matrix (rows) ===");
        Msg("  [%.3f, %.3f, %.3f, %.3f]",
            ozz::math::GetX(view_proj.cols[0]),
            ozz::math::GetX(view_proj.cols[1]),
            ozz::math::GetX(view_proj.cols[2]),
            ozz::math::GetX(view_proj.cols[3]));
        Msg("  [%.3f, %.3f, %.3f, %.3f]",
            ozz::math::GetY(view_proj.cols[0]),
            ozz::math::GetY(view_proj.cols[1]),
            ozz::math::GetY(view_proj.cols[2]),
            ozz::math::GetY(view_proj.cols[3]));
        Msg("  [%.3f, %.3f, %.3f, %.3f]",
            ozz::math::GetZ(view_proj.cols[0]),
            ozz::math::GetZ(view_proj.cols[1]),
            ozz::math::GetZ(view_proj.cols[2]),
            ozz::math::GetZ(view_proj.cols[3]));
        Msg("  [%.3f, %.3f, %.3f, %.3f]",
            ozz::math::GetW(view_proj.cols[0]),
            ozz::math::GetW(view_proj.cols[1]),
            ozz::math::GetW(view_proj.cols[2]),
            ozz::math::GetW(view_proj.cols[3]));

        Msg("=== Camera UBO Upload (16 floats as stored) ===");
        for (int i = 0; i < 16; i += 4) {
            Msg("  [%.3f, %.3f, %.3f, %.3f]",
                ubo.view_proj[i], ubo.view_proj[i+1], ubo.view_proj[i+2], ubo.view_proj[i+3]);
        }
    }

    uniform_buffer_.Upload(&ubo, sizeof(CameraUBO));
}

bool InstancedMeshRenderer::UpdateInstanceBufferData(const std::vector<MeshInstanceData>& instances) {
    if (!EnsureInstanceBuffer(instances.size())) {
        return false;
    }

    instance_count_ = static_cast<uint32_t>(instances.size());

    if (instances.empty()) {
        return true;
    }

    std::vector<InstanceGpuData> gpu_data(instance_count_);

    for (size_t i = 0; i < instances.size(); ++i) {
        InstanceGpuData& dst = gpu_data[i];
        StoreMatrix(instances[i].transform, dst.transform);
        dst.bone_matrix_offset = instances[i].bone_matrix_offset;
        dst.padding[0] = dst.padding[1] = dst.padding[2] = 0.0f;

        // DEBUG: Print first instance transform
        static int inst_frame = 0;
        if (i == 0 && inst_frame++ < 3) {
            Msg("=== Instance Transform 0 (rows) ===");
            const auto& m = instances[i].transform;
            Msg("  [%.3f, %.3f, %.3f, %.3f]",
                ozz::math::GetX(m.cols[0]), ozz::math::GetX(m.cols[1]),
                ozz::math::GetX(m.cols[2]), ozz::math::GetX(m.cols[3]));
            Msg("  [%.3f, %.3f, %.3f, %.3f]",
                ozz::math::GetY(m.cols[0]), ozz::math::GetY(m.cols[1]),
                ozz::math::GetY(m.cols[2]), ozz::math::GetY(m.cols[3]));
            Msg("  [%.3f, %.3f, %.3f, %.3f]",
                ozz::math::GetZ(m.cols[0]), ozz::math::GetZ(m.cols[1]),
                ozz::math::GetZ(m.cols[2]), ozz::math::GetZ(m.cols[3]));
            Msg("  [%.3f, %.3f, %.3f, %.3f]",
                ozz::math::GetW(m.cols[0]), ozz::math::GetW(m.cols[1]),
                ozz::math::GetW(m.cols[2]), ozz::math::GetW(m.cols[3]));
            Msg("  bone_matrix_offset = %u", instances[i].bone_matrix_offset);
        }
    }

    instance_buffer_.Upload(gpu_data.data(), sizeof(InstanceGpuData) * gpu_data.size());
    return true;
}

bool InstancedMeshRenderer::UpdateBoneBufferData(const std::vector<ozz::math::Float4x4>& bone_matrices) {
    const size_t matrix_count = bone_matrices.size();
    if (!EnsureBoneBuffer(matrix_count)) {
        return false;
    }

    if (matrix_count == 0) {
        return true;
    }

    std::vector<float> flattened(matrix_count * 16);
    for (size_t i = 0; i < matrix_count; ++i) {
        StoreMatrix(bone_matrices[i], flattened.data() + i * 16);

        // DEBUG: Print first 3 bone matrices
        static int bone_frame = 0;
        if (i < 3 && bone_frame < 3) {
            Msg("=== Bone Matrix %zu (rows) ===", i);
            const auto& m = bone_matrices[i];
            Msg("  [%.3f, %.3f, %.3f, %.3f]",
                ozz::math::GetX(m.cols[0]), ozz::math::GetX(m.cols[1]),
                ozz::math::GetX(m.cols[2]), ozz::math::GetX(m.cols[3]));
            Msg("  [%.3f, %.3f, %.3f, %.3f]",
                ozz::math::GetY(m.cols[0]), ozz::math::GetY(m.cols[1]),
                ozz::math::GetY(m.cols[2]), ozz::math::GetY(m.cols[3]));
            Msg("  [%.3f, %.3f, %.3f, %.3f]",
                ozz::math::GetZ(m.cols[0]), ozz::math::GetZ(m.cols[1]),
                ozz::math::GetZ(m.cols[2]), ozz::math::GetZ(m.cols[3]));
            Msg("  [%.3f, %.3f, %.3f, %.3f]",
                ozz::math::GetW(m.cols[0]), ozz::math::GetW(m.cols[1]),
                ozz::math::GetW(m.cols[2]), ozz::math::GetW(m.cols[3]));
            if (i == 2) bone_frame++;
        }
    }

    bone_matrix_buffer_.Upload(flattened.data(), sizeof(float) * flattened.size());
    return true;
}

void InstancedMeshRenderer::UpdateDescriptorSet() {
    if (descriptor_set_ == VK_NULL_HANDLE) {
        return;
    }

    VkDescriptorBufferInfo camera_info = {};
    camera_info.buffer = uniform_buffer_.GetBuffer();
    camera_info.offset = 0;
    camera_info.range = sizeof(CameraUBO);

    VkDescriptorBufferInfo bone_info = {};
    bone_info.buffer = bone_matrix_buffer_.GetBuffer();
    bone_info.offset = 0;
    bone_info.range = VK_WHOLE_SIZE;

    VkDescriptorBufferInfo clip_info = {};
    clip_info.buffer = clip_debug_buffer_.GetBuffer();
    clip_info.offset = 0;
    clip_info.range = VK_WHOLE_SIZE;

    VkDescriptorBufferInfo frag_debug_info = {};
    frag_debug_info.buffer = frag_debug_buffer_.GetBuffer();
    frag_debug_info.offset = 0;
    frag_debug_info.range = VK_WHOLE_SIZE;

    std::vector<VkWriteDescriptorSet> writes;

    // Always write camera and bones
    VkWriteDescriptorSet camera_write = {};
    camera_write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    camera_write.dstSet = descriptor_set_;
    camera_write.dstBinding = 0;
    camera_write.descriptorCount = 1;
    camera_write.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    camera_write.pBufferInfo = &camera_info;
    writes.push_back(camera_write);

    VkWriteDescriptorSet bones_write = {};
    bones_write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    bones_write.dstSet = descriptor_set_;
    bones_write.dstBinding = 1;
    bones_write.descriptorCount = 1;
    bones_write.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    bones_write.pBufferInfo = &bone_info;
    writes.push_back(bones_write);

    // Optionally write clip debug buffer
    if (clip_debug_buffer_.GetBuffer() != VK_NULL_HANDLE) {
        VkWriteDescriptorSet clip_write = {};
        clip_write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        clip_write.dstSet = descriptor_set_;
        clip_write.dstBinding = 2;
        clip_write.descriptorCount = 1;
        clip_write.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        clip_write.pBufferInfo = &clip_info;
        writes.push_back(clip_write);
    }

    // Optionally write fragment debug SSBO
    if (frag_debug_buffer_.GetBuffer() != VK_NULL_HANDLE) {
        VkWriteDescriptorSet frag_debug_write = {};
        frag_debug_write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        frag_debug_write.dstSet = descriptor_set_;
        frag_debug_write.dstBinding = 3;
        frag_debug_write.descriptorCount = 1;
        frag_debug_write.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        frag_debug_write.pBufferInfo = &frag_debug_info;
        writes.push_back(frag_debug_write);
    }

    if (!writes.empty()) {
        vkUpdateDescriptorSets(device_->GetDevice(), static_cast<uint32_t>(writes.size()), writes.data(), 0, nullptr);
    }
}

void InstancedMeshRenderer::SetDebugMode(uint32_t mode) {
    debug_mode_ = mode;
}

const char* InstancedMeshRenderer::GetDebugModeName() const {
    static const char* mode_names[] = {
        "Normal Lighting",
        "Face Orientation (gl_FrontFacing)",
        "Normal Direction Visualization",
        "Depth Visualization",
        "UV Coordinates",
        "Normal Z Component",
        "Checkerboard Front/Back",
        "Two-Sided Visualization",
        "World Position Gradient"
    };

    if (debug_mode_ < kNumDebugModes) {
        return mode_names[debug_mode_];
    }
    return "Unknown";
}

} // namespace renderer
} // namespace animation
} // namespace xray
