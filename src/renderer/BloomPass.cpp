#include "renderer/BloomPass.hpp"
#include "core/Log.hpp"

#include <array>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <vector>

namespace Supersonic {

namespace {

std::vector<char> readFile(const std::string& path) {
    std::ifstream file(path, std::ios::ate | std::ios::binary);
    if (!file.is_open()) {
        throw std::runtime_error("BloomPass could not open shader: " + path);
    }
    const auto size = static_cast<size_t>(file.tellg());
    std::vector<char> buffer(size);
    file.seekg(0);
    file.read(buffer.data(), static_cast<std::streamsize>(size));
    return buffer;
}

// A colour-only pass whose result is immediately sampled by the next one.
//
// `load` keeps what is already in the image instead of leaving it undefined:
// the screen overlay draws OVER the composite, so it starts from the composite's
// pixels and from the layout the composite left them in.
vk::RenderPass makeSamplablePass(vk::Device device, vk::Format format, bool load = false) {
    vk::AttachmentDescription color{};
    color.format = format;
    color.samples = vk::SampleCountFlagBits::e1;
    // Every pixel is overwritten, so there is nothing worth loading - except
    // for a pass that draws over what is there.
    color.loadOp = load ? vk::AttachmentLoadOp::eLoad : vk::AttachmentLoadOp::eDontCare;
    color.storeOp = vk::AttachmentStoreOp::eStore;
    color.stencilLoadOp = vk::AttachmentLoadOp::eDontCare;
    color.stencilStoreOp = vk::AttachmentStoreOp::eDontCare;
    color.initialLayout = load ? vk::ImageLayout::eShaderReadOnlyOptimal : vk::ImageLayout::eUndefined;
    color.finalLayout = vk::ImageLayout::eShaderReadOnlyOptimal;

    vk::AttachmentReference colorRef{};
    colorRef.attachment = 0;
    colorRef.layout = vk::ImageLayout::eColorAttachmentOptimal;

    vk::SubpassDescription subpass{};
    subpass.pipelineBindPoint = vk::PipelineBindPoint::eGraphics;
    subpass.colorAttachmentCount = 1;
    subpass.pColorAttachments = &colorRef;

    // Both directions are needed because these images ping-pong: an image
    // written by one pass is sampled by the next, and the one after that writes
    // it again while the previous read may still be in flight.
    std::array<vk::SubpassDependency, 2> dependencies{};

    // WRITE_AFTER_READ: do not start writing until the previous sample is done.
    dependencies[0].srcSubpass = VK_SUBPASS_EXTERNAL;
    dependencies[0].dstSubpass = 0;
    dependencies[0].srcStageMask = vk::PipelineStageFlagBits::eFragmentShader;
    dependencies[0].srcAccessMask = vk::AccessFlagBits::eShaderRead;
    dependencies[0].dstStageMask = vk::PipelineStageFlagBits::eColorAttachmentOutput;
    dependencies[0].dstAccessMask = vk::AccessFlagBits::eColorAttachmentWrite;

    // READ_AFTER_WRITE: make this pass's output visible to the next one's read.
    dependencies[1].srcSubpass = 0;
    dependencies[1].dstSubpass = VK_SUBPASS_EXTERNAL;
    dependencies[1].srcStageMask = vk::PipelineStageFlagBits::eColorAttachmentOutput;
    dependencies[1].srcAccessMask = vk::AccessFlagBits::eColorAttachmentWrite;
    dependencies[1].dstStageMask = vk::PipelineStageFlagBits::eFragmentShader;
    dependencies[1].dstAccessMask = vk::AccessFlagBits::eShaderRead;

    vk::RenderPassCreateInfo info{};
    info.attachmentCount = 1;
    info.pAttachments = &color;
    info.subpassCount = 1;
    info.pSubpasses = &subpass;
    info.dependencyCount = static_cast<uint32_t>(dependencies.size());
    info.pDependencies = dependencies.data();

    return device.createRenderPass(info);
}

vk::Sampler makeClampedSampler(vk::Device device) {
    vk::SamplerCreateInfo info{};
    info.magFilter = vk::Filter::eLinear;
    info.minFilter = vk::Filter::eLinear;
    // Clamped, not repeated: a blur reads past the edge, and wrapping would
    // smear the opposite side of the screen into it.
    info.addressModeU = vk::SamplerAddressMode::eClampToEdge;
    info.addressModeV = vk::SamplerAddressMode::eClampToEdge;
    info.addressModeW = vk::SamplerAddressMode::eClampToEdge;
    info.mipmapMode = vk::SamplerMipmapMode::eLinear;
    return device.createSampler(info);
}

} // namespace

BloomPass::BloomPass(VulkanDevice& device, uint32_t width, uint32_t height,
                     vk::ImageView sceneView, vk::Sampler sceneSampler,
                     vk::PipelineCache cache)
    : m_deviceRef(device),
      m_width(width == 0 ? 1 : width),
      m_height(height == 0 ? 1 : height) {

    m_halfWidth = m_width / 2 > 0 ? m_width / 2 : 1;
    m_halfHeight = m_height / 2 > 0 ? m_height / 2 : 1;

    m_pipelineCache = cache;

    createRenderPasses();
    createImages();
    createFramebuffers();
    createDescriptors(sceneView, sceneSampler);
    createPipelines();
}

BloomPass::~BloomPass() {
    vk::Device device = m_deviceRef.GetDevice();

    for (vk::Pipeline pipeline : {m_brightPipeline, m_blurPipeline, m_compositePipeline}) {
        if (pipeline) device.destroyPipeline(pipeline);
    }
    if (m_singlePipelineLayout) device.destroyPipelineLayout(m_singlePipelineLayout);
    if (m_doublePipelineLayout) device.destroyPipelineLayout(m_doublePipelineLayout);
    if (m_singleLayout) device.destroyDescriptorSetLayout(m_singleLayout);
    if (m_doubleLayout) device.destroyDescriptorSetLayout(m_doubleLayout);
    if (m_descriptorPool) device.destroyDescriptorPool(m_descriptorPool);

    for (vk::Framebuffer framebuffer :
         {m_brightFramebuffer, m_blurFramebuffer, m_outputFramebuffer}) {
        if (framebuffer) device.destroyFramebuffer(framebuffer);
    }

    m_brightImage.reset();
    m_blurImage.reset();
    m_outputImage.reset();

    if (m_sampler) device.destroySampler(m_sampler);
    if (m_outputSampler) device.destroySampler(m_outputSampler);

    if (m_hdrPass) device.destroyRenderPass(m_hdrPass);
    if (m_outputPass) device.destroyRenderPass(m_outputPass);
    if (m_overlayPass) device.destroyRenderPass(m_overlayPass);
}

void BloomPass::createRenderPasses() {
    vk::Device device = m_deviceRef.GetDevice();
    m_hdrPass = makeSamplablePass(device, kHdrFormat);
    m_outputPass = makeSamplablePass(device, kOutputFormat);
    m_overlayPass = MakeOverlayRenderPass(device);
}

vk::RenderPass BloomPass::MakeOverlayRenderPass(vk::Device device) {
    return makeSamplablePass(device, kOutputFormat, true);
}

void BloomPass::createImages() {
    // eTransferSrc so the composited result can be read back for --screenshot.
    // Without it the image is display-only: the engine could render a frame and
    // had no way to show anyone what it rendered, which made every visual
    // change unverifiable except by a human looking at a window.
    constexpr auto kUsage = vk::ImageUsageFlagBits::eColorAttachment
                          | vk::ImageUsageFlagBits::eSampled
                          | vk::ImageUsageFlagBits::eTransferSrc;

    m_brightImage = std::make_unique<VulkanImage>(m_deviceRef, m_halfWidth, m_halfHeight,
                                                  kHdrFormat, kUsage, vk::ImageAspectFlagBits::eColor);
    m_blurImage = std::make_unique<VulkanImage>(m_deviceRef, m_halfWidth, m_halfHeight,
                                                kHdrFormat, kUsage, vk::ImageAspectFlagBits::eColor);
    m_outputImage = std::make_unique<VulkanImage>(m_deviceRef, m_width, m_height,
                                                  kOutputFormat, kUsage, vk::ImageAspectFlagBits::eColor);

    m_sampler = makeClampedSampler(m_deviceRef.GetDevice());
    m_outputSampler = makeClampedSampler(m_deviceRef.GetDevice());
}

void BloomPass::createFramebuffers() {
    vk::Device device = m_deviceRef.GetDevice();

    const auto make = [&](vk::RenderPass pass, vk::ImageView view, uint32_t w, uint32_t h) {
        vk::FramebufferCreateInfo info{};
        info.renderPass = pass;
        info.attachmentCount = 1;
        info.pAttachments = &view;
        info.width = w;
        info.height = h;
        info.layers = 1;
        return device.createFramebuffer(info);
    };

    m_brightFramebuffer = make(m_hdrPass, m_brightImage->GetImageView(), m_halfWidth, m_halfHeight);
    m_blurFramebuffer = make(m_hdrPass, m_blurImage->GetImageView(), m_halfWidth, m_halfHeight);
    m_outputFramebuffer = make(m_outputPass, m_outputImage->GetImageView(), m_width, m_height);
}

void BloomPass::createDescriptors(vk::ImageView sceneView, vk::Sampler sceneSampler) {
    vk::Device device = m_deviceRef.GetDevice();

    const auto makeLayout = [&](uint32_t count) {
        std::array<vk::DescriptorSetLayoutBinding, 2> bindings{};
        for (uint32_t i = 0; i < count; ++i) {
            bindings[i].binding = i;
            bindings[i].descriptorType = vk::DescriptorType::eCombinedImageSampler;
            bindings[i].descriptorCount = 1;
            bindings[i].stageFlags = vk::ShaderStageFlagBits::eFragment;
        }
        vk::DescriptorSetLayoutCreateInfo info{};
        info.bindingCount = count;
        info.pBindings = bindings.data();
        return device.createDescriptorSetLayout(info);
    };

    m_singleLayout = makeLayout(1);
    m_doubleLayout = makeLayout(2);

    vk::DescriptorPoolSize poolSize{};
    poolSize.type = vk::DescriptorType::eCombinedImageSampler;
    // Three single-image sets plus two two-image sets.
    poolSize.descriptorCount = 7;

    vk::DescriptorPoolCreateInfo poolInfo{};
    poolInfo.maxSets = 5;
    poolInfo.poolSizeCount = 1;
    poolInfo.pPoolSizes = &poolSize;
    m_descriptorPool = device.createDescriptorPool(poolInfo);

    const auto allocate = [&](vk::DescriptorSetLayout layout) {
        vk::DescriptorSetAllocateInfo info{};
        info.descriptorPool = m_descriptorPool;
        info.descriptorSetCount = 1;
        info.pSetLayouts = &layout;
        return device.allocateDescriptorSets(info).front();
    };

    m_sceneSet = allocate(m_singleLayout);
    m_brightSet = allocate(m_singleLayout);
    m_blurSet = allocate(m_singleLayout);
    m_compositeSet = allocate(m_doubleLayout);
    m_compositeSceneOnlySet = allocate(m_doubleLayout);

    const auto write = [&](vk::DescriptorSet set, uint32_t binding,
                           vk::ImageView view, vk::Sampler sampler) {
        vk::DescriptorImageInfo image{};
        image.imageLayout = vk::ImageLayout::eShaderReadOnlyOptimal;
        image.imageView = view;
        image.sampler = sampler;

        vk::WriteDescriptorSet w{};
        w.dstSet = set;
        w.dstBinding = binding;
        w.descriptorType = vk::DescriptorType::eCombinedImageSampler;
        w.descriptorCount = 1;
        w.pImageInfo = &image;
        device.updateDescriptorSets(1, &w, 0, nullptr);
    };

    write(m_sceneSet, 0, sceneView, sceneSampler);
    write(m_brightSet, 0, m_brightImage->GetImageView(), m_sampler);
    write(m_blurSet, 0, m_blurImage->GetImageView(), m_sampler);
    write(m_compositeSet, 0, sceneView, sceneSampler);
    // The second blur direction writes back into the bright image, so that is
    // what the composite reads - not the blur image.
    write(m_compositeSet, 1, m_brightImage->GetImageView(), m_sampler);
    // A composite with no bloom still has a binding 1, and it must name an
    // image that is samplable on every frame: the scene is, because the scene
    // pass always runs first. The shader does not read it in that mode.
    write(m_compositeSceneOnlySet, 0, sceneView, sceneSampler);
    write(m_compositeSceneOnlySet, 1, sceneView, sceneSampler);

    vk::PushConstantRange range{};
    range.stageFlags = vk::ShaderStageFlagBits::eFragment;
    range.offset = 0;
    range.size = sizeof(Params);

    const auto makePipelineLayout = [&](vk::DescriptorSetLayout layout) {
        vk::PipelineLayoutCreateInfo info{};
        info.setLayoutCount = 1;
        info.pSetLayouts = &layout;
        info.pushConstantRangeCount = 1;
        info.pPushConstantRanges = &range;
        return device.createPipelineLayout(info);
    };

    m_singlePipelineLayout = makePipelineLayout(m_singleLayout);
    m_doublePipelineLayout = makePipelineLayout(m_doubleLayout);
}

vk::Pipeline BloomPass::buildPipeline(const std::string& fragmentPath, vk::RenderPass pass,
                                      vk::PipelineLayout layout) const {
    vk::Device device = m_deviceRef.GetDevice();

    const auto makeModule = [&](const std::string& path) {
        const std::vector<char> code = readFile(path);
        vk::ShaderModuleCreateInfo info{};
        info.codeSize = code.size();
        info.pCode = reinterpret_cast<const uint32_t*>(code.data());
        return device.createShaderModule(info);
    };

    vk::ShaderModule vertModule = makeModule("assets/shaders/fullscreen_vert.spv");
    vk::ShaderModule fragModule = makeModule(fragmentPath);

    std::array<vk::PipelineShaderStageCreateInfo, 2> stages{};
    stages[0].stage = vk::ShaderStageFlagBits::eVertex;
    stages[0].module = vertModule;
    stages[0].pName = "main";
    stages[1].stage = vk::ShaderStageFlagBits::eFragment;
    stages[1].module = fragModule;
    stages[1].pName = "main";

    // No vertex input at all: the vertex shader builds its triangle from
    // gl_VertexIndex.
    vk::PipelineVertexInputStateCreateInfo vertexInput{};

    vk::PipelineInputAssemblyStateCreateInfo inputAssembly{};
    inputAssembly.topology = vk::PrimitiveTopology::eTriangleList;

    vk::PipelineViewportStateCreateInfo viewportState{};
    viewportState.viewportCount = 1;
    viewportState.scissorCount = 1;

    vk::PipelineRasterizationStateCreateInfo rasterizer{};
    rasterizer.polygonMode = vk::PolygonMode::eFill;
    rasterizer.cullMode = vk::CullModeFlagBits::eNone;
    rasterizer.frontFace = vk::FrontFace::eCounterClockwise;
    rasterizer.lineWidth = 1.0f;

    vk::PipelineMultisampleStateCreateInfo multisampling{};
    multisampling.rasterizationSamples = vk::SampleCountFlagBits::e1;

    vk::PipelineColorBlendAttachmentState blendAttachment{};
    blendAttachment.colorWriteMask =
        vk::ColorComponentFlagBits::eR | vk::ColorComponentFlagBits::eG |
        vk::ColorComponentFlagBits::eB | vk::ColorComponentFlagBits::eA;
    blendAttachment.blendEnable = VK_FALSE;

    vk::PipelineColorBlendStateCreateInfo colorBlending{};
    colorBlending.attachmentCount = 1;
    colorBlending.pAttachments = &blendAttachment;

    const std::array<vk::DynamicState, 2> dynamicStates = {
        vk::DynamicState::eViewport, vk::DynamicState::eScissor
    };
    vk::PipelineDynamicStateCreateInfo dynamicState{};
    dynamicState.dynamicStateCount = static_cast<uint32_t>(dynamicStates.size());
    dynamicState.pDynamicStates = dynamicStates.data();

    vk::GraphicsPipelineCreateInfo info{};
    info.stageCount = 2;
    info.pStages = stages.data();
    info.pVertexInputState = &vertexInput;
    info.pInputAssemblyState = &inputAssembly;
    info.pViewportState = &viewportState;
    info.pRasterizationState = &rasterizer;
    info.pMultisampleState = &multisampling;
    info.pColorBlendState = &colorBlending;
    info.pDynamicState = &dynamicState;
    info.layout = layout;
    info.renderPass = pass;
    info.subpass = 0;

    // Named before the driver is asked, as VulkanPipeline names its own: the line a log ends on if the
    // driver dies compiling it.
    SUPERSONIC_LOG_INFO("BloomPass") << "Creating pipeline: " << fragmentPath << "..." << std::endl;
    const auto result = device.createGraphicsPipeline(m_pipelineCache, info);

    device.destroyShaderModule(vertModule);
    device.destroyShaderModule(fragModule);

    if (result.result != vk::Result::eSuccess) {
        throw std::runtime_error("BloomPass failed to create a pipeline for " + fragmentPath);
    }
    return result.value;
}

void BloomPass::createPipelines() {
    m_brightPipeline = buildPipeline("assets/shaders/bloom_bright.spv", m_hdrPass, m_singlePipelineLayout);
    m_blurPipeline = buildPipeline("assets/shaders/bloom_blur.spv", m_hdrPass, m_singlePipelineLayout);
    m_compositePipeline = buildPipeline("assets/shaders/bloom_composite.spv", m_outputPass, m_doublePipelineLayout);
}

void BloomPass::recordPass(vk::CommandBuffer cmd, vk::RenderPass pass, vk::Framebuffer framebuffer,
                           uint32_t width, uint32_t height, vk::Pipeline pipeline,
                           vk::PipelineLayout layout, vk::DescriptorSet set,
                           const Params& params) const {
    vk::RenderPassBeginInfo begin{};
    begin.renderPass = pass;
    begin.framebuffer = framebuffer;
    begin.renderArea.offset = vk::Offset2D{0, 0};
    begin.renderArea.extent = vk::Extent2D{width, height};
    // loadOp is DontCare on every attachment here, so there is nothing to clear.
    begin.clearValueCount = 0;

    cmd.beginRenderPass(begin, vk::SubpassContents::eInline);

    const vk::Viewport viewport{0.0f, 0.0f, static_cast<float>(width), static_cast<float>(height), 0.0f, 1.0f};
    const vk::Rect2D scissor{{0, 0}, {width, height}};
    cmd.setViewport(0, 1, &viewport);
    cmd.setScissor(0, 1, &scissor);

    cmd.bindPipeline(vk::PipelineBindPoint::eGraphics, pipeline);
    cmd.bindDescriptorSets(vk::PipelineBindPoint::eGraphics, layout, 0, 1, &set, 0, nullptr);
    cmd.pushConstants(layout, vk::ShaderStageFlagBits::eFragment, 0, sizeof(Params), &params);

    // Three vertices, no buffers: the fullscreen triangle.
    cmd.draw(3, 1, 0, 0);

    cmd.endRenderPass();
}

void BloomPass::RecordOverlay(vk::CommandBuffer cmd,
                              const std::function<void(vk::CommandBuffer)>& draw) const {
    vk::RenderPassBeginInfo begin{};
    begin.renderPass = m_overlayPass;
    // The composite's own framebuffer: a framebuffer serves any render pass
    // compatible with the one it was made for, and a load op is not part of
    // compatibility.
    begin.framebuffer = m_outputFramebuffer;
    begin.renderArea.offset = vk::Offset2D{0, 0};
    begin.renderArea.extent = vk::Extent2D{m_width, m_height};
    begin.clearValueCount = 0;

    cmd.beginRenderPass(begin, vk::SubpassContents::eInline);
    const vk::Viewport viewport{0.0f, 0.0f, static_cast<float>(m_width), static_cast<float>(m_height), 0.0f, 1.0f};
    const vk::Rect2D scissor{{0, 0}, {m_width, m_height}};
    cmd.setViewport(0, 1, &viewport);
    cmd.setScissor(0, 1, &scissor);
    draw(cmd);
    cmd.endRenderPass();
}

void BloomPass::Record(vk::CommandBuffer cmd) const {
    // 4. only, for a scene of display values (RenderSettings::SceneEncoding).
    // The composite is never skipped: it writes the image --screenshot reads
    // and the screen overlay loads.
    if (!RunsBloomChain(m_settings)) {
        Params composite{};
        composite.value = CompositeValue(m_settings);
        recordPass(cmd, m_outputPass, m_outputFramebuffer, m_width, m_height,
                   m_compositePipeline, m_doublePipelineLayout, m_compositeSceneOnlySet, composite);
        return;
    }

    const float texelX = 1.0f / static_cast<float>(m_halfWidth);
    const float texelY = 1.0f / static_cast<float>(m_halfHeight);

    // 1. Threshold the scene into the half-resolution bright image.
    Params bright{};
    bright.value = glm::vec4(m_settings.threshold, m_settings.softKnee, 0.0f, 0.0f);
    recordPass(cmd, m_hdrPass, m_brightFramebuffer, m_halfWidth, m_halfHeight,
               m_brightPipeline, m_singlePipelineLayout, m_sceneSet, bright);

    // 2. Horizontal blur, bright -> blur.
    Params horizontal{};
    horizontal.value = glm::vec4(texelX, 0.0f, 0.0f, 0.0f);
    recordPass(cmd, m_hdrPass, m_blurFramebuffer, m_halfWidth, m_halfHeight,
               m_blurPipeline, m_singlePipelineLayout, m_brightSet, horizontal);

    // 3. Vertical blur, blur -> bright. Ping-ponging back means the chain needs
    //    two half-resolution images rather than three.
    Params vertical{};
    vertical.value = glm::vec4(0.0f, texelY, 0.0f, 0.0f);
    recordPass(cmd, m_hdrPass, m_brightFramebuffer, m_halfWidth, m_halfHeight,
               m_blurPipeline, m_singlePipelineLayout, m_blurSet, vertical);

    // 4. Add it back to the scene, tone map, encode.
    Params composite{};
    composite.value = CompositeValue(m_settings);
    recordPass(cmd, m_outputPass, m_outputFramebuffer, m_width, m_height,
               m_compositePipeline, m_doublePipelineLayout, m_compositeSet, composite);
}

} // namespace Supersonic
