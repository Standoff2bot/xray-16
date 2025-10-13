#include "stdafx.h"
#include "VulkanRenderer.h"

#include "framework/mesh.h"
#include "../SimpleObjLoader.h"
#include <GLFW/glfw3.h>
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <iostream>
#include <ozz/animation/runtime/animation.h>
#include <ozz/animation/runtime/local_to_model_job.h>
#include <ozz/animation/runtime/sampling_job.h>
#include <ozz/animation/runtime/skeleton.h>
#include <ozz/base/containers/vector.h>
#include <ozz/base/maths/vec_float.h>
#include <ozz/base/span.h>

#include "../../../../Externals/imgui/backends/imgui_impl_glfw.h"
#include "../../../../Externals/imgui/backends/imgui_impl_vulkan.h"
#include "../../../../Externals/imgui/imgui.h"

// Simple logging replacement
#define Msg(...) printf(__VA_ARGS__), printf("\n")

namespace {

void CheckVkResult(VkResult result) {
    if (result == VK_SUCCESS) {
        return;
    }
    Msg("! ImGui Vulkan backend error: VkResult = %d", static_cast<int>(result));
}

constexpr float kSkeletonDebugEpsilon = 1e-5f;
constexpr float kSkeletonDefaultRadius = 0.05f;

inline ozz::math::Float3 ExtractTranslation(const ozz::math::Float4x4& matrix) {
    ozz::math::Float3 result{};
    ozz::math::Store3PtrU(matrix.cols[3], &result.x);
    return result;
}

inline float DistanceBetween(const ozz::math::Float4x4& a, const ozz::math::Float4x4& b) {
    const ozz::math::Float3 pa = ExtractTranslation(a);
    const ozz::math::Float3 pb = ExtractTranslation(b);
    const float dx = pa.x - pb.x;
    const float dy = pa.y - pb.y;
    const float dz = pa.z - pb.z;
    return std::sqrt(dx * dx + dy * dy + dz * dz);
}

inline ozz::math::Float3 TransformPoint(const ozz::math::Float4x4& matrix, const ozz::math::Float3& point) {
    const float data[4] = { point.x, point.y, point.z, 1.0f };
    const ozz::math::SimdFloat4 simd_point = ozz::math::simd_float4::LoadPtrU(data);
    ozz::math::Float3 result;
    ozz::math::Store3PtrU(ozz::math::TransformPoint(matrix, simd_point), &result.x);
    return result;
}

} // namespace

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

    // Initialize Vulkan device (validation layers disabled for wider compatibility)
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
        mesh_loaded_ = InitializeDebugMesh();
        if (!mesh_loaded_) {
            Msg("! Failed to upload debug skinned mesh");
        }
    }

    debug_renderer_initialized_ = debug_renderer_.Initialize(&device_);
    if (!debug_renderer_initialized_) {
        Msg("! Failed to initialize debug renderer");
    }

    if (!InitializeImGui()) {
        Msg("! Failed to initialize Dear ImGui");
        return false;
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

    // Dynamic rendering formats
    config.color_format = device_.GetSwapchainFormat();
    config.depth_format = VK_FORMAT_D32_SFLOAT;

    // No descriptor sets for simple triangle
    config.descriptor_set_layouts.clear();

    // Create pipeline
    if (!triangle_pipeline_.Create(device_.GetDevice(), config)) {
        return false;
    }

    triangle_pipeline_initialized_ = true;
    return true;
}

