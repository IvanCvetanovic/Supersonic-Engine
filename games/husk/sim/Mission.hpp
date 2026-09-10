#pragma once

// The mission framework (src/sim/mission.rs): loading a mission and its
// editor-owned sidecar files, validating every name against the catalogs
// before the world is touched, installing it, and running its triggers.

#include "MissionTypes.hpp"
#include "World.hpp"

#include <filesystem>
#include <optional>
#include <string>
#include <string_view>

namespace husk {

// `<root>/missions/<name>.ron`, with `<name>.map.ron`, `.spawns.ron`,
// `.triggers.ron` and `.scenes.ron` overriding the inline fields when present.
// Throws ron::Error on a missing or malformed file.
MissionDef loadMissionDef(const std::filesystem::path& root, std::string_view name);

// One MissionDef from one file, no sidecars - a test's own mission.
MissionDef parseMissionDefFile(const std::filesystem::path& path);

// Validate and install. An unknown catalog name is an error returned before the
// world is touched, as the game's Err. A mission with its own map needs a fresh
// world; installing one into a used world is a programming error and throws.
std::optional<std::string> installMission(World& w, MissionDef def);

// Load from the world's data root and install. The error of either step.
std::optional<std::string> loadMission(World& w, std::string_view name);

MissionOutcome missionOutcome(const World& w);

MapDef mapDefFromSpec(const MapSpec& spec);

// 180-degree rotations about the origin; each is its own inverse.
void rotateMapSpec180(MapSpec& m);
void rotateSpawnSpec180(SpawnSpec& s);
void rotateTriggers180(std::vector<TriggerDef>& triggers);
void rotateScenes180(std::vector<SceneStep>& steps);

// The oracle's mission dump, for a definition as loaded (before rotation).
std::string dumpMission(const MissionDef& def, std::string_view name);

} // namespace husk
