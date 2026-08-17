#pragma once

#include <deque>
#include <entt/entt.hpp>
#include <glm/glm.hpp>
#include <vector>

#include "core/Components.hpp"

namespace Supersonic {

struct EntityStateSnapshot {
    entt::entity entity{entt::null};
    glm::vec3 position{0.0f};
    glm::vec3 rotation{0.0f};
    glm::vec3 scale{1.0f};
    glm::vec3 velocity{0.0f};
    bool hasRigidBody{false};
};

struct FrameSnapshot {
    float timeStamp{0.0f};
    std::vector<EntityStateSnapshot> entityStates;
};

class TimeTravelDebugger {
public:
    static void RecordFrame(entt::registry& registry, float currentTime);

    // Returns how many entities were actually restored, so the UI can say when
    // a timeline no longer matches the scene instead of silently doing nothing.
    static size_t RestoreFrame(entt::registry& registry, size_t frameIndex);

    static bool IsRewinding() { return s_isRewinding; }
    static void SetRewinding(bool rewinding) { s_isRewinding = rewinding; }

    static size_t GetRecordedFrameCount() { return s_history.size(); }
    static size_t GetCurrentFrameIndex() { return s_currentFrameIndex; }

    static void Clear();
    static void RenderImGuiPanel(entt::registry& registry);

private:
    // deque, not vector: the ring buffer trims from the front every frame once
    // full, and erase(begin()) on a vector of vectors is an O(n) move of 1199
    // non-trivially-copyable elements at 60 Hz, forever.
    static std::deque<FrameSnapshot> s_history;
    static bool s_isRewinding;
    static size_t s_currentFrameIndex;
    static size_t s_lastRestoreCount;
    static constexpr size_t MAX_HISTORY_FRAMES = 1200; // 20 seconds at 60 FPS
};

} // namespace Supersonic
