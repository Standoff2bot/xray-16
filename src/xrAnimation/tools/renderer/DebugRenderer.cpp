#include "stdafx.h"

#include "DebugRenderer.h"

#include "VulkanDevice.h"
#include "VulkanPipeline.h"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cmath>
#include <cstring>
#include <filesystem>

#include "../../ExtendedBoneMetadata.h"

namespace xray {
namespace animation {
namespace renderer {

namespace {

constexpr VkDeviceSize kDefaultVertexCapacity = 1024;
constexpr float kTwoPi = 6.28318530718f;
constexpr float kEpsilon = 1e-5f;

inline ozz::math::Float3 ToFloat3(const Fvector3& v) {
    return ozz::math::Float3{ v.x, v.y, v.z };
}

inline ozz::math::Float3 Add(const ozz::math::Float3& a, const ozz::math::Float3& b) {
    return ozz::math::Float3{ a.x + b.x, a.y + b.y, a.z + b.z };
}

inline ozz::math::Float3 Sub(const ozz::math::Float3& a, const ozz::math::Float3& b) {
    return ozz::math::Float3{ a.x - b.x, a.y - b.y, a.z - b.z };
}

inline ozz::math::Float3 Scale(const ozz::math::Float3& v, float s) {
    return ozz::math::Float3{ v.x * s, v.y * s, v.z * s };
}

inline ozz::math::Float3 TransformPoint(const ozz::math::Float4x4& m, const ozz::math::Float3& p) {
    const ozz::math::SimdFloat4 point = ozz::math::simd_float4::Load3PtrU(&p.x);
    const ozz::math::SimdFloat4 transformed = ozz::math::TransformPoint(m, point);
    ozz::math::Float3 result;
    ozz::math::Store3PtrU(transformed, &result.x);
    return result;
}

inline ozz::math::Float3 TransformVector(const ozz::math::Float4x4& m, const ozz::math::Float3& v) {
    const ozz::math::SimdFloat4 vec = ozz::math::simd_float4::Load3PtrU(&v.x);
    const ozz::math::SimdFloat4 transformed = ozz::math::TransformVector(m, vec);
    ozz::math::Float3 result;
    ozz::math::Store3PtrU(transformed, &result.x);
    return result;
}

inline float ColumnLength(const ozz::math::SimdFloat4& column) {
    float values[4];
    ozz::math::StorePtrU(column, values);
    return std::sqrt(values[0] * values[0] + values[1] * values[1] + values[2] * values[2]);
}

using ozz::math::Cross;
using ozz::math::NormalizeSafe;

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

    if (!EnsureLineCapacity(kDefaultVertexCapacity)) {
        return false;
    }

    if (!EnsureSolidCapacity(kDefaultVertexCapacity)) {
        return false;
    }

    if (!CreateLinePipeline()) {
        return false;
    }

    if (!CreateSolidPipeline()) {
        return false;
    }

