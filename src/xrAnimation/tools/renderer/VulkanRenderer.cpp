#include "VulkanRenderer.h"

#include <GLFW/glfw3.h>
#include <cstdio>

// Simple logging replacement
#define Msg(...) printf(__VA_ARGS__), printf("\n")

namespace xray {
namespace animation {
namespace renderer {

VulkanRenderer::VulkanRenderer() {
}

VulkanRenderer::~VulkanRenderer() {
    Shutdown();
}

bool VulkanRenderer::Initialize(GLFWwindow* window) {
    Msg("* Initializing Vulkan Renderer...");

    // Initialize Vulkan device (disable validation layers for WSL2 compatibility)
    if (!device_.Initialize(window, false)) {
        Msg("! Failed to initialize Vulkan device");
        return false;
    }

    // Create command buffers (one per swapchain image)
    uint32_t image_count = device_.GetSwapchainImageCount();
    command_buffers_.resize(image_count);

    VkCommandBufferAllocateInfo alloc_info = {};
    alloc_info.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    alloc_info.commandPool = device_.GetCommandPool();
    alloc_info.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    alloc_info.commandBufferCount = image_count;

    VkResult result = vkAllocateCommandBuffers(device_.GetDevice(), &alloc_info, command_buffers_.data());
    if (result != VK_SUCCESS) {
        Msg("! Failed to allocate command buffers");
        return false;
    }

    Msg("* Vulkan Renderer initialized successfully");
    return true;
}

void VulkanRenderer::Shutdown() {
    if (device_.GetDevice() != VK_NULL_HANDLE) {
        device_.WaitIdle();

        if (!command_buffers_.empty() && device_.GetCommandPool() != VK_NULL_HANDLE) {
            vkFreeCommandBuffers(device_.GetDevice(), device_.GetCommandPool(),
                static_cast<uint32_t>(command_buffers_.size()), command_buffers_.data());
            command_buffers_.clear();
        }

        device_.Shutdown();
    }
}

void VulkanRenderer::BeginFrame() {
    device_.BeginFrame();

    // Get current frame info
    uint32_t image_index = device_.GetCurrentImageIndex();
    VkCommandBuffer cmd = command_buffers_[image_index];

    // Reset and begin command buffer
    vkResetCommandBuffer(cmd, 0);

    VkCommandBufferBeginInfo begin_info = {};
    begin_info.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    begin_info.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;

    vkBeginCommandBuffer(cmd, &begin_info);

    // Begin render pass
    VkRenderPassBeginInfo render_pass_info = {};
    render_pass_info.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
    render_pass_info.renderPass = device_.GetRenderPass();
    render_pass_info.framebuffer = device_.GetCurrentFramebuffer();
    render_pass_info.renderArea.offset = {0, 0};
    render_pass_info.renderArea.extent = device_.GetSwapchainExtent();

    VkClearValue clear_values[2];
    clear_values[0].color = {{clear_color_[0], clear_color_[1], clear_color_[2], 1.0f}};
    clear_values[1].depthStencil = {1.0f, 0};

    render_pass_info.clearValueCount = 2;
    render_pass_info.pClearValues = clear_values;

    vkCmdBeginRenderPass(cmd, &render_pass_info, VK_SUBPASS_CONTENTS_INLINE);

    // TODO: Actual rendering commands go here
}

void VulkanRenderer::EndFrame() {
    uint32_t image_index = device_.GetCurrentImageIndex();
    VkCommandBuffer cmd = command_buffers_[image_index];

    // End render pass
    vkCmdEndRenderPass(cmd);

    // End command buffer
    vkEndCommandBuffer(cmd);

    // Submit command buffer
    VkSubmitInfo submit_info = {};
    submit_info.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;

    VkSemaphore wait_semaphores[] = {device_.GetImageAvailableSemaphore()};
    VkPipelineStageFlags wait_stages[] = {VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT};
    submit_info.waitSemaphoreCount = 1;
    submit_info.pWaitSemaphores = wait_semaphores;
    submit_info.pWaitDstStageMask = wait_stages;

    submit_info.commandBufferCount = 1;
    submit_info.pCommandBuffers = &cmd;

    VkSemaphore signal_semaphores[] = {device_.GetRenderFinishedSemaphore()};
    submit_info.signalSemaphoreCount = 1;
    submit_info.pSignalSemaphores = signal_semaphores;

    // Reset fence before submitting
    VkFence fence = device_.GetInFlightFence();
    vkResetFences(device_.GetDevice(), 1, &fence);

    VkResult result = vkQueueSubmit(device_.GetGraphicsQueue(), 1, &submit_info, device_.GetInFlightFence());
    if (result != VK_SUCCESS) {
        Msg("! Failed to submit command buffer");
    }

    // Present
    device_.EndFrame();
}

void VulkanRenderer::SetClearColor(float r, float g, float b) {
    clear_color_[0] = r;
    clear_color_[1] = r;
    clear_color_[2] = b;
}

} // namespace renderer
} // namespace animation
} // namespace xray
