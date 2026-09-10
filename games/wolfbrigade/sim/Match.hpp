#pragma once

#include <memory>
#include <string>
#include <vector>

#include <glm/glm.hpp>

#include "core/Json.hpp"
#include "sim/BuildPlacement.hpp"
#include "sim/CapturePoint.hpp"
#include "sim/Commands.hpp"
#include "sim/Building.hpp"
#include "sim/EventBus.hpp"
#include "sim/GameData.hpp"
#include "sim/GameState.hpp"
#include "sim/HeroControl.hpp"
#include "sim/Lane.hpp"
#include "sim/Progression.hpp"
#include "sim/Projectiles.hpp"
#include "sim/ResourceNode.hpp"
#include "sim/Selection.hpp"
#include "sim/Snapshot.hpp"
#include "sim/Unit.hpp"
#include "sim/WaveDirector.hpp"
#include "sim/World.hpp"

namespace WolfBrigade {

// One live match, from `scripts/main.gd`.
//
// THE THING THAT OWNS THE ENTITIES. Every slice before this one deferred the
// question and said so - Snapshot.hpp in its own words, "does not own a single
// entity and does not decide who does" - and five test suites each answered it
// privately with a hand-rolled World. This is the answer: units, buildings and
// resource nodes live here and nowhere else.
//
// It is a World and it is a Snapshot::RestoreSink, which is what Snapshot.hpp
// predicted ("the sink shape is deliberately the shape a real Match will have,
// so that when one lands it can implement this by inheriting rather than by
// being reshaped around it"). The sink is inherited PRIVATELY and its three
// methods are private with it. That is not tidiness: SpawnUnit announces a new
// unit on the bus and CreateUnit must not, and the difference is invisible in
// every piece of state either one produces - Snapshot::Restore recounts the
// enemies it rebuilt and hands the number to WaveDirector::FromSave, so a
// restore that double-counted would still report the right AliveEnemies(). The
// only thing that can see the mistake is the number of emissions, and the
// cheapest way to stop somebody making it is to make the sink unreachable
// except through a Snapshot::RestoreSink&.
//
// WHAT THE CONSTRUCTOR DOES: subscribes five handlers to its own bus, and
// stores the data, the profile and the run path. That is all, deliberately. A
// Match that reset the run and armed the director in its constructor could not
// be handed to a suite that wants an empty world - which is exactly what
// test_wb_progression wants, and is why it has an EmptyWorld of its own.
// Booting is a call, not a side effect of existing, and `main.gd`'s _ready
// agrees: it is a shared prefix followed by a fork, not a constructor.
//
// NOT COPYABLE AND NOT MOVABLE. The constructor hands `this` to its own bus and
// to every Unit and Building it creates, and the director holds a spawn
// callback that captures it. A defaulted move would leave every entity's
// m_world pointing at the husk and the director calling into it. The cost is
// real and worth naming: a caller that wants to swap matches holds a
// std::unique_ptr<Match> rather than a Match by value, and cannot keep one in a
// std::optional and re-emplace it.
class Match final : public World, private Snapshot::RestoreSink {
public:
    // The world's shape, from `data/world.json` - `main.gd::_apply_world`,
    // minus everything with a pixel in it.
    //
    // A struct with its own parser rather than six members on Match, because
    // the parsing is worth testing WITHOUT booting a match: the fallbacks are
    // the interesting part, they are unreachable through shipped data, and one
    // line against an authored world.json reaches every one of them.
    //
    // Floats rather than doubles, and that is the house rule rather than an
    // oversight: every field here is a position or a distance between
    // positions, and positions are 32-bit because Godot's Vector2 is. Nothing
    // here accumulates.
    struct Layout {
        float width{6000.0f};
        float groundY{800.0f};

        // The walkable band runs from groundY down laneDepth pixels (world.json's
        // `lane` block). Units and placed buildings stand on any row inside it.
        float laneDepth{0.0f};

        // Two buildings may share a stretch of lane only when their base rows
        // are at least this far apart; any closer and they stack into mush.
        float buildingRowGap{115.0f};

        // Where enemies come in. The key is optional and the original's
        // fallback is `width - 40`, NOT the shipped 5960 - so a world.json with
        // a different width moves the spawn edge with it, and a port that
        // hardcoded 5960 as the fallback would put every wave 40 pixels from a
        // wall that is no longer there.
        float enemyX{5960.0f};

