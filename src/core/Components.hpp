#pragma once

#include <array>
#include <string>
#include <vector>

// HierarchyComponent stores an entt::entity, so the handle type is needed here.
#include <entt/entt.hpp>

// GLM_FORCE_RADIANS / GLM_FORCE_DEPTH_ZERO_TO_ONE are set on the target in
// CMakeLists.txt, never here. glm/detail/setup.hpp latches its configuration on
// first inclusion, so a header-local define only applies when this header
// happens to be the first one to pull GLM in - which silently produced two
// different getProjectionMatrix() bodies across translation units.
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>

#include <vulkan/vulkan.hpp>

namespace Engine {

struct Vertex {
    glm::vec3 pos;
    glm::vec3 normal;
    glm::vec3 color;
    glm::vec2 texCoord;

    static vk::VertexInputBindingDescription getBindingDescription() {
        vk::VertexInputBindingDescription bindingDescription{};
        bindingDescription.binding = 0;
        bindingDescription.stride = sizeof(Vertex);
        bindingDescription.inputRate = vk::VertexInputRate::eVertex;
        return bindingDescription;
    }

    static std::array<vk::VertexInputAttributeDescription, 4> getAttributeDescriptions() {
        std::array<vk::VertexInputAttributeDescription, 4> attributeDescriptions{};

        // Location 0: Position
        attributeDescriptions[0].binding = 0;
        attributeDescriptions[0].location = 0;
        attributeDescriptions[0].format = vk::Format::eR32G32B32Sfloat;
        attributeDescriptions[0].offset = offsetof(Vertex, pos);

        // Location 1: Normal
        attributeDescriptions[1].binding = 0;
        attributeDescriptions[1].location = 1;
        attributeDescriptions[1].format = vk::Format::eR32G32B32Sfloat;
        attributeDescriptions[1].offset = offsetof(Vertex, normal);

        // Location 2: Color
        attributeDescriptions[2].binding = 0;
        attributeDescriptions[2].location = 2;
        attributeDescriptions[2].format = vk::Format::eR32G32B32Sfloat;
        attributeDescriptions[2].offset = offsetof(Vertex, color);

        // Location 3: TexCoord
        attributeDescriptions[3].binding = 0;
        attributeDescriptions[3].location = 3;
        attributeDescriptions[3].format = vk::Format::eR32G32Sfloat;
        attributeDescriptions[3].offset = offsetof(Vertex, texCoord);

        return attributeDescriptions;
    }
};

// Parent link. Held as a separate component so the common case - an entity with
// no parent - costs nothing, and so a view of "things with parents" is cheap.
struct HierarchyComponent {
    entt::entity parent{entt::null};
};

// Cached world matrix, recomputed each frame by TransformSystem. Rendering,
// picking and the gizmo all read this rather than composing the local transform
// themselves, which is what makes parenting work everywhere at once.
struct WorldTransformComponent {
    glm::mat4 matrix{1.0f};
};

struct TransformComponent {
    // Local to the parent. With no parent this is world space, which is why the
    // engine behaved correctly before hierarchies existed.
    glm::vec3 position{0.0f, 0.0f, 0.0f};
    glm::vec3 rotation{0.0f, 0.0f, 0.0f}; // Euler angles in radians
    glm::vec3 scale{1.0f, 1.0f, 1.0f};

    glm::mat4 getModelMatrix() const {
        glm::mat4 mat = glm::translate(glm::mat4(1.0f), position);
        mat = glm::rotate(mat, rotation.x, glm::vec3(1.0f, 0.0f, 0.0f));
        mat = glm::rotate(mat, rotation.y, glm::vec3(0.0f, 1.0f, 0.0f));
        mat = glm::rotate(mat, rotation.z, glm::vec3(0.0f, 0.0f, 1.0f));
        mat = glm::scale(mat, scale);
        return mat;
    }
};

struct CameraComponent {
    float fov{45.0f};
    float aspect{16.0f / 9.0f};
    float nearPlane{0.1f};
    float farPlane{100.0f};

    glm::vec3 position{0.0f, 1.0f, 4.0f};
    glm::vec3 front{0.0f, 0.0f, -1.0f};
    glm::vec3 up{0.0f, 1.0f, 0.0f};
    glm::vec3 right{1.0f, 0.0f, 0.0f};
    glm::vec3 worldUp{0.0f, 1.0f, 0.0f};

    float yaw{-90.0f};
    float pitch{0.0f};
    float movementSpeed{3.5f};
    float mouseSensitivity{0.1f};

    glm::mat4 getViewMatrix() const {
        return glm::lookAt(position, position + front, up);
    }

    glm::mat4 getProjectionMatrix() const {
        glm::mat4 proj = glm::perspective(glm::radians(fov), aspect, nearPlane, farPlane);
        proj[1][1] *= -1.0f; // Flip Y coordinate for Vulkan
        return proj;
    }

