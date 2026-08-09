#include "core/TimeTravelDebugger.hpp"
#include "imgui.h"
#include <iostream>

namespace Engine {

std::vector<FrameSnapshot> TimeTravelDebugger::s_history;
bool TimeTravelDebugger::s_isRewinding = false;
size_t TimeTravelDebugger::s_currentFrameIndex = 0;

void TimeTravelDebugger::RecordFrame(entt::registry& registry, float currentTime) {
    if (s_isRewinding) return;

    FrameSnapshot snapshot;
    snapshot.timeStamp = currentTime;

    auto view = registry.view<TransformComponent>();
    for (auto entity : view) {
        const auto& transform = view.get<TransformComponent>(entity);
        snapshot.entityStates.push_back({entity, transform.position, transform.rotation, transform.scale});
    }

    s_history.push_back(snapshot);
    if (s_history.size() > MAX_HISTORY_FRAMES) {
        s_history.erase(s_history.begin());
    }
    s_currentFrameIndex = s_history.size() - 1;
}

void TimeTravelDebugger::RestoreFrame(entt::registry& registry, size_t frameIndex) {
    if (frameIndex >= s_history.size()) return;

    const auto& snapshot = s_history[frameIndex];
    for (const auto& state : snapshot.entityStates) {
        if (registry.valid(state.entity) && registry.all_of<TransformComponent>(state.entity)) {
            auto& transform = registry.get<TransformComponent>(state.entity);
            transform.position = state.position;
            transform.rotation = state.rotation;
            transform.scale = state.scale;
        }
    }
    s_currentFrameIndex = frameIndex;
}

void TimeTravelDebugger::RenderImGuiPanel(entt::registry& registry) {
    ImGui::Begin("Time-Travel Rewind Debugger");

    if (s_history.empty()) {
        ImGui::TextDisabled("Recording timeline...");
        ImGui::End();
        return;
    }

    ImGui::Text("Recorded Frames: %zu / %zu", s_history.size(), MAX_HISTORY_FRAMES);

    if (ImGui::Checkbox("Pause & Rewind Timeline", &s_isRewinding)) {
        if (s_isRewinding) {
            std::cout << "[TimeTravelDebugger] Paused physics & started timeline rewind." << std::endl;
        }
    }

    if (s_isRewinding) {
        int currentIndex = static_cast<int>(s_currentFrameIndex);
        int maxIndex = static_cast<int>(s_history.size() - 1);

        if (ImGui::SliderInt("Frame Timeline", &currentIndex, 0, maxIndex)) {
            RestoreFrame(registry, static_cast<size_t>(currentIndex));
        }

        ImGui::SameLine();
        if (ImGui::Button("Step Forward >>")) {
            if (s_currentFrameIndex < s_history.size() - 1) {
                RestoreFrame(registry, s_currentFrameIndex + 1);
            }
        }
        ImGui::SameLine();
        if (ImGui::Button("<< Step Back")) {
            if (s_currentFrameIndex > 0) {
                RestoreFrame(registry, s_currentFrameIndex - 1);
            }
        }
    }

    ImGui::End();
}

} // namespace Engine