        float townHallX{1500.0f};
        float playerSpawnX{1680.0f};
        float playerSpawnSpacing{60.0f};

        // Read from world.json's `input` block. These two were deferred by
        // the boot slice as presentation and that was half right: the drag
        // threshold and the touch hold time are, but these are not. A pick
        // radius decides WHAT THE PLAYER SELECTED and a formation spacing
        // decides WHERE THEIR UNITS GO, which are both answers a test can
        // be wrong about.
        float pickRadius{52.0f};
        float formationSpacing{46.0f};

        static Layout FromData(const GameData& data);
    };

    // `runPath` is where the Continue save lives, and it has NO DEFAULT on
    // purpose.
    //
    // An empty string means this Match never touches the filesystem:
    // AutosaveRun returns false without writing, and the run-end handler skips
    // ClearRun. A suite that is not about saving passes "" and says so by doing
    // it, which is the difference between a test that is filesystem-free and
    // one that merely has not hit the disk yet. A defaulted argument would have
    // made that invisible.
    //
    // `profile` is a REFERENCE because the profile outlives the run - that is
    // the first thing Progression.hpp says about it - and the run-end handler
    // banks renown into it. A Match that owned one would lose the renown it had
    // just banked the moment a Restart replaced it, which is the anti-farm
    // property inverted. The cost is one extra line in every caller: a Profile
    // has to exist before a Match does.
    //
    // `data` is a reference for a different reason. A Match that called
    // wb::Shipped() for itself could never be pointed at a scratch data
    // directory, and every branch only an authored file can reach would be
    // untestable forever.
    Match(const GameData& data, Profile& profile, std::string runPath);
    ~Match() override;

    Match(const Match&) = delete;
    Match& operator=(const Match&) = delete;
    Match(Match&&) = delete;
    Match& operator=(Match&&) = delete;

    // --- Boot --------------------------------------------------------------

    // The fork in `main.gd::_ready`: a valid pending snapshot restores an
    // in-progress run, anything else starts a fresh one. Returns true when it
    // restored.
    //
    // `Snapshot.consume()`'s clear-on-read is NOT ported and does not need to
    // be. In Godot the pending slot is a static on an autoload, and clearing it
    // on read is what makes an in-game Restart - which queues nothing - boot
    // fresh. Here a Restart is "drop this Match and build another", so there is
    // no slot to forget to clear. The document belongs to the caller.
    bool Boot(const Supersonic::Json::Value& pending);

    // `_boot_fresh`, plus the prefix it shares with a restore.
    //
    // Clears the board FIRST. Godot gets that for free - reload_current_scene
    // destroys the whole tree, so `_boot_fresh` may assume an empty world - and
    // a C++ Match is an object that gets reused. Booting a warm one without
    // clearing would leave the previous run's units stepping, its corpses in
    // the lane and its arrows in the pool.
    //
    // The persistent meta levels are pushed into GameState BEFORE Reset, and
    // that order is the whole of whether a New Game gets the Armory bonuses the
    // player bought. GameState::Reset adds StartingResourceBonus to each
    // opening balance and reads it out of its own m_metaLevels, which only ever
    // had one writer - FromSave, on the restore path. Without this line
    // Deeper Coffers would apply to a Continue and silently not to a New Game.
    void BootFresh();

    // The restore path. Returns false, having built nothing, for a document
    // Snapshot::IsValid refuses.
    //
    // THE DIRECTOR IS ARMED TWICE, and both calls are load-bearing.
    //
    // The first, before Restore, is what puts the schedule in place:
    // WaveDirector::FromSave clamps the saved wave index against m_waves, and
    // m_waves is only ever filled by Setup - so a restore into a never-armed
    // director clamps a wave-four run back to zero and replays the whole match.
    //
    // The second, after Restore, re-reads the schedule once the run's own state
    // is back. It is what made Endless stick while there was a mode for Setup
    // to read; with the mode gone (73999ce) it re-derives the same schedule,
    // and it stays because Setup is the director's one "read the data" step
    // and a restore is exactly when that data has to be the restored run's.
    // The original calls setup from INSIDE restore, after from_save and before
    // the entities, for the same reason. This port's Snapshot deliberately
    // knows nothing about a Match, so the Match brackets it instead.
    //
    // That this is safe is a fact about Setup and is asserted rather than
    // assumed: Setup writes the schedule and the configuration, FromSave writes
    // the counters, and the two sets are disjoint. A second Setup therefore
    // re-derives without rewinding.
    //
    // Skipping either has no diagnostic at all. WaveDirector::Step returns
    // early on a null spawn callback, so a Match that never armed the director
    // restores a run in which no wave ever arrives and nothing says so.
    bool BootFromSave(const Supersonic::Json::Value& snapshot,
                      Snapshot::RestoreReport* report = nullptr);