bool VulkanRenderer::InitializeImGui() {
    if (imgui_initialized_) {
        return true;
    }

    if (!window_) {
        Msg("! Cannot initialize Dear ImGui without a valid GLFW window");
        return false;
    }

    VkDevice device_handle = device_.GetDevice();
    if (device_handle == VK_NULL_HANDLE) {
        Msg("! Cannot initialize Dear ImGui without a valid Vulkan device");
        return false;
    }

    // Descriptor pool for Dear ImGui
    const std::array<VkDescriptorPoolSize, 11> pool_sizes = {{
        {VK_DESCRIPTOR_TYPE_SAMPLER, 1000},
        {VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1000},
        {VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, 1000},
        {VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 1000},
        {VK_DESCRIPTOR_TYPE_UNIFORM_TEXEL_BUFFER, 1000},
        {VK_DESCRIPTOR_TYPE_STORAGE_TEXEL_BUFFER, 1000},
        {VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1000},
        {VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1000},
        {VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC, 1000},
        {VK_DESCRIPTOR_TYPE_STORAGE_BUFFER_DYNAMIC, 1000},
        {VK_DESCRIPTOR_TYPE_INPUT_ATTACHMENT, 1000}
    }};

    VkDescriptorPoolCreateInfo pool_info = {};
    pool_info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    pool_info.flags = VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT;
    pool_info.maxSets = static_cast<uint32_t>(pool_sizes.size()) * 1000;
    pool_info.poolSizeCount = static_cast<uint32_t>(pool_sizes.size());
    pool_info.pPoolSizes = pool_sizes.data();

    if (vkCreateDescriptorPool(device_handle, &pool_info, nullptr, &imgui_descriptor_pool_) != VK_SUCCESS) {
        Msg("! Failed to create Dear ImGui descriptor pool");
        return false;
    }

    IMGUI_CHECKVERSION();
    imgui_context_ = ImGui::CreateContext();
    if (!imgui_context_) {
        Msg("! Failed to create Dear ImGui context");
        vkDestroyDescriptorPool(device_handle, imgui_descriptor_pool_, nullptr);
        imgui_descriptor_pool_ = VK_NULL_HANDLE;
        return false;
    }

    ImGui::SetCurrentContext(imgui_context_);
    ImGui::StyleColorsDark();
    ImGuiIO& io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;

    if (!ImGui_ImplGlfw_InitForVulkan(window_, true)) {
        Msg("! Failed to initialize Dear ImGui GLFW backend");
        ImGui::DestroyContext(imgui_context_);
        imgui_context_ = nullptr;
        vkDestroyDescriptorPool(device_handle, imgui_descriptor_pool_, nullptr);
        imgui_descriptor_pool_ = VK_NULL_HANDLE;
        return false;
    }

    ImGui_ImplVulkan_InitInfo init_info = {};
    init_info.ApiVersion = VK_API_VERSION_1_3;
    init_info.Instance = device_.GetInstance();
    init_info.PhysicalDevice = device_.GetPhysicalDevice();
    init_info.Device = device_handle;
    init_info.QueueFamily = device_.GetQueueFamilyIndices().graphics_family;
    init_info.Queue = device_.GetGraphicsQueue();
    init_info.DescriptorPool = imgui_descriptor_pool_;
    init_info.MinImageCount = device_.GetSwapchainImageCount();
    init_info.ImageCount = device_.GetSwapchainImageCount();
    init_info.PipelineCache = VK_NULL_HANDLE;
    init_info.PipelineInfoMain.MSAASamples = VK_SAMPLE_COUNT_1_BIT;
    init_info.UseDynamicRendering = true;

    // Setup dynamic rendering info
    VkPipelineRenderingCreateInfoKHR pipeline_rendering_info = {};
    pipeline_rendering_info.sType = VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO_KHR;
    pipeline_rendering_info.colorAttachmentCount = 1;
    VkFormat color_format = device_.GetSwapchainFormat();
    pipeline_rendering_info.pColorAttachmentFormats = &color_format;
    init_info.PipelineInfoMain.PipelineRenderingCreateInfo = pipeline_rendering_info;

    init_info.Allocator = nullptr;
    init_info.CheckVkResultFn = CheckVkResult;
    init_info.MinAllocationSize = 1024 * 1024;

    if (!ImGui_ImplVulkan_Init(&init_info)) {
        Msg("! Failed to initialize Dear ImGui Vulkan backend");
        ImGui_ImplGlfw_Shutdown();
        ImGui::DestroyContext(imgui_context_);
        imgui_context_ = nullptr;
        vkDestroyDescriptorPool(device_handle, imgui_descriptor_pool_, nullptr);
        imgui_descriptor_pool_ = VK_NULL_HANDLE;
        return false;
    }

    imgui_initialized_ = true;
    imgui_frame_started_ = false;
    imgui_image_count_ = init_info.ImageCount;
    return true;
}

void VulkanRenderer::ShutdownImGui() {
    if (!imgui_initialized_) {
        if (imgui_descriptor_pool_ != VK_NULL_HANDLE && device_.GetDevice() != VK_NULL_HANDLE) {
            vkDestroyDescriptorPool(device_.GetDevice(), imgui_descriptor_pool_, nullptr);
            imgui_descriptor_pool_ = VK_NULL_HANDLE;
        }
        return;
    }

    ImGui::SetCurrentContext(imgui_context_);
    ImGui_ImplVulkan_Shutdown();
    ImGui_ImplGlfw_Shutdown();
    ImGui::DestroyContext(imgui_context_);
    imgui_context_ = nullptr;
    imgui_initialized_ = false;
    imgui_frame_started_ = false;
    imgui_image_count_ = 0;

    if (imgui_descriptor_pool_ != VK_NULL_HANDLE && device_.GetDevice() != VK_NULL_HANDLE) {
        vkDestroyDescriptorPool(device_.GetDevice(), imgui_descriptor_pool_, nullptr);
        imgui_descriptor_pool_ = VK_NULL_HANDLE;
    }
}

void VulkanRenderer::BeginImGuiFrame() {
    if (!imgui_initialized_) {
        return;
    }

    ImGui::SetCurrentContext(imgui_context_);

    const uint32_t image_count = device_.GetSwapchainImageCount();
    if (image_count != imgui_image_count_) {
        ImGui_ImplVulkan_SetMinImageCount(image_count);
        imgui_image_count_ = image_count;
    }

    ImGui_ImplVulkan_NewFrame();
    ImGui_ImplGlfw_NewFrame();
    ImGui::NewFrame();
    imgui_frame_started_ = true;
}

