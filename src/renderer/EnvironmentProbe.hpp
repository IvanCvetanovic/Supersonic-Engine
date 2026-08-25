#pragma once

#include <memory>
#include <string>

#include <glm/glm.hpp>

#include "core/EnvironmentMap.hpp"
#include "renderer/VulkanImage.hpp"

namespace Supersonic {

class VulkanDevice;

// The two cubemaps a shader needs to light a surface from an environment.
//
// `EnvironmentMap` does the arithmetic and is tested against exact answers; this
// puts the results on the GPU and is the part a picture has to verify. The split
// is the same one `ClusterGrid` and its buffers have, and for the same reason:
// everything that can be checked with a number is on the other side of it.
//
// ALWAYS valid, even before anything is loaded. A descriptor slot that is never
// written is undefined to read even inside a branch the shader does not take,
// so an unloaded probe is a one-texel black cube rather than a null handle -
// and whether the shader uses it at all is a flag in the scene block.
class EnvironmentProbe {
public:
    // Small on purpose. Diffuse irradiance has no detail in it - it is the
    // integral of everything - and the convolution that produces it is the
    // expensive part of the whole feature.
    static constexpr uint32_t kIrradianceSize = 16;

    // The sharpest specular level, and how many roughness steps there are. Five
    // levels means the roughest is a 8x8 face, which is as blurred as a
    // reflection ever needs to be.
    // 256 rather than 128 because this is now what the SKY samples, not only
    // what a reflection does. A reflection is a small bright smear and forgives
    // a soft source; a background fills the screen and does not.
    //
    // Affordable because level 0 stopped integrating: it is a straight resample
    // now, so quadrupling its texel count costs less than the 128 samples per
    // texel it used to pay. The blurry levels grew 4x and they are the small
    // ones. Storage is 6 faces x (256^2 + 128^2 + ... ) x 8 bytes, about 4 MB.
    static constexpr uint32_t kPrefilteredSize = 256;
    static constexpr uint32_t kPrefilteredLevels = 5;

    EnvironmentProbe(VulkanDevice& device, vk::CommandPool commandPool);

    // Reads a Radiance .hdr, convolves it, and uploads both maps.
    //
    // On failure the probe keeps whatever it had and returns false, so a scene
    // naming a file that is not there renders with the environment it was
    // already using rather than with nothing.
    bool Load(const std::string& path, float intensity = 1.0f);

    // One colour in every direction. What an unloaded probe is, and the
    // baseline the renderer is checked against: with the scene's sky and ground
    // ambient set to this colour too, the environment path has to produce the
    // same image as the analytic hemisphere it replaces.
    void LoadConstant(const glm::vec3& colour);

    // False until something has actually been loaded from a file. The shader
    // falls back to the analytic hemisphere while this is false, so a scene
    // that names no environment renders exactly as it did before any of this
    // existed - which is what makes the change safe to land.
    bool hasEnvironment() const { return m_hasEnvironment; }

    const std::string& loadedPath() const { return m_loadedPath; }

    vk::DescriptorImageInfo IrradianceInfo() const;
    vk::DescriptorImageInfo PrefilteredInfo() const;

private:
    void Upload(const Cubemap& irradiance, const std::vector<Cubemap>& prefiltered);
    void UploadOne(std::unique_ptr<VulkanImage>& target, const std::vector<Cubemap>& levels);

    VulkanDevice& m_deviceRef;
    vk::CommandPool m_commandPool{};

    std::unique_ptr<VulkanImage> m_irradiance;
    std::unique_ptr<VulkanImage> m_prefiltered;

    bool m_hasEnvironment{false};
    std::string m_loadedPath;
};

} // namespace Supersonic
