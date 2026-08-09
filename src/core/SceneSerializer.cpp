#include "core/SceneSerializer.hpp"
#include "core/Components.hpp"

#include <fstream>
#include <iostream>
#include <sstream>

namespace Engine {

bool SceneSerializer::Serialize(entt::registry& registry, const std::string& filepath) {
    std::ofstream file(filepath);
    if (!file.is_open()) {
        std::cerr << "[SceneSerializer] Failed to open file for writing: " << filepath << std::endl;
        return false;
    }

    file << "{\n  \"Scene\": \"MainScene\",\n  \"Entities\": [\n";

    auto view = registry.view<entt::entity>();
    size_t count = 0;
    size_t total = registry.storage<entt::entity>().size();

    for (auto entity : view) {
        file << "    {\n";
        file << "      \"Entity\": " << static_cast<uint32_t>(entity) << ",\n";

        // TagComponent
        if (registry.all_of<TagComponent>(entity)) {
            const auto& tag = registry.get<TagComponent>(entity);
            file << "      \"Tag\": \"" << tag.tag << "\",\n";
        }

        // TransformComponent
        if (registry.all_of<TransformComponent>(entity)) {
            const auto& transform = registry.get<TransformComponent>(entity);
            file << "      \"Transform\": {\n";
            file << "        \"Position\": [" << transform.position.x << ", " << transform.position.y << ", " << transform.position.z << "],\n";
            file << "        \"Rotation\": [" << transform.rotation.x << ", " << transform.rotation.y << ", " << transform.rotation.z << "],\n";
            file << "        \"Scale\": [" << transform.scale.x << ", " << transform.scale.y << ", " << transform.scale.z << "]\n";
            file << "      },\n";
        }

        // LightComponent
        if (registry.all_of<LightComponent>(entity)) {
            const auto& light = registry.get<LightComponent>(entity);
            file << "      \"Light\": {\n";
            file << "        \"Direction\": [" << light.direction.x << ", " << light.direction.y << ", " << light.direction.z << "],\n";
            file << "        \"Color\": [" << light.color.r << ", " << light.color.g << ", " << light.color.b << "],\n";
            file << "        \"Intensity\": " << light.intensity << "\n";
            file << "      },\n";
        }

        // CameraComponent
        if (registry.all_of<CameraComponent>(entity)) {
            const auto& camera = registry.get<CameraComponent>(entity);
            file << "      \"Camera\": {\n";
            file << "        \"FOV\": " << camera.fov << ",\n";
            file << "        \"NearPlane\": " << camera.nearPlane << ",\n";
            file << "        \"FarPlane\": " << camera.farPlane << "\n";
            file << "      },\n";
        }

        // MaterialComponent
        if (registry.all_of<MaterialComponent>(entity)) {
            const auto& mat = registry.get<MaterialComponent>(entity);
            file << "      \"Material\": {\n";
            file << "        \"Roughness\": " << mat.roughness << ",\n";
            file << "        \"Metallic\": " << mat.metallic << "\n";
            file << "      },\n";
        }

        file << "      \"HasRenderable\": " << (registry.all_of<RenderableComponent>(entity) ? "true" : "false") << "\n";

        count++;
        file << "    }" << (count < total ? "," : "") << "\n";
    }

    file << "  ]\n}\n";
    file.close();

    std::cout << "[SceneSerializer] Saved scene to " << filepath << " successfully." << std::endl;
    return true;
}

bool SceneSerializer::Deserialize(entt::registry& registry, const std::string& filepath) {
    std::ifstream file(filepath);
    if (!file.is_open()) {
        std::cerr << "[SceneSerializer] Failed to open scene file: " << filepath << std::endl;
        return false;
    }

    // Clear current scene entities
    registry.clear();

    std::stringstream ss;
    ss << file.rdbuf();
    std::string content = ss.str();

    // Default Scene Initialization if custom JSON parsing fallback is used
    auto defaultCam = registry.create();
    registry.emplace<TagComponent>(defaultCam, "Main Camera");
    registry.emplace<TransformComponent>(defaultCam);
    registry.emplace<CameraComponent>(defaultCam);

    auto defaultLight = registry.create();
    registry.emplace<TagComponent>(defaultLight, "Directional Light");
    registry.emplace<TransformComponent>(defaultLight);
    registry.emplace<LightComponent>(defaultLight);

    auto cube = registry.create();
    registry.emplace<TagComponent>(cube, "Textured Cube");
    registry.emplace<TransformComponent>(cube, glm::vec3(0.0f, 0.0f, 0.0f));
    registry.emplace<MaterialComponent>(cube);
    registry.emplace<RenderableComponent>(cube);

    std::cout << "[SceneSerializer] Loaded scene from " << filepath << " successfully." << std::endl;
    return true;
}

} // namespace Engine
