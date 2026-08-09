#include "renderer/VulkanRenderer.hpp"

#include <array>
#include <iostream>
#include <stdexcept>

namespace Engine {

VulkanRenderer::VulkanRenderer(VulkanDevice& device, VulkanSwapchain& swapchain, Window& window)
    : m_deviceRef(device), m_swapchainRef(swapchain), m_windowRef(window) {
    
    createRenderPass();
    createFramebuffers();
    createCommandPool();
    createCommandBuffers();
    createSyncObjects();

    std::cout << "[VulkanRenderer] Subsystem initialized with 2 Frames in Flight." << std::endl;
}

VulkanRenderer::~VulkanRenderer() {
    vk::Device device = m_deviceRef.GetDevice();

    for (size_t i = 0; i < MAX_FRAMES_IN_FLIGHT; i++) {
        if (m_inFlightFences[i]) {
            device.destroyFence(m_inFlightFences[i]);
        }
        if (m_renderFinishedSemaphores[i]) {
            device.destroySemaphore(m_renderFinishedSemaphores[i]);
        }
        if (m_imageAvailableSemaphores[i]) {
            device.destroySemaphore(m_imageAvailableSemaphores[i]);
        }
    }

    if (m_commandPool) {
        device.destroyCommandPool(m_commandPool);
        m_commandPool = nullptr;
    }

    for (auto framebuffer : m_framebuffers) {
        if (framebuffer) {
            device.destroyFramebuffer(framebuffer);
        }
    }
    m_framebuffers.clear();

    if (m_renderPass) {
        device.destroyRenderPass(m_renderPass);
        m_renderPass = nullptr;
    }

    std::cout << "[VulkanRenderer] Subsystem resources destroyed cleanly." << std::endl;
}

void VulkanRenderer::createRenderPass() {
    vk::AttachmentDescription colorAttachment{};
    colorAttachment.format = m_swapchainRef.GetImageFormat();
    colorAttachment.samples = vk::SampleCountFlagBits::e1;
    colorAttachment.loadOp = vk::AttachmentLoadOp::eClear;
    colorAttachment.storeOp = vk::AttachmentStoreOp::eStore;
    colorAttachment.stencilLoadOp = vk::AttachmentLoadOp::eDontCare;
    colorAttachment.stencilStoreOp = vk::AttachmentStoreOp::eDontCare;
    colorAttachment.initialLayout = vk::ImageLayout::eUndefined;
    colorAttachment.finalLayout = vk::ImageLayout::ePresentSrcKHR;

    vk::AttachmentReference colorAttachmentRef{};
    colorAttachmentRef.attachment = 0;
    colorAttachmentRef.layout = vk::ImageLayout::eColorAttachmentOptimal;

    vk::SubpassDescription subpass{};
    subpass.pipelineBindPoint = vk::PipelineBindPoint::eGraphics;
    subpass.colorAttachmentCount = 1;
    subpass.pColorAttachments = &colorAttachmentRef;

    vk::SubpassDependency dependency{};
    dependency.srcSubpass = VK_SUBPASS_EXTERNAL;
    dependency.dstSubpass = 0;
    dependency.srcStageMask = vk::PipelineStageFlagBits::eColorAttachmentOutput;
    dependency.srcAccessMask = vk::AccessFlagBits::eNone;
    dependency.dstStageMask = vk::PipelineStageFlagBits::eColorAttachmentOutput;
    dependency.dstAccessMask = vk::AccessFlagBits::eColorAttachmentWrite;

    vk::RenderPassCreateInfo renderPassInfo{};
    renderPassInfo.attachmentCount = 1;
    renderPassInfo.pAttachments = &colorAttachment;
    renderPassInfo.subpassCount = 1;
    renderPassInfo.pSubpasses = &subpass;
    renderPassInfo.dependencyCount = 1;
    renderPassInfo.pDependencies = &dependency;

    m_renderPass = m_deviceRef.GetDevice().createRenderPass(renderPassInfo);
    std::cout << "[VulkanRenderer] RenderPass created successfully." << std::endl;
}

void VulkanRenderer::createFramebuffers() {
    const auto& imageViews = m_swapchainRef.GetImageViews();
    m_framebuffers.resize(imageViews.size());

    for (size_t i = 0; i < imageViews.size(); i++) {
        vk::ImageView attachments[] = {
            imageViews[i]
        };

        vk::FramebufferCreateInfo framebufferInfo{};
        framebufferInfo.renderPass = m_renderPass;
        framebufferInfo.attachmentCount = 1;
        framebufferInfo.pAttachments = attachments;
        framebufferInfo.width = m_swapchainRef.GetExtent().width;
        framebufferInfo.height = m_swapchainRef.GetExtent().height;
        framebufferInfo.layers = 1;

        m_framebuffers[i] = m_deviceRef.GetDevice().createFramebuffer(framebufferInfo);
    }

    std::cout << "[VulkanRenderer] Created " << m_framebuffers.size() << " Framebuffers." << std::endl;
}

void VulkanRenderer::createCommandPool() {
    QueueFamilyIndices queueFamilyIndices = m_deviceRef.GetQueueFamilyIndices();

    vk::CommandPoolCreateInfo poolInfo{};
    poolInfo.flags = vk::CommandPoolCreateFlagBits::eResetCommandBuffer;
    poolInfo.queueFamilyIndex = queueFamilyIndices.graphicsFamily.value();

    m_commandPool = m_deviceRef.GetDevice().createCommandPool(poolInfo);
    std::cout << "[VulkanRenderer] CommandPool created successfully." << std::endl;
}

void VulkanRenderer::createCommandBuffers() {
    m_commandBuffers.resize(MAX_FRAMES_IN_FLIGHT);

    vk::CommandBufferAllocateInfo allocInfo{};
    allocInfo.commandPool = m_commandPool;
    allocInfo.level = vk::CommandBufferLevel::ePrimary;
    allocInfo.commandBufferCount = static_cast<uint32_t>(MAX_FRAMES_IN_FLIGHT);

    m_commandBuffers = m_deviceRef.GetDevice().allocateCommandBuffers(allocInfo);
    std::cout << "[VulkanRenderer] Allocated " << m_commandBuffers.size() << " CommandBuffers." << std::endl;
}

void VulkanRenderer::createSyncObjects() {
    m_imageAvailableSemaphores.resize(MAX_FRAMES_IN_FLIGHT);
    m_renderFinishedSemaphores.resize(MAX_FRAMES_IN_FLIGHT);
    m_inFlightFences.resize(MAX_FRAMES_IN_FLIGHT);

    vk::SemaphoreCreateInfo semaphoreInfo{};
    vk::FenceCreateInfo fenceInfo{};
    fenceInfo.flags = vk::FenceCreateFlagBits::eSignaled;

    vk::Device device = m_deviceRef.GetDevice();
    for (size_t i = 0; i < MAX_FRAMES_IN_FLIGHT; i++) {
        m_imageAvailableSemaphores[i] = device.createSemaphore(semaphoreInfo);
        m_renderFinishedSemaphores[i] = device.createSemaphore(semaphoreInfo);
        m_inFlightFences[i] = device.createFence(fenceInfo);
    }

    std::cout << "[VulkanRenderer] Synchronization primitives (Semaphores & Fences) created." << std::endl;
}

void VulkanRenderer::DrawFrame() {
    vk::Device device = m_deviceRef.GetDevice();

    // 1. Wait for the inFlightFence of current frame
    vk::Result waitResult = device.waitForFences(1, &m_inFlightFences[m_currentFrame], VK_TRUE, UINT64_MAX);
    if (waitResult != vk::Result::eSuccess) {
        throw std::runtime_error("Failed to wait for Vulkan inFlightFence!");
    }

    // 2. Acquire next image from Swapchain
    uint32_t imageIndex = 0;
    VkResult acquireResult = vkAcquireNextImageKHR(
        static_cast<VkDevice>(device),
        static_cast<VkSwapchainKHR>(m_swapchainRef.GetSwapChain()),
        UINT64_MAX,
        static_cast<VkSemaphore>(m_imageAvailableSemaphores[m_currentFrame]),
        VK_NULL_HANDLE,
        &imageIndex
    );

    if (acquireResult == VK_ERROR_OUT_OF_DATE_KHR) {
        // TODO: Handle Swapchain recreation later on VK_ERROR_OUT_OF_DATE_KHR
        return;
    } else if (acquireResult != VK_SUCCESS && acquireResult != VK_SUBOPTIMAL_KHR) {
        throw std::runtime_error("Failed to acquire Vulkan swapchain image!");
    }

    // Reset fence only if we are about to submit work
    vk::Result resetFenceRes = device.resetFences(1, &m_inFlightFences[m_currentFrame]);
    if (resetFenceRes != vk::Result::eSuccess) {
        throw std::runtime_error("Failed to reset Vulkan inFlightFence!");
    }

    // 3. Reset current frame's command buffer
    m_commandBuffers[m_currentFrame].reset();

    // 4. Record Command Buffer
    vk::CommandBufferBeginInfo beginInfo{};
    m_commandBuffers[m_currentFrame].begin(beginInfo);

    vk::RenderPassBeginInfo renderPassInfo{};
    renderPassInfo.renderPass = m_renderPass;
    renderPassInfo.framebuffer = m_framebuffers[imageIndex];
    renderPassInfo.renderArea.offset = vk::Offset2D{0, 0};
    renderPassInfo.renderArea.extent = m_swapchainRef.GetExtent();

    // Clear color: Dark Grey {0.02f, 0.02f, 0.02f, 1.0f}
    vk::ClearValue clearColor = vk::ClearColorValue{std::array<float, 4>{0.02f, 0.02f, 0.02f, 1.0f}};
    renderPassInfo.clearValueCount = 1;
    renderPassInfo.pClearValues = &clearColor;

    m_commandBuffers[m_currentFrame].beginRenderPass(renderPassInfo, vk::SubpassContents::eInline);

    // Set dynamic viewport and scissor
    vk::Viewport viewport{
        0.0f, 0.0f,
        static_cast<float>(m_swapchainRef.GetExtent().width),
        static_cast<float>(m_swapchainRef.GetExtent().height),
        0.0f, 1.0f
    };
    vk::Rect2D scissor{{0, 0}, m_swapchainRef.GetExtent()};
    m_commandBuffers[m_currentFrame].setViewport(0, 1, &viewport);
    m_commandBuffers[m_currentFrame].setScissor(0, 1, &scissor);

    m_commandBuffers[m_currentFrame].endRenderPass();
    m_commandBuffers[m_currentFrame].end();

    // 5. Submit Command Buffer to Graphics Queue
    vk::Semaphore waitSemaphores[] = { m_imageAvailableSemaphores[m_currentFrame] };
    vk::PipelineStageFlags waitStages[] = { vk::PipelineStageFlagBits::eColorAttachmentOutput };
    vk::Semaphore signalSemaphores[] = { m_renderFinishedSemaphores[m_currentFrame] };

    vk::SubmitInfo submitInfo{};
    submitInfo.waitSemaphoreCount = 1;
    submitInfo.pWaitSemaphores = waitSemaphores;
    submitInfo.pWaitDstStageMask = waitStages;
    submitInfo.commandBufferCount = 1;
    submitInfo.pCommandBuffers = &m_commandBuffers[m_currentFrame];
    submitInfo.signalSemaphoreCount = 1;
    submitInfo.pSignalSemaphores = signalSemaphores;

    vk::Result submitRes = m_deviceRef.GetGraphicsQueue().submit(1, &submitInfo, m_inFlightFences[m_currentFrame]);
    if (submitRes != vk::Result::eSuccess) {
        throw std::runtime_error("Failed to submit command buffer to Vulkan graphics queue!");
    }

    // 6. Present image to Swapchain Present Queue
    vk::SwapchainKHR swapChains[] = { m_swapchainRef.GetSwapChain() };

    vk::PresentInfoKHR presentInfo{};
    presentInfo.waitSemaphoreCount = 1;
    presentInfo.pWaitSemaphores = signalSemaphores;
    presentInfo.swapchainCount = 1;
    presentInfo.pSwapchains = swapChains;
    presentInfo.pImageIndices = &imageIndex;

    VkResult presentResult = static_cast<VkResult>(m_deviceRef.GetPresentQueue().presentKHR(&presentInfo));
    if (presentResult == VK_ERROR_OUT_OF_DATE_KHR || presentResult == VK_SUBOPTIMAL_KHR) {
        // TODO: Handle Swapchain recreation later on VK_ERROR_OUT_OF_DATE_KHR / VK_SUBOPTIMAL_KHR
    } else if (presentResult != VK_SUCCESS) {
        throw std::runtime_error("Failed to present Vulkan swapchain image!");
    }

    // 7. Advance current frame index (2 Frames in Flight)
    m_currentFrame = (m_currentFrame + 1) % MAX_FRAMES_IN_FLIGHT;
}

} // namespace Engine
