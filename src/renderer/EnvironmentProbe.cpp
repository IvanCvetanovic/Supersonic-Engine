#include "renderer/EnvironmentProbe.hpp"

#include <cstring>
#include <vector>

#include "core/Log.hpp"
#include "renderer/VulkanBuffer.hpp"
#include "renderer/VulkanDevice.hpp"

namespace Supersonic {

namespace {

// Half floats, because R16G16B16A16_SFLOAT is the only HDR format whose linear
// filtering every Vulkan implementation must support. R32 would skip this
// conversion and is optional to filter, which would be a blocky reflection on
// somebody else's card and a smooth one here.
uint16_t toHalf(float value) {
    uint32_t bits = 0;
    std::memcpy(&bits, &value, sizeof(bits));

    const uint32_t sign = (bits >> 16) & 0x8000u;
    auto exponent = static_cast<int32_t>((bits >> 23) & 0xFFu) - 127 + 15;
    const uint32_t mantissa = bits & 0x7FFFFFu;

    // Flushed to zero rather than encoded as a subnormal. A radiance that small
    // is black, and the subnormal path is where half-float conversions get
    // written wrongly.
    if (exponent <= 0) return static_cast<uint16_t>(sign);

    // Clamped to the largest FINITE half rather than to infinity. An infinity in
    // a colour becomes a NaN the first time something multiplies it by zero -
    // which the ambient occlusion term does, on every pixel a model says is
    // fully occluded.
    if (exponent >= 31) return static_cast<uint16_t>(sign | 0x7BFFu);

    return static_cast<uint16_t>(sign | (static_cast<uint32_t>(exponent) << 10) |
                                 (mantissa >> 13));
}

constexpr vk::Format kFormat = vk::Format::eR16G16B16A16Sfloat;

// Four components, because a three-component image format is not something a
// Vulkan implementation has to support at all.
constexpr size_t kBytesPerTexel = 8;

} // namespace

EnvironmentProbe::EnvironmentProbe(VulkanDevice& device, vk::CommandPool commandPool)
    : m_deviceRef(device), m_commandPool(commandPool) {
    // Black until told otherwise, so every descriptor slot is written from the
    // first frame. `hasEnvironment` is what decides whether the shader looks at
    // any of it.
    LoadConstant(glm::vec3(0.0f));
    m_hasEnvironment = false;
}

void EnvironmentProbe::LoadConstant(const glm::vec3& colour) {
    const Cubemap flat = EnvironmentMap::Constant(kIrradianceSize, colour);

    std::vector<Cubemap> levels;
    for (uint32_t level = 0; level < kPrefilteredLevels; ++level) {
        levels.push_back(EnvironmentMap::Constant(
            std::max(kPrefilteredSize >> level, 1u), colour));
    }

    Upload(flat, levels);
    m_hasEnvironment = true;
    m_loadedPath.clear();
}

bool EnvironmentProbe::Load(const std::string& path, float intensity) {
    std::vector<glm::vec3> pixels;
    uint32_t width = 0;
    uint32_t height = 0;
    std::string error;

    if (!EnvironmentMap::LoadRadiance(path, pixels, width, height, error)) {
        SUPERSONIC_LOG_ERROR("EnvironmentProbe") << error << std::endl;
        return false;
    }

    if (intensity != 1.0f) {
        for (glm::vec3& pixel : pixels) pixel *= intensity;
    }

    Cubemap source;
    if (!EnvironmentMap::FromEquirectangular(pixels, width, height, kPrefilteredSize, source)) {
        SUPERSONIC_LOG_ERROR("EnvironmentProbe")
            << path << " could not be projected onto a cube." << std::endl;
        return false;
    }

    Cubemap irradiance;
    if (!EnvironmentMap::Irradiance(source, kIrradianceSize, irradiance)) return false;

    std::vector<Cubemap> prefiltered;
    if (!EnvironmentMap::Prefilter(source, kPrefilteredSize, kPrefilteredLevels, prefiltered)) {
        return false;
    }

    Upload(irradiance, prefiltered);
    m_hasEnvironment = true;
    m_loadedPath = path;

    SUPERSONIC_LOG_INFO("EnvironmentProbe")
        << "Loaded " << path << " (" << width << "x" << height << ") into a "
        << kPrefilteredSize << " cube with " << kPrefilteredLevels << " roughness levels."
        << std::endl;
    return true;
}

void EnvironmentProbe::Upload(const Cubemap& irradiance, const std::vector<Cubemap>& prefiltered) {
    UploadOne(m_irradiance, {irradiance});
    UploadOne(m_prefiltered, prefiltered);
}

void EnvironmentProbe::UploadOne(std::unique_ptr<VulkanImage>& target,
                                 const std::vector<Cubemap>& levels) {
    if (levels.empty() || !levels.front().valid()) return;

    const uint32_t baseSize = levels.front().size();
    const auto mipLevels = static_cast<uint32_t>(levels.size());

    // One staging buffer for every face of every level, laid out in the order
    // the copy regions below name them.
    size_t bytes = 0;
    for (const Cubemap& level : levels) {
        bytes += static_cast<size_t>(level.size()) * level.size() * Cubemap::kFaceCount *
                 kBytesPerTexel;
    }

    std::vector<uint16_t> staged;
    staged.reserve(bytes / sizeof(uint16_t));

    std::vector<vk::BufferImageCopy> regions;
    regions.reserve(static_cast<size_t>(mipLevels) * Cubemap::kFaceCount);

    size_t offset = 0;
    for (uint32_t level = 0; level < mipLevels; ++level) {
        const Cubemap& map = levels[level];
        const uint32_t size = map.size();

        for (uint32_t face = 0; face < Cubemap::kFaceCount; ++face) {
            vk::BufferImageCopy region{};
            region.bufferOffset = offset;
            region.bufferRowLength = 0;
            region.bufferImageHeight = 0;
            region.imageSubresource.aspectMask = vk::ImageAspectFlagBits::eColor;
            region.imageSubresource.mipLevel = level;
            // The layer IS the face, in the order Cubemap::Face names them,
            // which is the order Vulkan expects a cube image's layers in.
            region.imageSubresource.baseArrayLayer = face;
            region.imageSubresource.layerCount = 1;
            region.imageOffset = vk::Offset3D{0, 0, 0};
            region.imageExtent = vk::Extent3D{size, size, 1};
            regions.push_back(region);

            for (uint32_t y = 0; y < size; ++y) {
                for (uint32_t x = 0; x < size; ++x) {
                    const glm::vec3& texel = map.At(face, x, y);
                    staged.push_back(toHalf(texel.r));
                    staged.push_back(toHalf(texel.g));
                    staged.push_back(toHalf(texel.b));
                    staged.push_back(toHalf(1.0f));
                }
            }
            offset += static_cast<size_t>(size) * size * kBytesPerTexel;
        }
    }

    VulkanBuffer staging(m_deviceRef.GetAllocator(), offset,
                         vk::BufferUsageFlagBits::eTransferSrc, VMA_MEMORY_USAGE_CPU_ONLY);
    staging.UploadData(staged.data(), offset);

    target = std::make_unique<VulkanImage>(
        m_deviceRef, baseSize, baseSize, kFormat,
        vk::ImageUsageFlagBits::eSampled | vk::ImageUsageFlagBits::eTransferDst,
        vk::ImageAspectFlagBits::eColor,
        /*arrayLayers*/ Cubemap::kFaceCount, vk::SampleCountFlagBits::e1,
        /*cubeCompatible*/ true, /*generateMipmaps*/ false, mipLevels);

    // Clamped, not repeated. A cube sampler wraps across faces on its own, and
    // eRepeat on a cube is a sampler asking for a face that is not there.
    target->CreateSampler(vk::Filter::eLinear, vk::SamplerAddressMode::eClampToEdge);

    VulkanImage::UploadLayeredImage(m_deviceRef, m_commandPool, staging.GetBuffer(),
                                    target->GetImage(), Cubemap::kFaceCount, mipLevels, regions);
}

vk::DescriptorImageInfo EnvironmentProbe::IrradianceInfo() const {
    vk::DescriptorImageInfo info{};
    if (m_irradiance) {
        info.imageLayout = vk::ImageLayout::eShaderReadOnlyOptimal;
        info.imageView = m_irradiance->GetImageView();
        info.sampler = m_irradiance->GetSampler();
    }
    return info;
}

vk::DescriptorImageInfo EnvironmentProbe::PrefilteredInfo() const {
    vk::DescriptorImageInfo info{};
    if (m_prefiltered) {
        info.imageLayout = vk::ImageLayout::eShaderReadOnlyOptimal;
        info.imageView = m_prefiltered->GetImageView();
        info.sampler = m_prefiltered->GetSampler();
    }
    return info;
}

} // namespace Supersonic
