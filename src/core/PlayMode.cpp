#include "core/PlayMode.hpp"
#include "core/Log.hpp"
#include "core/TimeTravelDebugger.hpp"

#include <iostream>

namespace Supersonic {

SerializationResult PlayMode::Play(entt::registry& registry) {
    if (m_state == State::Paused) {
        Resume();
        return { true, "Resumed." };
    }
    if (m_state == State::Play) {
        return { true, "Already playing." };
    }

    // The snapshot is just a scene file that never touches the disk, so the
    // restore path is the same well-tested code as loading a scene.
    m_snapshot = SceneSerializer::SerializeToString(registry);
    if (m_snapshot.empty()) {
        return { false, "Could not snapshot the scene; not entering play mode." };
    }

    // A timeline recorded in edit mode refers to entities that are about to be
    // replaced by the snapshot's, so it would restore nothing.
    TimeTravelDebugger::Clear();

    m_state = State::Play;
    SUPERSONIC_LOG_INFO("PlayMode") << "Entering play mode (scene snapshotted)." << std::endl;
    return { true, "Playing. Stop restores the scene as it was." };
}

void PlayMode::Pause() {
    if (m_state == State::Play) {
        m_state = State::Paused;
        SUPERSONIC_LOG_INFO("PlayMode") << "Paused." << std::endl;
    }
}

void PlayMode::Resume() {
    if (m_state == State::Paused) {
        m_state = State::Play;
        SUPERSONIC_LOG_INFO("PlayMode") << "Resumed." << std::endl;
    }
}

SerializationResult PlayMode::Stop(entt::registry& registry) {
    if (m_state == State::Edit) {
        return { true, "Already in edit mode." };
    }

    m_state = State::Edit;
    m_singleStepRequested = false;

    if (m_snapshot.empty()) {
        return { false, "No snapshot to restore; the scene is left as it is." };
    }

    const auto result = SceneSerializer::DeserializeFromString(registry, m_snapshot);
    TimeTravelDebugger::Clear();

    if (!result.ok) {
        // Keep the snapshot: a failed restore should not also lose the only
        // copy of the pre-play scene.
        return { false, "Could not restore the scene: " + result.message };
    }

    m_snapshot.clear();
    SUPERSONIC_LOG_INFO("PlayMode") << "Stopped; scene restored to its pre-play state." << std::endl;
    return { true, "Stopped. Scene restored." };
}

bool PlayMode::ConsumeSingleStep() {
    if (m_state != State::Paused || !m_singleStepRequested) return false;
    m_singleStepRequested = false;
    return true;
}

} // namespace Supersonic