void VulkanRenderer::RenderImGui(VkCommandBuffer cmd) {
    if (!imgui_initialized_ || !imgui_frame_started_) {
        return;
    }

    ImGui::SetCurrentContext(imgui_context_);
    ImGui::Render();
    ImDrawData* draw_data = ImGui::GetDrawData();
    if (draw_data) {
        ImGui_ImplVulkan_RenderDrawData(draw_data, cmd);
    }

    imgui_frame_started_ = false;
}

void VulkanRenderer::Shutdown() {
    if (mesh_renderer_initialized_) {
        mesh_renderer_.Shutdown();
        mesh_renderer_initialized_ = false;
        mesh_loaded_ = false;
        mesh_instances_.clear();
        mesh_bone_matrices_.clear();
        mesh_bind_pose_palette_.clear();
    }

    if (skeleton_renderer_initialized_) {
        skeleton_renderer_.Shutdown();
        skeleton_renderer_initialized_ = false;
    }

    skeleton_loaded_ = false;
    skeleton_rest_models_.clear();
    skeleton_parents_.clear();
    bone_metadata_.clear();

    if (debug_renderer_initialized_) {
        debug_renderer_.Shutdown();
        debug_renderer_initialized_ = false;
    }

    window_ = nullptr;

    if (device_.GetDevice() != VK_NULL_HANDLE) {
        device_.WaitIdle();

        ShutdownImGui();

        if (triangle_pipeline_initialized_) {
            triangle_pipeline_.Destroy();
            triangle_pipeline_initialized_ = false;
        }

        if (!command_buffers_.empty() && device_.GetCommandPool() != VK_NULL_HANDLE) {
            vkFreeCommandBuffers(device_.GetDevice(), device_.GetCommandPool(),
                static_cast<uint32_t>(command_buffers_.size()), command_buffers_.data());
            command_buffers_.clear();
        }

        device_.Shutdown();
    }
}

