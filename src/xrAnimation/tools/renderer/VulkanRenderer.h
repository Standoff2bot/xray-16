#pragma once

#include "VulkanDevice.h"
#include "VulkanPipeline.h"
#include "Camera.h"
#include "InstancedSkeletonRenderer.h"
#include "InstancedMeshRenderer.h"
#include <ozz/base/maths/simd_math.h>
#include <vector>

struct GLFWwindow;

namespace xray {
namespace animation {
namespace renderer {

// Main Vulkan renderer - coordinates all rendering
class VulkanRenderer {
public:
    VulkanRenderer();
    ~VulkanRenderer();

    bool Initialize(GLFWwindow* window);
    void Shutdown();

    void BeginFrame();
    void EndFrame();

    // Triangle test rendering
    void RenderTriangle();

    void SetClearColor(float r, float g, float b);

    Camera& GetCamera() { return camera_; }
    const Camera& GetCamera() const { return camera_; }
    VulkanDevice* GetDevice() { return &device_; }
    InstancedSkeletonRenderer& GetSkeletonRenderer() { return skeleton_renderer_; }
    const InstancedSkeletonRenderer& GetSkeletonRenderer() const { return skeleton_renderer_; }

private:
    bool InitializeTrianglePipeline();
    bool InitializeDebugMesh();
    void RenderSkinnedMeshes(VkCommandBuffer cmd);
    void UpdateMeshAnimation(float time_seconds);

    GLFWwindow* window_ = nullptr;
    VulkanDevice device_;
    Camera camera_;
    InstancedSkeletonRenderer skeleton_renderer_;
    InstancedMeshRenderer mesh_renderer_;
    std::vector<VkCommandBuffer> command_buffers_;
    float clear_color_[3] = {0.1f, 0.1f, 0.2f};  // Dark blue default

    // Triangle test pipeline
    VulkanPipeline triangle_pipeline_;
    bool triangle_pipeline_initialized_ = false;
    bool skeleton_renderer_initialized_ = false;
    bool mesh_renderer_initialized_ = false;
    bool mesh_debug_loaded_ = false;

    std::vector<MeshInstanceData> mesh_instances_;
    std::vector<ozz::math::Float4x4> mesh_bone_matrices_;
};

} // namespace renderer
} // namespace animation
} // namespace xray