    // --- Stepping ----------------------------------------------------------

    // One frame, in the order Godot's scene tree runs it.
    //
    // `main.tscn` declares World before WaveDirector, and World's children in
    // the order CapturePoints, Resources, Buildings, Units, Projectiles,
    // FloatingText. No script sets process_priority anywhere in the game, and
    // at equal priority Godot walks the tree depth-first. So the game's order
    // is CapturePoints -> Buildings -> Units -> Projectiles -> WaveDirector,
    // and that is this.
    //
    // THE ENTITY COUNTS ARE TAKEN ONCE, AT FRAME ENTRY, and that is not a
    // micro-optimisation. Godot builds the list of nodes to process before any
    // of them runs, so a unit trained by a barracks during the buildings phase
    // does NOT tick on the frame it appeared. Taking each count as its own
    // phase begins would step it, because under this order the barracks has
    // already pushed it into m_units by the time the units phase starts - a
    // one-frame divergence from the original that nothing else would ever
    // notice.
    //
    // It is NOT the order the oracle harness drives. `verify_snapshot.gd`
    // steps Units -> Buildings -> WaveDirector and never steps projectiles at
    // all. Copying that here would make the shipped game permanently wrong in
    // order to satisfy a test, so the four phases below are public instead and
    // the suite drives whichever order it is reproducing. Which order a number
    // was recorded under is then a written fact rather than an accident.
    //
    // THIS ORDER IS DECIDED, NOT DEMONSTRATED, and the difference is worth
    // stating. It is read off the scene tree, and the suite asserts only that
    // it agrees with the harness's order on the board the oracle numbers come
    // from - which it does, and which means swapping the two entity phases
    // here would go unnoticed by every number in this port. What is measured
    // is the frame-entry rule above; what is argued from the tree is which
    // phase runs first.
    void Step(double delta);

    // The four phases, for a caller reproducing a harness that drives them in
    // its own order. Each steps everything that is on the board WHEN IT IS
    // CALLED, which is what a hand-written driver means by it.
    //
    // The cost of exposing them is that a caller can now run a partial frame -
    // units without the director, buildings without the units they train. The
    // real game never does; the harness this port is verified against does
    // exactly that, and there is no way to reproduce its numbers without it.
    void StepCapturePoints(double delta);
    void StepBuildings(double delta);
    void StepUnits(double delta);
    void StepProjectiles(double delta);
    void StepDirector(double delta);

    // --- Putting things on the board ---------------------------------------

    // THE spawn path, `main.gd::spawn_unit`. Public because training, waves and
    // the fresh boot all reuse it - the original says so in its own docstring,
    // and it is why there is exactly one place that announces a new unit.
    //
    // Registers in the lane and emits unitSpawned. That emission is the ONLY
    // difference between this and the restore path's CreateUnit, and it is the
    // whole reason the director hears about an enemy by listening rather than
    // by being told.
    Unit* SpawnUnit(const UnitStats& stats, const glm::vec2& position);

    // A building placed by the boot or, later, by the player. `building.gd`'s
    // setup() joins the building's own row against units.json for its training
    // times, and this is where that join lives - the restore path does NOT come
    // through here, because Snapshot::Restore does the same join itself.
    Building* PlaceBuilding(const BuildingStats& stats, bool complete,
                            const glm::vec2& position);

    ResourceNode* AddResourceNode(const ResourceNode& prototype);

    // --- The run file ------------------------------------------------------

    // `_autosave_run`, guard included.
    //
    // The guard is one half of the anti-farm property and is useless without
    // the other. A finished run drops its Continue save (see the run-end
    // handler below); without the IsPlaying test here, the very next
    // backgrounding or window close would write it straight back, and a player
    // could bank a run's renown and then resume the run to bank it again.
    //
    // Returns false, having written nothing, when the run is over or when this
    // Match has no run path.
    bool AutosaveRun();

