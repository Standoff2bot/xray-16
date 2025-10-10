#include "VulkanRenderer.h"

#include "framework/mesh.h"
#include <GLFW/glfw3.h>
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <ozz/base/maths/vec_float.h>

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

    window_ = window;
    VkExtent2D extent = device_.GetSwapchainExtent();
    camera_.Initialize(static_cast<float>(extent.width), static_cast<float>(extent.height));

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

    // Initialize triangle pipeline for testing
    if (!InitializeTrianglePipeline()) {
        Msg("! Failed to initialize triangle pipeline");
        return false;
    }

    skeleton_renderer_initialized_ = skeleton_renderer_.Initialize(&device_);
    if (!skeleton_renderer_initialized_) {
        Msg("! Failed to initialize skeleton renderer");
    }

    mesh_renderer_initialized_ = mesh_renderer_.Initialize(&device_);
    if (!mesh_renderer_initialized_) {
        Msg("! Failed to initialize mesh renderer");
    } else {
        mesh_debug_loaded_ = InitializeDebugMesh();
        if (!mesh_debug_loaded_) {
            Msg("! Failed to upload debug skinned mesh");
        }
    }

    Msg("* Vulkan Renderer initialized successfully");
    return true;
}

bool VulkanRenderer::InitializeTrianglePipeline() {
    PipelineConfig config;

#ifdef OZZ_SHADER_BINARY_DIR
    const std::filesystem::path shader_root{OZZ_SHADER_BINARY_DIR};
    config.vertex_shader_path = (shader_root / "triangle.vert.spv").string();
    config.fragment_shader_path = (shader_root / "triangle.frag.spv").string();
#else
    // Shader paths (relative to build directory)
    config.vertex_shader_path = "src/xrAnimation/tools/shaders/triangle.vert.spv";
    config.fragment_shader_path = "src/xrAnimation/tools/shaders/triangle.frag.spv";
#endif

    // No vertex input (triangle vertices are hardcoded in shader)
    config.vertex_bindings.clear();
    config.vertex_attributes.clear();

    // Triangle topology
    config.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
    config.polygon_mode = VK_POLYGON_MODE_FILL;
    config.cull_mode = VK_CULL_MODE_NONE;  // No culling for simple triangle
    config.front_face = VK_FRONT_FACE_COUNTER_CLOCKWISE;

    // Depth test enabled
    config.depth_test_enable = true;
    config.depth_write_enable = true;
    config.depth_compare_op = VK_COMPARE_OP_LESS;

    // No blending
    config.blend_enable = false;

    // Use device render pass
    config.render_pass = device_.GetRenderPass();
    config.subpass = 0;

    // No descriptor sets for simple triangle
    config.descriptor_set_layouts.clear();

    // Create pipeline
    if (!triangle_pipeline_.Create(device_.GetDevice(), config)) {
        return false;
    }

    triangle_pipeline_initialized_ = true;
    return true;
}