void VulkanRenderer::BeginFrame() {
    const double now_seconds = glfwGetTime();
    if (!frame_time_initialized_) {
        frame_delta_seconds_ = 0.0;
        frame_time_initialized_ = true;
    } else {
        frame_delta_seconds_ = std::max(0.0, now_seconds - last_frame_timestamp_);
    }
    last_frame_timestamp_ = now_seconds;

    device_.BeginFrame();

    if (debug_renderer_initialized_) {
        debug_renderer_.BeginFrame();
        // Draw ground grid for reference - BRIGHT colors for visibility
        const ozz::math::Float3 grid_center{0.0f, 0.0f, 0.0f};
        const ozz::math::Float4 grid_main{1.0f, 1.0f, 1.0f, 1.0f};  // WHITE main gridlines
        const ozz::math::Float4 grid_sub{0.8f, 0.8f, 0.8f, 1.0f};    // Light gray sub gridlines
        debug_renderer_.DrawGrid(grid_center, 20.0f, 20, grid_main, grid_sub);

        // Draw some test axes at origin to verify rendering - BRIGHT colors
        const ozz::math::Float4x4 origin = ozz::math::Float4x4::identity();
        debug_renderer_.DrawAxes(origin, 5.0f,  // Larger axes
            ozz::math::Float4{1.0f, 0.0f, 0.0f, 1.0f},  // Bright Red X
            ozz::math::Float4{0.0f, 1.0f, 0.0f, 1.0f},  // Bright Green Y
            ozz::math::Float4{0.0f, 0.0f, 1.0f, 1.0f}); // Bright Blue Z

        static int frame_count = 0;
        if (frame_count++ < 5) {
            Msg("* Debug renderer drawing grid and axes (frame %d)", frame_count);
        }
    } else {
        static bool warned = false;
        if (!warned) {
            Msg("! Debug renderer not initialized");
            warned = true;
        }
    }

    if (imgui_initialized_) {
        BeginImGuiFrame();
    }

    if (window_) {
        bool capture_mouse = false;
        if (imgui_initialized_) {
            ImGui::SetCurrentContext(imgui_context_);
            capture_mouse = ImGui::GetIO().WantCaptureMouse;
        }

        if (!capture_mouse) {
            camera_.Update(window_, static_cast<float>(frame_delta_seconds_));
        }
    }

    if (mesh_renderer_initialized_ && mesh_loaded_) {
        UpdateMeshAnimation(static_cast<float>(frame_delta_seconds_));
    } else if (skeleton_loaded_) {
        skeleton_world_transform_ = ozz::math::Float4x4::identity();
        if (skeleton_renderer_initialized_) {
            std::array<ozz::math::Float4x4, 1> transforms = { skeleton_world_transform_ };
            skeleton_renderer_.SetInstanceTransforms(ozz::make_span(transforms));
        }
    }

    // Get current frame info
    uint32_t image_index = device_.GetCurrentImageIndex();
    VkCommandBuffer cmd = command_buffers_[image_index];

    // Reset and begin command buffer
    vkResetCommandBuffer(cmd, 0);

    VkCommandBufferBeginInfo begin_info = {};
    begin_info.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    begin_info.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;

    vkBeginCommandBuffer(cmd, &begin_info);

    // Transition swapchain image to color attachment layout
    VkImageMemoryBarrier barrier = {};
    barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    barrier.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    barrier.newLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.image = device_.GetCurrentSwapchainImage();
    barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    barrier.subresourceRange.baseMipLevel = 0;
    barrier.subresourceRange.levelCount = 1;
    barrier.subresourceRange.baseArrayLayer = 0;
    barrier.subresourceRange.layerCount = 1;
    barrier.srcAccessMask = 0;
    barrier.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;

    vkCmdPipelineBarrier(cmd,
        VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
        VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
        0, 0, nullptr, 0, nullptr, 1, &barrier);

    // Begin dynamic rendering (Vulkan 1.3)
    VkRenderingAttachmentInfo color_attachment = {};
    color_attachment.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
    color_attachment.imageView = device_.GetCurrentSwapchainImageView();
    color_attachment.imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    color_attachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    color_attachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    color_attachment.clearValue.color = {{clear_color_[0], clear_color_[1], clear_color_[2], 1.0f}};

    VkRenderingAttachmentInfo depth_attachment = {};
    depth_attachment.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
    depth_attachment.imageView = device_.GetDepthImageView();
    depth_attachment.imageLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
    depth_attachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    depth_attachment.storeOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    depth_attachment.clearValue.depthStencil = {1.0f, 0};

    VkRenderingInfo rendering_info = {};
    rendering_info.sType = VK_STRUCTURE_TYPE_RENDERING_INFO;
    rendering_info.renderArea.offset = {0, 0};
    rendering_info.renderArea.extent = device_.GetSwapchainExtent();
    rendering_info.layerCount = 1;
    rendering_info.colorAttachmentCount = 1;
    rendering_info.pColorAttachments = &color_attachment;
    rendering_info.pDepthAttachment = &depth_attachment;

    vkCmdBeginRendering(cmd, &rendering_info);

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

    RenderImGui(cmd);

    // End dynamic rendering
    vkCmdEndRendering(cmd);

    // Transition swapchain image to present layout
    VkImageMemoryBarrier barrier = {};
    barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    barrier.oldLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    barrier.newLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
    barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.image = device_.GetCurrentSwapchainImage();
    barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    barrier.subresourceRange.baseMipLevel = 0;
    barrier.subresourceRange.levelCount = 1;
    barrier.subresourceRange.baseArrayLayer = 0;
    barrier.subresourceRange.layerCount = 1;
    barrier.srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
    barrier.dstAccessMask = 0;

    vkCmdPipelineBarrier(cmd,
        VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
        VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT,
        0, 0, nullptr, 0, nullptr, 1, &barrier);

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

    mesh_joint_remaps_.assign(mesh.joint_remaps.begin(), mesh.joint_remaps.end());
    mesh_inverse_bind_poses_.assign(mesh.inverse_bind_poses.begin(), mesh.inverse_bind_poses.end());

    mesh_instances_.clear();
    mesh_instances_.resize(1);
    mesh_instances_[0].transform = ozz::math::Float4x4::identity();
    mesh_instances_[0].bone_matrix_offset = 0;

    const uint32_t bones_per_instance = mesh_renderer_.BonesPerInstance();
    if (bones_per_instance == 0) {
        return false;
    }

    mesh_bone_matrices_.resize(static_cast<size_t>(bones_per_instance) * mesh_instances_.size());
    mesh_bind_pose_palette_.resize(bones_per_instance);
    sampled_palette_.assign(bones_per_instance, ozz::math::Float4x4::identity());

    for (size_t idx = 0; idx < mesh_bind_pose_palette_.size(); ++idx) {
        mesh_bind_pose_palette_[idx] = ozz::math::Float4x4::identity();
        sampled_palette_[idx] = mesh_bind_pose_palette_[idx];
    }

    for (size_t i = 0; i < mesh_instances_.size(); ++i) {
        mesh_instances_[i].bone_matrix_offset = static_cast<uint32_t>(i * bones_per_instance);
    }

    ApplyPaletteToInstances(mesh_bind_pose_palette_);

    Msg("* Debug skinned mesh uploaded (%d vertices, %zu indices)",
        mesh.vertex_count(), mesh.triangle_indices.size());

    return true;
}