    void updateCameraVectors() {
        glm::vec3 newFront;
        newFront.x = cos(glm::radians(yaw)) * cos(glm::radians(pitch));
        newFront.y = sin(glm::radians(pitch));
        newFront.z = sin(glm::radians(yaw)) * cos(glm::radians(pitch));
        front = glm::normalize(newFront);
        right = glm::normalize(glm::cross(front, worldUp));
        up = glm::normalize(glm::cross(right, front));
    }
};

struct LightComponent {
    // 0 = directional (uses `direction`), 1 = point (uses the entity transform).
    int type{0};

    // Points TOWARD the light, matching the shader's L vector.
    glm::vec3 direction{0.6f, 1.0f, 0.5f};
    glm::vec3 color{1.0f, 0.95f, 0.85f};
    float intensity{1.2f};

    // Ambient is a scene-wide term; only the first light's value is used.
    glm::vec3 ambient{0.12f, 0.12f, 0.14f};

    // Point lights only: cutoff distance.
    float range{25.0f};

    bool castsShadow{true};
};

struct MaterialComponent {
    glm::vec4 albedoColor{1.0f, 1.0f, 1.0f, 1.0f};
    float roughness{0.4f};
    float metallic{0.1f};
    float ao{1.0f};
    std::string albedoTexturePath;
    std::string normalTexturePath;
};

struct MeshComponent {
    std::string primitiveType{"Cube"};
    std::string filePath;
    uint32_t vertexCount{0};
    uint32_t indexCount{0};
};

struct RenderableComponent {
    uint32_t meshID{0};
    uint32_t materialID{0};

    // Resolved from MaterialComponent::albedoTexturePath by
    // RenderSystem::SyncResources. Defaults to the built-in white texture.
    uint32_t albedoTextureID{0};

    bool isVisible{true};
    bool castsShadow{true};

    // Local-space bounds of the resolved mesh, refreshed by
    // RenderSystem::SyncMeshes. Picking uses these so selection matches the
    // geometry actually drawn instead of assuming a unit cube.
    glm::vec3 localBoundsMin{-0.5f};
    glm::vec3 localBoundsMax{0.5f};
};

struct TagComponent {
    std::string tag;
};

// =========================================================================
// NEW SUB-SYSTEM COMPONENTS
// =========================================================================

struct RigidBodyComponent {
    glm::vec3 velocity{0.0f, 0.0f, 0.0f};
    float mass{1.0f};
    bool isKinematic{false};
    bool useGravity{true};
};

struct BoxColliderComponent {
    glm::vec3 size{1.0f, 1.0f, 1.0f};
    bool isTrigger{false};
};

struct SphereColliderComponent {
    float radius{0.5f};
    bool isTrigger{false};
};

struct AudioSourceComponent {
    std::string soundFile{"assets/audio/ambient.wav"};
    float volume{0.8f};
    float pitch{1.0f};
    bool isPlaying{true};
    bool loop{true};

    // Inverse-distance attenuation bounds.
    float referenceDistance{1.5f};
    float maxDistance{40.0f};

    // Runtime state owned by AudioSystem. 0xFFFFFFFF is AudioEngine::kInvalidVoice;
    // spelled out here so Components.hpp stays free of renderer/audio includes.
    uint32_t voice{0xFFFFFFFFu};
    bool failedToLoad{false};
};

struct AudioListenerComponent {
    bool isPrimary{true};
};

struct ScriptComponent {
    std::string scriptName{"RotatorScript"}; // RotatorScript, OscillatorScript, LightFlickerScript
    bool isEnabled{true};

    // Per-entity script state. A single file-static clock in ScriptEngine meant
    // oscillators could not be rewound or phase-offset from each other.
    float elapsed{0.0f};
    glm::vec3 baseline{0.0f};
    float baseIntensity{0.0f};
    bool baselineCaptured{false};

    // Rate-limits the "no such script" diagnostic to once per name change.
    bool warnedMissing{false};
};

struct Particle {
    glm::vec3 position{0.0f};
    glm::vec3 velocity{0.0f};
    glm::vec4 color{1.0f};
    float lifetime{0.0f};
    float maxLifetime{1.0f};
    bool active{false};
};

struct ParticleEmitterComponent {
    uint32_t maxParticles{100};
    float emitRate{10.0f};
    float particleLifetime{2.0f};
    glm::vec4 startColor{1.0f, 0.6f, 0.1f, 1.0f};
    glm::vec4 endColor{1.0f, 0.0f, 0.0f, 0.0f};
    glm::vec3 velocityRange{0.5f, 2.0f, 0.5f};
    float particleSize{0.08f};

    // Each emitter owns its particles. They used to share one 200-entry static
    // pool, so two emitters silently halved each other's throughput and
    // maxParticles/emitRate were never read at all.
    std::vector<Particle> particles;
    float emitAccumulator{0.0f};
};

} // namespace Engine
