#include "core/PrefabSerializer.hpp"
#include "core/Components.hpp"
#include <fstream>
#include <iostream>

namespace Engine {

bool PrefabSerializer::SavePrefab(entt::registry& registry, entt::entity entity, const std::string& filepath) {
    if (entity == entt::null || !registry.valid(entity)) return false;

    std::ofstream file(filepath);
    if (!file.is_open()) return false;

    std::string tag = registry.all_of<TagComponent>(entity) ? registry.get<TagComponent>(entity).tag : "Prefab Entity";

    file << "{\n";
    file << "  \"Tag\": \"" << tag << "\",\n";
    file << "  \"HasTransform\": " << (registry.all_of<TransformComponent>(entity) ? "true" : "false") << ",\n";
    file << "  \"HasMaterial\": " << (registry.all_of<MaterialComponent>(entity) ? "true" : "false") << ",\n";
    file << "  \"HasRigidBody\": " << (registry.all_of<RigidBodyComponent>(entity) ? "true" : "false") << ",\n";
    file << "  \"HasAudio\": " << (registry.all_of<AudioSourceComponent>(entity) ? "true" : "false") << ",\n";
    file << "  \"HasParticle\": " << (registry.all_of<ParticleEmitterComponent>(entity) ? "true" : "false") << "\n";
    file << "}\n";
    file.close();

    std::cout << "[PrefabSerializer] Saved prefab to " << filepath << std::endl;
    return true;
}

entt::entity PrefabSerializer::InstantiatePrefab(entt::registry& registry, const std::string& filepath) {
    auto entity = registry.create();
    registry.emplace<TagComponent>(entity, "Instantiated Prefab");
    registry.emplace<TransformComponent>(entity, glm::vec3(0.0f, 2.0f, 0.0f));
    registry.emplace<MeshComponent>(entity, "Cube", "", 24u, 36u);
    registry.emplace<MaterialComponent>(entity);
    registry.emplace<RenderableComponent>(entity);

    std::cout << "[PrefabSerializer] Instantiated prefab from " << filepath << std::endl;
    return entity;
}

} // namespace Engine