bool VulkanRenderer::LoadBundleMesh(const ozz::sample::Mesh& mesh, const ozz::animation::Skeleton& skeleton) {
    if (!mesh_renderer_initialized_) {
        Msg("! Mesh renderer not initialized; cannot upload bundle mesh");
        return false;
    }

    if (!mesh_renderer_.UploadMesh(mesh)) {
        Msg("! Failed to upload bundle mesh to GPU");
        return false;
    }

    mesh_joint_remaps_.assign(mesh.joint_remaps.begin(), mesh.joint_remaps.end());
    mesh_inverse_bind_poses_.assign(mesh.inverse_bind_poses.begin(), mesh.inverse_bind_poses.end());

    const uint32_t bones_per_instance = mesh_renderer_.BonesPerInstance();
    if (bones_per_instance == 0) {
        Msg("! Uploaded mesh reports zero bones; skipping mesh rendering");
        return false;
    }

    mesh_instances_.clear();
    mesh_instances_.resize(1);
    mesh_instances_[0].transform = ozz::math::Float4x4::identity();
    mesh_instances_[0].bone_matrix_offset = 0;

    mesh_bind_pose_palette_.assign(bones_per_instance, ozz::math::Float4x4::identity());
    sampled_palette_.assign(bones_per_instance, ozz::math::Float4x4::identity());
    mesh_bone_matrices_.assign(static_cast<size_t>(bones_per_instance) * mesh_instances_.size(), ozz::math::Float4x4::identity());

    if (skeleton.num_joints() == 0) {
        Msg("! Skeleton has no joints; mesh skinning palette will remain identity");
    } else {
        ozz::vector<ozz::math::Float4x4> models(skeleton.num_joints());
        ozz::animation::LocalToModelJob job;
        job.skeleton = &skeleton;
        job.input = skeleton.joint_rest_poses();
        job.output = ozz::make_span(models);

        if (!job.Run()) {
            Msg("! Failed to compute skeleton bind pose for mesh palette");
        } else {
            for (uint32_t palette_index = 0; palette_index < bones_per_instance; ++palette_index) {
                ozz::math::Float4x4 palette_matrix = ozz::math::Float4x4::identity();

                if (palette_index < mesh.joint_remaps.size()) {
                    const uint16_t joint = mesh.joint_remaps[palette_index];
                    if (joint < models.size() && palette_index < mesh.inverse_bind_poses.size()) {
                        palette_matrix = models[joint] * mesh.inverse_bind_poses[palette_index];
                    }
                }

                mesh_bind_pose_palette_[palette_index] = palette_matrix;
                sampled_palette_[palette_index] = palette_matrix;
                mesh_bind_pose_palette_[palette_index] = palette_matrix;
            }
        }
    }

    for (size_t i = 0; i < mesh_instances_.size(); ++i) {
        mesh_instances_[i].bone_matrix_offset = static_cast<uint32_t>(i * bones_per_instance);
    }

    ApplyPaletteToInstances(mesh_bind_pose_palette_);

    mesh_loaded_ = true;
    Msg("* Bundle mesh uploaded (%d vertices, %zu indices, bones=%u)",
        mesh.vertex_count(), mesh.triangle_indices.size(), bones_per_instance);

    return true;
}

bool VulkanRenderer::SetSkeletonDebugData(const ozz::animation::Skeleton& skeleton,
    const XRay::Animation::ExtendedBoneMetadataCollection& metadata) {
    if (!debug_renderer_initialized_) {
        return false;
    }

    const int joint_count = skeleton.num_joints();
    if (joint_count <= 0) {
        skeleton_rest_models_.clear();
        skeleton_pose_models_.clear();
        skeleton_parents_.clear();
        bone_metadata_.clear();
        skeleton_source_ = nullptr;
        local_transforms_.clear();
        model_transforms_.clear();
        sampling_context_.Resize(0);
        skeleton_loaded_ = false;
        skeleton_world_transform_ = ozz::math::Float4x4::identity();
        if (skeleton_renderer_initialized_) {
            std::array<ozz::math::Float4x4, 1> transforms = { ozz::math::Float4x4::identity() };
            skeleton_renderer_.SetInstanceTransforms(ozz::make_span(transforms));
        }
        return false;
    }

    ozz::vector<ozz::math::Float4x4> models;
    models.resize(joint_count);

    ozz::animation::LocalToModelJob job;
    job.skeleton = &skeleton;
    job.input = skeleton.joint_rest_poses();
    job.output = ozz::make_span(models);
    if (!job.Run()) {
        Msg("! Failed to compute skeleton rest pose for debug visualization");
        return false;
    }

    skeleton_rest_models_.assign(models.begin(), models.end());
    skeleton_pose_models_ = skeleton_rest_models_;
    skeleton_source_ = &skeleton;

    const ozz::span<const ozz::math::SoaTransform> rest_poses = skeleton.joint_rest_poses();
    local_transforms_.resize(rest_poses.size());
    for (size_t i = 0; i < rest_poses.size(); ++i) {
        local_transforms_[i] = rest_poses[i];
    }

    model_transforms_.assign(skeleton_rest_models_.begin(), skeleton_rest_models_.end());
    sampling_context_.Resize(joint_count);

    const auto parents = skeleton.joint_parents();
    skeleton_parents_.resize(parents.size());
    for (size_t i = 0; i < parents.size(); ++i) {
        skeleton_parents_[i] = static_cast<int>(parents[i]);
    }

    bone_metadata_ = metadata;
    if (bone_metadata_.size() < skeleton_rest_models_.size()) {
        bone_metadata_.resize(skeleton_rest_models_.size());
    }

    skeleton_world_transform_ = ozz::math::Float4x4::identity();
    if (skeleton_renderer_initialized_) {
        std::array<ozz::math::Float4x4, 1> transforms = { skeleton_world_transform_ };
        skeleton_renderer_.SetInstanceTransforms(ozz::make_span(transforms));
    }

    skeleton_loaded_ = true;
    return true;
}

