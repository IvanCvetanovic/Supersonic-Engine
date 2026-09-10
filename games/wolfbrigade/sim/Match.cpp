#include "sim/Match.hpp"

#include <algorithm>
#include <cmath>
#include <utility>

namespace WolfBrigade {

using Supersonic::Json::Value;

namespace {

// `_to_vec2`: an array of at least two numbers, or the fallback. A body size is
// a saved field with a shape, which is why this one is ported and `_to_color`
// is not.
glm::vec2 toVec2(const Value& value, const glm::vec2& fallback) {
    const auto& array = value.AsArray();
    if (array.size() < 2) return fallback;
    return glm::vec2(array[0].AsFloat(fallback.x), array[1].AsFloat(fallback.y));
}

} // namespace

Match::Layout Match::Layout::FromData(const GameData& data) {
    Layout layout;
    const Value& world = data.World();

    layout.width = world["width"].AsFloat(6000.0f);
    layout.groundY = world["ground_y"].AsFloat(800.0f);

    const Value& lane = world["lane"];
    layout.laneDepth = lane["depth"].AsFloat(0.0f);
    layout.buildingRowGap = lane["building_row_gap"].AsFloat(115.0f);

    // The width FIRST, because the enemy edge falls back to it. A world.json
    // that sets a width and omits the spawn edge gets a spawn edge that moved
    // with the wall.
    layout.enemyX = world["enemy_spawn_x"].AsFloat(layout.width - 40.0f);

    layout.townHallX = world["town_hall_spawn_x"].AsFloat(1500.0f);
    layout.playerSpawnX = world["player_spawn_x"].AsFloat(1680.0f);
    layout.playerSpawnSpacing = world["player_spawn_spacing"].AsFloat(60.0f);

    const Value& input = world["input"];
    layout.pickRadius = input["unit_pick_radius"].AsFloat(52.0f);
    layout.formationSpacing = input["formation_spacing"].AsFloat(46.0f);
    return layout;
}

Match::Match(const GameData& data, Profile& profile, std::string runPath)
    : m_data(data), m_profile(profile), m_runPath(std::move(runPath)) {
    m_bus.unitSpawned.Connect([this](Unit* unit) { OnUnitSpawned(unit); });
    m_bus.unitDied.Connect([this](Unit* unit) { OnUnitDied(unit); });
    m_bus.unitTrained.Connect([this](const std::string& id, const glm::vec2& at,
                                     const glm::vec2& rally, const std::string& squad) {
        OnUnitTrained(id, at, rally, squad);
    });
    m_bus.buildingDestroyed.Connect([this](Building* building) { OnBuildingDestroyed(building); });
    m_bus.waveStarted.Connect([this](int index) { OnWaveStarted(index); });
    m_bus.gameWon.Connect([this] { OnGameOver(true); });
    m_bus.gameLost.Connect([this] { OnGameOver(false); });

    // Last, so the selection's own prune handlers run after the director has
    // been told - the order does not matter today, and saying which one it is
    // costs nothing and stops it mattering later.
    m_selection.Connect();
}

Match::~Match() = default;

// --- Boot ------------------------------------------------------------------

bool Match::Boot(const Supersonic::Json::Value& pending) {
    if (Snapshot::IsValid(pending) && BootFromSave(pending)) return true;
    BootFresh();
    return false;
}

void Match::BootFresh() {
    ClearBoard();
    ApplyWorld();

    // Before Reset, not after. Reset adds the persistent starting bonus and
    // reads it out of GameState's own copy of the levels, and the only other
    // writer is the restore path.
    m_state.SetMetaLevels(m_profile.AllMetaLevels());
    m_state.Reset();

    m_director.Setup([this](const UnitStats& stats,
                            const glm::vec2& position) { SpawnUnit(stats, position); },
                     m_layout.enemyX, m_layout.groundY);

    SpawnTownHall();
    SpawnResourceNodes();
    SpawnCapturePoints();
    SpawnStartingWorkers();
}

bool Match::BootFromSave(const Supersonic::Json::Value& snapshot,
                         Snapshot::RestoreReport* report) {
    if (!Snapshot::IsValid(snapshot)) return false;

    ClearBoard();
    ApplyWorld();

    auto spawn = [this](const UnitStats& stats, const glm::vec2& position) {
        SpawnUnit(stats, position);
    };

    // First arming: the schedule, so FromSave has something to clamp the saved
    // wave index against.
    m_director.Setup(spawn, m_layout.enemyX, m_layout.groundY);

    // Level furniture exists on a restored boot as on a fresh one; its tug
    // state is the save's business.
    SpawnCapturePoints();

    const bool restored =
        Snapshot::Restore(snapshot, m_data, m_state, m_profile, m_director,
                          static_cast<Snapshot::RestoreSink&>(*this), m_lane, m_projectiles,
                          report);
    if (!restored) return false;

    // Second arming: the mode, which only exists now. Setup writes the schedule
    // and the configuration; FromSave wrote the counters; the two sets are
    // disjoint, so this re-derives without rewinding.
    m_director.Setup(spawn, m_layout.enemyX, m_layout.groundY);
    return true;
}

void Match::ApplyWorld() { m_layout = Layout::FromData(m_data); }

bool Match::BackCancelsPlacement() const {
    return m_state.IsPlaying() && m_placement.IsActive();
}

void Match::ClearBoard() {
    // A ghost does not survive a boot. Godot gets this from the scene going
    // away; here it has to be said, or a Continue would come back with the
    // previous run's placement still open over a board that no longer has
    // the resources it was priced against.
    m_placement.Cancel();

    // A selection cannot survive the board it pointed at.
    m_selection.Clear();

    m_lane.Clear();
    m_projectiles.Clear();

    // The director's counters are derived from a run the same way the lane is
    // derived from the units, and they go the same way. A restore then writes
    // all seven of them back through FromSave, which is why this is safe on
    // both paths and load-bearing on only one.
    m_director.Reset();

    // Before anything else goes: each point takes its army bonus with it.
    m_capturePoints.clear();

    m_units.clear();
    m_buildings.clear();
    m_nodes.clear();
}

void Match::SpawnTownHall() {
    // Through Upgrades, not straight off the data row: a Town Hall reflects the
    // research and the owned Armory levels the player brought into the run.
    PlaceBuilding(Upgrades::ForBuilding(m_data, m_state, m_profile, Ids::kTownHall), true,
                  glm::vec2(m_layout.townHallX, m_layout.groundY));
}

void Match::SpawnResourceNodes() {
    for (const Value& group : m_data.Economy()["resource_nodes"].AsArray()) {
        ResourceNode prototype;
        prototype.resource = group["resource"].AsString(Ids::kWood);
        prototype.maxAmount = static_cast<int>(group["amount"].AsNumber(200.0));
        prototype.amount = prototype.maxAmount;
        prototype.bodySize = toVec2(group["body_size"], glm::vec2(44.0f, 96.0f));
        prototype.color = group["color"].AsString("#3f6b34");

        for (const Value& x : group["positions"].AsArray()) {
            prototype.position = glm::vec2(x.AsFloat(), m_layout.groundY);
            AddResourceNode(prototype);
        }
    }
}

void Match::SpawnCapturePoints() {
    // Geometry from the level's data, on the band's middle row: landmarks, not
    // traffic.
    for (const Value& config : m_data.LevelCapturePoints()) {
        auto point = std::make_unique<CapturePoint>(m_state, m_bus);
        point->Setup(config);
        point->SetPosition(glm::vec2(config["x"].AsFloat(0.0f),
                                     m_layout.groundY + m_layout.laneDepth * 0.5f));
        m_capturePoints.push_back(std::move(point));
    }
}

void Match::SpawnStartingWorkers() {
    const int count = static_cast<int>(m_data.Economy()["starting_workers"].AsNumber(0.0));

    // Derived ONCE, outside the loop, exactly as the original does. Every unit
    // copies the block it is handed, so sharing the source is free.
    const UnitStats stats = Upgrades::ForUnit(m_data, m_state, m_profile, Ids::kWorker);

    for (int i = 0; i < count; ++i) {
        const float x = m_layout.playerSpawnX + static_cast<float>(i) * m_layout.playerSpawnSpacing;
        SpawnUnit(stats, glm::vec2(x, m_layout.groundY));
    }
}

// --- Stepping --------------------------------------------------------------

void Match::Step(double delta) {
    // Taken once, before anything runs, so a unit trained during the buildings
    // phase waits for the next frame the way a node added to a Godot scene
    // tree mid-frame does.
    const size_t buildings = m_buildings.size();
    const size_t units = m_units.size();

    for (const auto& point : m_capturePoints) point->Step(delta, m_lane);
    for (size_t i = 0; i < buildings; ++i) m_buildings[i]->Step(delta);
    for (size_t i = 0; i < units; ++i) m_units[i]->Step(delta);

    m_projectiles.Step(delta, m_state.IsPlaying());
    m_director.Step(delta);
}

void Match::StepBuildings(double delta) {
    const size_t count = m_buildings.size();
    for (size_t i = 0; i < count; ++i) m_buildings[i]->Step(delta);
}

void Match::StepUnits(double delta) {
    const size_t count = m_units.size();
    for (size_t i = 0; i < count; ++i) m_units[i]->Step(delta);
}

void Match::StepProjectiles(double delta) { m_projectiles.Step(delta, m_state.IsPlaying()); }

void Match::StepDirector(double delta) { m_director.Step(delta); }

void Match::StepCapturePoints(double delta) {
    for (const auto& point : m_capturePoints) point->Step(delta, m_lane);
}

// --- Putting things on the board -------------------------------------------

Unit* Match::SpawnUnit(const UnitStats& stats, const glm::vec2& position) {
    Unit* unit = CreateUnit(stats, position);
    m_bus.unitSpawned.Emit(unit);
    return unit;
}

Building* Match::PlaceBuilding(const BuildingStats& stats, bool complete,
                               const glm::vec2& position) {
    Building* building = CreateBuilding(stats, complete, position);
    building->SetTrainTimes(m_data.Units());
    return building;
}

ResourceNode* Match::AddResourceNode(const ResourceNode& prototype) {
    return CreateResourceNode(prototype);
}

// --- Snapshot::RestoreSink -------------------------------------------------

Building* Match::CreateBuilding(const BuildingStats& stats, bool complete,
                                const glm::vec2& position) {
    auto building = std::make_unique<Building>(stats, complete, m_state, m_bus, *this);
    building->SetPosition(position);
    m_buildings.push_back(std::move(building));
    return m_buildings.back().get();
}

ResourceNode* Match::CreateResourceNode(const ResourceNode& fromSave) {
    m_nodes.push_back(std::make_unique<ResourceNode>(fromSave));
    return m_nodes.back().get();
}

Unit* Match::CreateUnit(const UnitStats& stats, const glm::vec2& position) {
    auto unit = std::make_unique<Unit>(stats, m_state, m_bus, *this);
    unit->SetPosition(position);

    Unit* raw = unit.get();
    m_units.push_back(std::move(unit));

    // The lane, and nothing else. A restore clears the lane before it builds
    // anything, so a unit that never rejoins is invisible to every scan in the
    // game while looking perfectly alive on the board.
    m_lane.Register(raw);
    return raw;
}

// --- The run file ----------------------------------------------------------

bool Match::AutosaveRun() {
    if (!m_state.IsPlaying()) return false;
    if (m_runPath.empty()) return false;
    return Snapshot::SaveRun(m_runPath, Capture());
}

Supersonic::Json::Value Match::Capture(Snapshot::CaptureReport* report) {
    return Snapshot::Capture(View(), m_state, m_director, report);
}

Snapshot::Scene Match::View() {
    Snapshot::Scene scene;
    scene.units.reserve(m_units.size());
    scene.buildings.reserve(m_buildings.size());
    scene.resourceNodes.reserve(m_nodes.size());

    for (const auto& unit : m_units) scene.units.push_back(unit.get());
    for (const auto& building : m_buildings) scene.buildings.push_back(building.get());
    for (const auto& node : m_nodes) scene.resourceNodes.push_back(node.get());
    return scene;
}

// --- Bus handlers ----------------------------------------------------------

void Match::OnUnitTrained(const std::string& unitId, const glm::vec2& spawnPoint,
                          const glm::vec2& rally, const std::string& squad) {
    const UnitStats stats = Upgrades::ForUnit(m_data, m_state, m_profile, unitId);

    // The width EXACTLY, not the width minus half a body. A unit trained by a
    // building at the right-hand edge stands on the edge.
    const float x = std::min(std::max(spawnPoint.x, 0.0f), m_layout.width);

    Unit* unit = SpawnUnit(stats, glm::vec2(x, m_layout.groundY));

    // A rally point is an order to walk there, as the original's main gives
    // it. The squad is carried for the squads the port does not have yet.
    (void)squad;
    if (unit != nullptr && Building::IsRally(rally)) unit->CommandMoveTo(rally);
}

void Match::OnUnitSpawned(Unit* unit) {
    if (unit != nullptr && unit->Faction() == Factions::kEnemy) m_director.OnEnemySpawned();
}

void Match::OnUnitDied(Unit* unit) {
    if (unit != nullptr && unit->Faction() == Factions::kEnemy) m_director.OnEnemyDied();
}

void Match::OnBuildingDestroyed(Building* building) {
    if (building != nullptr && building->Stats().id == Ids::kTownHall) {
        m_director.OnTownHallDestroyed();
    }
}

void Match::OnWaveStarted(int index) { m_profile.RecordWave(index); }

void Match::OnGameOver(bool won) {
    Meta::AwardRunEnd(m_data, m_profile, m_state.CurrentWave(), won);
    if (!m_runPath.empty()) Snapshot::ClearRun(m_runPath);
}

// --- World -----------------------------------------------------------------

ResourceNode* Match::NearestHarvestable(float x) const {
    ResourceNode* best = nullptr;
    float bestDistance = 0.0f;
    for (const auto& node : m_nodes) {
        if (!node->Harvestable()) continue;
        const float distance = std::fabs(x - node->position.x);
        if (best == nullptr || distance < bestDistance) {
            best = node.get();
            bestDistance = distance;
        }
    }
    return best;
}

int Match::NearestDeposit(float x) const {
    int best = -1;
    float bestDistance = 0.0f;
    for (size_t i = 0; i < m_buildings.size(); ++i) {
        if (!m_buildings[i]->IsDepositPoint()) continue;
        const float distance = std::fabs(x - m_buildings[i]->Position().x);
        if (best < 0 || distance < bestDistance) {
            best = static_cast<int>(i);
            bestDistance = distance;
        }
    }
    return best;
}

bool Match::DepositExists(int index) const {
    return index >= 0 && index < static_cast<int>(m_buildings.size()) &&
           m_buildings[static_cast<size_t>(index)]->IsDepositPoint();
}

glm::vec2 Match::DepositPosition(int index) const {
    return DepositExists(index) ? m_buildings[static_cast<size_t>(index)]->Position()
                                : glm::vec2(0.0f);
}

Building* Match::NearestUnfinishedBuilding(const std::string& faction, float x) const {
    Building* best = nullptr;
    float bestDistance = 0.0f;
    for (const auto& building : m_buildings) {
        if (building->Faction() != faction) continue;
        if (building->IsComplete() || !building->IsAlive()) continue;
        const float distance = std::fabs(x - building->Position().x);
        if (best == nullptr || distance < bestDistance) {
            best = building.get();
            bestDistance = distance;
        }
    }
    return best;
}

Unit* Match::NearestEnemyUnit(const std::string& faction, float x, float maxRange) const {
    return m_lane.NearestEnemy(faction, x, maxRange);
}

Damageable* Match::NearestEnemyBuilding(const std::string& faction, float x) const {
    const std::string enemy =
        (faction == Factions::kPlayer) ? Factions::kEnemy : Factions::kPlayer;

    Building* best = nullptr;
    float bestDistance = 0.0f;
    for (const auto& building : m_buildings) {
        if (building->Faction() != enemy || !building->IsAlive()) continue;
        const float distance = std::fabs(building->Position().x - x);
        if (best == nullptr || distance < bestDistance) {
            best = building.get();
            bestDistance = distance;
        }
    }
    return best;
}

std::vector<Building*> Match::PlayerBuildings() const {
    std::vector<Building*> out;
    for (const auto& building : m_buildings) {
        if (building->Faction() == Factions::kPlayer) out.push_back(building.get());
    }
    return out;
}

std::vector<Unit*> Match::PlayerUnits() const {
    std::vector<Unit*> out;
    for (const auto& unit : m_units) {
        if (unit->IsPlayer()) out.push_back(unit.get());
    }
    return out;
}

// --- Odds and ends ---------------------------------------------------------

Building* Match::FindBuilding(const std::string& id) const {
    for (const auto& building : m_buildings) {
        if (building->Stats().id == id) return building.get();
    }
    return nullptr;
}

int Match::DeadUnits() const {
    int dead = 0;
    for (const auto& unit : m_units) {
        if (!unit->IsAlive()) ++dead;
    }
    return dead;
}

} // namespace WolfBrigade
