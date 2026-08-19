#include "renderer/VulkanRenderer.hpp"

#include "core/AnimationSystem.hpp"
#include "core/RenderSystem.hpp"
#include "core/Components.hpp"
#include "core/EcsUtils.hpp"

#include "editor/Theme.hpp"
#include "editor/EditorFonts.hpp"

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
    m_pointShadowMap = std::make_unique<PointShadowMap>(m_deviceRef);
    m_spotShadowMap = std::make_unique<ShadowMap>(m_deviceRef, 1024,
                                                 SpotLight::kMaxShadowCasters);

    createUniformBuffers();
    createDescriptorPool();

    initImGui();

    std::cout << "[VulkanRenderer] Renderer initialized (pipeline pending offscreen render pass)." << std::endl;
}

void VulkanRenderer::SetOffscreenRenderPass(vk::RenderPass pass, vk::SampleCountFlagBits samples) {
    // The scene pipeline is built against the offscreen render pass, which the
    // editor owns. It is supplied once the editor has created its target - and
    // with its sample count, because a pipeline whose rasterizationSamples does
    // not match its render pass is invalid, not merely lower quality.
    m_offscreenRenderPass = pass;
    m_offscreenSamples = samples;
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
    sceneOptions.samples = m_offscreenSamples;

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
    gridOptions.samples = m_offscreenSamples;

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
    // Vertex only: shadow.frag declares no push constant block, and pushing a
    // vertex-only range against a pipeline that claims both stages is a
    // validation error on every shadow draw.
    shadowOptions.pushConstantStages = vk::ShaderStageFlagBits::eVertex;
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
    //
    // Plus the point-light cubes, which are a second binding of the same type:
    // a pool size counts DESCRIPTORS, not bindings, so leaving these out makes
    // allocation fail at startup with a message about the pool being out of
    // memory rather than about the array nobody counted.
    poolSizes[1].type = vk::DescriptorType::eCombinedImageSampler;
    poolSizes[1].descriptorCount =
        static_cast<uint32_t>(MAX_FRAMES_IN_FLIGHT) * (2u + PointShadow::kMaxShadowCasters);

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

        const std::vector<vk::DescriptorImageInfo>& pointShadowInfos =
            m_pointShadowMap->GetDescriptorInfos();

        vk::DescriptorImageInfo spotShadowInfo{};
        spotShadowInfo.imageLayout = vk::ImageLayout::eDepthStencilReadOnlyOptimal;
        spotShadowInfo.imageView = m_spotShadowMap->GetImageView();
        spotShadowInfo.sampler = m_spotShadowMap->GetSampler();

        std::array<vk::WriteDescriptorSet, 5> writes{};

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

        // Every slot is written whether a light is using it or not: reading an
        // unwritten descriptor is undefined behaviour even inside a branch the
        // shader never takes.
        writes[3].dstSet = m_descriptorSets[i];
        writes[3].dstBinding = 3;
        writes[3].descriptorType = vk::DescriptorType::eCombinedImageSampler;
        writes[3].descriptorCount = static_cast<uint32_t>(pointShadowInfos.size());
        writes[3].pImageInfo = pointShadowInfos.data();

        writes[4].dstSet = m_descriptorSets[i];
        writes[4].dstBinding = 4;
        writes[4].descriptorType = vk::DescriptorType::eCombinedImageSampler;
        writes[4].descriptorCount = 1;
        writes[4].pImageInfo = &spotShadowInfo;

        m_deviceRef.GetDevice().updateDescriptorSets(writes, nullptr);
    }

    std::cout << "[VulkanRenderer] Allocated and updated " << m_descriptorSets.size() << " scene DescriptorSets." << std::endl;
}

