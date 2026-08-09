#include "renderer/VulkanBuffer.hpp"

#include <cstring>
#include <iostream>
#include <stdexcept>

namespace Engine {

VulkanBuffer::VulkanBuffer(
    VmaAllocator allocator,
    vk::DeviceSize size,
    vk::BufferUsageFlags usage,
    VmaMemoryUsage memoryUsage,
    VmaAllocationCreateFlags allocationFlags)
    : m_allocator(allocator), m_size(size) {

    if (m_allocator == VK_NULL_HANDLE) {
        throw std::runtime_error("Cannot create VulkanBuffer with null VmaAllocator!");
    }

    VkBufferCreateInfo bufferInfo{};
    bufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    bufferInfo.size = static_cast<VkDeviceSize>(m_size);
    bufferInfo.usage = static_cast<VkBufferUsageFlags>(usage);
    bufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

    VmaAllocationCreateInfo allocInfo{};
    allocInfo.usage = memoryUsage;
    allocInfo.flags = allocationFlags;

    VkBuffer rawBuffer = VK_NULL_HANDLE;
    VkResult result = vmaCreateBuffer(
        m_allocator,
        &bufferInfo,
        &allocInfo,
        &rawBuffer,
        &m_allocation,
        &m_allocationInfo
    );

    if (result != VK_SUCCESS) {
        throw std::runtime_error("Failed to allocate buffer via VMA! VkResult: " + std::to_string(static_cast<int>(result)));
    }

    m_buffer = rawBuffer;
}

VulkanBuffer::~VulkanBuffer() {
    if (m_buffer && m_allocation && m_allocator) {
        vmaDestroyBuffer(m_allocator, static_cast<VkBuffer>(m_buffer), m_allocation);
        m_buffer = nullptr;
        m_allocation = VK_NULL_HANDLE;
    }
}

void VulkanBuffer::Map(void** data) {
    VkResult result = vmaMapMemory(m_allocator, m_allocation, data);
    if (result != VK_SUCCESS) {
        throw std::runtime_error("Failed to map VMA memory!");
    }
}

void VulkanBuffer::Unmap() {
    vmaUnmapMemory(m_allocator, m_allocation);
}

void VulkanBuffer::UploadData(const void* data, vk::DeviceSize size) {
    if (size > m_size) {
        throw std::runtime_error("Attempted to upload data larger than VMA buffer size!");
    }

    if (m_allocationInfo.pMappedData != nullptr) {
        std::memcpy(m_allocationInfo.pMappedData, data, static_cast<size_t>(size));
    } else {
        void* mappedPtr = nullptr;
        Map(&mappedPtr);
        std::memcpy(mappedPtr, data, static_cast<size_t>(size));
        Unmap();
    }
}

void VulkanBuffer::CopyBuffer(
    VulkanDevice& device,
    vk::CommandPool commandPool,
    vk::Buffer srcBuffer,
    vk::Buffer dstBuffer,
    vk::DeviceSize size) {

    vk::CommandBufferAllocateInfo allocInfo{};
    allocInfo.level = vk::CommandBufferLevel::ePrimary;
    allocInfo.commandPool = commandPool;
    allocInfo.commandBufferCount = 1;

    vk::CommandBuffer commandBuffer = device.GetDevice().allocateCommandBuffers(allocInfo)[0];

    vk::CommandBufferBeginInfo beginInfo{};
    beginInfo.flags = vk::CommandBufferUsageFlagBits::eOneTimeSubmit;
    commandBuffer.begin(beginInfo);

    vk::BufferCopy copyRegion{};
    copyRegion.srcOffset = 0;
    copyRegion.dstOffset = 0;
    copyRegion.size = size;

    commandBuffer.copyBuffer(srcBuffer, dstBuffer, 1, &copyRegion);
    commandBuffer.end();

    vk::SubmitInfo submitInfo{};
    submitInfo.commandBufferCount = 1;
    submitInfo.pCommandBuffers = &commandBuffer;

    vk::Result submitRes = device.GetGraphicsQueue().submit(1, &submitInfo, nullptr);
    if (submitRes != vk::Result::eSuccess) {
        throw std::runtime_error("Failed to submit buffer copy command buffer!");
    }

    device.GetGraphicsQueue().waitIdle();
    device.GetDevice().freeCommandBuffers(commandPool, 1, &commandBuffer);
}

} // namespace Engine
