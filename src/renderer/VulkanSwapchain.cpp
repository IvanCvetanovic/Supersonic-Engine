#include "renderer/VulkanSwapchain.hpp"

#include <algorithm>
#include <iostream>
#include <limits>
#include <stdexcept>

namespace Engine {

VulkanSwapchain::VulkanSwapchain(VulkanDevice& device, Window& window)
    : m_deviceRef(device), m_device(device.GetDevice()) {
    if (!m_device) {
        throw std::runtime_error("Cannot create VulkanSwapchain with null logical device!");
    }

    createSwapChain(window);
    createImageViews();
}

VulkanSwapchain::~VulkanSwapchain() {
    Cleanup();
}

void VulkanSwapchain::Cleanup() {
    for (auto imageView : m_swapChainImageViews) {
        if (imageView) {
            m_device.destroyImageView(imageView);
        }
    }
    m_swapChainImageViews.clear();

    if (m_swapChain) {
        m_device.destroySwapchainKHR(m_swapChain);
        m_swapChain = nullptr;
    }
}

void VulkanSwapchain::Recreate(Window& window) {
    Cleanup();
    createSwapChain(window);
    createImageViews();
}

vk::SurfaceFormatKHR VulkanSwapchain::chooseSwapSurfaceFormat(const std::vector<vk::SurfaceFormatKHR>& availableFormats) {
    // UNORM, not SRGB.
    //
    // Everything drawn into this surface is already sRGB-encoded: the scene
    // shader encodes its own output, and ImGui's style colours are authored as
    // display-referred sRGB and written through a backend that performs no
    // colour conversion. An SRGB surface would encode all of it a second time,
    // which is why the theme's near-black (0.10, 0.10, 0.12) panels rendered as
    // mid-grey and the whole dark ramp collapsed into one narrow band.
    for (const auto& availableFormat : availableFormats) {
        if ((availableFormat.format == vk::Format::eB8G8R8A8Unorm ||
             availableFormat.format == vk::Format::eR8G8B8A8Unorm) &&
            availableFormat.colorSpace == vk::ColorSpaceKHR::eSrgbNonlinear) {
            return availableFormat;
        }
    }
    return availableFormats[0];
}

vk::PresentModeKHR VulkanSwapchain::chooseSwapPresentMode(const std::vector<vk::PresentModeKHR>& availablePresentModes) {
    for (const auto& availablePresentMode : availablePresentModes) {
        if (availablePresentMode == vk::PresentModeKHR::eMailbox) {
            std::cout << "[VulkanSwapchain] Present Mode: Mailbox (Triple Buffering)" << std::endl;
            return availablePresentMode;
        }
    }
    std::cout << "[VulkanSwapchain] Present Mode: FIFO (V-Sync Fallback)" << std::endl;
    return vk::PresentModeKHR::eFifo;
}

vk::Extent2D VulkanSwapchain::chooseSwapExtent(const vk::SurfaceCapabilitiesKHR& capabilities, Window& window) {
    if (capabilities.currentExtent.width != std::numeric_limits<uint32_t>::max()) {
        return capabilities.currentExtent;
    } else {
        int width = 0;
        int height = 0;
        glfwGetFramebufferSize(window.GetNativeWindow(), &width, &height);

        vk::Extent2D actualExtent = {
            static_cast<uint32_t>(width),
            static_cast<uint32_t>(height)
        };

        actualExtent.width = std::clamp(actualExtent.width,
                                        capabilities.minImageExtent.width,
                                        capabilities.maxImageExtent.width);
        actualExtent.height = std::clamp(actualExtent.height,
                                         capabilities.minImageExtent.height,
                                         capabilities.maxImageExtent.height);

        return actualExtent;
    }
}

void VulkanSwapchain::createSwapChain(Window& window) {
    SwapChainSupportDetails swapChainSupport = m_deviceRef.QuerySwapChainSupport();

    vk::SurfaceFormatKHR surfaceFormat = chooseSwapSurfaceFormat(swapChainSupport.formats);
    vk::PresentModeKHR presentMode = chooseSwapPresentMode(swapChainSupport.presentModes);
    vk::Extent2D extent = chooseSwapExtent(swapChainSupport.capabilities, window);

    uint32_t imageCount = swapChainSupport.capabilities.minImageCount + 1;
    if (swapChainSupport.capabilities.maxImageCount > 0 &&
        imageCount > swapChainSupport.capabilities.maxImageCount) {
        imageCount = swapChainSupport.capabilities.maxImageCount;
    }

    vk::SwapchainCreateInfoKHR createInfo{};
    createInfo.surface = m_deviceRef.GetSurface();
    createInfo.minImageCount = imageCount;
    createInfo.imageFormat = surfaceFormat.format;
    createInfo.imageColorSpace = surfaceFormat.colorSpace;
    createInfo.imageExtent = extent;
    createInfo.imageArrayLayers = 1;
    createInfo.imageUsage = vk::ImageUsageFlagBits::eColorAttachment;

    QueueFamilyIndices indices = m_deviceRef.GetQueueFamilyIndices();
    uint32_t queueFamilyIndices[] = {
        indices.graphicsFamily.value(),
        indices.presentFamily.value()
    };

    if (indices.graphicsFamily != indices.presentFamily) {
        createInfo.imageSharingMode = vk::SharingMode::eConcurrent;
        createInfo.queueFamilyIndexCount = 2;
        createInfo.pQueueFamilyIndices = queueFamilyIndices;
    } else {
        createInfo.imageSharingMode = vk::SharingMode::eExclusive;
        createInfo.queueFamilyIndexCount = 0;
        createInfo.pQueueFamilyIndices = nullptr;
    }

    createInfo.preTransform = swapChainSupport.capabilities.currentTransform;
    createInfo.compositeAlpha = vk::CompositeAlphaFlagBitsKHR::eOpaque;
    createInfo.presentMode = presentMode;
    createInfo.clipped = VK_TRUE;
    createInfo.oldSwapchain = nullptr;

    m_swapChain = m_device.createSwapchainKHR(createInfo);
    m_swapChainImageFormat = surfaceFormat.format;
    m_swapChainExtent = extent;

    m_swapChainImages = m_device.getSwapchainImagesKHR(m_swapChain);

    std::cout << "[VulkanSwapchain] Swapchain created successfully with "
              << m_swapChainImages.size() << " images ("
              << m_swapChainExtent.width << "x" << m_swapChainExtent.height << ")."
              << std::endl;
}

void VulkanSwapchain::createImageViews() {
    m_swapChainImageViews.resize(m_swapChainImages.size());

    for (size_t i = 0; i < m_swapChainImages.size(); i++) {
        vk::ImageViewCreateInfo createInfo{};
        createInfo.image = m_swapChainImages[i];
        createInfo.viewType = vk::ImageViewType::e2D;
        createInfo.format = m_swapChainImageFormat;
        createInfo.components.r = vk::ComponentSwizzle::eIdentity;
        createInfo.components.g = vk::ComponentSwizzle::eIdentity;
        createInfo.components.b = vk::ComponentSwizzle::eIdentity;
        createInfo.components.a = vk::ComponentSwizzle::eIdentity;
        createInfo.subresourceRange.aspectMask = vk::ImageAspectFlagBits::eColor;
        createInfo.subresourceRange.baseMipLevel = 0;
        createInfo.subresourceRange.levelCount = 1;
        createInfo.subresourceRange.baseArrayLayer = 0;
        createInfo.subresourceRange.layerCount = 1;

        m_swapChainImageViews[i] = m_device.createImageView(createInfo);
    }

    std::cout << "[VulkanSwapchain] Created " << m_swapChainImageViews.size()
              << " Swapchain Image Views." << std::endl;
}

} // namespace Engine
