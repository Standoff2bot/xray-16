#pragma once

#include "Common/Common.hpp"
#include "xrCore/xrCore.h"

#include "VulkanBuffer.h"
#include "VulkanPipeline.h"

#include <vulkan/vulkan.h>

#include "ozz/base/maths/vec_float.h"
#include "ozz/base/maths/simd_math.h"
#include "xrCommon/xr_vector.h"
#include "xrCore/_vector3d.h"

struct Fobb;
struct Fcylinder;
struct Fsphere;

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
    void DrawBoneShape(const ozz::math::Float3& head, const ozz::math::Float3& tail,
        float radius, const ozz::math::Float4& color);
    void DrawGrid(const ozz::math::Float3& center, float size, int divisions,
        const ozz::math::Float4& color_main, const ozz::math::Float4& color_sub);
    void EndFrame();
    void Render(VkCommandBuffer cmd, const ozz::math::Float4x4& view_proj);

private:
    struct LineVertex {
        float position[3];
        float color[4];
    };

    struct SolidVertex {
        float position[3];
        float normal[3];
        float color[4];
    };

    static VkDeviceSize VertexBufferSize(size_t vertex_count);
    bool EnsureLineCapacity(size_t vertex_count);
    bool EnsureSolidCapacity(size_t vertex_count);
    bool CreateLinePipeline();
    bool CreateSolidPipeline();
    bool CreateDescriptorSetLayout();
    bool CreateDescriptorPool();
    bool CreateUniformBuffer();
    void DrawOrientedBox(const ozz::math::Float4x4& transform, const Fobb& obb, const ozz::math::Float4& color);
    void DrawCapsuleShape(const ozz::math::Float4x4& transform, const Fcylinder& cylinder,
        const ozz::math::Float4& color, int segments);
    void DrawSphereShape(const ozz::math::Float4x4& transform, const Fsphere& sphere,
        const ozz::math::Float4& color, int segments);
    void DrawSolidSphere(const ozz::math::Float3& center, float radius, const ozz::math::Float4& color, int segments);
    void DrawOctahedronBone(const ozz::math::Float3& head, const ozz::math::Float3& tail,
        float radius, const ozz::math::Float4& color);
    void UpdateUniforms(const ozz::math::Float4x4& view_proj);
    void UpdateDescriptorSet();

    VulkanDevice* device_ = nullptr;

    VulkanBuffer line_vertex_buffer_;
    VulkanBuffer solid_vertex_buffer_;
    VulkanBuffer uniform_buffer_;

    VulkanPipeline line_pipeline_;
    VulkanPipeline solid_pipeline_;
    VkDescriptorSetLayout descriptor_set_layout_ = VK_NULL_HANDLE;
    VkDescriptorPool descriptor_pool_ = VK_NULL_HANDLE;
    VkDescriptorSet descriptor_set_ = VK_NULL_HANDLE;

    xr_vector<LineVertex> line_vertices_;
    xr_vector<SolidVertex> solid_vertices_;
    size_t line_vertex_capacity_ = 0;
    size_t solid_vertex_capacity_ = 0;
    bool line_buffer_dirty_ = false;
    bool solid_buffer_dirty_ = false;
    bool initialized_ = false;
};

} // namespace renderer
} // namespace animation
} // namespace xray