void VulkanRenderer::RenderSkinnedMeshes(VkCommandBuffer cmd) {
    if (!mesh_renderer_initialized_ || !mesh_loaded_ || !show_skinned_mesh_) {
        return;
    }

    mesh_renderer_.Render(cmd, camera_.GetViewProjectionMatrix(), mesh_instances_, mesh_bone_matrices_);
}

void VulkanRenderer::UpdateMeshAnimation(float delta_time_seconds) {
    if (mesh_instances_.empty()) {
        return;
    }

    if (animate_mesh_) {
        mesh_animation_time_ += delta_time_seconds * animation_playback_speed_;
    }

    bool sampled_pose = false;
    // If bind pose mode is active, skip animation sampling and use rest pose
    if (show_bind_pose_) {
        skeleton_pose_models_ = skeleton_rest_models_;
        ApplyPaletteToInstances(mesh_bind_pose_palette_);
        return;
    }

    if (active_animation_ && skeleton_source_) {
        const int joint_count = skeleton_source_->num_joints();
        const int track_count = active_animation_->num_tracks();
        if (joint_count > 0 && track_count == joint_count) {
            const size_t soa_count = static_cast<size_t>(skeleton_source_->num_soa_joints());
            if (local_transforms_.size() != soa_count) {
                local_transforms_.resize(soa_count);
            }

            if (model_transforms_.size() != static_cast<size_t>(joint_count)) {
                model_transforms_.resize(static_cast<size_t>(joint_count));
            }

            sampling_context_.Resize(track_count);

            const float duration = active_animation_->duration();
            float ratio = 0.0f;
            if (duration > 0.0f) {
                float sample_time = std::fmod(mesh_animation_time_, duration);
                if (sample_time < 0.0f) {
                    sample_time += duration;
                }
                ratio = sample_time / duration;
            }

            ozz::animation::SamplingJob sampling_job;
            sampling_job.animation = active_animation_;
            sampling_job.context = &sampling_context_;
            sampling_job.ratio = ratio;
            sampling_job.output = ozz::make_span(local_transforms_);

            if (sampling_job.Run()) {
                ozz::animation::LocalToModelJob ltm_job;
                ltm_job.skeleton = skeleton_source_;
                ltm_job.input = ozz::make_span(local_transforms_);
                ltm_job.output = ozz::make_span(model_transforms_);
                sampled_pose = ltm_job.Run();
                if (!sampled_pose) {
                    Msg("! LocalToModelJob failed while evaluating animation pose");
                }
            } else {
                Msg("! SamplingJob failed while evaluating animation pose");
            }
        }
    }

    if (sampled_pose) {
        skeleton_pose_models_.assign(model_transforms_.begin(), model_transforms_.end());

        const size_t palette_size = mesh_bind_pose_palette_.size();
        if (palette_size == mesh_inverse_bind_poses_.size() && palette_size > 0) {
            sampled_palette_.resize(palette_size);
            for (size_t palette_index = 0; palette_index < palette_size; ++palette_index) {
                uint32_t joint_index = (palette_index < mesh_joint_remaps_.size())
                    ? static_cast<uint32_t>(mesh_joint_remaps_[palette_index])
                    : static_cast<uint32_t>(palette_index);
                if (joint_index < model_transforms_.size()) {
                    sampled_palette_[palette_index] = model_transforms_[joint_index] * mesh_inverse_bind_poses_[palette_index];
                } else {
                    sampled_palette_[palette_index] = mesh_bind_pose_palette_[palette_index];
                }
            }
            ApplyPaletteToInstances(sampled_palette_);
        } else {
            ApplyPaletteToInstances(mesh_bind_pose_palette_);
        }
    } else {
        skeleton_pose_models_ = skeleton_rest_models_;
        ApplyPaletteToInstances(mesh_bind_pose_palette_);
    }
}

void VulkanRenderer::RenderScene() {
    if (!triangle_pipeline_initialized_) {
        return;
    }

    uint32_t image_index = device_.GetCurrentImageIndex();
    VkCommandBuffer cmd = command_buffers_[image_index];

    if (show_triangle_) {
        triangle_pipeline_.Bind(cmd);
        vkCmdDraw(cmd, 3, 1, 0, 0);
    }

    RenderSkinnedMeshes(cmd);

    if (skeleton_renderer_initialized_ && show_skeleton_lines_) {
        skeleton_renderer_.Render(cmd, camera_.GetViewProjectionMatrix());
    }

    RenderDebugPrimitives(cmd);
}

