#include "VulkanBuffer.h"

namespace xray {
namespace animation {
namespace renderer {

VulkanBuffer::VulkanBuffer() {}
VulkanBuffer::~VulkanBuffer() { Destroy(); }

void VulkanBuffer::Create(VkDevice device, VmaAllocator allocator, VkDeviceSize size,
                          VkBufferUsageFlags usage, VmaMemoryUsage memory_usage) {
    // STUB: To be implemented
}

void VulkanBuffer::Destroy() {
    // STUB: To be implemented
}

void* VulkanBuffer::Map() {
    // STUB: To be implemented
    return nullptr;
}

void VulkanBuffer::Unmap() {
    // STUB: To be implemented
}

void VulkanBuffer::Upload(const void* data, VkDeviceSize size) {
    // STUB: To be implemented
}

} // namespace renderer
} // namespace animation
} // namespace xray
