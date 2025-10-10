#pragma once

#include <vulkan/vulkan.h>
#include <vector>
#include <string>

namespace xray {
namespace animation {
namespace renderer {

class VulkanDevice;

// Configuration for creating a graphics pipeline
struct PipelineConfig {
    // Shader stages
    std::string vertex_shader_path;
    std::string fragment_shader_path;

    // Vertex input (for now, empty - triangle shader has hardcoded vertices)
    std::vector<VkVertexInputBindingDescription> vertex_bindings;
    std::vector<VkVertexInputAttributeDescription> vertex_attributes;

    // Rasterization
    VkPrimitiveTopology topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
    VkPolygonMode polygon_mode = VK_POLYGON_MODE_FILL;
    VkCullModeFlags cull_mode = VK_CULL_MODE_BACK_BIT;
    VkFrontFace front_face = VK_FRONT_FACE_CLOCKWISE;
    float line_width = 1.0f;

    // Depth/stencil
    bool depth_test_enable = true;
    bool depth_write_enable = true;
    VkCompareOp depth_compare_op = VK_COMPARE_OP_LESS;

    // Blending
    bool blend_enable = false;

    // Render pass
    VkRenderPass render_pass = VK_NULL_HANDLE;
    uint32_t subpass = 0;

    // Descriptor set layouts (empty for simple triangle)
    std::vector<VkDescriptorSetLayout> descriptor_set_layouts;
};

// Vulkan graphics pipeline wrapper
class VulkanPipeline {
public:
    VulkanPipeline();
    ~VulkanPipeline();

    bool Create(VkDevice device, const PipelineConfig& config);
    void Destroy();

    void Bind(VkCommandBuffer cmd_buffer);

    VkPipeline GetPipeline() const { return pipeline_; }
    VkPipelineLayout GetLayout() const { return pipeline_layout_; }

private:
    // Helper functions
    VkShaderModule CreateShaderModule(VkDevice device, const std::vector<char>& code);
    std::vector<char> ReadShaderFile(const std::string& filename);

    VkDevice device_ = VK_NULL_HANDLE;
    VkPipeline pipeline_ = VK_NULL_HANDLE;
    VkPipelineLayout pipeline_layout_ = VK_NULL_HANDLE;
};

} // namespace renderer
} // namespace animation
} // namespace xray