    Supersonic::Json::Value Capture(Snapshot::CaptureReport* report = nullptr);

    // What a capture walks. Non-const because Snapshot::Scene holds non-const
    // pointers, which it holds because a restore writes through them.
    Snapshot::Scene View();

    // --- What is on the board ----------------------------------------------
    //
    // Read-only views. Nothing outside a Match may erase from these - see the
    // removal note at the bottom of this header - and handing out const
    // references rather than the vectors is what says so.

    const std::vector<std::unique_ptr<Unit>>& Units() const { return m_units; }
    const std::vector<std::unique_ptr<Building>>& Buildings() const { return m_buildings; }
    const std::vector<std::unique_ptr<ResourceNode>>& Nodes() const { return m_nodes; }
    const std::vector<std::unique_ptr<CapturePoint>>& CapturePoints() const {
        return m_capturePoints;
    }

    const Layout& WorldLayout() const { return m_layout; }

    // The data this match was built from. Placement needs it to look a building
    // row up before entering a mode for an id nobody authored.
    const GameData& Data() const { return m_data; }

    BuildPlacement& Placement() { return m_placement; }
    const BuildPlacement& Placement() const { return m_placement; }

    // Who is picked, and what turns a tap into an order. Owned here because
    // the original owns them the same way - siblings under main, wired to
    // each other once at boot.
    class Selection& Picked() { return m_selection; }
    const class Selection& Picked() const { return m_selection; }
    class Commands& Orders() { return m_orders; }

    // Who the player is steering. The boot possesses nobody until slice 10
    // spawns the starting hero; a caller that spawns one possesses it here.
    HeroControl& Control() { return m_heroControl; }

    // `main.gd::_back_cancels_placement`, ported whole because it is a PURE
    // PREDICATE and the original says it was kept that way on purpose - so a
    // harness could verify the gate without tripping the `quit()` in the other
    // branch.
    //
    // Android's Back means "back out of the current thing". During a live match
    // with a ghost up that is the placement; once the match is over it is the
    // app. The second half is the regression this exists for: victory or defeat
    // can land while a ghost is still up, and that ghost then sits behind the
    // game-over overlay where nobody can see it - so Back has to EXIT on the
    // first press rather than spend one silently cancelling something invisible.
    //
    // What is NOT here is the notification that delivers it. There is no window
    // and no Android here; whoever owns one asks this and acts on the answer.
    bool BackCancelsPlacement() const;

    EventBus& Bus() { return m_bus; }
    GameState& Run() { return m_state; }
    const GameState& Run() const { return m_state; }
    WaveDirector& Director() { return m_director; }
    const WaveDirector& Director() const { return m_director; }
    Lane& LaneIndex() { return m_lane; }
    const Lane& LaneIndex() const { return m_lane; }
    ProjectilePool& Pool() { return m_projectiles; }
    Profile& PlayerProfile() { return m_profile; }

    Building* FindBuilding(const std::string& id) const;

    // How many corpses the board is carrying. Not a curiosity: it is the
    // running cost of the never-erase rule below, and a test puts a number on
    // it so the cost is something somebody has looked at.
    int DeadUnits() const;

    // --- World -------------------------------------------------------------
    //
    // The five hand-rolled implementations these are meant to replace all agree
    // on the scan shape - `best == nullptr || distance < bestDistance`, a
    // strict `<`, so the FIRST of two equidistant candidates wins - and that
    // convention is preserved here unchanged. Lane::NearestEnemy is the
    // deliberate exception and stays one: it seeds its best distance at the
    // range and compares `<=`, so it is last-wins and inclusive at the edge.

    ResourceNode* NearestHarvestable(float x) const override;

    // The index is into m_buildings, unfiltered - the same thing every existing
    // fixture does, and what makes it safe is the never-erase rule below.
    int NearestDeposit(float x) const override;
    bool DepositExists(int index) const override;
    glm::vec2 DepositPosition(int index) const override;

    Building* NearestUnfinishedBuilding(const std::string& faction, float x) const override;
    Unit* NearestEnemyUnit(const std::string& faction, float x, float maxRange) const override;
    Damageable* NearestEnemyBuilding(const std::string& faction, float x) const override;
    std::vector<Building*> PlayerBuildings() const override;
    std::vector<Unit*> PlayerUnits() const override;
    Unit* NearestWoundedAlly(const Unit* me, float maxRange) const override;
    ProjectilePool* Projectiles() override { return &m_projectiles; }

