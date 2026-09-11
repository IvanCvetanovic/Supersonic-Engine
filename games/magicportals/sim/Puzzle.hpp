#pragma once

// A level's buttons and the doors they open: behaviours.gd's Switch (:179-206)
// and LevelRuntime._wire_channels (level_runtime.gd:184-200).
//
// A button is pressed while any dynamic body is on its trigger. It keeps a count,
// and it is not a latch. When it changes it sets every door on its channel,
// because button `idx` and door_lift `switchIdx` are one numbering. As through the
// remake's EventBus, the last change wins. So of two buttons on one channel, the
// door follows whichever changed last rather than whichever is held. level30 has
// one button per channel.

#include "sim/LevelBuilder.hpp"
#include "sim/Mover.hpp"
#include "sim/Roles.hpp"
#include "sim/Trigger.hpp"
#include "sim/Tscn.hpp"

#include <string>
#include <vector>

#include <entt/entt.hpp>

namespace MagicPortals::Puzzle {

struct Button {
    std::string name;
    int channel = -1;
    Trigger::Box box;
    bool pressed = false;
};

struct SwitchedDoor {
    std::string name;
    int channel = -1;
    entt::entity entity = entt::null;
    Mover::Door motion;
};

struct Channels {
    std::vector<Button> buttons;
    std::vector<SwitchedDoor> doors;

    // One tick, before the physics step. Every button counts what is on it, a
    // button that changed sets its channel's doors, and every door travels.
    void Tick(entt::registry& registry, float dt);

    // Whether any button on the channel is pressed.
    bool Pressed(int channel) const;
    const Button* FindButton(const std::string& name) const;
    const SwitchedDoor* FindDoor(const std::string& name) const;
};

// Every switch and switched_door in a built level. False, with `error`, when a
// button has no idx, a door has no switchIdx or stride, or a door was not built.
bool Wire(const Tscn::Scene& scene, const Roles::Table& roles, const LevelBuilder::Built& built, Channels& out,
          std::string& error);

} // namespace MagicPortals::Puzzle
