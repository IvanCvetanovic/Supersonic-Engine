#pragma once

#include <optional>
#include <vector>
#include <set>
#include <string>

#include <vulkan/vulkan.hpp>
#include <vk_mem_alloc.h>

#include "platform/Window.hpp"

namespace Supersonic {

struct QueueFamilyIndices {
    std::optional<uint32_t> graphicsFamily;
    std::optional<uint32_t> presentFamily;

    bool isComplete() const {
        return graphicsFamily.has_value() && presentFamily.has_value();
    }
};

struct SwapChainSupportDetails {
    vk::SurfaceCapabilitiesKHR capabilities;
    std::vector<vk::SurfaceFormatKHR> formats;
    std::vector<vk::PresentModeKHR> presentModes;
};

class VulkanDevice {
public:
    // The instance requests this version and VMA is configured for it, so a
    // physical device below it is rejected during selection rather than
    // failing later inside VMA's statically bound 1.1 entry points.
    static constexpr uint32_t kRequiredApiVersion = VK_API_VERSION_1_2;

    VulkanDevice(vk::Instance instance, Window& window);
    ~VulkanDevice();

    VulkanDevice(const VulkanDevice&) = delete;
    VulkanDevice& operator=(const VulkanDevice&) = delete;

    vk::Instance GetInstance() const { return m_instance; }
    vk::SurfaceKHR GetSurface() const { return m_surface; }
    vk::PhysicalDevice GetPhysicalDevice() const { return m_physicalDevice; }
    vk::Device GetDevice() const { return m_device; }
    vk::Queue GetGraphicsQueue() const { return m_graphicsQueue; }
    vk::Queue GetPresentQueue() const { return m_presentQueue; }
    QueueFamilyIndices GetQueueFamilyIndices() const { return m_queueFamilyIndices; }
    VmaAllocator GetAllocator() const { return m_allocator; }

    SwapChainSupportDetails QuerySwapChainSupport() const { return querySwapChainSupport(m_physicalDevice); }
    QueueFamilyIndices FindQueueFamilies() const { return findQueueFamilies(m_physicalDevice); }

    vk::Format FindSupportedFormat(
        const std::vector<vk::Format>& candidates,
        vk::ImageTiling tiling,
        vk::FormatFeatureFlags features
    ) const;
    vk::Format FindDepthFormat() const;

private:
    void createSurface(Window& window);
    void pickPhysicalDevice();
    void createLogicalDevice();
    void initVMA();

    bool isDeviceSuitable(vk::PhysicalDevice device);
    bool checkDeviceExtensionSupport(vk::PhysicalDevice device);
    QueueFamilyIndices findQueueFamilies(vk::PhysicalDevice device) const;
    SwapChainSupportDetails querySwapChainSupport(vk::PhysicalDevice device) const;

    vk::Instance m_instance{nullptr};
    vk::SurfaceKHR m_surface{nullptr};
    vk::PhysicalDevice m_physicalDevice{nullptr};
    vk::Device m_device{nullptr};

    vk::Queue m_graphicsQueue{nullptr};
    vk::Queue m_presentQueue{nullptr};
    QueueFamilyIndices m_queueFamilyIndices;

    VmaAllocator m_allocator{VK_NULL_HANDLE};

    const std::vector<const char*> m_deviceExtensions = {
        VK_KHR_SWAPCHAIN_EXTENSION_NAME
    };
};

} // namespace Supersonic