glm::vec3 VulkanRenderer::gatherLights(entt::registry& registry, UniformBufferObject& ubo,
                                       std::vector<PointShadowCaster>& outCasters,
                                       std::vector<SpotShadowCaster>& outSpots) const {
    outCasters.clear();
    outSpots.clear();
    int count = 0;
    glm::vec3 shadowDirection(0.0f, 1.0f, 0.0f);
    bool haveShadowCaster = false;

    // Chosen before the loop, so it does not depend on which light the packing
    // order happens to put in slot 0 - and so the directional-light swap below
    // cannot skip past it.
    if (const auto ambientLight = FindAmbientLight(registry); ambientLight != entt::null) {
        const auto& source = registry.get<LightComponent>(ambientLight);
        ubo.ambientColor = glm::vec4(source.ambient, 1.0f);
        ubo.ambientGround = glm::vec4(source.ambientGround, 1.0f);
    }

    // The first directional light is the shadow caster, and is deliberately
    // placed at index 0 because the shader only shadows lights[0].
    for (auto entity : registry.view<LightComponent>()) {
        if (count >= kMaxLights) break;
        const auto& light = registry.get<LightComponent>(entity);

        GpuLight gpu{};
        if (light.type == static_cast<int>(LightType::Spot)) {
            glm::vec3 position(0.0f);
            if (const auto* transform = registry.try_get<TransformComponent>(entity)) {
                position = transform->position;
            }
            // A spot needs a position AND an aim, so w = 2 tells the shader to
            // read spotDirection as well and apply the cone.
            gpu.positionOrDirection = glm::vec4(position, 2.0f);

            const glm::vec3 aim = glm::length(light.direction) > 1e-4f
                                ? glm::normalize(light.direction)
                                : glm::vec3(0.0f, -1.0f, 0.0f);

            const float inner = std::min(light.innerAngle, light.outerAngle);
            const float outer = std::max(light.innerAngle, light.outerAngle);

            // Cosines rather than angles: the shader compares a dot product,
            // and doing the trigonometry here costs nothing per frame instead
            // of an inverse cosine per fragment per light.
            gpu.spotDirection = glm::vec4(aim, std::cos(outer));
            gpu.attenuation.z = std::cos(inner);

            if (light.castsShadow && outSpots.size() < SpotLight::kMaxShadowCasters) {
                const auto slot = static_cast<uint32_t>(outSpots.size());
                const glm::mat4 viewProj =
                    SpotLight::BuildViewProj(position, aim, outer, light.range);

                outSpots.push_back({ viewProj, slot });
                ubo.spotViewProj[slot] = viewProj;
                gpu.attenuation.w = static_cast<float>(slot);
            }
        } else if (light.type == static_cast<int>(LightType::Point)) {
            glm::vec3 position(0.0f);
            if (const auto* transform = registry.try_get<TransformComponent>(entity)) {
                position = transform->position;
            }
            gpu.positionOrDirection = glm::vec4(position, 1.0f);

            // First come, first served, up to the fixed number of cubes. A
            // light that misses out simply does not cast, which is visible and
            // predictable - unlike reallocating images mid-frame to fit it.
            if (light.castsShadow && outCasters.size() < PointShadow::kMaxShadowCasters) {
                const auto slot = static_cast<uint32_t>(outCasters.size());
                outCasters.push_back({ position, light.range, slot });
                gpu.attenuation.y = static_cast<float>(slot);
            }
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
                    ubo.lights[0].attenuation = glm::vec4(light.range, -1.0f, 0.0f, 0.0f);
                    ++count;
                    continue;
                }
            }
        }

        gpu.colorAndIntensity = glm::vec4(light.color, light.intensity);
        // Only x is rewritten. y, z and w carry the cube slot, the spot's
        // inner cone and the spot's shadow slot, all assigned above - and
        // overwriting the lot is exactly the bug that once made every point
        // light report cube slot 0.
        gpu.attenuation.x = light.range;
        ubo.lights[count] = gpu;

        ++count;
    }

    if (count == 0) {
        // No lights authored: fall back to a single overhead key light so the
        // scene is not simply black.
        ubo.lights[0].positionOrDirection = glm::vec4(glm::normalize(glm::vec3(0.6f, 1.0f, 0.5f)), 0.0f);
        ubo.lights[0].colorAndIntensity = glm::vec4(1.0f, 0.95f, 0.88f, 1.5f);
        ubo.lights[0].attenuation = glm::vec4(25.0f, -1.0f, 0.0f, 0.0f);
        ubo.ambientColor = glm::vec4(0.12f, 0.12f, 0.14f, 1.0f);
        ubo.ambientGround = glm::vec4(0.10f, 0.09f, 0.08f, 1.0f);
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

    // Fonts are built BEFORE the Vulkan backend is initialised. Adding a font
    // afterwards means destroying and re-uploading a font texture the backend
    // may already have recorded into an unsubmitted command buffer - the same
    // class of use-after-free that the offscreen target's descriptor set caused
    // twice in this renderer.
    //
    // The DPI scale comes from the monitor GLFW put the window on, so the
    // editor is legible on a 4K panel instead of being rendered at a third the
    // intended size.
    float dpiScale = 1.0f;
    if (GLFWmonitor* monitor = glfwGetPrimaryMonitor()) {
        float xScale = 1.0f;
        float yScale = 1.0f;
        glfwGetMonitorContentScale(monitor, &xScale, &yScale);
        if (xScale > 0.0f) dpiScale = xScale;
    }

    EditorFonts::Load(dpiScale);
    Theme::ApplyEngineDarkTheme(dpiScale);

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
    ubo.ambientGround = glm::vec4(0.10f, 0.09f, 0.08f, 1.0f);
    const glm::vec3 shadowDirection =
        gatherLights(registry, ubo, m_pointShadowCasters, m_spotShadowCasters);

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

    // Clear the frame's counters FIRST. This assignment used to sit below the
    // palette gather, so skinnedMatrices was written and then immediately
    // zeroed by the reset - the statistics panel reported 0 skinned matrices
    // for every frame the engine has ever rendered, including frames that
    // uploaded a thousand.
    m_renderStats = RenderSystem::Stats{};

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
    // PASS 0b: Cube shadow maps, six faces per shadow-casting point light
    // ---------------------------------------------------------------------
    //
    // Every slot is visited, not only the ones a light claimed. An unrendered
    // slot keeps the layout it was created with while its descriptor says it
    // is ready to sample - and since the cubes are one descriptor array, a
    // single untouched one is enough to make the whole array invalid. Clearing
    // it also discards whatever a previous frame left there, which would
    // otherwise be a shadow cast by a light that has stopped casting.
    for (uint32_t slot = 0; slot < PointShadow::kMaxShadowCasters; ++slot) {
        const PointShadowCaster* caster = nullptr;
        for (const PointShadowCaster& candidate : m_pointShadowCasters) {
            if (candidate.slot == slot) { caster = &candidate; break; }
        }

        const auto faceViewProj = caster
            ? PointShadow::BuildFaceViewProj(caster->position, caster->range)
            : std::array<glm::mat4, PointShadow::kFaceCount>{};

        for (uint32_t face = 0; face < PointShadow::kFaceCount; ++face) {
            vk::RenderPassBeginInfo facePass{};
            facePass.renderPass = m_pointShadowMap->GetRenderPass();
            facePass.framebuffer = m_pointShadowMap->GetFramebuffer(slot, face);
            facePass.renderArea.offset = vk::Offset2D{0, 0};
            facePass.renderArea.extent = vk::Extent2D{m_pointShadowMap->GetResolution(),
                                                      m_pointShadowMap->GetResolution()};

            vk::ClearValue faceClear{};
            faceClear.depthStencil = vk::ClearDepthStencilValue{1.0f, 0};
            facePass.clearValueCount = 1;
            facePass.pClearValues = &faceClear;

            cmd.beginRenderPass(facePass, vk::SubpassContents::eInline);

            const float faceDim = static_cast<float>(m_pointShadowMap->GetResolution());
            const vk::Viewport faceViewport{ 0.0f, 0.0f, faceDim, faceDim, 0.0f, 1.0f };
            const vk::Rect2D faceScissor{{0, 0}, facePass.renderArea.extent};
            cmd.setViewport(0, 1, &faceViewport);
            cmd.setScissor(0, 1, &faceScissor);

            // Culled per face, which is the whole reason for six passes rather
            // than one multiview pass: a face only rasterises what it can see.
            // A slot with no light draws nothing and keeps its clear, which
            // reads as "no occluder anywhere" and therefore as fully lit.
            if (caster) {
                RenderSystem::RenderDepthOnly(registry, *m_shadowPipeline, *m_meshRegistry,
                                              cmd, m_descriptorSets[m_currentFrame],
                                              faceViewProj[face],
                                              Frustum::FromMatrix(faceViewProj[face]),
                                              m_renderStats);
            }

            cmd.endRenderPass();
        }
    }
    // ---------------------------------------------------------------------
    // PASS 0c: Spot light shadow maps, one frustum each
    // ---------------------------------------------------------------------
    // Every layer, for the same reason as the cubes above.
    for (uint32_t slot = 0; slot < SpotLight::kMaxShadowCasters; ++slot) {
        const SpotShadowCaster* spot = nullptr;
        for (const SpotShadowCaster& candidate : m_spotShadowCasters) {
            if (candidate.slot == slot) { spot = &candidate; break; }
        }

        vk::RenderPassBeginInfo spotPass{};
        spotPass.renderPass = m_spotShadowMap->GetRenderPass();
        spotPass.framebuffer = m_spotShadowMap->GetFramebuffer(slot);
        spotPass.renderArea.offset = vk::Offset2D{0, 0};
        spotPass.renderArea.extent = vk::Extent2D{m_spotShadowMap->GetResolution(),
                                                  m_spotShadowMap->GetResolution()};

        vk::ClearValue spotClear{};
        spotClear.depthStencil = vk::ClearDepthStencilValue{1.0f, 0};
        spotPass.clearValueCount = 1;
        spotPass.pClearValues = &spotClear;

        cmd.beginRenderPass(spotPass, vk::SubpassContents::eInline);

        const float spotDim = static_cast<float>(m_spotShadowMap->GetResolution());
        const vk::Viewport spotViewport{ 0.0f, 0.0f, spotDim, spotDim, 0.0f, 1.0f };
        const vk::Rect2D spotScissor{{0, 0}, spotPass.renderArea.extent};
        cmd.setViewport(0, 1, &spotViewport);
        cmd.setScissor(0, 1, &spotScissor);

        // One frustum, unlike the six a point light needs: a cone only ever
        // looks one way.
        if (spot) {
            RenderSystem::RenderDepthOnly(registry, *m_shadowPipeline, *m_meshRegistry,
                                          cmd, m_descriptorSets[m_currentFrame],
                                          spot->viewProj,
                                          Frustum::FromMatrix(spot->viewProj),
                                          m_renderStats);
        }

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
    // Linear, not display-referred. This value now passes through the tone map
    // and the sRGB encode in the composite, so the old 0.02 - which used to be
    // written straight to a UNORM image and shown verbatim - came out at about
    // 0.18 and turned the background from near-black into mid-grey.
    //
    // 0.00023 is the linear radiance that encodes back to 0.02 on screen:
    // pow(x / (1 + x), 1/2.2) == 0.02.
    offscreenClearValues[0].color =
        vk::ClearColorValue{std::array<float, 4>{0.00023f, 0.00023f, 0.00023f, 1.0f}};
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

    // Bloom and the tone map, on the same command buffer and after the scene
    // pass has ended. The scene image is linear and floating point until this
    // runs; the chain's output is what the editor actually displays.
    offscreen.RecordPostProcess(cmd);

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