    // Whoever this match's HeroControl has possessed; null from a death until
    // a respawn. Per match rather than the original's static, so a new match
    // never inherits the last one's hero.
    Unit* Hero() const override { return m_heroControl.Hero(); }

private:
    // --- Snapshot::RestoreSink ---------------------------------------------
    //
    // THE MINIMUM THE CONTRACT ASKS FOR, AND NOT ONE LINE MORE.
    //
    // This is not fastidiousness. Two mutations survived the save slice because
    // its test sink did more than the contract required - it called
    // SetTrainTimes itself, and it set the finished state itself - so deleting
    // either from Snapshot::Restore changed nothing observable and the suite
    // stayed green. A sink that duplicates what Restore promises cannot tell
    // you whether Restore is keeping its promise.
    //
    // Concretely: CreateBuilding passes `complete` straight through as the
    // pre-placed flag and does NOT call SetTrainTimes - Restore owns that.
    // CreateUnit registers in the lane, because Snapshot::Restore clears the
    // lane before it builds anything and a unit that never rejoins it is
    // invisible to every 8 Hz scan in the game while looking perfectly alive on
    // the board. And CreateUnit does NOT emit unitSpawned and does not touch
    // the director.
    Building* CreateBuilding(const BuildingStats& stats, bool complete,
                             const glm::vec2& position) override;
    ResourceNode* CreateResourceNode(const ResourceNode& fromSave) override;
    Unit* CreateUnit(const UnitStats& stats, const glm::vec2& position) override;

    // `_apply_world`, minus every line with a pixel in it. Runs on BOTH boot
    // paths, because it runs before the fork in the original - and a port that
    // only filled the layout in on a fresh boot would leave a restored match
    // clamping every trained unit against a width of zero, which puts it on the
    // left edge of the world.
    void ApplyWorld();

    void SpawnTownHall();
    void SpawnResourceNodes();
    void SpawnCapturePoints();
    void SpawnStartingWorkers();

    // Drops everything and resets the derived indexes. Only a boot calls this.
    //
    // The indexes go FIRST and then the entities, which is the opposite of the
    // order it is tempting to write. Clearing a lane after destroying the units
    // it points at is safe only while nothing scans in between - a property of
    // today's code rather than of the design - and there is no reason to depend
    // on it.
    void ClearBoard();

    // --- Bus handlers ------------------------------------------------------

    // `_on_unit_trained`: the CURRENT research, x clamped to [0, width], y
    // forced to the ground line.
    //
    // All three matter and none is reachable from shipped data. The research
    // lookup is what makes a soldier trained after Iron Swords stronger than
    // one trained before it. The y-force means a trained unit can never inherit
    // a height from the building that made it. And the clamp's ceiling is the
    // world width EXACTLY, not the width minus half a body - so a unit trained
    // by a building at the right-hand edge stands ON the edge, forty pixels
    // past the enemy spawn line.
    void OnUnitTrained(const std::string& unitId, const glm::vec2& spawnPoint,
                       const glm::vec2& rally, const std::string& squad);

    // The enemy tally. The port's WaveDirector takes these as CALLS rather than
    // as subscriptions and its header says why, so the Match does the
    // subscribing on its behalf. That is a deviation from the original, where
    // the director wires itself in its own _ready, and it is taken because
    // making the director subscribe would rewrite the best-tested slice in the
    // port and change what test_wb_waves is testing.
    //
    // REENTRANCY, stated because it is real and currently harmless. OnUnitDied
    // can be reached from inside ProjectilePool::Step's walk over its own
    // vector, by way of an arrow landing, TakeDamage and Kill; and from there
    // through CheckVictory, GameState::Win, gameWon and OnGameOver - which
    // deletes a file. Nothing in that chain adds a projectile or clears the
    // pool, which is the only thing that would invalidate the walk, and nothing
    // in it removes an entity, because nothing here ever does. Both of those
    // are properties somebody could break; this is where they are written down.
    void OnUnitSpawned(Unit* unit);
    void OnUnitDied(Unit* unit);

