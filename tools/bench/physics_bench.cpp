// Physics step timings on three synthetic scenes, with a bitwise checksum of the
// result.
//
// The checksum is the point of the file as much as the timing: a speed-up is only
// an improvement here if it changes nothing, and "changes nothing" is a claim about
// every bit of every body after the run (60 warm-up and 600 timed steps). Run the same scene before and
// after a change; the timings may move, the checksums must not (on one machine and
// compiler - they are not a cross-platform golden, the StateHash suites are).
//
//     cmake -B build -DCMAKE_BUILD_TYPE=Release -DSUPERSONIC_BUILD_BENCHMARKS=ON
//     cmake --build build --target physics_bench && ./build/tools/bench/physics_bench
//
// Release only: a Debug timing measures the checks, not the engine.
#include "core/PhysicsSystem.hpp"
#include "core/PhysicsSettings.hpp"
#include "core/Components.hpp"

#include <chrono>
#include <cstdio>
#include <cstring>
#include <random>
#include <vector>

using namespace Supersonic;

static entt::entity box(entt::registry& r, glm::vec3 p, float mass, glm::vec3 size = glm::vec3(1.0f), bool dynamic = true) {
    const auto e = r.create();
    auto& t = r.emplace<TransformComponent>(e);
    t.position = p;
    t.scale = size;
    if (dynamic) r.emplace<RigidBodyComponent>(e).mass = mass;
    r.emplace<BoxColliderComponent>(e);
    return e;
}
static entt::entity sphere(entt::registry& r, glm::vec3 p, float radius) {
    const auto e = r.create();
    r.emplace<TransformComponent>(e).position = p;
    r.emplace<RigidBodyComponent>(e).mass = 1.0f;
    r.emplace<SphereColliderComponent>(e).radius = radius;
    return e;
}
static void groundPlane(entt::registry& r) {
    PhysicsSettings s; s.hasGroundPlane = true; s.groundPlaneY = 0.0f;
    r.ctx().insert_or_assign<PhysicsSettings>(std::move(s));
}
static uint64_t checksum(entt::registry& r) {
    uint64_t h = 1469598103934665603ull;
    const auto mix = [&](const void* p, size_t n) {
        const auto* b = static_cast<const unsigned char*>(p);
        for (size_t i = 0; i < n; ++i) { h ^= b[i]; h *= 1099511628211ull; }
    };
    for (auto [e, t, rb] : r.view<TransformComponent, RigidBodyComponent>().each()) {
        mix(&t.position, sizeof(t.position)); mix(&t.rotation, sizeof(t.rotation));
        mix(&rb.velocity, sizeof(rb.velocity)); mix(&rb.angularVelocity, sizeof(rb.angularVelocity));
        mix(&rb.isSleeping, sizeof(rb.isSleeping));
    }
    return h;
}

template <typename Step>
static double timeSteps(int warm, int steps, Step&& step) {
    for (int i = 0; i < warm; ++i) step();
    const auto t0 = std::chrono::steady_clock::now();
    for (int i = 0; i < steps; ++i) step();
    const auto t1 = std::chrono::steady_clock::now();
    return std::chrono::duration<double, std::milli>(t1 - t0).count() / steps;
}

int main() {
    const float dt = 1.0f / 60.0f;
    // A: 400 boxes in 10x10 stacks of four, falling onto the ground plane.
    {
        entt::registry r; groundPlane(r);
        for (int i = 0; i < 10; ++i) for (int j = 0; j < 10; ++j) for (int k = 0; k < 4; ++k)
            box(r, glm::vec3(i * 1.2f, 0.6f + k * 1.1f, j * 1.2f), 1.0f);
        const double ms = timeSteps(60, 600, [&] { PhysicsSystem::Update(r, dt); });
        std::printf("stacks   %7.3f ms/step  checksum %016llx\n", ms, (unsigned long long)checksum(r));
    }
    // B: 300 spheres at rest plus 200 raycasts and 50 overlaps per step.
    {
        entt::registry r; groundPlane(r);
        std::mt19937 rng(7); std::uniform_real_distribution<float> u(0.0f, 30.0f);
        for (int i = 0; i < 300; ++i) sphere(r, glm::vec3(u(rng), 0.5f + u(rng) * 0.3f, u(rng)), 0.5f);
        std::vector<glm::vec3> origins; for (int i = 0; i < 200; ++i) origins.push_back(glm::vec3(u(rng), 20.0f, u(rng)));
        size_t hits = 0;
        const double ms = timeSteps(30, 300, [&] {
            PhysicsSystem::Update(r, dt);
            for (const auto& o : origins) hits += PhysicsSystem::Raycast(r, o, glm::vec3(0, -1, 0), 50.0f).hit ? 1 : 0;
            std::vector<entt::entity> found;
            for (int i = 0; i < 50; ++i) { found.clear(); PhysicsSystem::OverlapSphere(r, origins[i] - glm::vec3(0, 19.0f, 0), 3.0f, found); hits += found.size(); }
        });
        std::printf("queries  %7.3f ms/step  checksum %016llx  hits %zu\n", ms, (unsigned long long)checksum(r), hits);
    }
    // C: ten chains of twenty links on distance joints, hanging from the world, over 60 boxes.
    {
        entt::registry r; groundPlane(r);
        for (int c = 0; c < 10; ++c) {
            entt::entity previous = entt::null;
            for (int l = 0; l < 20; ++l) {
                const auto link = box(r, glm::vec3(c * 3.0f, 12.0f - l * 0.45f, 0.0f), 0.5f, glm::vec3(0.3f));
                auto& joint = r.emplace<JointComponent>(link);
                joint.type = JointComponent::Type::Distance;
                joint.distance = 0.45f;
                if (previous == entt::null) {
                    joint.connectedBody = entt::null;
                    joint.connectedAnchor = glm::vec3(c * 3.0f, 12.45f, 0.0f);
                } else {
                    joint.connectedBody = previous;
                }
                previous = link;
            }
        }
        for (int i = 0; i < 60; ++i) box(r, glm::vec3((i % 10) * 3.0f + 0.4f, 0.5f + (i / 10) * 1.1f, 0.3f), 1.0f);
        const double ms = timeSteps(60, 600, [&] { PhysicsSystem::Update(r, dt); });
        std::printf("chains   %7.3f ms/step  checksum %016llx\n", ms, (unsigned long long)checksum(r));
    }
    return 0;
}