void VulkanRenderer::PopulateSkeletonDebugShapes() {
    if (!debug_renderer_initialized_ || !show_debug_overlay_ || !skeleton_loaded_) {
        return;
    }

    const std::vector<ozz::math::Float4x4>& pose_models =
        !skeleton_pose_models_.empty() ? skeleton_pose_models_ : skeleton_rest_models_;
    const size_t pose_count = pose_models.size();
    if (pose_count == 0) {
        return;
    }

    // Get instance transforms from skeleton renderer
    const auto& instance_transforms = skeleton_renderer_.GetInstanceTransforms();
    if (instance_transforms.empty()) {
        return;
    }

    const ozz::math::Float4 bone_color{0.95f, 0.85f, 0.25f, 0.45f};
    const ozz::math::Float4 root_color{0.55f, 0.75f, 1.0f, 0.55f};
    const ozz::math::Float4 joint_color{0.92f, 0.35f, 0.35f, 0.65f};
    const ozz::math::Float4 link_color{0.65f, 0.65f, 0.95f, 0.55f};

    auto compute_rest_length = [&](int bone_index) -> float {
        float result = 0.0f;
        if (bone_index < 0 || static_cast<size_t>(bone_index) >= skeleton_rest_models_.size()) {
            return result;
        }

        const int parent = (bone_index < static_cast<int>(skeleton_parents_.size())) ? skeleton_parents_[bone_index] : -1;
        if (parent >= 0 && static_cast<size_t>(parent) < skeleton_rest_models_.size()) {
            result = DistanceBetween(skeleton_rest_models_[bone_index], skeleton_rest_models_[parent]);
        }

        if (result > kSkeletonDebugEpsilon) {
            return result;
        }

        for (size_t child = 0; child < skeleton_parents_.size(); ++child) {
            if (skeleton_parents_[child] == bone_index) {
                result = std::max(result, DistanceBetween(skeleton_rest_models_[bone_index], skeleton_rest_models_[child]));
            }
        }

        return result;
    };

    // Draw skeleton for each instance in the grid
    for (size_t instance_idx = 0; instance_idx < instance_transforms.size(); ++instance_idx) {
        const ozz::math::Float4x4& instance_transform = instance_transforms[instance_idx];

        for (size_t bone = 0; bone < pose_count; ++bone) {
            const ozz::math::Float4x4& pose_transform = pose_models[bone];
            const ozz::math::Float4x4 world_transform = instance_transform * pose_transform;

            XRay::Animation::ExtendedBoneMetadata metadata_entry;
            if (bone < bone_metadata_.size()) {
                metadata_entry = bone_metadata_[bone];
            }

            if (metadata_entry.rest_length <= kSkeletonDebugEpsilon) {
                metadata_entry.rest_length = compute_rest_length(static_cast<int>(bone));
            }

            if (metadata_entry.rest_length <= kSkeletonDebugEpsilon) {
                metadata_entry.rest_length = kSkeletonDefaultRadius;
            }

            const ozz::math::Float3 bone_position = ExtractTranslation(world_transform);

            const int parent_index = (bone < skeleton_parents_.size()) ? skeleton_parents_[bone] : -1;
            if (parent_index >= 0 && static_cast<size_t>(parent_index) < pose_models.size()) {
                const ozz::math::Float4x4 parent_world = instance_transform * pose_models[parent_index];
                const ozz::math::Float3 parent_position = ExtractTranslation(parent_world);
                debug_renderer_.DrawLine(parent_position, bone_position, link_color);
            }

            ozz::math::Float3 tail_position = bone_position;
            bool has_child = false;
            for (size_t child = 0; child < skeleton_parents_.size(); ++child) {
                if (skeleton_parents_[child] == static_cast<int>(bone) && child < pose_models.size()) {
                    const ozz::math::Float4x4 child_world = instance_transform * pose_models[child];
                    tail_position = ExtractTranslation(child_world);
                    has_child = true;
                    break;
                }
            }

            if (!has_child) {
                ozz::math::Float3 axis;
                ozz::math::Store3PtrU(world_transform.cols[0], &axis.x);
                float axis_len = std::sqrt(axis.x * axis.x + axis.y * axis.y + axis.z * axis.z);
                if (axis_len <= kSkeletonDebugEpsilon) {
                    axis = ozz::math::Float3::x_axis();
                    axis_len = 1.0f;
                }
                const float target_length = (metadata_entry.rest_length > kSkeletonDebugEpsilon)
                    ? metadata_entry.rest_length
                    : kSkeletonDefaultRadius;
                axis = ozz::math::Float3{axis.x / axis_len, axis.y / axis_len, axis.z / axis_len};
                tail_position = ozz::math::Float3{
                    bone_position.x + axis.x * target_length,
                    bone_position.y + axis.y * target_length,
                    bone_position.z + axis.z * target_length
                };
            }

            const float dx = tail_position.x - bone_position.x;
            const float dy = tail_position.y - bone_position.y;
            const float dz = tail_position.z - bone_position.z;
            float bone_length = std::sqrt(dx * dx + dy * dy + dz * dz);
            if (bone_length <= kSkeletonDebugEpsilon) {
                bone_length = std::max(metadata_entry.rest_length, kSkeletonDefaultRadius);
            }

            const float joint_radius = std::clamp(bone_length * 0.18f,
                kSkeletonDefaultRadius * 0.4f, bone_length * 0.45f);

            const ozz::math::Float4 draw_color = (bone == 0) ? root_color : bone_color;
            debug_renderer_.DrawBoneShape(bone_position, tail_position, joint_radius, draw_color);
            debug_renderer_.DrawSphere(bone_position, joint_radius, joint_color, 24);
            debug_renderer_.DrawSphere(tail_position, joint_radius * 0.6f, joint_color, 24);
            if (bone == 0) {
                debug_renderer_.DrawAxes(world_transform, metadata_entry.rest_length * 0.2f,
                    ozz::math::Float4{1.0f, 0.3f, 0.3f, 1.0f},
                    ozz::math::Float4{0.3f, 1.0f, 0.3f, 1.0f},
                    ozz::math::Float4{0.3f, 0.6f, 1.0f, 1.0f});
            }
        }
    }
}