    UpdateDescriptorSet();
    initialized_ = true;
    return true;
}

void DebugRenderer::Shutdown() {
    if (!device_ || device_->GetDevice() == VK_NULL_HANDLE) {
        return;
    }

    VkDevice vk_device = device_->GetDevice();

    line_pipeline_.Destroy();
    solid_pipeline_.Destroy();

    if (descriptor_pool_) {
        vkDestroyDescriptorPool(vk_device, descriptor_pool_, nullptr);
        descriptor_pool_ = VK_NULL_HANDLE;
    }

    if (descriptor_set_layout_) {
        vkDestroyDescriptorSetLayout(vk_device, descriptor_set_layout_, nullptr);
        descriptor_set_layout_ = VK_NULL_HANDLE;
    }

    uniform_buffer_.Destroy();
    line_vertex_buffer_.Destroy();
    solid_vertex_buffer_.Destroy();

    line_vertices_.clear();
    solid_vertices_.clear();
    line_vertex_capacity_ = 0;
    solid_vertex_capacity_ = 0;
    line_buffer_dirty_ = false;
    solid_buffer_dirty_ = false;
    initialized_ = false;
}

void DebugRenderer::BeginFrame() {
    line_vertices_.clear();
    solid_vertices_.clear();
    line_buffer_dirty_ = false;
    solid_buffer_dirty_ = false;
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

    line_vertices_.push_back(v0);
    line_vertices_.push_back(v1);
    line_buffer_dirty_ = true;
}

void DebugRenderer::DrawAxes(const ozz::math::Float4x4& transform, float scale, const ozz::math::Float4& color_x,
    const ozz::math::Float4& color_y, const ozz::math::Float4& color_z) {
    ozz::math::Float3 origin;
    ozz::math::Float3 axis_x;
    ozz::math::Float3 axis_y;
    ozz::math::Float3 axis_z;

    ozz::math::Store3PtrU(transform.cols[3], &origin.x);
    ozz::math::Store3PtrU(transform.cols[0], &axis_x.x);
    ozz::math::Store3PtrU(transform.cols[1], &axis_y.x);
    ozz::math::Store3PtrU(transform.cols[2], &axis_z.x);

    axis_x = Scale(axis_x, scale);
    axis_y = Scale(axis_y, scale);
    axis_z = Scale(axis_z, scale);

    DrawLine(origin, Add(origin, axis_x), color_x);
    DrawLine(origin, Add(origin, axis_y), color_y);
    DrawLine(origin, Add(origin, axis_z), color_z);
}

void DebugRenderer::DrawPoint(const ozz::math::Float3& position, float radius, const ozz::math::Float4& color, int segments) {
    segments = std::max(segments, 4);
    const float angle_step = kTwoPi / static_cast<float>(segments);

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
    const float angle_step = kTwoPi / static_cast<float>(segments);

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

void DebugRenderer::DrawOrientedBox(const ozz::math::Float4x4& transform, const Fobb& obb, const ozz::math::Float4& color) {
    const ozz::math::Float3 center = ToFloat3(obb.m_translate);
    const ozz::math::Float3 axis_x = Scale(ToFloat3(obb.m_rotate.i), obb.m_halfsize.x);
    const ozz::math::Float3 axis_y = Scale(ToFloat3(obb.m_rotate.j), obb.m_halfsize.y);
    const ozz::math::Float3 axis_z = Scale(ToFloat3(obb.m_rotate.k), obb.m_halfsize.z);

    std::array<ozz::math::Float3, 8> corners{};
    int index = 0;
    for (int sx = -1; sx <= 1; sx += 2) {
        for (int sy = -1; sy <= 1; sy += 2) {
            for (int sz = -1; sz <= 1; sz += 2) {
                ozz::math::Float3 local = center;
                local = Add(local, Scale(axis_x, static_cast<float>(sx)));
                local = Add(local, Scale(axis_y, static_cast<float>(sy)));
                local = Add(local, Scale(axis_z, static_cast<float>(sz)));
                corners[index++] = TransformPoint(transform, local);
            }
        }
    }

    static constexpr int edges[12][2] = {
        {0, 1}, {0, 2}, {0, 4}, {1, 3},
        {1, 5}, {2, 3}, {2, 6}, {3, 7},
        {4, 5}, {4, 6}, {5, 7}, {6, 7},
    };

    for (const auto& edge : edges) {
        DrawLine(corners[edge[0]], corners[edge[1]], color);
    }
}

void DebugRenderer::DrawSphereShape(const ozz::math::Float4x4& transform, const Fsphere& sphere,
    const ozz::math::Float4& color, int segments) {
    const ozz::math::Float3 center_local = ToFloat3(sphere.P);
    const ozz::math::Float3 center_world = TransformPoint(transform, center_local);

    const float sx = ColumnLength(transform.cols[0]);
    const float sy = ColumnLength(transform.cols[1]);
    const float sz = ColumnLength(transform.cols[2]);
    const float uniform_scale = (sx + sy + sz) / 3.f;

    DrawSphere(center_world, sphere.R * uniform_scale, color, segments);
}

void DebugRenderer::DrawCapsuleShape(const ozz::math::Float4x4& transform, const Fcylinder& cylinder,
    const ozz::math::Float4& color, int segments) {
    segments = std::max(segments, 8);

    ozz::math::Float3 axis_dir = NormalizeSafe(ToFloat3(cylinder.m_direction), ozz::math::Float3::z_axis());
    const float half_height = std::max(0.f, cylinder.m_height * 0.5f);

    const ozz::math::Float3 center = ToFloat3(cylinder.m_center);
    const ozz::math::Float3 top_center = Add(center, Scale(axis_dir, half_height));
    const ozz::math::Float3 bottom_center = Sub(center, Scale(axis_dir, half_height));

    ozz::math::Float3 tangent = Cross(axis_dir, ozz::math::Float3::y_axis());
    if (Length(tangent) <= kEpsilon) {
        tangent = Cross(axis_dir, ozz::math::Float3::x_axis());
    }
    tangent = NormalizeSafe(tangent, ozz::math::Float3::x_axis());
    ozz::math::Float3 bitangent = NormalizeSafe(Cross(axis_dir, tangent), ozz::math::Float3::z_axis());

    const float angle_step = kTwoPi / static_cast<float>(segments);

    ozz::math::Float3 first_top{};
    ozz::math::Float3 prev_top{};
    ozz::math::Float3 first_bottom{};
    ozz::math::Float3 prev_bottom{};
    bool first_point = true;

    for (int i = 0; i < segments; ++i) {
        const float angle = angle_step * static_cast<float>(i);
        const float cos_a = std::cos(angle);
        const float sin_a = std::sin(angle);
        const ozz::math::Float3 offset =
            Add(Scale(tangent, cos_a * cylinder.m_radius), Scale(bitangent, sin_a * cylinder.m_radius));

        const ozz::math::Float3 top_world = TransformPoint(transform, Add(top_center, offset));
        const ozz::math::Float3 bottom_world = TransformPoint(transform, Add(bottom_center, offset));

        if (first_point) {
            first_top = top_world;
            first_bottom = bottom_world;
            first_point = false;
        } else {
            DrawLine(prev_top, top_world, color);
            DrawLine(prev_bottom, bottom_world, color);
        }

        DrawLine(top_world, bottom_world, color);

        prev_top = top_world;
        prev_bottom = bottom_world;
    }

    if (!first_point) {
        DrawLine(prev_top, first_top, color);
        DrawLine(prev_bottom, first_bottom, color);
    }

    const ozz::math::Float3 top_world = TransformPoint(transform, top_center);
    const ozz::math::Float3 bottom_world = TransformPoint(transform, bottom_center);
    DrawLine(top_world, bottom_world, color);
}

void DebugRenderer::DrawBoneShape(const XRay::Animation::ExtendedBoneMetadata& metadata,
    const ozz::math::Float4x4& local_to_world, const ozz::math::Float4& color, int segments) {
    switch (metadata.shape.type) {
    case SBoneShape::stBox:
        DrawOrientedBox(local_to_world, metadata.shape.box, color);
        break;
    case SBoneShape::stSphere:
        DrawSphereShape(local_to_world, metadata.shape.sphere, color, segments);
        break;
    case SBoneShape::stCylinder:
        DrawCapsuleShape(local_to_world, metadata.shape.cylinder, color, segments);
        break;
    default:
    {
        float length = metadata.rest_length;
        if (length <= kEpsilon) {
            Fvector diag;
            diag.sub(metadata.local_aabb_max, metadata.local_aabb_min);
            length = diag.magnitude();
        }
        if (length <= kEpsilon) {
            length = 0.1f;
        }

        const float mid_radius = std::max(length * 0.18f, 0.015f);
        const float mid_offset = length * 0.5f;

        const std::array<ozz::math::Float3, 6> local_points = {
            ozz::math::Float3{0.f, 0.f, 0.f},
            ozz::math::Float3{length, 0.f, 0.f},
            ozz::math::Float3{mid_offset, mid_radius, 0.f},
            ozz::math::Float3{mid_offset, -mid_radius, 0.f},
            ozz::math::Float3{mid_offset, 0.f, mid_radius},
            ozz::math::Float3{mid_offset, 0.f, -mid_radius},
        };

        std::array<ozz::math::Float3, local_points.size()> world_points{};
        for (size_t i = 0; i < local_points.size(); ++i) {
            world_points[i] = TransformPoint(local_to_world, local_points[i]);
        }

        auto push_triangle = [&](int ia, int ib, int ic) {
            SolidVertex va{};
            va.position[0] = world_points[ia].x;
            va.position[1] = world_points[ia].y;
            va.position[2] = world_points[ia].z;
            va.color[0] = color.x;
            va.color[1] = color.y;
            va.color[2] = color.z;
            va.color[3] = color.w;

            SolidVertex vb = va;
            vb.position[0] = world_points[ib].x;
            vb.position[1] = world_points[ib].y;
            vb.position[2] = world_points[ib].z;

            SolidVertex vc = va;
            vc.position[0] = world_points[ic].x;
            vc.position[1] = world_points[ic].y;
            vc.position[2] = world_points[ic].z;

            solid_vertices_.push_back(va);
            solid_vertices_.push_back(vb);
            solid_vertices_.push_back(vc);
            solid_buffer_dirty_ = true;
        };

        // Root pyramid
        push_triangle(0, 2, 4);
        push_triangle(0, 4, 3);
        push_triangle(0, 3, 5);
        push_triangle(0, 5, 2);

        // Tip pyramid
        push_triangle(1, 4, 2);
        push_triangle(1, 3, 4);
        push_triangle(1, 5, 3);
        push_triangle(1, 2, 5);

        constexpr int ring_edges[][2] = {
            {2, 4}, {4, 3}, {3, 5}, {5, 2},
        };

        for (const auto& edge : ring_edges) {
            DrawLine(world_points[edge[0]], world_points[edge[1]], color);
        }

        for (int i = 2; i <= 5; ++i) {
            DrawLine(world_points[0], world_points[i], color);
            DrawLine(world_points[1], world_points[i], color);
        }

        DrawLine(world_points[0], world_points[1], color);
        break;
    }
    }
}

void DebugRenderer::EndFrame() {
    if (line_buffer_dirty_) {
        const size_t vertex_count = line_vertices_.size();
        if (EnsureLineCapacity(vertex_count) && vertex_count > 0) {
            line_vertex_buffer_.Upload(line_vertices_.data(), VertexBufferSize(vertex_count));
        }
        line_buffer_dirty_ = false;
    }

    if (solid_buffer_dirty_) {
        const size_t vertex_count = solid_vertices_.size();
        if (EnsureSolidCapacity(vertex_count) && vertex_count > 0) {
            const VkDeviceSize upload_size = static_cast<VkDeviceSize>(vertex_count * sizeof(SolidVertex));
            solid_vertex_buffer_.Upload(solid_vertices_.data(), upload_size);
        }
        solid_buffer_dirty_ = false;
    }
}

void DebugRenderer::Render(VkCommandBuffer cmd, const ozz::math::Float4x4& view_proj) {
    if (!initialized_) {
        return;
    }

    UpdateUniforms(view_proj);

    if (!line_vertices_.empty()) {
        line_pipeline_.Bind(cmd);
        VkBuffer buffers[] = { line_vertex_buffer_.GetBuffer() };
        VkDeviceSize offsets[] = { 0 };
        vkCmdBindVertexBuffers(cmd, 0, 1, buffers, offsets);
        vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, line_pipeline_.GetLayout(), 0, 1, &descriptor_set_, 0, nullptr);
        vkCmdDraw(cmd, static_cast<uint32_t>(line_vertices_.size()), 1, 0, 0);
    }

    if (!solid_vertices_.empty()) {
        solid_pipeline_.Bind(cmd);
        VkBuffer buffers[] = { solid_vertex_buffer_.GetBuffer() };
        VkDeviceSize offsets[] = { 0 };
        vkCmdBindVertexBuffers(cmd, 0, 1, buffers, offsets);
        vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, solid_pipeline_.GetLayout(), 0, 1, &descriptor_set_, 0, nullptr);
        vkCmdDraw(cmd, static_cast<uint32_t>(solid_vertices_.size()), 1, 0, 0);
    }
}

bool DebugRenderer::EnsureLineCapacity(size_t vertex_count) {
    if (vertex_count <= line_vertex_capacity_) {
        return true;
    }

    size_t new_capacity = std::max(vertex_count, line_vertex_capacity_ * 2);
    if (new_capacity == 0) {
        new_capacity = static_cast<size_t>(kDefaultVertexCapacity);
    }

    line_vertex_buffer_.Destroy();
    line_vertex_buffer_.Create(device_->GetDevice(), device_->GetAllocator(), VertexBufferSize(new_capacity),
        VK_BUFFER_USAGE_VERTEX_BUFFER_BIT, VMA_MEMORY_USAGE_CPU_TO_GPU);

    if (line_vertex_buffer_.GetBuffer() == VK_NULL_HANDLE) {
        return false;
    }

    line_vertex_capacity_ = new_capacity;
    return true;
}

bool DebugRenderer::EnsureSolidCapacity(size_t vertex_count) {
    if (vertex_count <= solid_vertex_capacity_) {
        return true;
    }

    size_t new_capacity = std::max(vertex_count, solid_vertex_capacity_ * 2);
    if (new_capacity == 0) {
        new_capacity = static_cast<size_t>(kDefaultVertexCapacity);
    }

    solid_vertex_buffer_.Destroy();
    const VkDeviceSize buffer_size = static_cast<VkDeviceSize>(new_capacity * sizeof(SolidVertex));
    solid_vertex_buffer_.Create(device_->GetDevice(), device_->GetAllocator(), buffer_size,
        VK_BUFFER_USAGE_VERTEX_BUFFER_BIT, VMA_MEMORY_USAGE_CPU_TO_GPU);

    if (solid_vertex_buffer_.GetBuffer() == VK_NULL_HANDLE) {
        return false;
    }

    solid_vertex_capacity_ = new_capacity;
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

bool DebugRenderer::CreateLinePipeline() {
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

    return line_pipeline_.Create(device_->GetDevice(), config);
}

bool DebugRenderer::CreateSolidPipeline() {
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
    config.vertex_bindings[0].stride = sizeof(SolidVertex);
    config.vertex_bindings[0].inputRate = VK_VERTEX_INPUT_RATE_VERTEX;

    config.vertex_attributes.resize(2);
    config.vertex_attributes[0].location = 0;
    config.vertex_attributes[0].binding = 0;
    config.vertex_attributes[0].format = VK_FORMAT_R32G32B32_SFLOAT;
    config.vertex_attributes[0].offset = offsetof(SolidVertex, position);

    config.vertex_attributes[1].location = 1;
    config.vertex_attributes[1].binding = 0;
    config.vertex_attributes[1].format = VK_FORMAT_R32G32B32A32_SFLOAT;
    config.vertex_attributes[1].offset = offsetof(SolidVertex, color);

    config.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
    config.polygon_mode = VK_POLYGON_MODE_FILL;
    config.cull_mode = VK_CULL_MODE_NONE;
    config.front_face = VK_FRONT_FACE_COUNTER_CLOCKWISE;
    config.depth_test_enable = true;
    config.depth_write_enable = false;
    config.blend_enable = true;
    config.render_pass = device_->GetRenderPass();
    config.subpass = 0;
    config.descriptor_set_layouts.push_back(descriptor_set_layout_);

    return solid_pipeline_.Create(device_->GetDevice(), config);
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
