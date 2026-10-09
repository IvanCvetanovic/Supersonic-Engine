#pragma once

#include <string>
#include <vector>

#include <vulkan/vulkan.hpp>

#include "renderer/VulkanDevice.hpp"
#include "platform/Window.hpp"

namespace Supersonic {

class VulkanSwapchain {
public:
    // `readable` asks for images that can be copied from, so the finished
    // frame - UI included - can be read back (--screenshot-ui). Granted only
    // where the surface lists eTransferSrc among its supported usages;
    // IsReadable() says whether it was. Off, the swapchain is exactly what it
    // always was.
    VulkanSwapchain(VulkanDevice& device, Window& window, bool readable = false);
    ~VulkanSwapchain();

    VulkanSwapchain(const VulkanSwapchain&) = delete;
    VulkanSwapchain& operator=(const VulkanSwapchain&) = delete;

    vk::SwapchainKHR GetSwapChain() const { return m_swapChain; }
    vk::Format GetImageFormat() const { return m_swapChainImageFormat; }
    vk::Extent2D GetExtent() const { return m_swapChainExtent; }
    const std::vector<vk::Image>& GetImages() const { return m_swapChainImages; }
    const std::vector<vk::ImageView>& GetImageViews() const { return m_swapChainImageViews; }

    // Whether the images carry eTransferSrc, so a frame can be copied out of
    // one before it is presented. Decided each time the swapchain is built.
    bool IsReadable() const { return m_readable; }

    // The present mode in use ("Mailbox", "Fifo"), for a log line or a readout.
    const std::string& PresentModeName() const { return m_presentModeName; }

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
    std::string m_presentModeName{"Fifo"};

    std::vector<vk::Image> m_swapChainImages;
    std::vector<vk::ImageView> m_swapChainImageViews;

    bool m_wantReadable{false};
    bool m_readable{false};
};

} // namespace Supersonic
