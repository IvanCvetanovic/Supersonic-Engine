#pragma once

#include <string>

#include <entt/entt.hpp>

#include "core/SceneSerializer.hpp"

namespace Supersonic {

// Edit / Play / Paused, with the scene restored on Stop.
//
// The editor used to simulate permanently: physics ran the moment the window
// opened, so the demo cube had already fallen before you could look at it, and
// there was no way to author a scene, try it, and get your authored state back.
// The time-travel debugger existed largely as a workaround for this.
class PlayMode {
public:
    enum class State { Edit, Play, Paused };

    State GetState() const { return m_state; }
    bool IsPlaying() const { return m_state == State::Play; }
    bool IsEditing() const { return m_state == State::Edit; }
    bool IsPaused() const { return m_state == State::Paused; }

    // True while gameplay systems should run: physics, scripts, particles,
    // audio and the time-travel recorder.
    bool ShouldSimulate() const { return m_state == State::Play; }

    // Snapshots the scene in memory, then begins simulating.
    SerializationResult Play(entt::registry& registry);

    void Pause();
    void Resume();

    // Restores the snapshot taken at Play. Everything that happened during
    // play is discarded, which is the point.
    SerializationResult Stop(entt::registry& registry);

    // Advances exactly one frame while paused, for stepping through a bug.
    bool ConsumeSingleStep();
    void RequestSingleStep() { m_singleStepRequested = true; }

    bool HasSnapshot() const { return !m_snapshot.empty(); }

private:
    State m_state{State::Edit};
    std::string m_snapshot;
    bool m_singleStepRequested{false};
};

} // namespace Supersonic
