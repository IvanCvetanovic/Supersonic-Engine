#include "core/Light2D.hpp"

#include <algorithm>
#include <cmath>

#include "core/Components.hpp"
#include "core/LightSelection.hpp"

namespace Supersonic {

namespace Light2D {

uint32_t GatherLights2D(const entt::registry& registry,
                        std::vector<GpuLight2D>& out,
                        uint32_t capacity,
                        uint32_t* outDropped) {
    out.clear();
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
        gpu.layers = light.layers;
        out.push_back(gpu);
    }

    if (outDropped != nullptr) *outDropped = dropped;
    return static_cast<uint32_t>(out.size());
}

GpuLight2DHeader MakeHeader(const entt::registry& registry, uint32_t count) {
    GpuLight2DHeader header;
    header.count = count;
    if (const Light2DEye* eye = registry.ctx().find<Light2DEye>()) {
        header.eyeMirrorY = eye->mirrorY;
        header.eyeHeight = eye->height;
    }
    return header;
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
                       const glm::vec3& tint) {
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

glm::vec3 SpecularContribution(const GpuLight2D& light, uint8_t mask,
                               const glm::vec3& surface, const glm::vec3& normal,
                               const glm::vec3& tint, const Highlight& highlight) {
    const auto inverseLength = [](float squared) { return 1.0f / std::sqrt(std::max(squared, 1e-12f)); };

    // The loop's head is Contribution's, line for line.
    if ((light.layers & static_cast<uint32_t>(mask)) == 0u) return glm::vec3(0.0f);
    const glm::vec3 v = light.position - surface;
    const float d2 = glm::dot(v, v);
    const float r2 = light.range * light.range;
    if (d2 >= r2) return glm::vec3(0.0f);
    const float attenuation = 1.0f - d2 / r2;
    const float facing = glm::dot(v, normal) * inverseLength(d2);

    // vec3 e = vec3(l.x, 2.0 * eyeMirrorY - l.y, eyeHeight) - p;
    const glm::vec3 e = EyeFor(light.position, highlight.eyeMirrorY, highlight.eyeHeight) - surface;
    // vec3 h = v * inversesqrt(max(d2, 1e-12)) + e * inversesqrt(max(dot(e, e), 1e-12));
    glm::vec3 h = v * inverseLength(d2) + e * inverseLength(glm::dot(e, e));
    // h *= inversesqrt(max(dot(h, h), 1e-12));
    h *= inverseLength(glm::dot(h, h));
    // float nh = clamp(dot(n, h), 0.0, 1.0);
    const float nh = glm::clamp(glm::dot(normal, h), 0.0f, 1.0f);
    // float shine = nh > 0.0 ? pow(nh, specularPower) : 0.0;
    const float shine = nh > 0.0f ? std::pow(nh, highlight.power) : 0.0f;

    // lit += clamp(tint * color * (attenuation * facing)
    //              + color * gloss * (shine * attenuation), 0.0, 1.0);
    return glm::clamp(tint * light.color * (attenuation * facing) +
                          light.color * highlight.gloss * (shine * attenuation),
                      0.0f, 1.0f);
}

} // namespace Light2D

} // namespace Supersonic
