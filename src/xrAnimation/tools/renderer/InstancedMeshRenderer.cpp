#include "InstancedMeshRenderer.h"

#include "VulkanDevice.h"
#include "framework/mesh.h"
#include <ozz/base/maths/simd_math.h>

#include <algorithm>
#include <array>
#include <vector>
#include <filesystem>
#include <limits>
#include <cstring>

#define Msg(...) printf(__VA_ARGS__), printf("\n")

namespace xray {
namespace animation {
namespace renderer {

namespace {

struct CameraUBO {
    float view_proj[16];
};

struct Influence {
    uint32_t joint = 0;
    float weight = 0.0f;
};

void StoreMatrix(const ozz::math::Float4x4& source, float* destination) {
    for (int c = 0; c < 4; ++c) {
        ozz::math::StorePtrU(source.cols[c], destination + c * 4);
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
        vertex_buffer_.Destroy();
        index_buffer_.Destroy();
        instance_buffer_.Destroy();
        bone_matrix_buffer_.Destroy();
        uniform_buffer_.Destroy();
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
    bone_matrix_buffer_.Destroy();
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
                    uint32_t joint_index = raw_joint_index;

                    if (!mesh.joint_remaps.empty() && raw_joint_index < mesh.joint_remaps.size()) {
                        joint_index = mesh.joint_remaps[raw_joint_index];
                    }

                    float weight = 0.0f;
                    if (i < influences_per_vertex - 1) {
                        weight = part.joint_weights[v * (influences_per_vertex - 1) + i];
                        accum_weight += weight;
                    } else {
                        weight = std::max(0.0f, 1.0f - accum_weight);
                    }

                    if (weight > 0.0f) {
                        influences_buffer.push_back({joint_index, weight});
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
                        dst.bone_indices[i] = influences_buffer[i].joint;
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
    index_buffer_.Upload(mesh.triangle_indices.data(), sizeof(uint16_t) * mesh.triangle_indices.size());

    bones_per_instance_ = static_cast<uint32_t>(mesh.num_joints());
    mesh_uploaded_ = true;
    return true;
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
    if (bones_per_instance_ > 0) {
        if (bone_matrices.size() < required_bones) {
            Msg("! Bone matrix buffer smaller than expected (%zu < %zu)", bone_matrices.size(), required_bones);
            return;
        }
    }

    if (!UpdateBoneBufferData(bone_matrices)) {
        return;
    }

    UpdateUniforms(view_proj);

    pipeline_.Bind(cmd);

    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline_.GetLayout(), 0, 1, &descriptor_set_, 0, nullptr);

    VkBuffer vertex_buffers[] = {vertex_buffer_.GetBuffer(), instance_buffer_.GetBuffer()};
    VkDeviceSize offsets[] = {0, 0};
    vkCmdBindVertexBuffers(cmd, 0, 2, vertex_buffers, offsets);
    vkCmdBindIndexBuffer(cmd, index_buffer_.GetBuffer(), 0, VK_INDEX_TYPE_UINT16);

    vkCmdDrawIndexed(cmd, index_count_, instance_count_, 0, 0, 0);
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

    std::array<VkDescriptorSetLayoutBinding, 2> bindings = {camera_binding, bones_binding};

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
    config.cull_mode = VK_CULL_MODE_NONE;
    config.front_face = VK_FRONT_FACE_COUNTER_CLOCKWISE;
    config.depth_test_enable = true;
    config.depth_write_enable = true;
    config.blend_enable = false;
    config.render_pass = device_->GetRenderPass();
    config.subpass = 0;
    config.descriptor_set_layouts = {descriptor_set_layout_};

    if (!pipeline_.Create(device_->GetDevice(), config)) {
        return false;
    }

    return true;
}

bool InstancedMeshRenderer::CreateDescriptorPool() {
    VkDescriptorPoolSize pool_sizes[2] = {};
    pool_sizes[0].type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    pool_sizes[0].descriptorCount = 1;
    pool_sizes[1].type = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    pool_sizes[1].descriptorCount = 1;

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
    return uniform_buffer_.GetBuffer() != VK_NULL_HANDLE;
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
    const VkDeviceSize size_bytes = sizeof(uint16_t) * count;
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

    std::array<VkWriteDescriptorSet, 2> writes = {};

    writes[0].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    writes[0].dstSet = descriptor_set_;
    writes[0].dstBinding = 0;
    writes[0].descriptorCount = 1;
    writes[0].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    writes[0].pBufferInfo = &camera_info;

    writes[1].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    writes[1].dstSet = descriptor_set_;
    writes[1].dstBinding = 1;
    writes[1].descriptorCount = 1;
    writes[1].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    writes[1].pBufferInfo = &bone_info;

    vkUpdateDescriptorSets(device_->GetDevice(), static_cast<uint32_t>(writes.size()), writes.data(), 0, nullptr);
}

} // namespace renderer
} // namespace animation
} // namespace xray
