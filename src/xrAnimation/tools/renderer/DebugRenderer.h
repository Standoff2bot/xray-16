#pragma once

#include "Common/Common.hpp"

#include "VulkanBuffer.h"
#include "VulkanPipeline.h"

#include <vulkan/vulkan.h>

#include "ozz/base/maths/vec_float.h"
#include "ozz/base/maths/simd_math.h"
#include "xrCommon/xr_vector.h"

namespace xray {
namespace animation {
namespace renderer {

class VulkanDevice;

// Immediate-mode debug line renderer.
class DebugRenderer {
public:
    DebugRenderer() = default;
    ~DebugRenderer() = default;

    bool Initialize(VulkanDevice* device);
    void Shutdown();

    void BeginFrame();
    void DrawLine(const ozz::math::Float3& start, const ozz::math::Float3& end, const ozz::math::Float4& color);
    void DrawAxes(const ozz::math::Float4x4& transform, float scale, const ozz::math::Float4& color_x,
        const ozz::math::Float4& color_y, const ozz::math::Float4& color_z);
    void DrawPoint(const ozz::math::Float3& position, float radius, const ozz::math::Float4& color, int segments = 8);
    void DrawSphere(const ozz::math::Float3& center, float radius, const ozz::math::Float4& color, int segments = 12);
    void EndFrame();
    void Render(VkCommandBuffer cmd, const ozz::math::Float4x4& view_proj);

private:
    struct LineVertex {
        float position[3];
        float color[4];
    };

    static VkDeviceSize VertexBufferSize(size_t vertex_count);
    bool EnsureVertexCapacity(size_t vertex_count);
    bool CreatePipeline();
    bool CreateDescriptorSetLayout();
    bool CreateDescriptorPool();
    bool CreateUniformBuffer();
    void UpdateUniforms(const ozz::math::Float4x4& view_proj);
    void UpdateDescriptorSet();

    VulkanDevice* device_ = nullptr;

    VulkanBuffer vertex_buffer_;
    VulkanBuffer uniform_buffer_;

    VulkanPipeline pipeline_;
    VkDescriptorSetLayout descriptor_set_layout_ = VK_NULL_HANDLE;
    VkDescriptorPool descriptor_pool_ = VK_NULL_HANDLE;
    VkDescriptorSet descriptor_set_ = VK_NULL_HANDLE;

    xr_vector<LineVertex> vertices_;
    size_t vertex_capacity_ = 0;
    bool buffers_dirty_ = false;
    bool initialized_ = false;
};

} // namespace renderer
} // namespace animation
} // namespace xray