    // A Town Hall falling loses the run - and the original tests the building's
    // ID with NO faction test at all, so an enemy Town Hall would lose the
    // player's run too. Faithfully reproduced: the shipped game has exactly one
    // Town Hall and no enemy buildings, so the two readings cannot be told
    // apart at shipped values, and inventing the stricter one would be the port
    // deciding a question the original never asked.
    void OnBuildingDestroyed(Building* building);

    // The high-water mark, max only: a player replaying the early waves must
    // not lower their own record.
    void OnWaveStarted(int index);

    // Run-end housekeeping, and the other half of the anti-farm property.
    //
    // Banks the run's renown and then DROPS the Continue save, in that order,
    // exactly as the original's game-over overlay does. It fires once because
    // GameState::Win and Lose are one-way and ignore a second call - the same
    // guard the original leans on. Without the ClearRun a player could reach
    // game over, bank the renown, and resume the same run from disk to bank it
    // again. Both halves or neither, which is why they are two lines in one
    // function rather than two functions.
    //
    // ONE ASYMMETRY, named because it is a choice and not an oversight. The
    // original's add_renown writes the profile through to disk on the way past;
    // this banks into a Profile the caller owns and then goes to the filesystem
    // to delete the Continue. A crash between those two lines costs the player
    // the run AND the renown it earned. Persisting the profile stays the
    // caller's, because the profile outlives the match and a Match that wrote
    // it would be deciding where it lives - but the window is real and this is
    // where it is written down.
    //
    // The empty-path guard here is belt and braces and is NOT testable:
    // Snapshot::ClearRun is std::remove, and std::remove("") is a silent
    // no-op, so removing the guard changes nothing any assertion could see.
    // Said rather than left to look like coverage. The guard on AutosaveRun is
    // a different matter - that one is observable, because it decides a return
    // value, and it has a test.
    void OnGameOver(bool won);

    const GameData& m_data;
    Profile& m_profile;
    std::string m_runPath;

    EventBus m_bus;
    GameState m_state{m_data, m_bus};
    WaveDirector m_director{m_data, m_state, m_bus};
    Lane m_lane;
    ProjectilePool m_projectiles;

    // Level furniture, rebuilt from the level's data on every boot. Declared
    // after m_state because each one deregisters its army bonus from it on the
    // way out, so it has to go first.
    std::vector<std::unique_ptr<CapturePoint>> m_capturePoints;

    Layout m_layout;

    // Declared AFTER everything it will ask questions of, and constructed
    // with a reference to a Match that is not finished being built yet.
    // That is legal because it only stores the reference - it asks nothing
    // until somebody calls Begin - and it is the same shape the original
    // has, where BuildPlacement is a child node of main handed the world
    // container to place into.
    BuildPlacement m_placement{*this};
    Selection m_selection{*this};
    Commands m_orders{*this, m_selection};

    // After the bus it listens to for the hero's death, which therefore
    // outlives it. The hero it holds points into m_units, so ClearBoard makes
    // it forget.
    HeroControl m_heroControl{m_bus};

    // unique_ptr, and it is load-bearing twice over.
    //
    // Snapshot.hpp requires stable addresses for the life of a capture: a
    // vector<Unit> by value invalidates every raw pointer already in the id
    // table the next time it grows. Independently, both of these grow MID-STEP
    // - a building's unitTrained emission pushes a unit into m_units while
    // m_units is being walked, and the director's spawn callback does the same
    // during the director's phase. Growing them invalidates ITERATORS but never
    // the heap objects other entities point at, which is what makes both legal
    // and why every phase below indexes rather than range-fors.
    std::vector<std::unique_ptr<Unit>> m_units;
    std::vector<std::unique_ptr<Building>> m_buildings;
    std::vector<std::unique_ptr<ResourceNode>> m_nodes;