void VulkanRenderer::Shutdown() {
    if (mesh_renderer_initialized_) {
        mesh_renderer_.Shutdown();
        mesh_renderer_initialized_ = false;
        mesh_debug_loaded_ = false;
        mesh_instances_.clear();
        mesh_bone_matrices_.clear();
    }

    if (skeleton_renderer_initialized_) {
        skeleton_renderer_.Shutdown();
        skeleton_renderer_initialized_ = false;
    }
    window_ = nullptr;

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
    if (window_) {
        camera_.Update(window_, 0.0f);
    }

    if (mesh_renderer_initialized_ && mesh_debug_loaded_) {
        UpdateMeshAnimation(static_cast<float>(glfwGetTime()));
    }

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

    // Set dynamic viewport and scissor
    VkViewport viewport = {};
    viewport.x = 0.0f;
    viewport.y = 0.0f;
    viewport.width = static_cast<float>(device_.GetSwapchainExtent().width);
    viewport.height = static_cast<float>(device_.GetSwapchainExtent().height);
    viewport.minDepth = 0.0f;
    viewport.maxDepth = 1.0f;
    vkCmdSetViewport(cmd, 0, 1, &viewport);

    VkRect2D scissor = {};
    scissor.offset = {0, 0};
    scissor.extent = device_.GetSwapchainExtent();
    vkCmdSetScissor(cmd, 0, 1, &scissor);
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

bool VulkanRenderer::InitializeDebugMesh() {
    if (!mesh_renderer_initialized_) {
        return false;
    }

    ozz::sample::Mesh mesh;
    mesh.parts.resize(1);
    ozz::sample::Mesh::Part& part = mesh.parts.back();

    constexpr int kVertexCount = 3;
    part.positions.resize(static_cast<size_t>(kVertexCount * ozz::sample::Mesh::Part::kPositionsCpnts));
    part.normals.resize(static_cast<size_t>(kVertexCount * ozz::sample::Mesh::Part::kNormalsCpnts));
    part.uvs.resize(static_cast<size_t>(kVertexCount * ozz::sample::Mesh::Part::kUVsCpnts));

    // Simple upright triangle
    part.positions[0] = -0.5f; part.positions[1] = 0.0f; part.positions[2] = 0.0f;
    part.positions[3] = 0.5f;  part.positions[4] = 0.0f; part.positions[5] = 0.0f;
    part.positions[6] = 0.0f;  part.positions[7] = 1.0f; part.positions[8] = 0.0f;

    // Normals pointing forward
    part.normals[0] = 0.0f; part.normals[1] = 0.0f; part.normals[2] = 1.0f;
    part.normals[3] = 0.0f; part.normals[4] = 0.0f; part.normals[5] = 1.0f;
    part.normals[6] = 0.0f; part.normals[7] = 0.0f; part.normals[8] = 1.0f;

    // Basic UVs
    part.uvs[0] = 0.0f; part.uvs[1] = 0.0f;
    part.uvs[2] = 1.0f; part.uvs[3] = 0.0f;
    part.uvs[4] = 0.5f; part.uvs[5] = 1.0f;

    // One influence per vertex
    part.joint_indices.resize(kVertexCount);
    std::fill(part.joint_indices.begin(), part.joint_indices.end(), static_cast<uint16_t>(0));
    part.joint_weights.clear(); // Automatically infers final weight

    mesh.triangle_indices.push_back(0);
    mesh.triangle_indices.push_back(1);
    mesh.triangle_indices.push_back(2);

    mesh.inverse_bind_poses.push_back(ozz::math::Float4x4::identity());
    mesh.joint_remaps.push_back(0);

    if (!mesh_renderer_.UploadMesh(mesh)) {
        return false;
    }

    mesh_instances_.clear();
    mesh_instances_.resize(1);
    mesh_instances_[0].transform = ozz::math::Float4x4::identity();
    mesh_instances_[0].bone_matrix_offset = 0;

    const uint32_t bones_per_instance = mesh_renderer_.BonesPerInstance();
    if (bones_per_instance == 0) {
        return false;
    }

    mesh_bone_matrices_.resize(static_cast<size_t>(bones_per_instance) * mesh_instances_.size());
    for (auto& matrix : mesh_bone_matrices_) {
        matrix = ozz::math::Float4x4::identity();
    }

    for (size_t i = 0; i < mesh_instances_.size(); ++i) {
        mesh_instances_[i].bone_matrix_offset = static_cast<uint32_t>(i * bones_per_instance);
    }

    Msg("* Debug skinned mesh uploaded (%d vertices, %zu indices)",
        mesh.vertex_count(), mesh.triangle_indices.size());

    return true;
}

void VulkanRenderer::RenderSkinnedMeshes(VkCommandBuffer cmd) {
    if (!mesh_renderer_initialized_ || !mesh_debug_loaded_) {
        return;
    }

    mesh_renderer_.Render(cmd, camera_.GetViewProjectionMatrix(), mesh_instances_, mesh_bone_matrices_);
}

void VulkanRenderer::UpdateMeshAnimation(float time_seconds) {
    if (mesh_instances_.empty()) {
        return;
    }

    const float rotation_speed = 0.75f;
    const float angle = time_seconds * rotation_speed;

    const ozz::math::Float4x4 rotation = ozz::math::Float4x4::FromAxisAngle(ozz::math::Float3::y_axis(), angle);
    const ozz::math::Float4x4 translation = ozz::math::Float4x4::Translation(ozz::math::Float3(0.0f, -0.5f, 0.0f));
    mesh_instances_[0].transform = translation * rotation;

    if (!mesh_bone_matrices_.empty()) {
        mesh_bone_matrices_[0] = ozz::math::Float4x4::identity();
    }
}

void VulkanRenderer::RenderTriangle() {
    if (!triangle_pipeline_initialized_) {
        return;
    }

    uint32_t image_index = device_.GetCurrentImageIndex();
    VkCommandBuffer cmd = command_buffers_[image_index];

    // Bind triangle pipeline
    triangle_pipeline_.Bind(cmd);

    // Draw triangle (3 vertices, 1 instance, hardcoded in shader)
    vkCmdDraw(cmd, 3, 1, 0, 0);

    RenderSkinnedMeshes(cmd);

    if (skeleton_renderer_initialized_) {
        skeleton_renderer_.Render(cmd, camera_.GetViewProjectionMatrix());
    }
}

void VulkanRenderer::SetClearColor(float r, float g, float b) {
    clear_color_[0] = r;
    clear_color_[1] = g;
    clear_color_[2] = b;
}

} // namespace renderer
} // namespace animation
} // namespace xray
