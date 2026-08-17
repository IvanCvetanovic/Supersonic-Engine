#pragma once

#include <vulkan/vulkan.hpp>
#include <vk_mem_alloc.h>

#include "renderer/VulkanDevice.hpp"

namespace Supersonic {

class VulkanBuffer {
public:
    VulkanBuffer(
        VmaAllocator allocator,
        vk::DeviceSize size,
        vk::BufferUsageFlags usage,
        VmaMemoryUsage memoryUsage,
        VmaAllocationCreateFlags allocationFlags = 0
    );
    ~VulkanBuffer();

    VulkanBuffer(const VulkanBuffer&) = delete;
    VulkanBuffer& operator=(const VulkanBuffer&) = delete;

    vk::Buffer GetBuffer() const { return m_buffer; }
    VmaAllocation GetAllocation() const { return m_allocation; }
    vk::DeviceSize GetSize() const { return m_size; }
    void* GetMappedData() const { return m_allocationInfo.pMappedData; }

    void Map(void** data);
    void Unmap();
    void UploadData(const void* data, vk::DeviceSize size);

    static void CopyBuffer(
        VulkanDevice& device,
        vk::CommandPool commandPool,
        vk::Buffer srcBuffer,
        vk::Buffer dstBuffer,
        vk::DeviceSize size
    );

private:
    VmaAllocator m_allocator{VK_NULL_HANDLE};
    vk::Buffer m_buffer{nullptr};
    VmaAllocation m_allocation{VK_NULL_HANDLE};
    VmaAllocationInfo m_allocationInfo{};
    vk::DeviceSize m_size{0};
};

} // namespace Supersonic
