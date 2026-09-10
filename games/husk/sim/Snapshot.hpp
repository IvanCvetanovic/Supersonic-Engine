#pragma once

// Save and load (src/sim/snapshot.rs): the complete sim, so a loaded game
// continues bit-identically, plus the campaign state that outlives a mission.
// Caches - flow fields, the index, nav blocking - are rebuilt, not stored.
// Mission hooks are code and are not saved.

#include "World.hpp"

#include <optional>
#include <string>
#include <vector>

namespace husk {

inline constexpr uint32_t kSaveVersion = 6;

struct CampaignState {
    std::vector<std::string> completed;
    std::optional<Hero> hero; // carried between missions
    Difficulty difficulty = Difficulty::Normal;
};

struct UnitSave {
    uint32_t id = 0;
    uint16_t kind = 0;
    uint8_t team = 0;
    Vec2 prev;
    Vec2 cur;
    float hpCur = 0.0f;
    float hpMax = 0.0f;
    float cooldown = 0.0f;
    std::optional<uint32_t> target;
    Vec2 home;
    Orders orders;
    std::optional<uint32_t> arrived;
    std::optional<Hero> hero;
    std::optional<OverchargeBuff> overcharge;
    std::optional<SlowDebuff> slow;
    std::optional<AvatarBuff> avatar;
    std::optional<TimedLife> timed;
    std::optional<uint32_t> raider;
};

struct BuildingSave {
    uint32_t id = 0;
    uint8_t team = 0;
    Vec2 pos;
    float hpCur = 0.0f;
    float hpMax = 0.0f;
    Building building;
};

struct SourceSave {
    uint32_t id = 0;
    Vec2 pos;
    EssenceSource source;
};

struct ItemSave {
    uint32_t id = 0;
    uint16_t kind = 0;
    Vec2 pos;
};

struct SimSnapshot {
    uint64_t tick = 0;
    std::pair<uint64_t, uint64_t> rng;
    uint32_t idAlloc = 0;
    MapDef map;
    PlayerEconomy economy;
    AffinityState affinity;
    ResearchState research;
    HeroState heroState;
    std::optional<MissionRuntime> mission;
    std::vector<OrderMsg> pendingOrders; // pushed since the last tick drained them
    std::vector<UnitSave> units;
    std::vector<BuildingSave> buildings;
    std::vector<SourceSave> sources;
    std::vector<ItemSave> items;
};

struct SaveGame {
    uint32_t version = kSaveVersion;
    CampaignState campaign;
    SimSnapshot sim;
};

// The whole sim. Call between ticks.
SaveGame capture(const World& w, CampaignState campaign);

// Every sim entity gone and the derived stores cleared.
void clearSimWorld(World& w);

// Restore into `w`, clearing whatever it held. The difficulty comes from the
// campaign, as the game's does.
void restore(World& w, const SaveGame& save);

} // namespace husk