void VulkanRenderer::RenderDebugPrimitives(VkCommandBuffer cmd) {
    if (!debug_renderer_initialized_) {
        return;
    }

    // Always populate skeleton debug shapes if overlay is enabled
    if (show_debug_overlay_ && skeleton_loaded_) {
        PopulateSkeletonDebugShapes();
    }

    // Always render debug primitives (including grid)
    debug_renderer_.EndFrame();
    debug_renderer_.Render(cmd, camera_.GetViewProjectionMatrix());
}

void VulkanRenderer::ApplyPaletteToInstances(const std::vector<ozz::math::Float4x4>& palette) {
    if (palette.empty() || mesh_instances_.empty()) {
        return;
    }

    const size_t bones_per_instance = palette.size();
    for (const MeshInstanceData& instance : mesh_instances_) {
        const size_t offset = static_cast<size_t>(instance.bone_matrix_offset);
        for (size_t bone = 0; bone < bones_per_instance; ++bone) {
            const size_t dst_index = offset + bone;
            if (dst_index < mesh_bone_matrices_.size()) {
                mesh_bone_matrices_[dst_index] = palette[bone];
            }
        }
    }
}

void VulkanRenderer::SetClearColor(float r, float g, float b) {
    clear_color_[0] = r;
    clear_color_[1] = g;
    clear_color_[2] = b;
}

void VulkanRenderer::GetClearColor(float& r, float& g, float& b) const {
    r = clear_color_[0];
    g = clear_color_[1];
    b = clear_color_[2];
}

void VulkanRenderer::SetShowTriangle(bool show) {
    show_triangle_ = show;
}

void VulkanRenderer::SetShowSkeletonLines(bool show) {
    show_skeleton_lines_ = show;
}

void VulkanRenderer::SetShowSkinnedMesh(bool show) {
    std::cout << "SetShowSkinnedMesh: " << show << std::endl;
    show_skinned_mesh_ = show;
}

void VulkanRenderer::SetShowDebugOverlay(bool show) {
    show_debug_overlay_ = show;
}

void VulkanRenderer::SetMeshRotationSpeed(float radians_per_second) {
    mesh_rotation_speed_ = radians_per_second;
}

void VulkanRenderer::SetAnimateMesh(bool enabled) {
    animate_mesh_ = enabled;
}

void VulkanRenderer::SetMeshAnimationTime(float time_seconds) {
    mesh_animation_time_ = std::max(0.0f, time_seconds);
}

void VulkanRenderer::SetActiveAnimation(const ozz::animation::Animation* animation) {
    active_animation_ = animation;
    if (active_animation_ && skeleton_source_) {
        if (active_animation_->num_tracks() != skeleton_source_->num_joints()) {
            Msg("! Active animation tracks (%d) mismatch skeleton joints (%d); ignoring animation",
                active_animation_->num_tracks(), skeleton_source_->num_joints());
            active_animation_ = nullptr;
            sampling_context_.Resize(0);
            sampled_palette_ = mesh_bind_pose_palette_;
            skeleton_pose_models_ = skeleton_rest_models_;
            ApplyPaletteToInstances(mesh_bind_pose_palette_);
            return;
        }
        sampling_context_.Resize(active_animation_->num_tracks());
        mesh_animation_time_ = 0.0f;
    } else {
        sampling_context_.Resize(0);
        skeleton_pose_models_ = skeleton_rest_models_;
        ApplyPaletteToInstances(mesh_bind_pose_palette_);
    }
    sampled_palette_ = mesh_bind_pose_palette_;
}

void VulkanRenderer::SetECSInstances(const std::vector<MeshInstanceData>& instances,
                                      const std::vector<ozz::math::Float4x4>& bone_matrices) {
    mesh_instances_ = instances;
    mesh_bone_matrices_ = bone_matrices;
}

void VulkanRenderer::ClearECSInstances() {
    if (!mesh_instances_.empty()) {
        mesh_instances_.resize(1);
        mesh_instances_[0].transform = ozz::math::Float4x4::identity();
        mesh_instances_[0].bone_matrix_offset = 0;

        const uint32_t bones_per_instance = mesh_renderer_.BonesPerInstance();
        mesh_bone_matrices_.resize(bones_per_instance);
        // Restore bind pose or current animation state
        if (!mesh_bind_pose_palette_.empty()) {
            for (size_t i = 0; i < bones_per_instance && i < mesh_bind_pose_palette_.size(); ++i) {
                mesh_bone_matrices_[i] = mesh_bind_pose_palette_[i];
            }
        }
    }
}

} // namespace renderer
} // namespace animation
} // namespace xray
