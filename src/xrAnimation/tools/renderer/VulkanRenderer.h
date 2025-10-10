#pragma once

#include "VulkanDevice.h"
#include "VulkanPipeline.h"
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

    VulkanDevice* GetDevice() { return &device_; }

private:
    bool InitializeTrianglePipeline();

    VulkanDevice device_;
    std::vector<VkCommandBuffer> command_buffers_;
    float clear_color_[3] = {0.1f, 0.1f, 0.2f};  // Dark blue default

    // Triangle test pipeline
    VulkanPipeline triangle_pipeline_;
    bool triangle_pipeline_initialized_ = false;
};

} // namespace renderer
} // namespace animation
} // namespace xray
