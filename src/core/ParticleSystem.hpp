#pragma once

#include <entt/entt.hpp>
#include <vector>
#include <glm/glm.hpp>

namespace Engine {

struct Particle {
    glm::vec3 position{0.0f};
    glm::vec3 velocity{0.0f};
    glm::vec4 color{1.0f};
    float lifetime{1.0f};
    float maxLifetime{1.0f};
    bool active{false};
};

class ParticleSystem {
public:
    static void Update(entt::registry& registry, float deltaTime);

private:
    static std::vector<Particle> s_particlePool;
};

} // namespace Engine
