#pragma once

#include <entt/entt.hpp>
#include <glm/glm.hpp>
#include <vector>
#include <unordered_map>
#include "core/Components.hpp"

namespace Engine {

struct EntityStateSnapshot {
    entt::entity entity;
    glm::vec3 position;
    glm::vec3 rotation;
    glm::vec3 scale;
};

struct FrameSnapshot {
    float timeStamp{0.0f};
    std::vector<EntityStateSnapshot> entityStates;
};

class TimeTravelDebugger {
public:
    static void RecordFrame(entt::registry& registry, float currentTime);
    static void RestoreFrame(entt::registry& registry, size_t frameIndex);

    static bool IsRewinding() { return s_isRewinding; }
    static void SetRewinding(bool rewinding) { s_isRewinding = rewinding; }

    static size_t GetRecordedFrameCount() { return s_history.size(); }
    static size_t GetCurrentFrameIndex() { return s_currentFrameIndex; }
    static void SetCurrentFrameIndex(size_t index) { s_currentFrameIndex = index; }

    static void RenderImGuiPanel(entt::registry& registry);

private:
    static std::vector<FrameSnapshot> s_history;
    static bool s_isRewinding;
    static size_t s_currentFrameIndex;
    static constexpr size_t MAX_HISTORY_FRAMES = 1200; // 20 seconds at 60 FPS
};

} // namespace Engine
