#pragma once

#include <array>
#include <string>
#include <vector>

#define GLM_FORCE_RADIANS
#define GLM_FORCE_DEPTH_ZERO_TO_ONE
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

struct TransformComponent {
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
    glm::vec3 direction{-0.5f, -1.0f, -0.3f};
    glm::vec3 color{1.0f, 0.95f, 0.85f};
    float intensity{1.2f};
    glm::vec3 ambient{0.15f, 0.15f, 0.2f};
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
    bool isVisible{true};
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
};

struct AudioListenerComponent {
    bool isPrimary{true};
};

struct ScriptComponent {
    std::string scriptName{"RotatorScript"}; // RotatorScript, OscillatorScript, LightFlickerScript
    bool isEnabled{true};
};

struct ParticleEmitterComponent {
    uint32_t maxParticles{100};
    float emitRate{10.0f};
    float particleLifetime{2.0f};
    glm::vec4 startColor{1.0f, 0.6f, 0.1f, 1.0f};
    glm::vec4 endColor{1.0f, 0.0f, 0.0f, 0.0f};
    glm::vec3 velocityRange{0.5f, 2.0f, 0.5f};
};

} // namespace Engine