    // -----------------------------------------------------------------------
    // REMOVAL: NOTHING IS EVER ERASED FROM THE THREE VECTORS ABOVE.
    //
    // A dead unit stays as a Dead tombstone, a fallen building as a Dead one,
    // an exhausted node at amount zero, until the next boot clears the board
    // wholesale. Every scan already filters on exactly those - IsAlive,
    // Harvestable, IsDepositPoint - and Snapshot::Capture already skips them,
    // so a tombstone is the same "gone" every slice before this one has been
    // testing against.
    //
    // WHY, and it is not squeamishness about pointers. World.hpp claims a
    // deposit INDEX is "the same question Godot's is_instance_valid answers,
    // asked in a way that cannot dangle". That claim is true only while the
    // container is append-only. An index is a POSITIONAL alias, not an
    // identity: erase a building from the middle and a worker's cached index
    // silently names a DIFFERENT live Town Hall - it still passes
    // DepositExists, the worker still delivers, the wood still balances because
    // GameState::Add does not care who banked it, and no economy assertion
    // anywhere notices. Erasing turns a crash into a wrong answer, which is
    // strictly worse. Never erasing makes the header's claim true rather than
    // aspirational.
    //
    // It also settles the pointer question by construction. Nothing is freed,
    // so nothing dangles - which is Damageable.hpp's rule ("whoever owns the
    // world keeps its entities alive at least as long as anything can be
    // pointing at one") honoured literally rather than approximately. And it is
    // what makes the reentrant emissions above safe: an emission that reaches
    // this class mid-walk cannot remove anything, because nothing can.
    //
    // WHAT IT COSTS, plainly. Memory grows with total spawns rather than with
    // what is standing, and the scans walk the corpses: Lane::CountOf counts
    // them, and every fighter and every tower walks the full lane list at 8 Hz
    // for the rest of the run. Over a long run that is O(everything
    // ever spawned) per fighter per tick. The original does not have the
    // problem because Godot frees a unit behind its death fade and the lane
    // unregisters on the way out. DeadUnits() exists so a test can say what the
    // number actually is.
    //
    // The alternative, and why not: compacting behind a free list buys memory
    // this game does not need at these unit counts, and spends the one
    // invariant every cached deposit index depends on. When the presentation
    // slice needs bodies to disappear, the honest fix is a reap at a point
    // BETWEEN frames that unregisters from the lane and invalidates every
    // cached index in the same moment. That is a slice, not a line.
    // -----------------------------------------------------------------------

    // -----------------------------------------------------------------------
    // WHAT OF main.gd IS DELIBERATELY NOT PORTED HERE.
    //
    // PRESENTATION AND INPUT - a later slice, and none of it changes a number:
    //   the scene preloads and every @onready UI reference; the reusable order
    //   marker and _ping_order; Audio.reload_data / connect_events / set_muted
    //   / play_music; FloatingNumbers.setup / connect_events; Commands.setup,
    //   BuildPlacement.setup and the HUD's pause edge; _wire_input's thirteen
    //   input signal connections; BottomBar.setup;
    //   _on_building_destroyed_shake; and the presentation half of
    //   _apply_world - the clear colour, the ground rectangle, the camera
    //   bounds, focus and edge-push margin, the drag threshold, the touch hold
    //   time, the unit pick radius and the formation spacing.
    //
    //   _to_color is not ported and is only ALMOST presentation: a node's
    //   colour is a SAVED field, and the port already carries it as a
    //   "#3f6b34" string on ResourceNode. What is not ported is turning that
    //   string into a colour. _to_vec2 IS ported, inside SpawnResourceNodes,
    //   because a body size is a saved field with a shape.
    //
    // GODOT PLATFORM - no analogue exists here and inventing one would be
    // fiction:
    //   auto_accept_quit and quit_on_go_back, and their restoration in
    //   _exit_tree. They exist so _notification gets to run before the OS kills
    //   the app; a static library has no window and no tree. The ACTION each of
    //   _notification's three cases takes is ported - it is AutosaveRun() - and
    //   the plumbing that delivers it is not. Whoever owns a window calls
    //   AutosaveRun on close and on background, and that is the whole of it.
    //   The original's own harness declines to fire the close notification for
    //   the same reason a test here would, and exercises the background one
    //   instead.
    //
    //   _back_cancels_placement is a pure predicate, but its second term is
    //   BuildPlacement::is_active(), which is input-slice state. It arrives
    //   with that slice.
    //
    //   add_child -> _ready as a registration hook, queue_free behind a fade,
    //   and reload_current_scene / change_scene_to_file - the mechanism that
    //   lets _boot_fresh assume an empty world, replaced here by ClearBoard.
    //
    // ELSEWHERE, NOT HERE: the game's other two clear_run sites - New Game
    // abandoning a saved run, and an in-game Restart abandoning it - are menu
    // slices. The anti-farm property does not depend on either; it depends on
    // this file's run-end handler and its autosave guard.
    // -----------------------------------------------------------------------
};

} // namespace WolfBrigade
