#include "renderer/VulkanPipeline.hpp"
#include "core/Components.hpp"
#include "core/Log.hpp"

#include <array>
#include <chrono>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>

namespace Supersonic {

namespace {

// Destroys a shader module when it leaves scope. Without this, a throw between
// creating the vertex module and finishing the pipeline leaked it: the
// destructor never runs for an object whose constructor did not complete.
class ScopedShaderModule {
public:
    ScopedShaderModule(vk::Device device, vk::ShaderModule module)
        : m_device(device), m_module(module) {}
    ~ScopedShaderModule() {
        if (m_module) m_device.destroyShaderModule(m_module);
    }
    ScopedShaderModule(const ScopedShaderModule&) = delete;
    ScopedShaderModule& operator=(const ScopedShaderModule&) = delete;

    vk::ShaderModule get() const { return m_module; }

private:
    vk::Device m_device;
    vk::ShaderModule m_module;
};

} // namespace

vk::PipelineColorBlendAttachmentState ColorBlendFor(const VulkanPipelineOptions& options) {
    vk::PipelineColorBlendAttachmentState state{};
    state.colorWriteMask = vk::ColorComponentFlagBits::eR | vk::ColorComponentFlagBits::eG |
                           vk::ColorComponentFlagBits::eB | vk::ColorComponentFlagBits::eA;
    if (!options.blendEnable) {
        state.blendEnable = VK_FALSE;
        return state;
    }
    state.blendEnable = VK_TRUE;
    state.srcColorBlendFactor = vk::BlendFactor::eSrcAlpha;
    state.colorBlendOp = vk::BlendOp::eAdd;
    state.alphaBlendOp = vk::BlendOp::eAdd;
    switch (options.blendEquation) {
    case BlendEquation::Add:
        state.dstColorBlendFactor = vk::BlendFactor::eOne;
        state.srcAlphaBlendFactor = vk::BlendFactor::eZero;
        state.dstAlphaBlendFactor = vk::BlendFactor::eOne;
        break;
    case BlendEquation::Premultiplied:
        // The colour already carries its alpha, so the source is taken whole.
        // Alpha composites the same way, which is what straight Mix gives the
        // alpha channel too - so a premultiplied draw that adds nothing leaves
        // the target as a Mix draw would.
        state.srcColorBlendFactor = vk::BlendFactor::eOne;
        state.dstColorBlendFactor = vk::BlendFactor::eOneMinusSrcAlpha;
        state.srcAlphaBlendFactor = vk::BlendFactor::eOne;
        state.dstAlphaBlendFactor = vk::BlendFactor::eOneMinusSrcAlpha;
        break;
    case BlendEquation::Mix:
    default:
        state.dstColorBlendFactor = vk::BlendFactor::eOneMinusSrcAlpha;
        state.srcAlphaBlendFactor = vk::BlendFactor::eOne;
        state.dstAlphaBlendFactor = vk::BlendFactor::eOneMinusSrcAlpha;
        break;
    }
    return state;
}

VulkanPipeline::VulkanPipeline(vk::Device device, vk::RenderPass renderPass,
                               const std::string& vertPath, const std::string& fragPath,
                               const Options& options)
    : m_device(device) {

    if (!m_device) {
        throw std::runtime_error("Cannot create VulkanPipeline with null logical device!");
    }
    if (!renderPass) {
        throw std::runtime_error("Cannot create VulkanPipeline with null RenderPass!");
    }

    // Anything acquired past this point must survive a throw, so every failure
    // path runs destroy() before rethrowing.
    //
    // Named before the driver is asked: a driver that dies compiling a shader dies inside the call, and
    // the last line of the log is then the pipeline it died on (a mobile driver compiles each of these
    // from SPIR-V on the first run, which can take seconds; the second line says how long it took).
    SUPERSONIC_LOG_INFO("VulkanPipeline") << "Creating pipeline: " << vertPath << " + " << fragPath << " ("
              << static_cast<uint32_t>(options.samples) << "x samples)..." << std::endl;
    const auto started = std::chrono::steady_clock::now();
    try {
        createDescriptorSetLayout();

        const auto vertShaderCode = readFile(vertPath);
        const auto fragShaderCode = readFile(fragPath);

        ScopedShaderModule vertModule(m_device, createShaderModule(vertShaderCode));
        ScopedShaderModule fragModule(m_device, createShaderModule(fragShaderCode));

        vk::PipelineShaderStageCreateInfo vertShaderStageInfo{};
        vertShaderStageInfo.stage = vk::ShaderStageFlagBits::eVertex;
        vertShaderStageInfo.module = vertModule.get();
        vertShaderStageInfo.pName = "main";

        vk::PipelineShaderStageCreateInfo fragShaderStageInfo{};
        fragShaderStageInfo.stage = vk::ShaderStageFlagBits::eFragment;
        fragShaderStageInfo.module = fragModule.get();
        fragShaderStageInfo.pName = "main";

        const vk::PipelineShaderStageCreateInfo shaderStages[] = { vertShaderStageInfo, fragShaderStageInfo };

        // Vertex Input State: Configured from Vertex struct
        auto bindingDescription = Vertex::getBindingDescription();
        auto attributeDescriptions = Vertex::getAttributeDescriptions();

        vk::PipelineVertexInputStateCreateInfo vertexInputInfo{};
        if (options.useVertexInput) {
            // A caller-supplied layout wins. It is how a pipeline whose vertex
            // is not the scene's Vertex - the world-space shape layer, whose
            // vertex is a position and a colour - describes what it actually
            // reads. Declaring the scene's full layout over a buffer that does
            // not contain those attributes is a validation error, not a
            // harmless overstatement.
            if (options.vertexBinding != nullptr && options.vertexAttributeCount > 0) {
                vertexInputInfo.vertexBindingDescriptionCount = 1;
                vertexInputInfo.pVertexBindingDescriptions = options.vertexBinding;
                vertexInputInfo.vertexAttributeDescriptionCount = options.vertexAttributeCount;
                vertexInputInfo.pVertexAttributeDescriptions = options.vertexAttributes;
            } else {
                vertexInputInfo.vertexBindingDescriptionCount = 1;
                vertexInputInfo.pVertexBindingDescriptions = &bindingDescription;
                vertexInputInfo.vertexAttributeDescriptionCount = static_cast<uint32_t>(attributeDescriptions.size());
                vertexInputInfo.pVertexAttributeDescriptions = attributeDescriptions.data();
            }
        }

        vk::PipelineInputAssemblyStateCreateInfo inputAssembly{};
        inputAssembly.topology = options.topology;
        inputAssembly.primitiveRestartEnable = VK_FALSE;

        const std::array<vk::DynamicState, 2> dynamicStates = {
            vk::DynamicState::eViewport,
            vk::DynamicState::eScissor
        };

        vk::PipelineDynamicStateCreateInfo dynamicState{};
        dynamicState.dynamicStateCount = static_cast<uint32_t>(dynamicStates.size());
        dynamicState.pDynamicStates = dynamicStates.data();

        vk::PipelineViewportStateCreateInfo viewportState{};
        viewportState.viewportCount = 1;
        viewportState.scissorCount = 1;

        vk::PipelineRasterizationStateCreateInfo rasterizer{};
        rasterizer.depthClampEnable = VK_FALSE;
        rasterizer.rasterizerDiscardEnable = VK_FALSE;
        rasterizer.polygonMode = vk::PolygonMode::eFill;
        rasterizer.lineWidth = 1.0f;
        rasterizer.cullMode = options.cullMode;
        rasterizer.frontFace = vk::FrontFace::eCounterClockwise;
        rasterizer.depthBiasEnable = options.depthBias ? VK_TRUE : VK_FALSE;
        if (options.depthBias) {
            rasterizer.depthBiasConstantFactor = options.depthBiasConstant;
            rasterizer.depthBiasSlopeFactor = options.depthBiasSlope;
            rasterizer.depthBiasClamp = 0.0f;
        }

        vk::PipelineMultisampleStateCreateInfo multisampling{};
        multisampling.sampleShadingEnable = VK_FALSE;
        multisampling.rasterizationSamples = options.samples;

        vk::PipelineDepthStencilStateCreateInfo depthStencil{};
        depthStencil.depthTestEnable = VK_TRUE;
        depthStencil.depthWriteEnable = options.depthWrite ? VK_TRUE : VK_FALSE;
        depthStencil.depthCompareOp = vk::CompareOp::eLessOrEqual;
        depthStencil.depthBoundsTestEnable = VK_FALSE;
        depthStencil.stencilTestEnable = VK_FALSE;

        const vk::PipelineColorBlendAttachmentState colorBlendAttachment = ColorBlendFor(options);

        vk::PipelineColorBlendStateCreateInfo colorBlending{};
        colorBlending.logicOpEnable = VK_FALSE;
        colorBlending.attachmentCount = options.colorAttachmentCount;
        colorBlending.pAttachments = options.colorAttachmentCount > 0 ? &colorBlendAttachment : nullptr;

        // One range spanning both stages: the vertex stage reads the model
        // matrix, the fragment stage reads albedo and material parameters.
        vk::PushConstantRange pushConstantRange{};
        pushConstantRange.stageFlags = options.pushConstantStages;
        pushConstantRange.offset = 0;
        pushConstantRange.size = options.pushConstantSize;
        static_assert(sizeof(PushConstantData) <= 128,
                      "Push constants must fit the 128-byte guaranteed minimum");
        static_assert(sizeof(ShadowPushConstantData) <= 128,
                      "Shadow push constants must fit the 128-byte guaranteed minimum");
        // Pinned rather than assumed: these are the std430 push-constant offsets
        // the shaders declare, and GLM_FORCE_ALIGNED_GENTYPES would shift every
        // one of them without a word.
        static_assert(sizeof(PushConstantData) == 128, "push constant layout shifted");
        static_assert(offsetof(PushConstantData, flags) == 124,
                      "flags must sit in the last four bytes of the guaranteed minimum");
        static_assert(offsetof(PushConstantData, probeIndex) == 120,
                      "probeIndex must sit in the four bytes after skinJointCount");
        static_assert(offsetof(PushConstantData, emissive) == 96, "push constant layout shifted");
        static_assert(offsetof(PushConstantData, skinPaletteBase) == 112, "push constant layout shifted");
        static_assert(offsetof(PushConstantData, skinJointCount) == 116, "push constant layout shifted");
        static_assert(offsetof(ShadowPushConstantData, skinPaletteBase) == 64, "shadow push layout shifted");
        // 80 is 16-byte aligned, which a vec4 in a push constant block must be.
        // At 84 or 88 the shader would read the transform from the wrong offset
        // and no validation layer would mention it.
        static_assert(offsetof(ShadowPushConstantData, uvTransform) == 80,
                      "the depth pass's UV transform must stay 16-byte aligned");
        static_assert(sizeof(ShadowPushConstantData) <= 128,
                      "the depth push block must stay inside the guaranteed minimum");
        static_assert(offsetof(ShadowPushConstantData, alphaCutoff) == 72, "shadow push layout shifted");
        static_assert(offsetof(ShadowPushConstantData, baseAlpha) == 76, "shadow push layout shifted");
        static_assert(sizeof(ShadowPushConstantData) == 112, "shadow push layout shifted");
        static_assert(sizeof(ShadowPushConstantData) % 4 == 0, "push constant size must be a multiple of 4");
        static_assert(sizeof(PushConstantData) % 4 == 0, "push constant size must be a multiple of 4");
        if (options.pushConstantSize == 0 || options.pushConstantSize > 128) {
            throw std::runtime_error("Push constant range must be 1..128 bytes, got "
                                     + std::to_string(options.pushConstantSize));
        }

        const std::array<vk::DescriptorSetLayout, 2> setLayouts = {
            m_sceneSetLayout, m_materialSetLayout
        };

        vk::PipelineLayoutCreateInfo pipelineLayoutInfo{};
        pipelineLayoutInfo.setLayoutCount = static_cast<uint32_t>(setLayouts.size());
        pipelineLayoutInfo.pSetLayouts = setLayouts.data();
        pipelineLayoutInfo.pushConstantRangeCount = 1;
        pipelineLayoutInfo.pPushConstantRanges = &pushConstantRange;

        m_pipelineLayout = m_device.createPipelineLayout(pipelineLayoutInfo);
        if (!m_pipelineLayout) {
            throw std::runtime_error("Failed to create Vulkan Pipeline Layout!");
        }

        vk::GraphicsPipelineCreateInfo pipelineInfo{};
        pipelineInfo.stageCount = 2;
        pipelineInfo.pStages = shaderStages;
        pipelineInfo.pVertexInputState = &vertexInputInfo;
        pipelineInfo.pInputAssemblyState = &inputAssembly;
        pipelineInfo.pViewportState = &viewportState;
        pipelineInfo.pRasterizationState = &rasterizer;
        pipelineInfo.pMultisampleState = &multisampling;
        pipelineInfo.pDepthStencilState = &depthStencil;
        pipelineInfo.pColorBlendState = &colorBlending;
        pipelineInfo.pDynamicState = &dynamicState;
        pipelineInfo.layout = m_pipelineLayout;
        pipelineInfo.renderPass = renderPass;
        pipelineInfo.subpass = 0;

        auto result = m_device.createGraphicsPipeline(options.cache, pipelineInfo);
        if (result.result != vk::Result::eSuccess) {
            throw std::runtime_error("Failed to create Vulkan Graphics Pipeline! Result code: "
                                     + std::to_string(static_cast<int>(result.result)));
        }
        m_graphicsPipeline = result.value;
        SUPERSONIC_LOG_INFO("VulkanPipeline") << "Pipeline created: " << fragPath << " in "
                  << std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - started).count()
                  << " ms." << std::endl;
    } catch (...) {
        destroy();
        throw;
    }
}

