#include "renderer/VulkanRenderer.hpp"
#include "core/RenderSystem.hpp"
#include "core/Components.hpp"

#include "editor/Theme.hpp"

#include "imgui.h"
#include "backends/imgui_impl_glfw.h"
#include "backends/imgui_impl_vulkan.h"
#include "ImGuizmo.h"

#include <array>
#include <iostream>
#include <stdexcept>

namespace Engine {

// 3D Cube Vertex & Index Data with Normals for Lighting
static const std::vector<Vertex> cubeVertices = {
    // Front face (Z = +0.5, Normal = {0, 0, 1})
    {{-0.5f, -0.5f,  0.5f}, {0.0f, 0.0f, 1.0f}, {1.0f, 0.2f, 0.2f}, {0.0f, 0.0f}},
    {{ 0.5f, -0.5f,  0.5f}, {0.0f, 0.0f, 1.0f}, {0.2f, 1.0f, 0.2f}, {1.0f, 0.0f}},
    {{ 0.5f,  0.5f,  0.5f}, {0.0f, 0.0f, 1.0f}, {0.2f, 0.2f, 1.0f}, {1.0f, 1.0f}},
    {{-0.5f,  0.5f,  0.5f}, {0.0f, 0.0f, 1.0f}, {1.0f, 1.0f, 0.2f}, {0.0f, 1.0f}},
    // Back face (Z = -0.5, Normal = {0, 0, -1})
    {{ 0.5f, -0.5f, -0.5f}, {0.0f, 0.0f, -1.0f}, {1.0f, 0.2f, 1.0f}, {0.0f, 0.0f}},
    {{-0.5f, -0.5f, -0.5f}, {0.0f, 0.0f, -1.0f}, {0.2f, 1.0f, 1.0f}, {1.0f, 0.0f}},
    {{-0.5f,  0.5f, -0.5f}, {0.0f, 0.0f, -1.0f}, {1.0f, 1.0f, 1.0f}, {1.0f, 1.0f}},
    {{ 0.5f,  0.5f, -0.5f}, {0.0f, 0.0f, -1.0f}, {0.5f, 0.5f, 0.5f}, {0.0f, 1.0f}},
    // Top face (Y = -0.5, Normal = {0, -1, 0})
    {{-0.5f, -0.5f, -0.5f}, {0.0f, -1.0f, 0.0f}, {1.0f, 0.4f, 0.4f}, {0.0f, 0.0f}},
    {{ 0.5f, -0.5f, -0.5f}, {0.0f, -1.0f, 0.0f}, {0.4f, 1.0f, 0.4f}, {1.0f, 0.0f}},
    {{ 0.5f, -0.5f,  0.5f}, {0.0f, -1.0f, 0.0f}, {0.4f, 0.4f, 1.0f}, {1.0f, 1.0f}},
    {{-0.5f, -0.5f,  0.5f}, {0.0f, -1.0f, 0.0f}, {1.0f, 1.0f, 0.4f}, {0.0f, 1.0f}},
    // Bottom face (Y = +0.5, Normal = {0, 1, 0})
    {{-0.5f,  0.5f,  0.5f}, {0.0f, 1.0f, 0.0f}, {0.8f, 0.3f, 0.3f}, {0.0f, 0.0f}},
    {{ 0.5f,  0.5f,  0.5f}, {0.0f, 1.0f, 0.0f}, {0.3f, 0.8f, 0.3f}, {1.0f, 0.0f}},
    {{ 0.5f,  0.5f, -0.5f}, {0.0f, 1.0f, 0.0f}, {0.3f, 0.3f, 0.8f}, {1.0f, 1.0f}},
    {{-0.5f,  0.5f, -0.5f}, {0.0f, 1.0f, 0.0f}, {0.8f, 0.8f, 0.3f}, {0.0f, 1.0f}},
    // Right face (X = +0.5, Normal = {1, 0, 0})
    {{ 0.5f, -0.5f,  0.5f}, {1.0f, 0.0f, 0.0f}, {0.9f, 0.5f, 0.2f}, {0.0f, 0.0f}},
    {{ 0.5f, -0.5f, -0.5f}, {1.0f, 0.0f, 0.0f}, {0.2f, 0.9f, 0.5f}, {1.0f, 0.0f}},
    {{ 0.5f,  0.5f, -0.5f}, {1.0f, 0.0f, 0.0f}, {0.5f, 0.2f, 0.9f}, {1.0f, 1.0f}},
    {{ 0.5f,  0.5f,  0.5f}, {1.0f, 0.0f, 0.0f}, {0.9f, 0.9f, 0.2f}, {0.0f, 1.0f}},
    // Left face (X = -0.5, Normal = {-1, 0, 0})
    {{-0.5f, -0.5f, -0.5f}, {-1.0f, 0.0f, 0.0f}, {0.2f, 0.6f, 0.9f}, {0.0f, 0.0f}},
    {{-0.5f, -0.5f,  0.5f}, {-1.0f, 0.0f, 0.0f}, {0.9f, 0.2f, 0.6f}, {1.0f, 0.0f}},
    {{-0.5f,  0.5f,  0.5f}, {-1.0f, 0.0f, 0.0f}, {0.6f, 0.9f, 0.2f}, {1.0f, 1.0f}},
    {{-0.5f,  0.5f, -0.5f}, {-1.0f, 0.0f, 0.0f}, {0.2f, 0.9f, 0.6f}, {0.0f, 1.0f}}
};

static const std::vector<uint16_t> cubeIndices = {
     0,  1,  2,  2,  3,  0, // Front
     4,  5,  6,  6,  7,  4, // Back
     8,  9, 10, 10, 11,  8, // Top
    12, 13, 14, 14, 15, 12, // Bottom
    16, 17, 18, 18, 19, 16, // Right
    20, 21, 22, 22, 23, 20  // Left
};

VulkanRenderer::VulkanRenderer(VulkanDevice& device, VulkanSwapchain& swapchain, Window& window)
    : m_deviceRef(device), m_swapchainRef(swapchain), m_windowRef(window) {
    
    createRenderPass();
    createFramebuffers();
    createCommandPool();
    createCommandBuffers();
    createSyncObjects();

    createVertexBuffer();
    createIndexBuffer();
    createUniformBuffers();
    createTextureImage();
    createDescriptorPool();
    
    initImGui();
    m_editorLayer.Init(m_deviceRef, m_swapchainRef.GetExtent().width, m_swapchainRef.GetExtent().height);
    createGraphicsPipeline();
    createDescriptorSets();

    std::cout << "[VulkanRenderer] Dockable Editor Engine Renderer initialized." << std::endl;
}

VulkanRenderer::~VulkanRenderer() {
    vk::Device device = m_deviceRef.GetDevice();

    m_editorLayer.Shutdown();

    ImGui_ImplVulkan_Shutdown();
    ImGui_ImplGlfw_Shutdown();
    ImGui::DestroyContext();

    if (m_imguiPool) {
        device.destroyDescriptorPool(m_imguiPool);
        m_imguiPool = nullptr;
    }

    if (m_descriptorPool) {
        device.destroyDescriptorPool(m_descriptorPool);
        m_descriptorPool = nullptr;
    }

    m_textureImage.reset();
    m_uniformBuffers.clear();
    m_indexBuffer.reset();
    m_vertexBuffer.reset();

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

    cleanupSwapchain();

    std::cout << "[VulkanRenderer] Subsystem resources destroyed cleanly." << std::endl;
}

void VulkanRenderer::cleanupSwapchain() {
    vk::Device device = m_deviceRef.GetDevice();

    for (auto framebuffer : m_framebuffers) {
        if (framebuffer) {
            device.destroyFramebuffer(framebuffer);
        }
    }
    m_framebuffers.clear();

    m_pipeline.reset();

    if (m_renderPass) {
        device.destroyRenderPass(m_renderPass);
        m_renderPass = nullptr;
    }
}

void VulkanRenderer::RecreateSwapchain() {
    int width = 0, height = 0;
    glfwGetFramebufferSize(m_windowRef.GetNativeWindow(), &width, &height);
    while (width == 0 || height == 0) {
        glfwGetFramebufferSize(m_windowRef.GetNativeWindow(), &width, &height);
        glfwWaitEvents();
    }

    m_deviceRef.GetDevice().waitIdle();
    cleanupSwapchain();

    m_swapchainRef.Recreate(m_windowRef);
    createRenderPass();
    createGraphicsPipeline();
    createFramebuffers();

    m_windowRef.ResetResizedFlag();
    std::cout << "[VulkanRenderer] Swapchain recreated successfully for window size (" << width << "x" << height << ")." << std::endl;
}

void VulkanRenderer::createRenderPass() {
    // Swapchain Render Pass (Color only, loadOp = eClear, finalLayout = ePresentSrcKHR for ImGui presentation)
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
    std::cout << "[VulkanRenderer] Swapchain RenderPass (ImGui UI Pass) created." << std::endl;
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

    std::cout << "[VulkanRenderer] Created " << m_framebuffers.size() << " Swapchain Framebuffers." << std::endl;
}

void VulkanRenderer::createGraphicsPipeline() {
    // 3D Scene Pipeline target offscreen render pass
    m_pipeline = std::make_unique<VulkanPipeline>(
        m_deviceRef.GetDevice(),
        m_editorLayer.GetOffscreen().GetRenderPass(),
        "assets/shaders/vert.spv",
        "assets/shaders/frag.spv"
    );
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

void VulkanRenderer::createVertexBuffer() {
    vk::DeviceSize bufferSize = sizeof(cubeVertices[0]) * cubeVertices.size();

    VulkanBuffer stagingBuffer(
        m_deviceRef.GetAllocator(),
        bufferSize,
        vk::BufferUsageFlagBits::eTransferSrc,
        VMA_MEMORY_USAGE_CPU_ONLY
    );

    stagingBuffer.UploadData(cubeVertices.data(), bufferSize);

    m_vertexBuffer = std::make_unique<VulkanBuffer>(
        m_deviceRef.GetAllocator(),
        bufferSize,
        vk::BufferUsageFlagBits::eTransferDst | vk::BufferUsageFlagBits::eVertexBuffer,
        VMA_MEMORY_USAGE_GPU_ONLY
    );

    VulkanBuffer::CopyBuffer(
        m_deviceRef,
        m_commandPool,
        stagingBuffer.GetBuffer(),
        m_vertexBuffer->GetBuffer(),
        bufferSize
    );

    std::cout << "[VulkanRenderer] Device-Local Vertex Buffer uploaded (" << cubeVertices.size() << " vertices)." << std::endl;
}

void VulkanRenderer::createIndexBuffer() {
    m_indexCount = static_cast<uint32_t>(cubeIndices.size());
    vk::DeviceSize bufferSize = sizeof(cubeIndices[0]) * cubeIndices.size();

    VulkanBuffer stagingBuffer(
        m_deviceRef.GetAllocator(),
        bufferSize,
        vk::BufferUsageFlagBits::eTransferSrc,
        VMA_MEMORY_USAGE_CPU_ONLY
    );

    stagingBuffer.UploadData(cubeIndices.data(), bufferSize);

    m_indexBuffer = std::make_unique<VulkanBuffer>(
        m_deviceRef.GetAllocator(),
        bufferSize,
        vk::BufferUsageFlagBits::eTransferDst | vk::BufferUsageFlagBits::eIndexBuffer,
        VMA_MEMORY_USAGE_GPU_ONLY
    );

    VulkanBuffer::CopyBuffer(
        m_deviceRef,
        m_commandPool,
        stagingBuffer.GetBuffer(),
        m_indexBuffer->GetBuffer(),
        bufferSize
    );

    std::cout << "[VulkanRenderer] Device-Local Index Buffer uploaded (" << m_indexCount << " indices)." << std::endl;
}

void VulkanRenderer::createUniformBuffers() {
    m_uniformBuffers.resize(MAX_FRAMES_IN_FLIGHT);
    vk::DeviceSize bufferSize = sizeof(UniformBufferObject);

    for (size_t i = 0; i < MAX_FRAMES_IN_FLIGHT; i++) {
        m_uniformBuffers[i] = std::make_unique<VulkanBuffer>(
            m_deviceRef.GetAllocator(),
            bufferSize,
            vk::BufferUsageFlagBits::eUniformBuffer,
            VMA_MEMORY_USAGE_CPU_TO_GPU,
            VMA_ALLOCATION_CREATE_MAPPED_BIT
        );
    }

    std::cout << "[VulkanRenderer] Created " << MAX_FRAMES_IN_FLIGHT << " VMA Uniform Buffers." << std::endl;
}

void VulkanRenderer::createTextureImage() {
    const uint32_t texWidth = 64;
    const uint32_t texHeight = 64;
    vk::DeviceSize imageSize = texWidth * texHeight * 4;

    std::vector<uint8_t> pixels(imageSize);
    for (uint32_t y = 0; y < texHeight; y++) {
        for (uint32_t x = 0; x < texWidth; x++) {
            bool isWhite = ((x / 8) + (y / 8)) % 2 == 0;
            uint32_t idx = (y * texWidth + x) * 4;
            pixels[idx + 0] = isWhite ? 255 : 40;  // Red
            pixels[idx + 1] = isWhite ? 255 : 120; // Green
            pixels[idx + 2] = isWhite ? 255 : 220; // Blue
            pixels[idx + 3] = 255;                 // Alpha
        }
    }

    VulkanBuffer stagingBuffer(
        m_deviceRef.GetAllocator(),
        imageSize,
        vk::BufferUsageFlagBits::eTransferSrc,
        VMA_MEMORY_USAGE_CPU_ONLY
    );
    stagingBuffer.UploadData(pixels.data(), imageSize);

    m_textureImage = std::make_unique<VulkanImage>(
        m_deviceRef,
        texWidth,
        texHeight,
        vk::Format::eR8G8B8A8Srgb
    );

    VulkanImage::TransitionLayout(
        m_deviceRef,
        m_commandPool,
        m_textureImage->GetImage(),
        vk::ImageLayout::eUndefined,
        vk::ImageLayout::eTransferDstOptimal
    );

    VulkanImage::CopyBufferToImage(
        m_deviceRef,
        m_commandPool,
        stagingBuffer.GetBuffer(),
        m_textureImage->GetImage(),
        texWidth,
        texHeight
    );

    VulkanImage::TransitionLayout(
        m_deviceRef,
        m_commandPool,
        m_textureImage->GetImage(),
        vk::ImageLayout::eTransferDstOptimal,
        vk::ImageLayout::eShaderReadOnlyOptimal
    );

    std::cout << "[VulkanRenderer] Created and uploaded 2D Checkerboard Texture Image." << std::endl;
}

void VulkanRenderer::createDescriptorPool() {
    std::array<vk::DescriptorPoolSize, 2> poolSizes{};
    poolSizes[0].type = vk::DescriptorType::eUniformBuffer;
    poolSizes[0].descriptorCount = static_cast<uint32_t>(MAX_FRAMES_IN_FLIGHT);

    poolSizes[1].type = vk::DescriptorType::eCombinedImageSampler;
    poolSizes[1].descriptorCount = static_cast<uint32_t>(MAX_FRAMES_IN_FLIGHT);

    vk::DescriptorPoolCreateInfo poolInfo{};
    poolInfo.poolSizeCount = static_cast<uint32_t>(poolSizes.size());
    poolInfo.pPoolSizes = poolSizes.data();
    poolInfo.maxSets = static_cast<uint32_t>(MAX_FRAMES_IN_FLIGHT);

    m_descriptorPool = m_deviceRef.GetDevice().createDescriptorPool(poolInfo);
    std::cout << "[VulkanRenderer] DescriptorPool created successfully." << std::endl;
}

void VulkanRenderer::createDescriptorSets() {
    std::vector<vk::DescriptorSetLayout> layouts(MAX_FRAMES_IN_FLIGHT, m_pipeline->GetDescriptorSetLayout());

    vk::DescriptorSetAllocateInfo allocInfo{};
    allocInfo.descriptorPool = m_descriptorPool;
    allocInfo.descriptorSetCount = static_cast<uint32_t>(MAX_FRAMES_IN_FLIGHT);
    allocInfo.pSetLayouts = layouts.data();

    m_descriptorSets = m_deviceRef.GetDevice().allocateDescriptorSets(allocInfo);

    for (size_t i = 0; i < MAX_FRAMES_IN_FLIGHT; i++) {
        vk::DescriptorBufferInfo bufferInfo{};
        bufferInfo.buffer = m_uniformBuffers[i]->GetBuffer();
        bufferInfo.offset = 0;
        bufferInfo.range = sizeof(UniformBufferObject);

        vk::DescriptorImageInfo imageInfo{};
        imageInfo.imageLayout = vk::ImageLayout::eShaderReadOnlyOptimal;
        imageInfo.imageView = m_textureImage->GetImageView();
        imageInfo.sampler = m_textureImage->GetSampler();

        std::array<vk::WriteDescriptorSet, 2> descriptorWrites{};

        // Binding 0: UBO
        descriptorWrites[0].dstSet = m_descriptorSets[i];
        descriptorWrites[0].dstBinding = 0;
        descriptorWrites[0].dstArrayElement = 0;
        descriptorWrites[0].descriptorType = vk::DescriptorType::eUniformBuffer;
        descriptorWrites[0].descriptorCount = 1;
        descriptorWrites[0].pBufferInfo = &bufferInfo;

        // Binding 1: Texture Sampler
        descriptorWrites[1].dstSet = m_descriptorSets[i];
        descriptorWrites[1].dstBinding = 1;
        descriptorWrites[1].dstArrayElement = 0;
        descriptorWrites[1].descriptorType = vk::DescriptorType::eCombinedImageSampler;
        descriptorWrites[1].descriptorCount = 1;
        descriptorWrites[1].pImageInfo = &imageInfo;

        m_deviceRef.GetDevice().updateDescriptorSets(descriptorWrites, nullptr);
    }

    std::cout << "[VulkanRenderer] Allocated and updated " << m_descriptorSets.size() << " DescriptorSets." << std::endl;
}

void VulkanRenderer::initImGui() {
    // 1. Create Dedicated Descriptor Pool for ImGui
    std::array<vk::DescriptorPoolSize, 11> poolSizes = {{
        { vk::DescriptorType::eSampler, 1000 },
        { vk::DescriptorType::eCombinedImageSampler, 1000 },
        { vk::DescriptorType::eSampledImage, 1000 },
        { vk::DescriptorType::eStorageImage, 1000 },
        { vk::DescriptorType::eUniformTexelBuffer, 1000 },
        { vk::DescriptorType::eStorageTexelBuffer, 1000 },
        { vk::DescriptorType::eUniformBuffer, 1000 },
        { vk::DescriptorType::eStorageBuffer, 1000 },
        { vk::DescriptorType::eUniformBufferDynamic, 1000 },
        { vk::DescriptorType::eStorageBufferDynamic, 1000 },
        { vk::DescriptorType::eInputAttachment, 1000 }
    }};

    vk::DescriptorPoolCreateInfo poolInfo{};
    poolInfo.flags = vk::DescriptorPoolCreateFlagBits::eFreeDescriptorSet;
    poolInfo.maxSets = 1000 * static_cast<uint32_t>(poolSizes.size());
    poolInfo.poolSizeCount = static_cast<uint32_t>(poolSizes.size());
    poolInfo.pPoolSizes = poolSizes.data();

    m_imguiPool = m_deviceRef.GetDevice().createDescriptorPool(poolInfo);

    // 2. Setup ImGui Context
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO(); (void)io;
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
    io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;

    Theme::ApplyEngineDarkTheme();

    // 3. Init ImGui GLFW and Vulkan Backends
    ImGui_ImplGlfw_InitForVulkan(m_windowRef.GetNativeWindow(), true);

    ImGui_ImplVulkan_InitInfo initInfo{};
    initInfo.Instance = static_cast<VkInstance>(m_deviceRef.GetInstance());
    initInfo.PhysicalDevice = static_cast<VkPhysicalDevice>(m_deviceRef.GetPhysicalDevice());
    initInfo.Device = static_cast<VkDevice>(m_deviceRef.GetDevice());
    initInfo.QueueFamily = m_deviceRef.GetQueueFamilyIndices().graphicsFamily.value();
    initInfo.Queue = static_cast<VkQueue>(m_deviceRef.GetGraphicsQueue());
    initInfo.PipelineCache = VK_NULL_HANDLE;
    initInfo.DescriptorPool = static_cast<VkDescriptorPool>(m_imguiPool);
    initInfo.MinImageCount = static_cast<uint32_t>(MAX_FRAMES_IN_FLIGHT);
    initInfo.ImageCount = static_cast<uint32_t>(m_swapchainRef.GetImages().size());
    initInfo.PipelineInfoMain.Subpass = 0;
    initInfo.PipelineInfoMain.MSAASamples = VK_SAMPLE_COUNT_1_BIT;
    initInfo.PipelineInfoMain.RenderPass = static_cast<VkRenderPass>(m_renderPass);

    ImGui_ImplVulkan_Init(&initInfo);

    std::cout << "[VulkanRenderer] ImGui Docking & Vulkan backend initialized successfully." << std::endl;
}

void VulkanRenderer::DrawFrame(entt::registry& registry, const glm::mat4& viewMatrix, const glm::mat4& projMatrix) {
    vk::Device device = m_deviceRef.GetDevice();

    // 1. Wait for the inFlightFence of current frame
    vk::Result waitResult = device.waitForFences(1, &m_inFlightFences[m_currentFrame], VK_TRUE, UINT64_MAX);
    if (waitResult != vk::Result::eSuccess) {
        throw std::runtime_error("Failed to wait for Vulkan inFlightFence!");
    }

    // 2. Update Uniform Buffer (View/Proj matrices) for current frame
    UniformBufferObject ubo{};
    ubo.view = viewMatrix;
    ubo.proj = projMatrix;
    m_uniformBuffers[m_currentFrame]->UploadData(&ubo, sizeof(ubo));

    // 3. Acquire next image from Swapchain
    uint32_t imageIndex = 0;
    VkResult acquireResult = vkAcquireNextImageKHR(
        static_cast<VkDevice>(device),
        static_cast<VkSwapchainKHR>(m_swapchainRef.GetSwapChain()),
        UINT64_MAX,
        static_cast<VkSemaphore>(m_imageAvailableSemaphores[m_currentFrame]),
        VK_NULL_HANDLE,
        &imageIndex
    );

    if (acquireResult == VK_ERROR_OUT_OF_DATE_KHR || m_windowRef.IsResized()) {
        RecreateSwapchain();
        return;
    } else if (acquireResult != VK_SUCCESS && acquireResult != VK_SUBOPTIMAL_KHR) {
        throw std::runtime_error("Failed to acquire Vulkan swapchain image!");
    }

    // Reset fence only if work is submitted
    vk::Result resetFenceRes = device.resetFences(1, &m_inFlightFences[m_currentFrame]);
    if (resetFenceRes != vk::Result::eSuccess) {
        throw std::runtime_error("Failed to reset Vulkan inFlightFence!");
    }

    // 4. Reset current frame's command buffer
    m_commandBuffers[m_currentFrame].reset();

    // 5. Begin Command Buffer Recording
    vk::CommandBufferBeginInfo beginInfo{};
    m_commandBuffers[m_currentFrame].begin(beginInfo);

    // =========================================================================
    // PASS 1: OFFSCREEN RENDER PASS (3D Scene rendering into Viewport Texture)
    // =========================================================================
    VulkanOffscreen& offscreen = m_editorLayer.GetOffscreen();

    vk::RenderPassBeginInfo offscreenPassInfo{};
    offscreenPassInfo.renderPass = offscreen.GetRenderPass();
    offscreenPassInfo.framebuffer = offscreen.GetFramebuffer();
    offscreenPassInfo.renderArea.offset = vk::Offset2D{0, 0};
    offscreenPassInfo.renderArea.extent = vk::Extent2D{offscreen.GetWidth(), offscreen.GetHeight()};

    std::array<vk::ClearValue, 2> offscreenClearValues{};
    offscreenClearValues[0].color = vk::ClearColorValue{std::array<float, 4>{0.02f, 0.02f, 0.02f, 1.0f}};
    offscreenClearValues[1].depthStencil = vk::ClearDepthStencilValue{1.0f, 0};

    offscreenPassInfo.clearValueCount = static_cast<uint32_t>(offscreenClearValues.size());
    offscreenPassInfo.pClearValues = offscreenClearValues.data();

    m_commandBuffers[m_currentFrame].beginRenderPass(offscreenPassInfo, vk::SubpassContents::eInline);

    // Set offscreen viewport and scissor
    vk::Viewport offscreenViewport{
        0.0f, 0.0f,
        static_cast<float>(offscreen.GetWidth()),
        static_cast<float>(offscreen.GetHeight()),
        0.0f, 1.0f
    };
    vk::Rect2D offscreenScissor{{0, 0}, offscreenPassInfo.renderArea.extent};
    m_commandBuffers[m_currentFrame].setViewport(0, 1, &offscreenViewport);
    m_commandBuffers[m_currentFrame].setScissor(0, 1, &offscreenScissor);

    // Stateless EnTT RenderSystem renders 3D entities offscreen
    RenderSystem::Render(
        registry,
        *m_pipeline,
        m_commandBuffers[m_currentFrame],
        m_descriptorSets[m_currentFrame],
        m_vertexBuffer->GetBuffer(),
        m_indexBuffer->GetBuffer(),
        m_indexCount
    );

    m_commandBuffers[m_currentFrame].endRenderPass();

    // =========================================================================
    // IMGUI FRAME RECORDING (Dockspace, Scene Hierarchy, Inspector, Viewport, ImGuizmo)
    // =========================================================================
    ImGui_ImplVulkan_NewFrame();
    ImGui_ImplGlfw_NewFrame();
    ImGui::NewFrame();
    ImGuizmo::BeginFrame();

    m_editorLayer.OnImGuiRender(registry, m_windowRef);

    ImGui::Render();

    // =========================================================================
    // PASS 2: SWAPCHAIN RENDER PASS (ImGui UI Presentation Pass)
    // =========================================================================
    vk::RenderPassBeginInfo swapchainPassInfo{};
    swapchainPassInfo.renderPass = m_renderPass;
    swapchainPassInfo.framebuffer = m_framebuffers[imageIndex];
    swapchainPassInfo.renderArea.offset = vk::Offset2D{0, 0};
    swapchainPassInfo.renderArea.extent = m_swapchainRef.GetExtent();

    vk::ClearValue swapchainClearColor = vk::ClearColorValue{std::array<float, 4>{0.1f, 0.1f, 0.1f, 1.0f}};
    swapchainPassInfo.clearValueCount = 1;
    swapchainPassInfo.pClearValues = &swapchainClearColor;

    m_commandBuffers[m_currentFrame].beginRenderPass(swapchainPassInfo, vk::SubpassContents::eInline);

    vk::Viewport swapchainViewport{
        0.0f, 0.0f,
        static_cast<float>(m_swapchainRef.GetExtent().width),
        static_cast<float>(m_swapchainRef.GetExtent().height),
        0.0f, 1.0f
    };
    vk::Rect2D swapchainScissor{{0, 0}, m_swapchainRef.GetExtent()};
    m_commandBuffers[m_currentFrame].setViewport(0, 1, &swapchainViewport);
    m_commandBuffers[m_currentFrame].setScissor(0, 1, &swapchainScissor);

    // Render ImGui draw data into Swapchain
    ImGui_ImplVulkan_RenderDrawData(ImGui::GetDrawData(), static_cast<VkCommandBuffer>(m_commandBuffers[m_currentFrame]));

    m_commandBuffers[m_currentFrame].endRenderPass();
    m_commandBuffers[m_currentFrame].end();

    // 6. Submit Command Buffer to Graphics Queue
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

    // 7. Present image to Swapchain Present Queue
    vk::SwapchainKHR swapChains[] = { m_swapchainRef.GetSwapChain() };

    vk::PresentInfoKHR presentInfo{};
    presentInfo.waitSemaphoreCount = 1;
    presentInfo.pWaitSemaphores = signalSemaphores;
    presentInfo.swapchainCount = 1;
    presentInfo.pSwapchains = swapChains;
    presentInfo.pImageIndices = &imageIndex;

    VkResult presentResult = static_cast<VkResult>(m_deviceRef.GetPresentQueue().presentKHR(&presentInfo));
    if (presentResult == VK_ERROR_OUT_OF_DATE_KHR || presentResult == VK_SUBOPTIMAL_KHR || m_windowRef.IsResized()) {
        RecreateSwapchain();
    } else if (presentResult != VK_SUCCESS) {
        throw std::runtime_error("Failed to present Vulkan swapchain image!");
    }

    // 8. Advance current frame index (2 Frames in Flight)
    m_currentFrame = (m_currentFrame + 1) % MAX_FRAMES_IN_FLIGHT;
}

} // namespace Engine
