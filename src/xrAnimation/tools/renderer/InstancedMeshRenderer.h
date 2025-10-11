#pragma once

#include "VulkanBuffer.h"
#include "VulkanPipeline.h"
#include <cstdint>
#include <vector>
#include <vulkan/vulkan.h>
#include <ozz/base/maths/simd_math.h>

namespace ozz {
namespace math {
struct Float4x4;
} // namespace math
namespace sample {
struct Mesh;
} // namespace sample
} // namespace ozz

namespace xray {
namespace animation {
namespace renderer {

class VulkanDevice;

struct MeshInstanceData {
    ozz::math::Float4x4 transform;
    uint32_t bone_matrix_offset = 0;
};

// GPU skinned mesh renderer supporting instanced draws.
class InstancedMeshRenderer {
public:
    InstancedMeshRenderer() = default;
    ~InstancedMeshRenderer() = default;

    bool Initialize(VulkanDevice* device);
    void Shutdown();

    bool UploadMesh(const ozz::sample::Mesh& mesh);

    void Render(VkCommandBuffer cmd,
                const ozz::math::Float4x4& view_proj,
                const std::vector<MeshInstanceData>& instances,
                const std::vector<ozz::math::Float4x4>& bone_matrices);

    bool IsInitialized() const { return initialized_; }
    bool HasMesh() const { return mesh_uploaded_; }
    uint32_t BonesPerInstance() const { return bones_per_instance_; }

private:
    struct Vertex {
        float position[3];
        float normal[3];
        float uv[2];
        float bone_weights[4];
        uint32_t bone_indices[4];
    };

    struct InstanceGpuData {
        float transform[16];
        uint32_t bone_matrix_offset;
        float padding[3]; // Keep stride 16-byte aligned.
    };

    bool CreateDescriptorSetLayout();
    bool CreatePipeline();
    bool CreateDescriptorPool();
    bool CreateUniformBuffer();

    bool EnsureVertexBuffer(size_t vertex_count);
    bool EnsureIndexBuffer(size_t index_count);
    bool EnsureInstanceBuffer(size_t instance_count);
    bool EnsureBoneBuffer(size_t matrix_count);

    void UpdateUniforms(const ozz::math::Float4x4& view_proj);
    bool UpdateInstanceBufferData(const std::vector<MeshInstanceData>& instances);
    bool UpdateBoneBufferData(const std::vector<ozz::math::Float4x4>& bone_matrices);
    void UpdateDescriptorSet();

    // Vertex shader debug SSBO (gl_Position instrumentation)
    bool EnsureClipDebugBuffer(size_t count);
    void DumpClipDebugBuffer();

    // Fragment shader debug SSBO
    bool EnsureFragDebugBuffer(size_t count);
    void ResetFragDebugBuffer(uint32_t count);
    void DumpFragDebugBuffer();

    struct ClipDebug {
        float clip_x;
        float clip_y;
        float clip_z;
        float clip_w;
    };

    struct FragDebugEntry {
        uint32_t recorded;
        float sample[4];
    };

    VulkanDevice* device_ = nullptr;

    VulkanBuffer vertex_buffer_;
    VulkanBuffer index_buffer_;
    VulkanBuffer instance_buffer_;
    VulkanBuffer bone_matrix_buffer_;
    VulkanBuffer uniform_buffer_;

    VulkanPipeline pipeline_;

    VkDescriptorSetLayout descriptor_set_layout_ = VK_NULL_HANDLE;
    VkDescriptorPool descriptor_pool_ = VK_NULL_HANDLE;
    VkDescriptorSet descriptor_set_ = VK_NULL_HANDLE;

    uint32_t vertex_count_ = 0;
    uint32_t index_count_ = 0;
    uint32_t instance_capacity_ = 0;
    uint32_t instance_count_ = 0;
    uint32_t bone_matrix_capacity_ = 0;
    uint32_t bones_per_instance_ = 0;
    bool initialized_ = false;
    bool mesh_uploaded_ = false;

    // Vertex shader gl_Position debug SSBO
    VulkanBuffer clip_debug_buffer_;
    uint32_t clip_debug_capacity_ = 0;
    mutable ClipDebug clip_debug_cpu_[4];

    // Fragment shader debug SSBO
    VulkanBuffer frag_debug_buffer_;
    uint32_t frag_debug_capacity_ = 0;
    mutable FragDebugEntry frag_debug_cpu_[4];

    // Debug vertex data for CPU-side verification
    std::vector<Vertex> debug_vertices_;
};

} // namespace renderer
} // namespace animation
} // namespace xray
