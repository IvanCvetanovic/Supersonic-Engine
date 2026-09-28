#pragma once

#include <functional>

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

    // What the physical device reported, so callers can ask before using an
    // optional capability rather than assuming it.
    const vk::PhysicalDeviceFeatures& GetFeatures() const { return m_features; }

    // ---- Deferred destruction -------------------------------------------
    //
    // A GPU resource cannot be destroyed the moment nothing in the scene refers
    // to it: a command buffer submitted one or two frames ago may still name it,
    // and the driver is still reading it. Destroying it there is a
    // use-after-free that no validation layer necessarily catches, because the
    // handle was legal when it was recorded.
    //
    // Both registries were write-once precisely so this problem never arose -
    // nothing was ever freed, and they grew for the lifetime of the process.
    // Anything that wants to reload a texture or drop a mission's terrain needs
    // this first.
    //
    // Deleters run once the frame they were queued on is provably complete,
    // which the renderer establishes by having waited on that frame's fence.
    void DeferDestroy(std::function<void()> deleter);

    // Called by the renderer once per frame with the number of the newest frame
    // whose fence has been waited on. Everything queued at or before it is safe.
    void CollectGarbage(uint64_t completedFrame);

    void SetFrameNumber(uint64_t frame) { m_frameNumber = frame; }
    uint64_t FrameNumber() const { return m_frameNumber; }

    // Runs every pending deleter regardless of frame. Only safe after a
    // waitIdle; used at shutdown.
    void FlushDeferredDestroys();

    size_t PendingDestroyCount() const { return m_pendingDestroys.size(); }

    bool SupportsAnisotropy() const { return m_features.samplerAnisotropy == VK_TRUE; }

    // Clamped to the device's limit. Requesting more than maxSamplerAnisotropy
    // is invalid usage, not a request the driver quietly rounds down.
    float MaxAnisotropy() const {
        const float limit = m_physicalDevice.getProperties().limits.maxSamplerAnisotropy;
        return limit < 16.0f ? limit : 16.0f;
    }
    vk::Device GetDevice() const { return m_device; }

    // Highest sample count supported for BOTH colour and depth, capped at the
    // requested maximum. Both limits matter: a device can advertise more colour
    // samples than depth, and a mismatched pair is an invalid framebuffer.
    vk::SampleCountFlagBits GetMaxUsableSampleCount(
        vk::SampleCountFlagBits cap = vk::SampleCountFlagBits::e4) const;
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

#if !SUPERSONIC_WINDOW_GLFW
    // A borrowed window (WindowBackend.hpp) can be taken away while the app
    // runs - Android destroys it whenever the app leaves the screen - and every
    // surface made from it must go first. The device itself survives: the
    // physical device, queues and allocations do not belong to the window, and
    // the next window's surface is made on the same device when it arrives.
    void DestroySurface();
    void RecreateSurface(Window& window);
#endif

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
    vk::PhysicalDeviceFeatures m_features{};

    struct PendingDestroy {
        uint64_t frame{0};
        std::function<void()> deleter;
    };
    std::vector<PendingDestroy> m_pendingDestroys;
    uint64_t m_frameNumber{0};
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
