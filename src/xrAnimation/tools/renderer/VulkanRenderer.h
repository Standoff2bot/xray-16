#pragma once

#include "VulkanDevice.h"
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

    void SetClearColor(float r, float g, float b);

    VulkanDevice* GetDevice() { return &device_; }

private:
    VulkanDevice device_;
    std::vector<VkCommandBuffer> command_buffers_;
    float clear_color_[3] = {0.1f, 0.1f, 0.2f};  // Dark blue default
};

} // namespace renderer
} // namespace animation
} // namespace xray
