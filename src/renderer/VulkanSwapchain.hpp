#pragma once

#include <vector>

#include <vulkan/vulkan.hpp>

#include "renderer/VulkanDevice.hpp"
#include "platform/Window.hpp"

namespace Supersonic {

class VulkanSwapchain {
public:
    VulkanSwapchain(VulkanDevice& device, Window& window);
    ~VulkanSwapchain();

    VulkanSwapchain(const VulkanSwapchain&) = delete;
    VulkanSwapchain& operator=(const VulkanSwapchain&) = delete;

    vk::SwapchainKHR GetSwapChain() const { return m_swapChain; }
    vk::Format GetImageFormat() const { return m_swapChainImageFormat; }
    vk::Extent2D GetExtent() const { return m_swapChainExtent; }
    const std::vector<vk::Image>& GetImages() const { return m_swapChainImages; }
    const std::vector<vk::ImageView>& GetImageViews() const { return m_swapChainImageViews; }

    void Recreate(Window& window);
    void Cleanup();

private:
    vk::SurfaceFormatKHR chooseSwapSurfaceFormat(const std::vector<vk::SurfaceFormatKHR>& availableFormats);
    vk::PresentModeKHR chooseSwapPresentMode(const std::vector<vk::PresentModeKHR>& availablePresentModes);
    vk::Extent2D chooseSwapExtent(const vk::SurfaceCapabilitiesKHR& capabilities, Window& window);

    void createSwapChain(Window& window);
    void createImageViews();

    VulkanDevice& m_deviceRef;
    vk::Device m_device{nullptr};

    vk::SwapchainKHR m_swapChain{nullptr};
    vk::Format m_swapChainImageFormat;
    vk::Extent2D m_swapChainExtent;

    std::vector<vk::Image> m_swapChainImages;
    std::vector<vk::ImageView> m_swapChainImageViews;
};

} // namespace Supersonic
