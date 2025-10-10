#include "stdafx.h"

#include "DebugRenderer.h"

#include "VulkanDevice.h"
#include "VulkanPipeline.h"

#include <algorithm>
#include <cstddef>
#include <cmath>
#include <cstring>
#include <filesystem>

namespace xray {
namespace animation {
namespace renderer {

namespace {

constexpr VkDeviceSize kDefaultVertexCapacity = 1024;

} // namespace

VkDeviceSize DebugRenderer::VertexBufferSize(size_t vertex_count) {
    return static_cast<VkDeviceSize>(vertex_count * sizeof(LineVertex));
}

bool DebugRenderer::Initialize(VulkanDevice* device) {
    device_ = device;

    if (!device_) {
        return false;
    }

    if (!CreateDescriptorSetLayout()) {
        return false;
    }

    if (!CreateDescriptorPool()) {
        return false;
    }

    if (!CreateUniformBuffer()) {
        return false;
    }

    if (!EnsureVertexCapacity(kDefaultVertexCapacity)) {
        return false;
    }

    if (!CreatePipeline()) {
        return false;
    }

    UpdateDescriptorSet();
    initialized_ = true;
    return true;
}

void DebugRenderer::Shutdown() {
    if (!device_) {
        return;
    }

    VkDevice vk_device = device_->GetDevice();

    pipeline_.Destroy();

    if (descriptor_pool_) {
        vkDestroyDescriptorPool(vk_device, descriptor_pool_, nullptr);
        descriptor_pool_ = VK_NULL_HANDLE;
    }

    if (descriptor_set_layout_) {
        vkDestroyDescriptorSetLayout(vk_device, descriptor_set_layout_, nullptr);
        descriptor_set_layout_ = VK_NULL_HANDLE;
    }

    uniform_buffer_.Destroy();
    vertex_buffer_.Destroy();

    vertices_.clear();
    vertex_capacity_ = 0;
    buffers_dirty_ = false;
    initialized_ = false;
}

void DebugRenderer::BeginFrame() {
    vertices_.clear();
    buffers_dirty_ = false;
}

void DebugRenderer::DrawLine(const ozz::math::Float3& start, const ozz::math::Float3& end, const ozz::math::Float4& color) {
    LineVertex v0{};
    v0.position[0] = start.x;
    v0.position[1] = start.y;
    v0.position[2] = start.z;
    v0.color[0] = color.x;
    v0.color[1] = color.y;
    v0.color[2] = color.z;
    v0.color[3] = color.w;

    LineVertex v1{};
    v1.position[0] = end.x;
    v1.position[1] = end.y;
    v1.position[2] = end.z;
    v1.color[0] = color.x;
    v1.color[1] = color.y;
    v1.color[2] = color.z;
    v1.color[3] = color.w;

    vertices_.push_back(v0);
    vertices_.push_back(v1);
    buffers_dirty_ = true;
}

void DebugRenderer::DrawAxes(const ozz::math::Float4x4& transform, float scale, const ozz::math::Float4& color_x,
    const ozz::math::Float4& color_y, const ozz::math::Float4& color_z) {
    ozz::math::Float3 origin{};
    ozz::math::Float3 axis_x{};
    ozz::math::Float3 axis_y{};
    ozz::math::Float3 axis_z{};

    ozz::math::Store3PtrU(transform.cols[3], &origin.x);
    ozz::math::Store3PtrU(transform.cols[0], &axis_x.x);
    ozz::math::Store3PtrU(transform.cols[1], &axis_y.x);
    ozz::math::Store3PtrU(transform.cols[2], &axis_z.x);

    axis_x.x *= scale;
    axis_x.y *= scale;
    axis_x.z *= scale;
    axis_y.x *= scale;
    axis_y.y *= scale;
    axis_y.z *= scale;
    axis_z.x *= scale;
    axis_z.y *= scale;
    axis_z.z *= scale;

    DrawLine(origin, ozz::math::Float3{ origin.x + axis_x.x, origin.y + axis_x.y, origin.z + axis_x.z }, color_x);
    DrawLine(origin, ozz::math::Float3{ origin.x + axis_y.x, origin.y + axis_y.y, origin.z + axis_y.z }, color_y);
    DrawLine(origin, ozz::math::Float3{ origin.x + axis_z.x, origin.y + axis_z.y, origin.z + axis_z.z }, color_z);
}

void DebugRenderer::DrawPoint(const ozz::math::Float3& position, float radius, const ozz::math::Float4& color, int segments) {
    segments = std::max(segments, 4);
    const float angle_step = 6.28318530718f / static_cast<float>(segments);

    float current_angle = 0.f;
    for (int i = 0; i < segments; ++i) {
        const float next_angle = current_angle + angle_step;
        const ozz::math::Float3 start{
            position.x + std::cos(current_angle) * radius,
            position.y,
            position.z + std::sin(current_angle) * radius };
        const ozz::math::Float3 end{
            position.x + std::cos(next_angle) * radius,
            position.y,
            position.z + std::sin(next_angle) * radius };
        DrawLine(start, end, color);
        current_angle = next_angle;
    }
}

void DebugRenderer::DrawSphere(const ozz::math::Float3& center, float radius, const ozz::math::Float4& color, int segments) {
    segments = std::max(segments, 8);
    const float angle_step = 6.28318530718f / static_cast<float>(segments);

    auto draw_ring = [&](auto sample_point) {
        float current = 0.f;
        for (int i = 0; i < segments; ++i) {
            const float next = current + angle_step;
            const ozz::math::Float3 start = sample_point(current);
            const ozz::math::Float3 end = sample_point(next);
            DrawLine(start, end, color);
            current = next;
        }
    };

    draw_ring([&](float angle) {
        return ozz::math::Float3{
            center.x + std::cos(angle) * radius,
            center.y + std::sin(angle) * radius,
            center.z };
    });

    draw_ring([&](float angle) {
        return ozz::math::Float3{
            center.x + std::cos(angle) * radius,
            center.y,
            center.z + std::sin(angle) * radius };
    });

    draw_ring([&](float angle) {
        return ozz::math::Float3{
            center.x,
            center.y + std::cos(angle) * radius,
            center.z + std::sin(angle) * radius };
    });
}

void DebugRenderer::EndFrame() {
    if (!buffers_dirty_) {
        return;
    }

    const size_t vertex_count = vertices_.size();
    if (!EnsureVertexCapacity(vertex_count)) {
        return;
    }

    if (vertex_count == 0) {
        buffers_dirty_ = false;
        return;
    }

    vertex_buffer_.Upload(vertices_.data(), VertexBufferSize(vertex_count));
    buffers_dirty_ = false;
}

void DebugRenderer::Render(VkCommandBuffer cmd, const ozz::math::Float4x4& view_proj) {
    if (!initialized_ || vertices_.empty()) {
        return;
    }

    UpdateUniforms(view_proj);

    pipeline_.Bind(cmd);
    VkBuffer buffers[] = { vertex_buffer_.GetBuffer() };
    VkDeviceSize offsets[] = { 0 };
    vkCmdBindVertexBuffers(cmd, 0, 1, buffers, offsets);
    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline_.GetLayout(), 0, 1, &descriptor_set_, 0, nullptr);
    vkCmdDraw(cmd, static_cast<uint32_t>(vertices_.size()), 1, 0, 0);
}

bool DebugRenderer::EnsureVertexCapacity(size_t vertex_count) {
    if (vertex_count <= vertex_capacity_) {
        return true;
    }

    size_t new_capacity = std::max(vertex_count, vertex_capacity_ * 2);
    if (new_capacity == 0) {
        new_capacity = static_cast<size_t>(kDefaultVertexCapacity);
    }

    vertex_buffer_.Destroy();
    vertex_buffer_.Create(device_->GetDevice(), device_->GetAllocator(), VertexBufferSize(new_capacity),
        VK_BUFFER_USAGE_VERTEX_BUFFER_BIT, VMA_MEMORY_USAGE_CPU_TO_GPU);

    if (vertex_buffer_.GetBuffer() == VK_NULL_HANDLE) {
        return false;
    }

    vertex_capacity_ = new_capacity;
    buffers_dirty_ = true;
    return true;
}

bool DebugRenderer::CreateDescriptorSetLayout() {
    VkDescriptorSetLayoutBinding binding{};
    binding.binding = 0;
    binding.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    binding.descriptorCount = 1;
    binding.stageFlags = VK_SHADER_STAGE_VERTEX_BIT;

    VkDescriptorSetLayoutCreateInfo layout_info{};
    layout_info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    layout_info.bindingCount = 1;
    layout_info.pBindings = &binding;

    VkResult result = vkCreateDescriptorSetLayout(device_->GetDevice(), &layout_info, nullptr, &descriptor_set_layout_);
    return result == VK_SUCCESS;
}

bool DebugRenderer::CreateDescriptorPool() {
    VkDescriptorPoolSize pool_size{};
    pool_size.type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    pool_size.descriptorCount = 1;

    VkDescriptorPoolCreateInfo pool_info{};
    pool_info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    pool_info.poolSizeCount = 1;
    pool_info.pPoolSizes = &pool_size;
    pool_info.maxSets = 1;

    VkResult result = vkCreateDescriptorPool(device_->GetDevice(), &pool_info, nullptr, &descriptor_pool_);
    if (result != VK_SUCCESS) {
        return false;
    }

    VkDescriptorSetAllocateInfo alloc_info{};
    alloc_info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    alloc_info.descriptorPool = descriptor_pool_;
    alloc_info.descriptorSetCount = 1;
    alloc_info.pSetLayouts = &descriptor_set_layout_;

    result = vkAllocateDescriptorSets(device_->GetDevice(), &alloc_info, &descriptor_set_);
    return result == VK_SUCCESS;
}

bool DebugRenderer::CreateUniformBuffer() {
    uniform_buffer_.Create(device_->GetDevice(), device_->GetAllocator(), sizeof(ozz::math::Float4x4),
        VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT, VMA_MEMORY_USAGE_CPU_TO_GPU);
    return uniform_buffer_.GetBuffer() != VK_NULL_HANDLE;
}

bool DebugRenderer::CreatePipeline() {
    PipelineConfig config{};

#ifdef OZZ_SHADER_BINARY_DIR
    std::filesystem::path shader_root{ OZZ_SHADER_BINARY_DIR };
    config.vertex_shader_path = (shader_root / "debug_lines.vert.spv").string();
    config.fragment_shader_path = (shader_root / "debug_lines.frag.spv").string();
#else
    config.vertex_shader_path = "src/xrAnimation/tools/renderer/shaders/debug_lines.vert.spv";
    config.fragment_shader_path = "src/xrAnimation/tools/renderer/shaders/debug_lines.frag.spv";
#endif

    config.vertex_bindings.resize(1);
    config.vertex_bindings[0].binding = 0;
    config.vertex_bindings[0].stride = sizeof(LineVertex);
    config.vertex_bindings[0].inputRate = VK_VERTEX_INPUT_RATE_VERTEX;

    config.vertex_attributes.resize(2);
    config.vertex_attributes[0].location = 0;
    config.vertex_attributes[0].binding = 0;
    config.vertex_attributes[0].format = VK_FORMAT_R32G32B32_SFLOAT;
    config.vertex_attributes[0].offset = offsetof(LineVertex, position);

    config.vertex_attributes[1].location = 1;
    config.vertex_attributes[1].binding = 0;
    config.vertex_attributes[1].format = VK_FORMAT_R32G32B32A32_SFLOAT;
    config.vertex_attributes[1].offset = offsetof(LineVertex, color);

    config.topology = VK_PRIMITIVE_TOPOLOGY_LINE_LIST;
    config.polygon_mode = VK_POLYGON_MODE_LINE;
    config.cull_mode = VK_CULL_MODE_NONE;
    config.front_face = VK_FRONT_FACE_COUNTER_CLOCKWISE;
    config.depth_test_enable = true;
    config.depth_write_enable = false;
    config.blend_enable = true;
    config.line_width = 1.0f;
    config.render_pass = device_->GetRenderPass();
    config.subpass = 0;
    config.descriptor_set_layouts.push_back(descriptor_set_layout_);

    return pipeline_.Create(device_->GetDevice(), config);
}

void DebugRenderer::UpdateDescriptorSet() {
    VkDescriptorBufferInfo buffer_info{};
    buffer_info.buffer = uniform_buffer_.GetBuffer();
    buffer_info.offset = 0;
    buffer_info.range = sizeof(ozz::math::Float4x4);

    VkWriteDescriptorSet write{};
    write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    write.dstSet = descriptor_set_;
    write.dstBinding = 0;
    write.dstArrayElement = 0;
    write.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    write.descriptorCount = 1;
    write.pBufferInfo = &buffer_info;

    vkUpdateDescriptorSets(device_->GetDevice(), 1, &write, 0, nullptr);
}

void DebugRenderer::UpdateUniforms(const ozz::math::Float4x4& view_proj) {
    uniform_buffer_.Upload(&view_proj, sizeof(ozz::math::Float4x4));
}

} // namespace renderer
} // namespace animation
} // namespace xray
