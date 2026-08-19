#include "core/TimeTravelDebugger.hpp"
#include "core/Log.hpp"
#include "imgui.h"

#include <iostream>

namespace Supersonic {

std::deque<FrameSnapshot> TimeTravelDebugger::s_history;
bool TimeTravelDebugger::s_isRewinding = false;
size_t TimeTravelDebugger::s_currentFrameIndex = 0;
size_t TimeTravelDebugger::s_lastRestoreCount = 0;

void TimeTravelDebugger::Clear() {
    // The rewind flag has to come down with the history. The checkbox that sets
    // it lives behind an early-return that fires when the history is empty, so
    // leaving it set after a Clear made it unreachable - and SupersonicApp gates
    // physics, audio, scripts, particles AND the recorder on !IsRewinding(), so
    // nothing could ever record a frame again. Play and Step stayed dead for the
    // rest of the session.
    s_isRewinding = false;
    s_history.clear();
    s_currentFrameIndex = 0;
    s_lastRestoreCount = 0;
}

void TimeTravelDebugger::RecordFrame(entt::registry& registry, float currentTime) {
    if (s_isRewinding) return;

    // Resuming after a scrub discards the branch that was rewound past. Without
    // this the old future stayed in the buffer and new frames were appended
    // behind it, so scrubbing crossed between two mutually exclusive histories.
    if (!s_history.empty() && s_currentFrameIndex + 1 < s_history.size()) {
        s_history.erase(s_history.begin() + static_cast<std::ptrdiff_t>(s_currentFrameIndex) + 1,
                        s_history.end());
    }

    FrameSnapshot snapshot;
    snapshot.timeStamp = currentTime;

    auto view = registry.view<TransformComponent>();
    for (auto entity : view) {
        const auto& transform = view.get<TransformComponent>(entity);

        EntityStateSnapshot state{};
        state.entity = entity;
        state.position = transform.position;
        state.rotation = transform.rotation;
        state.scale = transform.scale;

        // Velocity too, so rewinding a falling body actually rewinds its
        // motion instead of resuming with whatever velocity it had at the
        // moment the scrub ended.
        if (const auto* body = registry.try_get<RigidBodyComponent>(entity)) {
            state.velocity = body->velocity;
            state.hasRigidBody = true;
        }

        snapshot.entityStates.push_back(state);
    }

    s_history.push_back(std::move(snapshot));
    while (s_history.size() > MAX_HISTORY_FRAMES) {
        s_history.pop_front(); // O(1)
    }
    s_currentFrameIndex = s_history.size() - 1;
}

size_t TimeTravelDebugger::RestoreFrame(entt::registry& registry, size_t frameIndex) {
    if (frameIndex >= s_history.size()) return 0;

    size_t restored = 0;
    const auto& snapshot = s_history[frameIndex];
    for (const auto& state : snapshot.entityStates) {
        if (!registry.valid(state.entity)) continue;
        if (auto* transform = registry.try_get<TransformComponent>(state.entity)) {
            transform->position = state.position;
            transform->rotation = state.rotation;
            transform->scale = state.scale;
            ++restored;
        }
        if (state.hasRigidBody) {
            if (auto* body = registry.try_get<RigidBodyComponent>(state.entity)) {
                body->velocity = state.velocity;
            }
        }
    }

    s_currentFrameIndex = frameIndex;
    s_lastRestoreCount = restored;
    return restored;
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
            SUPERSONIC_LOG_INFO("TimeTravelDebugger") << "Paused simulation, timeline scrubbing enabled." << std::endl;
        } else {
            SUPERSONIC_LOG_INFO("TimeTravelDebugger") << "Resumed from frame " << s_currentFrameIndex
                      << "; discarding " << (s_history.size() - s_currentFrameIndex - 1)
                      << " superseded frames." << std::endl;
        }
    }

    if (s_isRewinding) {
        int currentIndex = static_cast<int>(s_currentFrameIndex);
        const int maxIndex = static_cast<int>(s_history.size() - 1);

        if (ImGui::SliderInt("Frame Timeline", &currentIndex, 0, maxIndex)) {
            RestoreFrame(registry, static_cast<size_t>(currentIndex));
        }

        if (ImGui::Button("<< Step Back")) {
            if (s_currentFrameIndex > 0) RestoreFrame(registry, s_currentFrameIndex - 1);
        }
        ImGui::SameLine();
        if (ImGui::Button("Step Forward >>")) {
            if (s_currentFrameIndex + 1 < s_history.size()) RestoreFrame(registry, s_currentFrameIndex + 1);
        }

        ImGui::Text("Frame time: %.2fs", static_cast<double>(s_history[s_currentFrameIndex].timeStamp));

        // The restore used to be guarded per entity with no else branch, so a
        // timeline recorded before a scene load appeared fully functional while
        // restoring nothing at all.
        const size_t recordedHere = s_history[s_currentFrameIndex].entityStates.size();
        if (s_lastRestoreCount == 0 && recordedHere > 0) {
            ImGui::TextColored(ImVec4(1.0f, 0.45f, 0.40f, 1.0f),
                               "Timeline does not match the current scene.");
            ImGui::TextDisabled("Recorded entities no longer exist. Clear to start over.");
        } else {
            ImGui::TextDisabled("Restored %zu / %zu entities.", s_lastRestoreCount, recordedHere);
        }

        if (ImGui::Button("Clear Timeline")) {
            Clear();
        }
    }

    ImGui::End();
}

} // namespace Supersonic
