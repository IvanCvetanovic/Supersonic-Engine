#include "core/Light2D.hpp"

#include <algorithm>
#include <cmath>
#include <cstring>

#include "core/Components.hpp"
#include "core/LightSelection.hpp"

namespace Supersonic {

namespace Light2D {

uint32_t GatherLights2D(const entt::registry& registry,
                        std::vector<GpuLight2D>& out,
                        uint32_t capacity,
                        uint32_t* outDropped,
                        std::vector<entt::entity>* outEntities) {
    out.clear();
    if (outEntities != nullptr) outEntities->clear();
    uint32_t dropped = 0;

    for (const auto entity : registry.view<Light2DComponent>()) {
        const Light2DComponent& light = registry.get<Light2DComponent>(entity);
        if (!light.enabled) continue;

        // Folded before the zero test, so an intensity of zero is the same
        // light as a black one: neither can add anything.
        const glm::vec3 color = light.color * light.intensity;
        if (color == glm::vec3(0.0f)) continue;

        if (static_cast<uint32_t>(out.size()) >= capacity) {
            ++dropped;
            continue;
        }

        const glm::vec3 world = LightWorldPosition(registry, entity);

        GpuLight2D gpu;
        gpu.position = glm::vec3(world.x, world.y, light.height);
        gpu.range = light.range;
        gpu.color = color;
        // A light that is not baked packs the layer byte alone, as it always did.
        gpu.layers = light.baked ? (static_cast<uint32_t>(light.layers) | kLight2DBakedBit) : light.layers;
        out.push_back(gpu);
        if (outEntities != nullptr) outEntities->push_back(entity);
    }

    if (outDropped != nullptr) *outDropped = dropped;
    return static_cast<uint32_t>(out.size());
}

uint32_t GatherShadows2D(const entt::registry& registry,
                         const std::vector<entt::entity>& lights,
                         std::vector<glm::uvec2>& outRanges,
                         std::vector<GpuShadow2D>& outStrips,
                         uint32_t capacity,
                         uint32_t* outDropped) {
    outRanges.assign(lights.size(), glm::uvec2(0u));
    outStrips.clear();
    uint32_t dropped = 0;

    for (size_t i = 0; i < lights.size(); ++i) {
        const auto* shadows = registry.valid(lights[i]) ? registry.try_get<Light2DShadowsComponent>(lights[i])
                                                        : nullptr;
        if (shadows == nullptr) continue;
        const uint32_t first = static_cast<uint32_t>(outStrips.size());
        for (const Light2DShadowsComponent::Strip& strip : shadows->strips) {
            if (static_cast<uint32_t>(outStrips.size()) >= capacity) {
                ++dropped;
                continue;
            }
            GpuShadow2D gpu;
            glm::vec2 lo = strip.corners[0];
            glm::vec2 hi = strip.corners[0];
            for (int c = 0; c < 5; ++c) {
                gpu.corners[c] = strip.corners[c];
                lo = glm::min(lo, strip.corners[c]);
                hi = glm::max(hi, strip.corners[c]);
            }
            gpu.bounds = glm::vec4(lo, hi);
            gpu.opacity = strip.opacity;
            outStrips.push_back(gpu);
        }
        outRanges[i] = glm::uvec2(first, static_cast<uint32_t>(outStrips.size()) - first);
    }

    if (outDropped != nullptr) *outDropped = dropped;
    return static_cast<uint32_t>(outStrips.size());
}

void PackShadows2D(const entt::registry& registry,
                   const std::vector<glm::uvec2>& ranges,
                   const std::vector<GpuShadow2D>& strips,
                   std::vector<uint8_t>& out) {
    GpuShadow2DHeader header;
    header.count = static_cast<uint32_t>(std::min<size_t>(strips.size(), kMaxShadows2D));
    const Light2DShadowMask* mask = registry.ctx().find<Light2DShadowMask>();
    const bool maskFits = mask != nullptr && mask->width > 0 && mask->height > 0 &&
                          static_cast<uint64_t>(mask->width) * mask->height <= kMaxShadowMask2DTexels &&
                          mask->alpha.size() == static_cast<size_t>(mask->width) * mask->height;
    if (maskFits) {
        header.maskWidth = mask->width;
        header.maskHeight = mask->height;
    }

    // Nothing past the count is read while it is zero.
    if (header.count == 0) {
        out.resize(sizeof(GpuShadow2DHeader));
        std::memcpy(out.data(), &header, sizeof(header));
        return;
    }

    out.assign(kShadow2DStripsOffset + sizeof(GpuShadow2D) * header.count, uint8_t{0});
    std::memcpy(out.data(), &header, sizeof(header));
    const size_t rangeCount = std::min<size_t>(ranges.size(), kMaxLights2D);
    if (rangeCount > 0) {
        std::memcpy(out.data() + kShadow2DRangesOffset, ranges.data(), sizeof(glm::uvec2) * rangeCount);
    }
    if (maskFits) {
        std::memcpy(out.data() + kShadow2DMaskOffset, mask->alpha.data(), sizeof(float) * mask->alpha.size());
    }
    std::memcpy(out.data() + kShadow2DStripsOffset, strips.data(), sizeof(GpuShadow2D) * header.count);
}

GpuLight2DHeader MakeHeader(const entt::registry& registry, uint32_t count) {
    GpuLight2DHeader header;
    header.count = count;
    if (const Light2DEye* eye = registry.ctx().find<Light2DEye>()) {
        header.eyeMirrorY = eye->mirrorY;
        header.eyeHeight = eye->height;
    }
    if (const Light2DAlphaTest* alphaTest = registry.ctx().find<Light2DAlphaTest>()) {
        header.passAlphaIntensity = alphaTest->intensity;
    }
    return header;
}

float PassAlphaOf(const PassAlpha& pass, float facing, float attenuation, bool highlighted, float shine) {
    if (highlighted) {
        // albedoTex.a * attenuation * (colorAlpha * facing * I + shine * glossAlpha * I)
        return pass.albedoAlpha * attenuation *
               (pass.colorAlpha * facing * pass.intensity + shine * pass.glossAlpha * pass.intensity);
    }
    // passAlphaScale * attenuation * facing, the scale taking the texel's alpha
    // twice for a flat sprite (hPixelLight's main weights its whole output by it)
    const float scale = pass.albedoAlpha * pass.colorAlpha * pass.intensity * (pass.vertical ? 1.0f : pass.albedoAlpha);
    return scale * attenuation * facing;
}

glm::vec3 WorldNormal(const glm::vec3& encodedTexel, bool normalYDown, const glm::mat4& model) {
    // vec3 c = texture(normalMap, uv).rgb * 2.0 - 1.0;
    glm::vec3 c = encodedTexel * 2.0f - 1.0f;
    // if ((flags & FLAG_NORMAL_Y_DOWN) != 0) c.y = -c.y;
    if (normalYDown) c.y = -c.y;
    // normalize(model[0].xyz) * c.x + normalize(model[1].xyz) * c.y + vec3(0.0, 0.0, c.z)
    return glm::normalize(glm::vec3(model[0])) * c.x + glm::normalize(glm::vec3(model[1])) * c.y +
           glm::vec3(0.0f, 0.0f, c.z);
}

glm::vec3 Contribution(const GpuLight2D& light, uint8_t mask,
                       const glm::vec3& surface, const glm::vec3& normal,
                       const glm::vec3& tint, const PassAlpha* passAlpha) {
    // if ((light2D.lights[i].layers & mask) == 0u) continue;
    if ((light.layers & static_cast<uint32_t>(mask)) == 0u) return glm::vec3(0.0f);

    // vec3 v = position - p; float d2 = dot(v, v); float r2 = range * range;
    const glm::vec3 v = light.position - surface;
    const float d2 = glm::dot(v, v);
    const float r2 = light.range * light.range;
    // if (d2 >= r2) continue;
    if (d2 >= r2) return glm::vec3(0.0f);

    // float attenuation = 1.0 - d2 / r2;
    const float attenuation = 1.0f - d2 / r2;
    // float facing = dot(v, n) * inversesqrt(max(d2, 1e-12));
    const float facing = glm::dot(v, normal) * (1.0f / std::sqrt(std::max(d2, 1e-12f)));
    // if (alphaTest && passAlphaScale * attenuation * facing < LIGHT_PASS_ALPHA_REF) continue;
    if (passAlpha != nullptr && PassAlphaOf(*passAlpha, facing, attenuation) < kLightPassAlphaRef) {
        return glm::vec3(0.0f);
    }
    // lit += clamp(tint * color * (attenuation * facing), 0.0, 1.0);
    return glm::clamp(tint * light.color * (attenuation * facing), 0.0f, 1.0f);
}

StoodUp StandUp(const glm::vec3& flatPoint, const glm::vec3& flatNormal, float baseY) {
    StoodUp out;
    // p = vec3(fragWorldPos.x, baseY, p.z + (fragWorldPos.y - baseY));
    out.point = glm::vec3(flatPoint.x, baseY, flatPoint.z + (flatPoint.y - baseY));
    // n = vec3(n.x, -n.z, n.y);
    out.normal = glm::vec3(flatNormal.x, -flatNormal.z, flatNormal.y);
    return out;
}

glm::vec3 EyeFor(const glm::vec3& lightPosition, float eyeMirrorY, float eyeHeight) {
    // vec3(l.x, 2.0 * light2D.eyeMirrorY - l.y, light2D.eyeHeight)
    return glm::vec3(lightPosition.x, 2.0f * eyeMirrorY - lightPosition.y, eyeHeight);
}

glm::vec3 BakedEyeFor(const glm::vec3& lightPosition, float bakedEyeY, float spriteHeight, float eyeHeight) {
    // vec3(l.x, bakedEyeY, instances[fragInstance].emissive.w + light2D.eyeHeight)
    return glm::vec3(lightPosition.x, bakedEyeY, spriteHeight + eyeHeight);
}

glm::vec3 SpecularContribution(const GpuLight2D& light, uint8_t mask,
                               const glm::vec3& surface, const glm::vec3& normal,
                               const glm::vec3& tint, const Highlight& highlight,
                               const PassAlpha* passAlpha) {
    const auto inverseLength = [](float squared) { return 1.0f / std::sqrt(std::max(squared, 1e-12f)); };

    // The loop's head is Contribution's, line for line.
    if ((light.layers & static_cast<uint32_t>(mask)) == 0u) return glm::vec3(0.0f);
    const glm::vec3 v = light.position - surface;
    const float d2 = glm::dot(v, v);
    const float r2 = light.range * light.range;
    if (d2 >= r2) return glm::vec3(0.0f);
    const float attenuation = 1.0f - d2 / r2;
    const float facing = glm::dot(v, normal) * inverseLength(d2);

    // vec3 e = (bakedEye && (layers & LIGHT_BAKED_BIT) != 0u)
    //     ? vec3(l.x, bakedEyeY, emissive.w + eyeHeight) - p
    //     : vec3(l.x, 2.0 * eyeMirrorY - l.y, eyeHeight) - p;
    const glm::vec3 e =
        (highlight.bakedEye && (light.layers & kLight2DBakedBit) != 0u)
            ? BakedEyeFor(light.position, highlight.bakedEyeY, highlight.spriteHeight, highlight.eyeHeight) - surface
            : EyeFor(light.position, highlight.eyeMirrorY, highlight.eyeHeight) - surface;
    // vec3 h = v * inversesqrt(max(d2, 1e-12)) + e * inversesqrt(max(dot(e, e), 1e-12));
    glm::vec3 h = v * inverseLength(d2) + e * inverseLength(glm::dot(e, e));
    // h *= inversesqrt(max(dot(h, h), 1e-12));
    h *= inverseLength(glm::dot(h, h));
    // float nh = clamp(dot(n, h), 0.0, 1.0);
    const float nh = glm::clamp(glm::dot(normal, h), 0.0f, 1.0f);
    // float shine = nh > 0.0 ? pow(nh, specularPower) : 0.0;
    const float shine = nh > 0.0f ? std::pow(nh, highlight.power) : 0.0f;

    // if (alphaTest && albedoTex.a * attenuation * (colorAlpha * facing * I + shine * glossAlpha * I)
    //                  < LIGHT_PASS_ALPHA_REF) continue;
    if (passAlpha != nullptr && PassAlphaOf(*passAlpha, facing, attenuation, true, shine) < kLightPassAlphaRef) {
        return glm::vec3(0.0f);
    }

    // lit += clamp(tint * color * (attenuation * facing)
    //              + color * gloss * (shine * attenuation), 0.0, 1.0);
    return glm::clamp(tint * light.color * (attenuation * facing) +
                          light.color * highlight.gloss * (shine * attenuation),
                      0.0f, 1.0f);
}

float ShadowMaskAt(const float* mask, uint32_t width, uint32_t height, const glm::vec2& uv) {
    // if (w == 0u || h == 0u || w * h > MAX_SHADOW_MASK_TEXELS_2D) return 1.0;
    if (mask == nullptr || width == 0u || height == 0u ||
        static_cast<uint64_t>(width) * height > kMaxShadowMask2DTexels) {
        return 1.0f;
    }
    // vec2 t = uv * vec2(w, h) - 0.5; ivec2 i0 = ivec2(floor(t)); vec2 f = t - floor(t);
    const glm::vec2 t = uv * glm::vec2(static_cast<float>(width), static_cast<float>(height)) - 0.5f;
    const glm::vec2 base = glm::floor(t);
    const glm::vec2 f = t - base;
    const glm::ivec2 i0(base);
    // Clamped to the edge texels, both corners of each pair.
    const glm::ivec2 top(static_cast<int>(width) - 1, static_cast<int>(height) - 1);
    const glm::ivec2 a = glm::clamp(i0, glm::ivec2(0), top);
    const glm::ivec2 b = glm::clamp(i0 + 1, glm::ivec2(0), top);
    const auto at = [&](int x, int y) { return mask[static_cast<size_t>(y) * width + static_cast<size_t>(x)]; };
    // mix(mix(m00, m10, f.x), mix(m01, m11, f.x), f.y)
    const float upper = glm::mix(at(a.x, a.y), at(b.x, a.y), f.x);
    const float lower = glm::mix(at(a.x, b.y), at(b.x, b.y), f.x);
    return glm::mix(upper, lower, f.y);
}

bool ShadowStripUv(const GpuShadow2D& strip, const glm::vec2& at, glm::vec2& outUv) {
    for (const auto& triangle : kShadowStripTriangles) {
        // vec2 e1 = b - a, e2 = c - a, d = at - a; float den = e1.x * e2.y - e2.x * e1.y;
        const glm::vec2 a = strip.corners[triangle[0]];
        const glm::vec2 e1 = strip.corners[triangle[1]] - a;
        const glm::vec2 e2 = strip.corners[triangle[2]] - a;
        const glm::vec2 d = at - a;
        const float den = e1.x * e2.y - e2.x * e1.y;
        // if (abs(den) < 1e-12) continue;
        if (std::abs(den) < 1e-12f) continue;
        // float wb = (d.x * e2.y - e2.x * d.y) / den; float wc = (e1.x * d.y - d.x * e1.y) / den;
        const float wb = (d.x * e2.y - e2.x * d.y) / den;
        const float wc = (e1.x * d.y - d.x * e1.y) / den;
        const float wa = 1.0f - wb - wc;
        if (wa < 0.0f || wb < 0.0f || wc < 0.0f) continue;
        const auto uvOf = [](uint32_t corner) { return glm::vec2(kShadowStripUv[corner][0], kShadowStripUv[corner][1]); };
        outUv = uvOf(triangle[0]) * wa + uvOf(triangle[1]) * wb + uvOf(triangle[2]) * wc;
        return true;
    }
    return false;
}

float ShadowKeep(const std::vector<GpuShadow2D>& strips, const glm::uvec2& range,
                 const float* mask, uint32_t maskWidth, uint32_t maskHeight, const glm::vec2& at) {
    float keep = 1.0f;
    for (uint32_t s = range.x; s < range.x + range.y && s < strips.size(); ++s) {
        const GpuShadow2D& strip = strips[s];
        // The box first: most strips are nowhere near a given fragment.
        if (at.x < strip.bounds.x || at.y < strip.bounds.y || at.x > strip.bounds.z || at.y > strip.bounds.w) {
            continue;
        }
        glm::vec2 uv;
        if (!ShadowStripUv(strip, at, uv)) continue;
        keep *= 1.0f - strip.opacity * ShadowMaskAt(mask, maskWidth, maskHeight, uv);
    }
    return keep;
}

} // namespace Light2D

} // namespace Supersonic