VulkanPipeline::~VulkanPipeline() {
    destroy();
}

void VulkanPipeline::destroy() noexcept {
    if (!m_device) return;

    if (m_graphicsPipeline) {
        m_device.destroyPipeline(m_graphicsPipeline);
        m_graphicsPipeline = nullptr;
    }
    if (m_pipelineLayout) {
        m_device.destroyPipelineLayout(m_pipelineLayout);
        m_pipelineLayout = nullptr;
    }
    if (m_materialSetLayout) {
        m_device.destroyDescriptorSetLayout(m_materialSetLayout);
        m_materialSetLayout = nullptr;
    }
    if (m_sceneSetLayout) {
        m_device.destroyDescriptorSetLayout(m_sceneSetLayout);
        m_sceneSetLayout = nullptr;
    }
}

void VulkanPipeline::createDescriptorSetLayout() {
    // ---- Set 0: per-frame scene data ----
    // binding 0: UBO with camera, lights and the cascade transforms.
    // binding 1: the cascaded shadow map array, sampled by the fragment stage.
    // binding 2: this frame's joint matrices, read by the vertex stage.
    //
    // The palette goes here rather than in a set of its own because set 0 is
    // already bound exactly once per pass at every call site: a non-dynamic
    // storage buffer costs no extra bindDescriptorSets and no per-draw
    // descriptor traffic, and the per-draw offset travels in a push constant
    // instead. Storage-buffer READS in the vertex stage are core Vulkan 1.0 -
    // only stores and atomics there need a device feature.
    vk::DescriptorSetLayoutBinding uboBinding{};
    uboBinding.binding = 0;
    uboBinding.descriptorType = vk::DescriptorType::eUniformBuffer;
    uboBinding.descriptorCount = 1;
    uboBinding.stageFlags = vk::ShaderStageFlagBits::eVertex | vk::ShaderStageFlagBits::eFragment;

    vk::DescriptorSetLayoutBinding shadowBinding{};
    shadowBinding.binding = 1;
    shadowBinding.descriptorType = vk::DescriptorType::eCombinedImageSampler;
    shadowBinding.descriptorCount = 1;
    shadowBinding.stageFlags = vk::ShaderStageFlagBits::eFragment;

    vk::DescriptorSetLayoutBinding paletteBinding{};
    paletteBinding.binding = 2;
    paletteBinding.descriptorType = vk::DescriptorType::eStorageBuffer;
    paletteBinding.descriptorCount = 1;
    paletteBinding.stageFlags = vk::ShaderStageFlagBits::eVertex;

    // binding 3: one cube shadow map per point light that can cast. An array
    // of descriptors rather than a cube ARRAY image, so no optional device
    // feature is needed - and indexing it by the light loop's counter is
    // dynamically uniform, which is what the rule actually requires.
    vk::DescriptorSetLayoutBinding pointShadowBinding{};
    pointShadowBinding.binding = 3;
    pointShadowBinding.descriptorType = vk::DescriptorType::eCombinedImageSampler;
    pointShadowBinding.descriptorCount = PointShadow::kMaxShadowCasters;
    pointShadowBinding.stageFlags = vk::ShaderStageFlagBits::eFragment;

    // binding 4: one depth map per spot light that can cast. Same shape as the
    // cube array above, deliberately: a second convention for the same idea
    // would be one more thing to get wrong.
    vk::DescriptorSetLayoutBinding spotShadowBinding{};
    spotShadowBinding.binding = 4;
    spotShadowBinding.descriptorType = vk::DescriptorType::eCombinedImageSampler;
    spotShadowBinding.descriptorCount = 1;
    spotShadowBinding.stageFlags = vk::ShaderStageFlagBits::eFragment;

    // bindings 5, 6 and 7: the clustered light data.
    //
    // A storage buffer rather than more of the UBO, and that is the whole point
    // of the change: a uniform block is a fixed array whose size is compiled
    // into the shader, so eight was a number the renderer had to promise and a
    // scene had to live within. These three are sized at capacity once and the
    // frame writes as much of them as it needs.
    //
    // 5 is every light in the frame, directionals first. 6 is one (offset,
    // count) pair per froxel. 7 is the flat index list those pairs point into.
    std::array<vk::DescriptorSetLayoutBinding, 3> clusterBindings{};
    for (uint32_t i = 0; i < clusterBindings.size(); ++i) {
        clusterBindings[i].binding = 5 + i;
        clusterBindings[i].descriptorType = vk::DescriptorType::eStorageBuffer;
        clusterBindings[i].descriptorCount = 1;
        clusterBindings[i].stageFlags = vk::ShaderStageFlagBits::eFragment;
    }

    // The environment, as the two maps a shader needs to light from one: the
    // diffuse irradiance, and the specular chain indexed by roughness.
    //
    // Both are ALWAYS bound. A descriptor slot that is never written is
    // undefined to read even inside a branch the shader does not take, so a
    // scene with no environment binds a one-colour cube the shader is told to
    // ignore rather than leaving the slots empty.
    std::array<vk::DescriptorSetLayoutBinding, 2> environmentBindings{};
    for (uint32_t i = 0; i < environmentBindings.size(); ++i) {
        environmentBindings[i].binding = 8 + i;
        environmentBindings[i].descriptorType = vk::DescriptorType::eCombinedImageSampler;
        environmentBindings[i].descriptorCount = kMaxEnvironmentProbes;
        environmentBindings[i].stageFlags = vk::ShaderStageFlagBits::eFragment;
    }

    // Every texture coordinate transform in the frame. Beside the joint
    // palette rather than in a set of its own for the same reason that one is
    // here: both are per-frame arrays a draw indexes into, and set 0 is already
    // bound once per frame while set 1 is rebound per draw.
    //
    // Fragment only. The transform could as correctly be applied in the vertex
    // stage - it is affine, so transforming interpolated coordinates and
    // interpolating transformed ones give the same answer - but doing it in the
    // fragment stage keeps it in the one shader that samples, so a pass that
    // only writes depth needs nothing from this binding.
    vk::DescriptorSetLayoutBinding uvTransformBinding{};
    uvTransformBinding.binding = 10;
    uvTransformBinding.descriptorType = vk::DescriptorType::eStorageBuffer;
    uvTransformBinding.descriptorCount = 1;
    uvTransformBinding.stageFlags = vk::ShaderStageFlagBits::eFragment;

    // Per-draw data, one record per INSTANCE.
    //
    // Vertex AND fragment: the vertex stage needs the model matrix and the skin
    // range, the fragment stage the material. The fragment stage cannot read
    // gl_InstanceIndex - it is a vertex input - so the index is carried across
    // as a flat varying and the record is read twice rather than passed.
    vk::DescriptorSetLayoutBinding instanceBinding{};
    instanceBinding.binding = 11;
    instanceBinding.descriptorType = vk::DescriptorType::eStorageBuffer;
    instanceBinding.descriptorCount = 1;
    instanceBinding.stageFlags =
        vk::ShaderStageFlagBits::eVertex | vk::ShaderStageFlagBits::eFragment;

    // Every 2D point light in the frame (Light2DComponent, core/Light2D.hpp): a
    // count and a flat list, read by shader.frag's 2D sprite path only.
    //
    // Not bindings 5 to 7. Those lights are tied to the camera's depth slices,
    // chosen by importance and given shadow slots; a 2D light is none of that,
    // and a sprite loops the handful there are rather than looking up a froxel.
    // Fragment only, like the transforms: nothing about a light moves a vertex.
    vk::DescriptorSetLayoutBinding light2DBinding{};
    light2DBinding.binding = 12;
    light2DBinding.descriptorType = vk::DescriptorType::eStorageBuffer;
    light2DBinding.descriptorCount = 1;
    light2DBinding.stageFlags = vk::ShaderStageFlagBits::eFragment;

    // The shadows those lights cast (Light2DShadowsComponent, core/Light2D.hpp),
    // read only by a sprite that asks for them (Sprite2DLight::lightShadows).
    // Its own binding rather than more of binding 12, whose last member is the
    // lights' open-ended array.
    vk::DescriptorSetLayoutBinding shadow2DBinding{};
    shadow2DBinding.binding = 13;
    shadow2DBinding.descriptorType = vk::DescriptorType::eStorageBuffer;
    shadow2DBinding.descriptorCount = 1;
    shadow2DBinding.stageFlags = vk::ShaderStageFlagBits::eFragment;

    const std::array<vk::DescriptorSetLayoutBinding, 14> sceneBindings = {
        uboBinding, shadowBinding, paletteBinding, pointShadowBinding, spotShadowBinding,
        clusterBindings[0], clusterBindings[1], clusterBindings[2],
        environmentBindings[0], environmentBindings[1], uvTransformBinding,
        instanceBinding, light2DBinding, shadow2DBinding
    };

    // VulkanRenderer's pool is sized from kStorageBuffersPerSceneSet; a set that
    // declares more than the pool budgets fails only on drivers that enforce it.
    uint32_t storageBuffers = 0;
    for (const vk::DescriptorSetLayoutBinding& binding : sceneBindings) {
        if (binding.descriptorType == vk::DescriptorType::eStorageBuffer) storageBuffers += binding.descriptorCount;
    }
    if (storageBuffers != kStorageBuffersPerSceneSet) {
        throw std::logic_error("VulkanPipeline: the scene set declares " + std::to_string(storageBuffers) +
                               " storage buffers but kStorageBuffersPerSceneSet is " +
                               std::to_string(kStorageBuffersPerSceneSet));
    }

    vk::DescriptorSetLayoutCreateInfo sceneInfo{};
    sceneInfo.bindingCount = static_cast<uint32_t>(sceneBindings.size());
    sceneInfo.pBindings = sceneBindings.data();
    m_sceneSetLayout = m_device.createDescriptorSetLayout(sceneInfo);

    // ---- Set 1: per-material ----
    // binding 0: albedo, binding 1: tangent-space normal map, binding 2: the
    // packed occlusion/roughness/metallic map, binding 3: the additive overlay
    // the 2D sprite path adds after its multiply (1x1 black when unnamed),
    // binding 4: the gloss its highlight takes (1x1 white when unnamed).
    // Rebound per draw, which is what lets each entity carry its own maps
    // rather than sharing one global sampler.
    //
    // The count is read from the array by everything below it, and
    // TextureRegistry sizes its pool from kMaterialBindingCount for the same
    // reason: a pool sized for two while the layout declares three does not
    // fail, it quietly runs out of sets a third early.
    std::array<vk::DescriptorSetLayoutBinding, kMaterialBindingCount> materialBindings{};
    for (uint32_t i = 0; i < materialBindings.size(); ++i) {
        materialBindings[i].binding = i;
        materialBindings[i].descriptorType = vk::DescriptorType::eCombinedImageSampler;
        materialBindings[i].descriptorCount = 1;
        materialBindings[i].stageFlags = vk::ShaderStageFlagBits::eFragment;
    }

    vk::DescriptorSetLayoutCreateInfo materialInfo{};
    materialInfo.bindingCount = static_cast<uint32_t>(materialBindings.size());
    materialInfo.pBindings = materialBindings.data();
    m_materialSetLayout = m_device.createDescriptorSetLayout(materialInfo);
}

vk::ShaderModule VulkanPipeline::createShaderModule(const std::vector<char>& code) {
    if (code.empty() || code.size() % 4 != 0) {
        throw std::runtime_error("Invalid SPIR-V bytecode size!");
    }

    vk::ShaderModuleCreateInfo createInfo{};
    createInfo.codeSize = code.size();
    createInfo.pCode = reinterpret_cast<const uint32_t*>(code.data());

    return m_device.createShaderModule(createInfo);
}

std::vector<char> VulkanPipeline::readFile(const std::string& filename) {
    std::ifstream file(filename, std::ios::ate | std::ios::binary);

    if (!file.is_open()) {
        throw std::runtime_error(
            "Failed to open shader file at path: " + filename +
            "\nThe engine resolves asset paths relative to the working directory; "
            "launch it from the project root.");
    }

    const auto fileSize = static_cast<size_t>(file.tellg());
    std::vector<char> buffer(fileSize);

    file.seekg(0);
    file.read(buffer.data(), static_cast<std::streamsize>(fileSize));

    if (!file) {
        throw std::runtime_error("Failed to read shader file: " + filename);
    }
    return buffer;
}

} // namespace Supersonic
