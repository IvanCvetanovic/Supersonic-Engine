#include "renderer/VulkanRenderer.hpp"

#include "core/AnimationSystem.hpp"
#include "core/RenderSystem.hpp"
#include "core/Components.hpp"
#include "core/EcsUtils.hpp"

#include "editor/Theme.hpp"

#include "imgui.h"
#include "backends/imgui_impl_glfw.h"
#include "backends/imgui_impl_vulkan.h"
#include "ImGuizmo.h"

#include <array>
#include <iostream>
#include <stdexcept>

namespace Supersonic {

VulkanRenderer::VulkanRenderer(VulkanDevice& device, VulkanSwapchain& swapchain, Window& window)
    : m_deviceRef(device), m_swapchainRef(swapchain), m_windowRef(window) {

    createRenderPass();
    createFramebuffers();
    createCommandPool();
    createCommandBuffers();
    createSyncObjects();

    m_meshRegistry = std::make_unique<MeshRegistry>(m_deviceRef, m_commandPool);
    m_shadowMap = std::make_unique<ShadowMap>(m_deviceRef);

    createUniformBuffers();
    createDescriptorPool();

    initImGui();

    std::cout << "[VulkanRenderer] Renderer initialized (pipeline pending offscreen render pass)." << std::endl;
}

void VulkanRenderer::SetOffscreenRenderPass(vk::RenderPass pass) {
    // The scene pipeline is built against the offscreen render pass, which the
    // editor owns. It is supplied once the editor has created its target.
    m_offscreenRenderPass = pass;
    createGraphicsPipeline();

    // The texture registry allocates against the pipeline's material set layout,
    // so it cannot exist until the pipeline does.
    m_textureRegistry = std::make_unique<TextureRegistry>(
        m_deviceRef, m_commandPool, m_pipeline->GetMaterialSetLayout());

    createDescriptorSets();
}

VulkanRenderer::~VulkanRenderer() {
    vk::Device device = m_deviceRef.GetDevice();

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

    m_textureRegistry.reset();
    m_uniformBuffers.clear();
    m_jointPaletteBuffers.clear();
    m_meshRegistry.reset();
    m_shadowPipeline.reset();
    m_shadowMap.reset();

    destroySyncObjects();

    if (m_commandPool) {
        device.destroyCommandPool(m_commandPool);
        m_commandPool = nullptr;
    }

    cleanupSwapchain();

    // After the pipelines, so anything compiled during this run is in the blob
    // that gets written out.
    m_pipelineCache.reset();

    std::cout << "[VulkanRenderer] Subsystem resources destroyed cleanly." << std::endl;
}

void VulkanRenderer::destroySyncObjects() {
    vk::Device device = m_deviceRef.GetDevice();

    for (auto& fence : m_inFlightFences) {
        if (fence) device.destroyFence(fence);
    }
    for (auto& semaphore : m_renderFinishedSemaphores) {
        if (semaphore) device.destroySemaphore(semaphore);
    }
    for (auto& semaphore : m_imageAvailableSemaphores) {
        if (semaphore) device.destroySemaphore(semaphore);
    }

    m_inFlightFences.clear();
    m_renderFinishedSemaphores.clear();
    m_imageAvailableSemaphores.clear();
    m_imagesInFlight.clear();
}

void VulkanRenderer::cleanupSwapchain() {
    vk::Device device = m_deviceRef.GetDevice();

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
    createFramebuffers();

    // The swapchain image count can change, and renderFinished semaphores are
    // sized per image, so the sync objects have to be rebuilt with it.
    destroySyncObjects();
    createSyncObjects();

    m_windowRef.ResetResizedFlag();
    std::cout << "[VulkanRenderer] Swapchain recreated for window size (" << width << "x" << height << ")." << std::endl;
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
    std::cout << "[VulkanRenderer] Swapchain RenderPass (ImGui UI Pass) created." << std::endl;
}

void VulkanRenderer::createFramebuffers() {
    const auto& imageViews = m_swapchainRef.GetImageViews();
    m_framebuffers.resize(imageViews.size());

    for (size_t i = 0; i < imageViews.size(); i++) {
        vk::ImageView attachments[] = { imageViews[i] };

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

void VulkanRenderer::createCommandPool() {
    QueueFamilyIndices queueFamilyIndices = m_deviceRef.FindQueueFamilies();

    vk::CommandPoolCreateInfo poolInfo{};
    poolInfo.flags = vk::CommandPoolCreateFlagBits::eResetCommandBuffer;
    poolInfo.queueFamilyIndex = queueFamilyIndices.graphicsFamily.value();

    m_commandPool = m_deviceRef.GetDevice().createCommandPool(poolInfo);
    std::cout << "[VulkanRenderer] CommandPool created successfully." << std::endl;
}

void VulkanRenderer::createCommandBuffers() {
    vk::CommandBufferAllocateInfo allocInfo{};
    allocInfo.commandPool = m_commandPool;
    allocInfo.level = vk::CommandBufferLevel::ePrimary;
    allocInfo.commandBufferCount = static_cast<uint32_t>(MAX_FRAMES_IN_FLIGHT);

    m_commandBuffers = m_deviceRef.GetDevice().allocateCommandBuffers(allocInfo);
    std::cout << "[VulkanRenderer] Allocated " << m_commandBuffers.size() << " CommandBuffers." << std::endl;
}

void VulkanRenderer::createSyncObjects() {
    const size_t imageCount = m_swapchainRef.GetImageViews().size();

    m_imageAvailableSemaphores.resize(MAX_FRAMES_IN_FLIGHT);
    m_inFlightFences.resize(MAX_FRAMES_IN_FLIGHT);

    // One renderFinished semaphore per swapchain image. With only
    // MAX_FRAMES_IN_FLIGHT of them, a submit could re-signal a semaphore whose
    // present wait had not been consumed yet on a 3-image swapchain.
    m_renderFinishedSemaphores.resize(imageCount);

    // Tracks which fence, if any, is currently guarding each swapchain image.
    m_imagesInFlight.assign(imageCount, vk::Fence{});

    vk::SemaphoreCreateInfo semaphoreInfo{};
    vk::FenceCreateInfo fenceInfo{};
    fenceInfo.flags = vk::FenceCreateFlagBits::eSignaled;

    vk::Device device = m_deviceRef.GetDevice();
    for (size_t i = 0; i < MAX_FRAMES_IN_FLIGHT; i++) {
        m_imageAvailableSemaphores[i] = device.createSemaphore(semaphoreInfo);
        m_inFlightFences[i] = device.createFence(fenceInfo);
    }
    for (size_t i = 0; i < imageCount; i++) {
        m_renderFinishedSemaphores[i] = device.createSemaphore(semaphoreInfo);
    }

    std::cout << "[VulkanRenderer] Sync primitives created ("
              << MAX_FRAMES_IN_FLIGHT << " frames in flight, " << imageCount << " images)." << std::endl;
}

void VulkanRenderer::createGraphicsPipeline() {
    if (!m_offscreenRenderPass) {
        throw std::runtime_error("Offscreen render pass must be set before creating the scene pipeline!");
    }

    // One cache backs all three pipelines and outlives the process, so a
    // swapchain recreation - which rebuilds every pipeline - costs a lookup
    // rather than three shader compiles.
    if (!m_pipelineCache) {
        m_pipelineCache = std::make_unique<PipelineCache>(m_deviceRef, "cache/pipeline_cache.bin");
    }

    VulkanPipeline::Options sceneOptions{};
    sceneOptions.cache = m_pipelineCache->Get();

    m_pipeline = std::make_unique<VulkanPipeline>(
        m_deviceRef.GetDevice(),
        m_offscreenRenderPass,
        "assets/shaders/vert.spv",
        "assets/shaders/frag.spv",
        sceneOptions);

    // Infinite ground grid: a full-screen triangle pair with no vertex input,
    // alpha blended, writing depth so scene geometry occludes it.
    VulkanPipeline::Options gridOptions{};
    gridOptions.blendEnable = true;
    gridOptions.depthWrite = false;
    gridOptions.cullMode = vk::CullModeFlagBits::eNone;
    gridOptions.useVertexInput = false;
    gridOptions.cache = m_pipelineCache->Get();

    m_gridPipeline = std::make_unique<VulkanPipeline>(
        m_deviceRef.GetDevice(),
        m_offscreenRenderPass,
        "assets/shaders/grid_vert.spv",
        "assets/shaders/grid_frag.spv",
        gridOptions);

    // Depth-only pass from the light. No colour attachment, and depth bias to
    // stop surfaces shadowing themselves.
    VulkanPipeline::Options shadowOptions{};
    shadowOptions.colorAttachmentCount = 0;
    shadowOptions.depthBias = true;
    // Front-face culling during the depth pass pushes acne to back faces, which
    // the camera cannot see.
    shadowOptions.cullMode = vk::CullModeFlagBits::eFront;
    shadowOptions.cache = m_pipelineCache->Get();
    // The depth pass takes the cascade's transform in the push constant instead
    // of reading the scene UBO, so its range is a different size.
    shadowOptions.pushConstantSize = static_cast<uint32_t>(sizeof(ShadowPushConstantData));
    // Retuned down. These constants were set against a fixed ~80-unit ortho
    // range; a cascade's depth range now spans the whole scene along the light
    // axis and can be several times that, which turns the same constants into
    // several times the world-space offset and visibly detaches contact
    // shadows. The shader's per-cascade normal offset does the work now.
    shadowOptions.depthBiasConstant = 0.6f;
    shadowOptions.depthBiasSlope = 1.1f;

    m_shadowPipeline = std::make_unique<VulkanPipeline>(
        m_deviceRef.GetDevice(),
        m_shadowMap->GetRenderPass(),
        "assets/shaders/shadow_vert.spv",
        "assets/shaders/shadow_frag.spv",
        shadowOptions);

    // Persist whatever the driver just compiled, so the next launch starts warm.
    m_pipelineCache->Save();

    std::cout << "[VulkanRenderer] Scene, grid and shadow pipelines created." << std::endl;
}

void VulkanRenderer::createUniformBuffers() {
    const vk::DeviceSize bufferSize = sizeof(UniformBufferObject);
    m_uniformBuffers.resize(MAX_FRAMES_IN_FLIGHT);

    for (size_t i = 0; i < MAX_FRAMES_IN_FLIGHT; i++) {
        m_uniformBuffers[i] = std::make_unique<VulkanBuffer>(
            m_deviceRef.GetAllocator(),
            bufferSize,
            vk::BufferUsageFlagBits::eUniformBuffer,
            VMA_MEMORY_USAGE_CPU_TO_GPU,
            VMA_ALLOCATION_CREATE_MAPPED_BIT);
    }

    // One joint palette per frame in flight, persistently mapped. Sized once at
    // capacity: a storage buffer descriptor has to be valid every frame whether
    // or not the scene has anything skinned in it.
    m_jointPaletteBuffers.resize(MAX_FRAMES_IN_FLIGHT);
    const vk::DeviceSize paletteSize = sizeof(glm::mat4) * kMaxPaletteMatrices;
    for (size_t i = 0; i < MAX_FRAMES_IN_FLIGHT; i++) {
        m_jointPaletteBuffers[i] = std::make_unique<VulkanBuffer>(
            m_deviceRef.GetAllocator(),
            paletteSize,
            vk::BufferUsageFlagBits::eStorageBuffer,
            VMA_MEMORY_USAGE_CPU_TO_GPU,
            VMA_ALLOCATION_CREATE_MAPPED_BIT);
    }

    std::cout << "[VulkanRenderer] Created " << m_uniformBuffers.size() << " VMA Uniform Buffers and "
              << m_jointPaletteBuffers.size() << " joint palettes ("
              << kMaxPaletteMatrices << " matrices each)." << std::endl;
}

void VulkanRenderer::createDescriptorPool() {
    std::array<vk::DescriptorPoolSize, 3> poolSizes{};
    poolSizes[0].type = vk::DescriptorType::eUniformBuffer;
    poolSizes[0].descriptorCount = static_cast<uint32_t>(MAX_FRAMES_IN_FLIGHT);

    // One shadow map sampler per frame in flight. Material textures live in the
    // TextureRegistry's own pool.
    poolSizes[1].type = vk::DescriptorType::eCombinedImageSampler;
    poolSizes[1].descriptorCount = static_cast<uint32_t>(MAX_FRAMES_IN_FLIGHT);

    // The joint palette. Omitting this makes allocateDescriptorSets throw at
    // startup, which presents as a launch failure rather than as a rendering
    // bug - so it is worth being explicit that binding 2 needs its own size.
    poolSizes[2].type = vk::DescriptorType::eStorageBuffer;
    poolSizes[2].descriptorCount = static_cast<uint32_t>(MAX_FRAMES_IN_FLIGHT);

    vk::DescriptorPoolCreateInfo poolInfo{};
    poolInfo.poolSizeCount = static_cast<uint32_t>(poolSizes.size());
    poolInfo.pPoolSizes = poolSizes.data();
    poolInfo.maxSets = static_cast<uint32_t>(MAX_FRAMES_IN_FLIGHT);

    m_descriptorPool = m_deviceRef.GetDevice().createDescriptorPool(poolInfo);
    std::cout << "[VulkanRenderer] DescriptorPool created successfully." << std::endl;
}

void VulkanRenderer::createDescriptorSets() {
    const std::vector<vk::DescriptorSetLayout> layouts(MAX_FRAMES_IN_FLIGHT, m_pipeline->GetSceneSetLayout());

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

        vk::DescriptorImageInfo shadowInfo{};
        // The shadow render pass leaves the image in this layout.
        shadowInfo.imageLayout = vk::ImageLayout::eDepthStencilReadOnlyOptimal;
        shadowInfo.imageView = m_shadowMap->GetImageView();
        shadowInfo.sampler = m_shadowMap->GetSampler();

        if (!shadowInfo.sampler || !shadowInfo.imageView) {
            throw std::runtime_error("Shadow map is missing a sampler or view; the descriptor write would fault!");
        }

        vk::DescriptorBufferInfo paletteInfo{};
        paletteInfo.buffer = m_jointPaletteBuffers[i]->GetBuffer();
        paletteInfo.offset = 0;
        paletteInfo.range = sizeof(glm::mat4) * kMaxPaletteMatrices;

        std::array<vk::WriteDescriptorSet, 3> writes{};

        writes[0].dstSet = m_descriptorSets[i];
        writes[0].dstBinding = 0;
        writes[0].descriptorType = vk::DescriptorType::eUniformBuffer;
        writes[0].descriptorCount = 1;
        writes[0].pBufferInfo = &bufferInfo;

        writes[1].dstSet = m_descriptorSets[i];
        writes[1].dstBinding = 1;
        writes[1].descriptorType = vk::DescriptorType::eCombinedImageSampler;
        writes[1].descriptorCount = 1;
        writes[1].pImageInfo = &shadowInfo;

        writes[2].dstSet = m_descriptorSets[i];
        writes[2].dstBinding = 2;
        writes[2].descriptorType = vk::DescriptorType::eStorageBuffer;
        writes[2].descriptorCount = 1;
        writes[2].pBufferInfo = &paletteInfo;

        m_deviceRef.GetDevice().updateDescriptorSets(writes, nullptr);
    }

    std::cout << "[VulkanRenderer] Allocated and updated " << m_descriptorSets.size() << " scene DescriptorSets." << std::endl;
}

glm::vec3 VulkanRenderer::gatherLights(entt::registry& registry, UniformBufferObject& ubo) const {
    int count = 0;
    glm::vec3 shadowDirection(0.0f, 1.0f, 0.0f);
    bool haveShadowCaster = false;

    // The first directional light is the shadow caster, and is deliberately
    // placed at index 0 because the shader only shadows lights[0].
    for (auto entity : registry.view<LightComponent>()) {
        if (count >= kMaxLights) break;
        const auto& light = registry.get<LightComponent>(entity);

        GpuLight gpu{};
        if (light.type == static_cast<int>(LightType::Point)) {
            glm::vec3 position(0.0f);
            if (const auto* transform = registry.try_get<TransformComponent>(entity)) {
                position = transform->position;
            }
            gpu.positionOrDirection = glm::vec4(position, 1.0f);
        } else {
            const glm::vec3 dir = glm::length(light.direction) > 1e-4f
                                ? glm::normalize(light.direction)
                                : glm::vec3(0.0f, 1.0f, 0.0f);
            gpu.positionOrDirection = glm::vec4(dir, 0.0f);

            if (!haveShadowCaster && light.castsShadow) {
                shadowDirection = dir;
                haveShadowCaster = true;
                // Swap into slot 0 so the shadowed light is the one the shader
                // applies the shadow factor to.
                if (count != 0) {
                    ubo.lights[count] = ubo.lights[0];
                    ubo.lights[0] = gpu;
                    ubo.lights[0].colorAndIntensity = glm::vec4(light.color, light.intensity);
                    ubo.lights[0].attenuation = glm::vec4(light.range, 0.0f, 0.0f, 0.0f);
                    ++count;
                    continue;
                }
            }
        }

        gpu.colorAndIntensity = glm::vec4(light.color, light.intensity);
        gpu.attenuation = glm::vec4(light.range, 0.0f, 0.0f, 0.0f);
        ubo.lights[count] = gpu;

        if (count == 0) {
            ubo.ambientColor = glm::vec4(light.ambient, 1.0f);
        }
        ++count;
    }

    if (count == 0) {
        // No lights authored: fall back to a single overhead key light so the
        // scene is not simply black.
        ubo.lights[0].positionOrDirection = glm::vec4(glm::normalize(glm::vec3(0.6f, 1.0f, 0.5f)), 0.0f);
        ubo.lights[0].colorAndIntensity = glm::vec4(1.0f, 0.95f, 0.88f, 1.5f);
        ubo.lights[0].attenuation = glm::vec4(25.0f, 0.0f, 0.0f, 0.0f);
        ubo.ambientColor = glm::vec4(0.12f, 0.12f, 0.14f, 1.0f);
        shadowDirection = glm::normalize(glm::vec3(0.6f, 1.0f, 0.5f));
        count = 1;
    }

    ubo.lightCount = glm::vec4(static_cast<float>(count), 0.0f, 0.0f, 0.0f);
    return shadowDirection;
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
    // ImGui_ImplVulkan_RemoveTexture frees individual sets, which requires this.
    poolInfo.flags = vk::DescriptorPoolCreateFlagBits::eFreeDescriptorSet;
    poolInfo.maxSets = 1000 * static_cast<uint32_t>(poolSizes.size());
    poolInfo.poolSizeCount = static_cast<uint32_t>(poolSizes.size());
    poolInfo.pPoolSizes = poolSizes.data();

    m_imguiPool = m_deviceRef.GetDevice().createDescriptorPool(poolInfo);

    // 2. Setup ImGui Context
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
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

void VulkanRenderer::NewImGuiFrame() {
    ImGui_ImplVulkan_NewFrame();
    ImGui_ImplGlfw_NewFrame();
    ImGui::NewFrame();
    ImGuizmo::BeginFrame();
}

void VulkanRenderer::DrawFrame(entt::registry& registry,
                               VulkanOffscreen& offscreen,
                               ImDrawData* drawData,
                               const CameraComponent& camera) {
    vk::Device device = m_deviceRef.GetDevice();

    const glm::mat4 viewMatrix = camera.getViewMatrix();
    const glm::mat4 projMatrix = camera.getProjectionMatrix();
    const glm::vec3 cameraPosition = camera.position;

    // The resize flag is checked BEFORE acquiring. Checking it after a
    // successful acquire abandoned the frame with the acquire semaphore left
    // signalled, and the same semaphore was then reused next frame.
    if (m_windowRef.IsResized()) {
        RecreateSwapchain();
        return;
    }

    vk::Result waitResult = device.waitForFences(1, &m_inFlightFences[m_currentFrame], VK_TRUE, UINT64_MAX);
    if (waitResult != vk::Result::eSuccess) {
        throw std::runtime_error("Failed to wait for Vulkan inFlightFence!");
    }

    uint32_t imageIndex = 0;
    VkResult acquireResult = vkAcquireNextImageKHR(
        static_cast<VkDevice>(device),
        static_cast<VkSwapchainKHR>(m_swapchainRef.GetSwapChain()),
        UINT64_MAX,
        static_cast<VkSemaphore>(m_imageAvailableSemaphores[m_currentFrame]),
        VK_NULL_HANDLE,
        &imageIndex);

    if (acquireResult == VK_ERROR_OUT_OF_DATE_KHR) {
        RecreateSwapchain();
        return;
    }
    if (acquireResult != VK_SUCCESS && acquireResult != VK_SUBOPTIMAL_KHR) {
        throw std::runtime_error("Failed to acquire Vulkan swapchain image!");
    }

    // If a previous frame is still using this image, wait on its fence before
    // reusing the image's renderFinished semaphore.
    if (m_imagesInFlight[imageIndex]) {
        vk::Result imgWait = device.waitForFences(1, &m_imagesInFlight[imageIndex], VK_TRUE, UINT64_MAX);
        if (imgWait != vk::Result::eSuccess) {
            throw std::runtime_error("Failed to wait for in-flight image fence!");
        }
    }
    m_imagesInFlight[imageIndex] = m_inFlightFences[m_currentFrame];

    // Update per-frame scene constants.
    UniformBufferObject ubo{};
    ubo.view = viewMatrix;
    ubo.proj = projMatrix;
    ubo.cameraPosition = glm::vec4(cameraPosition, 1.0f);
    ubo.ambientColor = glm::vec4(0.12f, 0.12f, 0.14f, 1.0f);
    const glm::vec3 shadowDirection = gatherLights(registry, ubo);

    // Cascades are fitted to the camera, so they need the same camera the scene
    // pass is about to use rather than a fixed box around the origin.
    glm::vec3 sceneMin(-20.0f);
    glm::vec3 sceneMax(20.0f);
    RenderSystem::ComputeSceneBounds(registry, *m_meshRegistry, sceneMin, sceneMax);

    const CascadeSetup cascades = ShadowCascades::Build(
        camera, shadowDirection, sceneMin, sceneMax, m_shadowMap->GetResolution());

    for (uint32_t i = 0; i < kShadowCascadeCount; ++i) {
        ubo.cascadeViewProj[i] = cascades.viewProj[i];
        ubo.cascadeSplits[static_cast<int>(i)] = cascades.splitDepth[i];
        ubo.cascadeTexelWorld[static_cast<int>(i)] = cascades.texelWorldSize[i];
    }

    // Joint palettes for this frame. Must happen before recording, because the
    // per-draw push constant carries the offset this writes.
    const uint32_t paletteCount =
        AnimationSystem::GatherPalettes(registry, m_paletteScratch, kMaxPaletteMatrices);
    if (paletteCount > 0) {
        m_jointPaletteBuffers[m_currentFrame]->UploadData(
            m_paletteScratch.data(), sizeof(glm::mat4) * paletteCount);
    }
    m_renderStats.skinnedMatrices = paletteCount;

    // Culling frustum for the scene pass. Each cascade carries its own for the
    // depth pass - an object behind the camera can still cast a shadow into
    // view, so culling the depth pass against the camera would make shadows pop
    // in and out.
    const Frustum cameraFrustum = Frustum::FromMatrix(projMatrix * viewMatrix);
    m_renderStats = RenderSystem::Stats{};

    m_uniformBuffers[m_currentFrame]->UploadData(&ubo, sizeof(ubo));

    vk::Result resetFenceRes = device.resetFences(1, &m_inFlightFences[m_currentFrame]);
    if (resetFenceRes != vk::Result::eSuccess) {
        throw std::runtime_error("Failed to reset Vulkan inFlightFence!");
    }

    vk::CommandBuffer cmd = m_commandBuffers[m_currentFrame];
    cmd.reset();

    vk::CommandBufferBeginInfo beginInfo{};
    cmd.begin(beginInfo);

    // ---------------------------------------------------------------------
    // PASS 0: Shadow map (depth only, from the light)
    // ---------------------------------------------------------------------
    for (uint32_t cascade = 0; cascade < kShadowCascadeCount; ++cascade) {
        vk::RenderPassBeginInfo shadowPassInfo{};
        shadowPassInfo.renderPass = m_shadowMap->GetRenderPass();
        shadowPassInfo.framebuffer = m_shadowMap->GetFramebuffer(cascade);
        shadowPassInfo.renderArea.offset = vk::Offset2D{0, 0};
        shadowPassInfo.renderArea.extent = vk::Extent2D{m_shadowMap->GetResolution(),
                                                        m_shadowMap->GetResolution()};

        vk::ClearValue shadowClear{};
        shadowClear.depthStencil = vk::ClearDepthStencilValue{1.0f, 0};
        shadowPassInfo.clearValueCount = 1;
        shadowPassInfo.pClearValues = &shadowClear;

        cmd.beginRenderPass(shadowPassInfo, vk::SubpassContents::eInline);

        const float shadowDim = static_cast<float>(m_shadowMap->GetResolution());
        const vk::Viewport shadowViewport{ 0.0f, 0.0f, shadowDim, shadowDim, 0.0f, 1.0f };
        const vk::Rect2D shadowScissor{{0, 0}, shadowPassInfo.renderArea.extent};
        cmd.setViewport(0, 1, &shadowViewport);
        cmd.setScissor(0, 1, &shadowScissor);

        RenderSystem::RenderDepthOnly(registry, *m_shadowPipeline, *m_meshRegistry,
                                      cmd, m_descriptorSets[m_currentFrame],
                                      cascades.viewProj[cascade],
                                      cascades.frustum[cascade], m_renderStats);

        cmd.endRenderPass();
    }

    // ---------------------------------------------------------------------
    // PASS 1: Offscreen 3D scene
    // ---------------------------------------------------------------------
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

    cmd.beginRenderPass(offscreenPassInfo, vk::SubpassContents::eInline);

    const vk::Viewport offscreenViewport{
        0.0f, 0.0f,
        static_cast<float>(offscreen.GetWidth()),
        static_cast<float>(offscreen.GetHeight()),
        0.0f, 1.0f
    };
    const vk::Rect2D offscreenScissor{{0, 0}, offscreenPassInfo.renderArea.extent};
    cmd.setViewport(0, 1, &offscreenViewport);
    cmd.setScissor(0, 1, &offscreenScissor);

    RenderSystem::Render(registry, *m_pipeline, *m_meshRegistry, *m_textureRegistry,
                         cmd, m_descriptorSets[m_currentFrame],
                         cameraFrustum, m_renderStats);

    // Ground grid last so it blends over the scene it is depth-tested against.
    cmd.bindPipeline(vk::PipelineBindPoint::eGraphics, m_gridPipeline->GetPipeline());
    cmd.bindDescriptorSets(vk::PipelineBindPoint::eGraphics, m_gridPipeline->GetLayout(),
                           VulkanPipeline::kSceneSet, 1, &m_descriptorSets[m_currentFrame], 0, nullptr);
    cmd.draw(6, 1, 0, 0);

    cmd.endRenderPass();

    // ---------------------------------------------------------------------
    // PASS 2: Swapchain (ImGui)
    // ---------------------------------------------------------------------
    vk::RenderPassBeginInfo swapchainPassInfo{};
    swapchainPassInfo.renderPass = m_renderPass;
    swapchainPassInfo.framebuffer = m_framebuffers[imageIndex];
    swapchainPassInfo.renderArea.offset = vk::Offset2D{0, 0};
    swapchainPassInfo.renderArea.extent = m_swapchainRef.GetExtent();

    vk::ClearValue swapchainClearColor = vk::ClearColorValue{std::array<float, 4>{0.1f, 0.1f, 0.1f, 1.0f}};
    swapchainPassInfo.clearValueCount = 1;
    swapchainPassInfo.pClearValues = &swapchainClearColor;

    cmd.beginRenderPass(swapchainPassInfo, vk::SubpassContents::eInline);

    const vk::Viewport swapchainViewport{
        0.0f, 0.0f,
        static_cast<float>(m_swapchainRef.GetExtent().width),
        static_cast<float>(m_swapchainRef.GetExtent().height),
        0.0f, 1.0f
    };
    const vk::Rect2D swapchainScissor{{0, 0}, m_swapchainRef.GetExtent()};
    cmd.setViewport(0, 1, &swapchainViewport);
    cmd.setScissor(0, 1, &swapchainScissor);

    if (drawData) {
        ImGui_ImplVulkan_RenderDrawData(drawData, static_cast<VkCommandBuffer>(cmd));
    }

    cmd.endRenderPass();
    cmd.end();

    const vk::Semaphore waitSemaphores[] = { m_imageAvailableSemaphores[m_currentFrame] };
    const vk::PipelineStageFlags waitStages[] = { vk::PipelineStageFlagBits::eColorAttachmentOutput };
    const vk::Semaphore signalSemaphores[] = { m_renderFinishedSemaphores[imageIndex] };

    vk::SubmitInfo submitInfo{};
    submitInfo.waitSemaphoreCount = 1;
    submitInfo.pWaitSemaphores = waitSemaphores;
    submitInfo.pWaitDstStageMask = waitStages;
    submitInfo.commandBufferCount = 1;
    submitInfo.pCommandBuffers = &cmd;
    submitInfo.signalSemaphoreCount = 1;
    submitInfo.pSignalSemaphores = signalSemaphores;

    vk::Result submitRes = m_deviceRef.GetGraphicsQueue().submit(1, &submitInfo, m_inFlightFences[m_currentFrame]);
    if (submitRes != vk::Result::eSuccess) {
        throw std::runtime_error("Failed to submit command buffer to Vulkan graphics queue!");
    }

    const vk::SwapchainKHR swapChains[] = { m_swapchainRef.GetSwapChain() };

    vk::PresentInfoKHR presentInfo{};
    presentInfo.waitSemaphoreCount = 1;
    presentInfo.pWaitSemaphores = signalSemaphores;
    presentInfo.swapchainCount = 1;
    presentInfo.pSwapchains = swapChains;
    presentInfo.pImageIndices = &imageIndex;

    VkResult presentResult = static_cast<VkResult>(m_deviceRef.GetPresentQueue().presentKHR(&presentInfo));
    if (presentResult == VK_ERROR_OUT_OF_DATE_KHR || presentResult == VK_SUBOPTIMAL_KHR) {
        RecreateSwapchain();
    } else if (presentResult != VK_SUCCESS) {
        throw std::runtime_error("Failed to present Vulkan swapchain image!");
    }

    m_currentFrame = (m_currentFrame + 1) % MAX_FRAMES_IN_FLIGHT;
}

} // namespace Supersonic
