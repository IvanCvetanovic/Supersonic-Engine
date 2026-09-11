#include "sim/Puzzle.hpp"

namespace MagicPortals::Puzzle {

namespace {

// A channel number off metadata. The remake reads a missing one as -1, a channel
// nothing else is on; the port refuses it rather than invent one.
bool ChannelOf(const Tscn::Node& node, const char* key, int& out, std::string& error) {
    const Tscn::Value* value = node.Meta(key);
    double number = 0.0;
    if (value == nullptr || !value->AsNumber(number)) {
        error = node.name + " has no " + key;
        return false;
    }
    out = static_cast<int>(number);
    return true;
}

} // namespace

void Channels::Tick(entt::registry& registry, float dt) {
    for (Button& button : buttons) {
        const bool pressed = Trigger::DynamicBodiesIn(registry, button.box) > 0;
        if (pressed == button.pressed) continue;
        button.pressed = pressed;
        for (SwitchedDoor& door : doors) {
            if (door.channel == button.channel) door.motion.opening = pressed;
        }
    }
    for (SwitchedDoor& door : doors) door.motion.Tick(registry, door.entity, dt);
}

bool Channels::Pressed(int channel) const {
    for (const Button& button : buttons) {
        if (button.channel == channel && button.pressed) return true;
    }
    return false;
}

const Button* Channels::FindButton(const std::string& name) const {
    for (const Button& button : buttons) {
        if (button.name == name) return &button;
    }
    return nullptr;
}

const SwitchedDoor* Channels::FindDoor(const std::string& name) const {
    for (const SwitchedDoor& door : doors) {
        if (door.name == name) return &door;
    }
    return nullptr;
}

bool Wire(const Tscn::Scene& scene, const Roles::Table& roles, const LevelBuilder::Built& built, Channels& out,
          std::string& error) {
    out = Channels{};
    for (const Tscn::Node& node : scene.nodes) {
        if (node.parent != ".") continue;
        const std::string role = Roles::RoleOf(roles, node);
        if (role == Roles::kSwitch) {
            Button button;
            button.name = node.name;
            if (!ChannelOf(node, "idx", button.channel, error)) return false;
            if (!Trigger::FromNode(node, button.box, error)) return false;
            out.buttons.push_back(button);
        } else if (role == Roles::kSwitchedDoor) {
            SwitchedDoor door;
            door.name = node.name;
            if (!ChannelOf(node, "switchIdx", door.channel, error)) return false;
            if (!Mover::DoorFromNode(node, door.motion, error)) return false;
            const auto found = built.entities.find(node.name);
            if (found == built.entities.end()) {
                error = node.name + " was not built";
                return false;
            }
            door.entity = found->second;
            out.doors.push_back(door);
        }
    }
    return true;
}

} // namespace MagicPortals::Puzzle
