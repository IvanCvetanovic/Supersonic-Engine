// The Magic Portals spike: level30 through the engine's solver, measured.
//
// Not a game, and not a test. It prints the numbers step 2 of
// docs/planning/2026-09-10-magic-portals-spike.md records, each beside the
// threshold that doc wrote down before this first ran, with a verdict. A failed
// threshold is a finding about the ENGINE - whether S1, F1 or F2 has to be built
// before the port - not a regression, which is why this is not a ctest suite: a
// red test here would only ever mean "not built yet".
//
//   MagicPortalsSpike [--unlocked] [levels-dir] [data-dir]
//
// Every body is locked to the plane (S1, LevelBuilder::kPlaneLock*) unless
// --unlocked is given, which measures the solver as it was before the locks -
// and, since an unlocked body's arithmetic is unchanged, must reproduce the
// first table in the doc to the digit.
//
// Built with -DSUPERSONIC_BUILD_MAGICPORTALS=ON. The level is read from outside
// the repository (SUPERSONIC_MAGICPORTALS_LEVELS); the player's size and the
// portal numbers come from the remake's own player.json and portals.json
// (SUPERSONIC_MAGICPORTALS_DATA) - read, never copied into code, because the
// remake marks every one of them _guess.
//
// Everything runs at the original's scale and gravity (Units.hpp): 50 px to the
// metre and 10 m/s^2, overriding the engine's 9.81 through PhysicsSettings.

#include "core/Components.hpp"
#include "core/Json.hpp"
#include "core/PhysicsSettings.hpp"
#include "core/PhysicsSystem.hpp"
#include "sim/LevelBuilder.hpp"
#include "sim/Portal.hpp"
#include "sim/Prism.hpp"
#include "sim/Tscn.hpp"
#include "sim/Units.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <limits>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

#include <entt/entt.hpp>
#include <glm/glm.hpp>

using namespace Supersonic;
using namespace MagicPortals;

namespace {

constexpr float kStep = 1.0f / 60.0f;

// The thresholds, as the doc states them.
constexpr int kDriftTicks = 3600;           // one minute at 60 Hz
constexpr double kDriftLimitPx = 1.0;       // out of the plane
constexpr double kTiltLimitRad = 0.01;      // about x or y
constexpr int kLandingTicks = 45;           // verify_gameplay's window
constexpr double kRestTolerancePx = 1.0;
constexpr double kPortalToleranceMs = 1e-4; // m/s

// How hard the spike pushes things along the floor. A choice of the spike's,
// not the remake's walk speed - that is 160 px/s, and marked _guess.
constexpr float kDriveSpeed = 1.0f; // m/s, 50 px/s

struct Paths {
    std::string levels;
    std::string data;
    std::filesystem::path prisms;
};

struct PlayerBody {
    double widthPx = 0.0;
    double heightPx = 0.0;
};

int g_failures = 0;
bool g_lockToPlane = true;

[[noreturn]] void Die(const std::string& why) {
    std::fprintf(stderr, "MagicPortalsSpike: %s\n", why.c_str());
    std::exit(2);
}

std::string Fixed(double value, int decimals) {
    char text[64];
    std::snprintf(text, sizeof text, "%.*f", decimals, value);
    return text;
}

void Row(const std::string& what, const std::string& measured, const std::string& threshold,
         const std::string& verdict) {
    if (verdict == "FAIL") ++g_failures;
    std::printf("  %-48s %-40s %-22s %s\n", what.c_str(), measured.c_str(), threshold.c_str(), verdict.c_str());
}

const char* Toolchain() {
#if defined(_MSC_VER)
    static char text[48];
    std::snprintf(text, sizeof text, "MSVC %d", _MSC_FULL_VER);
    return text;
#elif defined(__clang__)
    return "clang " __clang_version__;
#elif defined(__GNUC__)
    return "GCC " __VERSION__;
#else
    return "an unknown compiler";
#endif
}

std::string ReadFile(const std::string& path) {
    std::ifstream file(path, std::ios::binary);
    if (!file) return {};
    std::ostringstream buffer;
    buffer << file.rdbuf();
    return buffer.str();
}

PlayerBody LoadPlayerBody(const std::string& dataDir) {
    const std::string path = dataDir + "/player.json";
    const std::string text = ReadFile(path);
    if (text.empty()) Die(path + ": cannot read");
    Json::Parser parser(text);
    Json::Value root;
    if (!parser.Parse(root)) Die(path + ": " + parser.Error());
    if (!root.IsObject() || !root.Has("body") || !root["body"].IsObject()) Die(path + ": no \"body\" object");
    const Json::Value& body = root["body"];
    if (!body.Has("width_px") || !body.Has("height_px") || !body["width_px"].IsNumber() ||
        !body["height_px"].IsNumber())
        Die(path + ": body.width_px and body.height_px must be numbers");
    return {body["width_px"].AsNumber(), body["height_px"].AsNumber()};
}

const Tscn::Node* ByEntityName(const Tscn::Scene& scene, const std::string& entityName) {
    for (const Tscn::Node& node : scene.nodes) {
        const Tscn::Value* name = node.Meta("entity_name");
        if (name != nullptr && name->kind == Tscn::Value::Kind::String && name->text == entityName) return &node;
    }
    Die("level30 has no " + entityName);
}

glm::dvec2 PositionPx(const Tscn::Node& node) {
    const Tscn::Value* v = node.Find("position");
    if (v == nullptr || v->kind != Tscn::Value::Kind::Vector2) return glm::dvec2(0.0);
    return glm::dvec2(v->numbers[0], v->numbers[1]);
}

// The top of a polygon body, in the remake's pixels (+y down).
double TopPx(const Tscn::Scene& scene, const std::string& entity) {
    const Tscn::Node* node = scene.FindNode(entity);
    const Tscn::Node* shape = scene.FindNode(entity + "/Body/Shape");
    const Tscn::Value* polygon = shape != nullptr ? shape->Find("polygon") : nullptr;
    if (node == nullptr || polygon == nullptr) Die(entity + " is not a polygon body");
    double least = std::numeric_limits<double>::infinity();
    for (std::size_t i = 1; i < polygon->numbers.size(); i += 2) least = std::min(least, polygon->numbers[i]);
    return PositionPx(*node).y + least;
}

// The size of a box body's rectangle, in pixels.
glm::dvec2 BoxSizePx(const Tscn::Scene& scene, const std::string& entity) {
    const Tscn::Node* shape = scene.FindNode(entity + "/Body/Shape");
    const Tscn::Value* ref = shape != nullptr ? shape->Find("shape") : nullptr;
    const Tscn::Resource* rect = ref != nullptr ? scene.Embedded(ref->text) : nullptr;
    const Tscn::Value* size = rect != nullptr ? rect->Find("size") : nullptr;
    if (size == nullptr || size->kind != Tscn::Value::Kind::Vector2) Die(entity + " is not a box body");
    return glm::dvec2(size->numbers[0], size->numbers[1]);
}

void UseOriginalGravity(entt::registry& registry) {
    PhysicsSettings settings;
    settings.gravity = glm::vec3(0.0f, -static_cast<float>(Units::kGravity), 0.0f);
    registry.ctx().insert_or_assign<PhysicsSettings>(std::move(settings));
}

LevelBuilder::Built LoadLevel(entt::registry& registry, const Tscn::Scene& scene, const Paths& paths,
                              bool withStatics) {
    UseOriginalGravity(registry);
    LevelBuilder::Options options;
    options.prismDirectory = paths.prisms;
    options.withStatics = withStatics;
    options.lockToPlane = g_lockToPlane;
    LevelBuilder::Built built;
    std::string error;
    if (!LevelBuilder::Build(scene, registry, options, built, error)) Die(error);
    return built;
}

entt::entity Entity(const LevelBuilder::Built& built, const std::string& name) {
    const auto found = built.entities.find(name);
    if (found == built.entities.end()) Die(name + " was not built");
    return found->second;
}

// The spike's player is an INSTRUMENT, not the port's answer: a dynamic capsule
// with rotation frozen, the size of the remake's player.json body. The remake's
// player is a kinematic CharacterBody2D and the engine has no character
// controller; which body the port uses is decided by these numbers.
entt::entity AddPlayer(entt::registry& registry, const glm::dvec2& atPx, const PlayerBody& body, bool kinematic,
                       bool allowSleep) {
    const entt::entity entity = registry.create();
    registry.emplace<TransformComponent>(entity).position = Units::ToWorld(atPx.x, atPx.y);
    auto& capsule = registry.emplace<CapsuleColliderComponent>(entity);
    capsule.radius = Units::ToMetres(body.widthPx * 0.5);
    capsule.height = Units::ToMetres(body.heightPx);
    auto& rigid = registry.emplace<RigidBodyComponent>(entity);
    rigid.freezeRotation = true;
    rigid.isKinematic = kinematic;
    rigid.allowSleep = allowSleep;
    rigid.friction = LevelBuilder::kBodyFriction;
    rigid.restitution = LevelBuilder::kBodyRestitution;
    if (g_lockToPlane) rigid.lockPosition = LevelBuilder::kPlaneLockPosition; // rotation is frozen already
    registry.emplace<TagComponent>(entity, TagComponent{"player"});
    return entity;
}

entt::entity AddCrate(entt::registry& registry, const glm::dvec2& atPx, const glm::dvec2& sizePx, bool allowSleep) {
    const entt::entity entity = registry.create();
    registry.emplace<TransformComponent>(entity).position = Units::ToWorld(atPx.x, atPx.y);
    auto& box = registry.emplace<BoxColliderComponent>(entity);
    box.size = glm::vec3(Units::ToMetres(sizePx.x), Units::ToMetres(sizePx.y),
                         static_cast<float>(LevelBuilder::kBodyDepthMetres));
    auto& rigid = registry.emplace<RigidBodyComponent>(entity);
    rigid.allowSleep = allowSleep;
    rigid.friction = LevelBuilder::kBodyFriction;
    rigid.restitution = LevelBuilder::kBodyRestitution;
    if (g_lockToPlane) {
        rigid.lockPosition = LevelBuilder::kPlaneLockPosition;
        rigid.lockRotation = LevelBuilder::kPlaneLockRotation;
    }
    return entity;
}

// ---- 1. Out-of-plane drift ------------------------------------------------

// How far a body's own z axis has turned from the world's: its tilt out of the
// plane, whatever it has done in it. Not the Euler x and y, which are not
// unique - a crate that has rolled half over IN the plane can come back as
// (pi, pi, 0), the same rotation as (0, 0, pi), and read as a full tilt.
double TiltOutOfPlane(const TransformComponent& transform) {
    const glm::vec3 localZ = glm::vec3(transform.getModelMatrix()[2]);
    return std::atan2(std::sqrt(static_cast<double>(localZ.x) * localZ.x + static_cast<double>(localZ.y) * localZ.y),
                      std::fabs(static_cast<double>(localZ.z)));
}

enum class Drive { Nothing, Crate, Player };

struct Drift {
    double zPx = 0.0;
    double tiltRad = 0.0;
    std::string worst = "-";
    int firstBreach = -1; // the first tick either threshold was crossed
    int asleep = 0;
    int watched = 0;
};

Drift MeasureDrift(const Tscn::Scene& scene, const Paths& paths, const PlayerBody& body, Drive drive,
                   bool allowSleep) {
    entt::registry registry;
    const LevelBuilder::Built built = LoadLevel(registry, scene, paths, true);
    const entt::entity player = AddPlayer(registry, PositionPx(*ByEntityName(scene, "main_char")), body, false, allowSleep);
    for (auto entity : registry.view<RigidBodyComponent>()) registry.get<RigidBodyComponent>(entity).allowSleep = allowSleep;

    const entt::entity crate = Entity(built, "crate_969");
    std::vector<std::pair<std::string, entt::entity>> watched;
    if (drive == Drive::Crate) {
        watched = {{"crate_969", crate}};
    } else if (drive == Drive::Player) {
        watched = {{"player", player}};
    } else {
        for (const char* name : {"crate_969", "crate_ent_968", "crate_small_ent_973"})
            watched.emplace_back(name, Entity(built, name));
        watched.emplace_back("player", player);
    }

    // Back and forth along the floor. The crate's run crosses x = 256, where
    // two chamfered platforms meet in a 3.2 px notch; the player walks its own.
    const entt::entity driven = drive == Drive::Crate ? crate : player;
    const double low = drive == Drive::Crate ? 100.0 : 30.0;
    const double high = drive == Drive::Crate ? 350.0 : 230.0;
    float direction = -1.0f;

    Drift drift;
    drift.watched = static_cast<int>(watched.size());
    for (int tick = 0; tick < kDriftTicks; ++tick) {
        if (drive != Drive::Nothing) {
            const double xPx = Units::ToPixels(registry.get<TransformComponent>(driven).position).x;
            if (xPx < low) direction = 1.0f;
            else if (xPx > high) direction = -1.0f;
            registry.get<RigidBodyComponent>(driven).velocity.x = direction * kDriveSpeed;
        }
        PhysicsSystem::Update(registry, kStep);
        for (const auto& watch : watched) {
            const auto& transform = registry.get<TransformComponent>(watch.second);
            const double z = std::fabs(transform.position.z) * Units::kPixelsPerMetre;
            const double tilt = TiltOutOfPlane(transform);
            if (drift.firstBreach < 0 && (z >= kDriftLimitPx || tilt >= kTiltLimitRad)) drift.firstBreach = tick + 1;
            if (z > drift.zPx || tilt > drift.tiltRad) drift.worst = watch.first;
            drift.zPx = std::max(drift.zPx, z);
            drift.tiltRad = std::max(drift.tiltRad, tilt);
        }
    }
    for (const auto& watch : watched)
        if (registry.get<RigidBodyComponent>(watch.second).isSleeping) ++drift.asleep;
    return drift;
}

// ---- 2. Landing -----------------------------------------------------------

struct Landing {
    int tick = -1;
    double restYPx = 0.0;
};

Landing MeasureLanding(const Tscn::Scene& scene, const Paths& paths, const PlayerBody& body, bool withStatics) {
    entt::registry registry;
    LoadLevel(registry, scene, paths, withStatics);
    const entt::entity player = AddPlayer(registry, PositionPx(*ByEntityName(scene, "main_char")), body, false, true);
    const float half = Units::ToMetres(body.heightPx * 0.5);

    Landing landing;
    for (int tick = 1; tick <= 120; ++tick) {
        PhysicsSystem::Update(registry, kStep);
        const auto& transform = registry.get<TransformComponent>(player);
        const auto& rigid = registry.get<RigidBodyComponent>(player);
        // LANDED, not "did not fall": something solid just under the feet and
        // the fall stopped. A ray from a pixel above the feet, reaching three
        // below them.
        const glm::vec3 feet = transform.position - glm::vec3(0.0f, half - Units::ToMetres(1.0), 0.0f);
        const bool grounded = PhysicsSystem::IsGrounded(registry, feet, Units::ToMetres(4.0), player);
        if (landing.tick < 0 && grounded && std::fabs(rigid.velocity.y) < 0.05f) landing.tick = tick;
    }
    landing.restYPx = Units::ToPixels(registry.get<TransformComponent>(player).position).y;
    return landing;
}

// ---- 3. Riders ------------------------------------------------------------

struct Rider {
    double lowGapPx = 0.0;
    double highGapPx = 0.0;
    double finalGapPx = 0.0;
    double carried = 0.0;
    bool onTop = false;
};

// level30's own shapes on a rig of the spike's: its 256 x 32 slab as the moving
// platform and its small crate as the rider. level30's three lifts are doors
// that rise into the ceiling and carry nothing, so they cannot answer F1.
Rider MeasureRider(const Tscn::Scene& scene, const Paths& paths, const glm::vec3& velocity) {
    const Tscn::Node* shape = scene.FindNode("platform_ent_966/Body/Shape");
    const Tscn::Value* polygon = shape != nullptr ? shape->Find("polygon") : nullptr;
    if (polygon == nullptr) Die("platform_ent_966 has no polygon");
    const glm::dvec2 size = BoxSizePx(scene, "crate_small_ent_973");

    entt::registry registry;
    UseOriginalGravity(registry);
    std::string error;
    const std::string prism = Prism::Write(polygon->numbers, LevelBuilder::kStaticDepthMetres, paths.prisms, error);
    if (prism.empty()) Die(error);

    double topPx = std::numeric_limits<double>::infinity();
    double halfWidthPx = 0.0;
    for (std::size_t i = 0; i + 1 < polygon->numbers.size(); i += 2) {
        halfWidthPx = std::max(halfWidthPx, std::fabs(polygon->numbers[i]));
        topPx = std::min(topPx, polygon->numbers[i + 1]);
    }

    const entt::entity platform = registry.create();
    registry.emplace<TransformComponent>(platform);
    registry.emplace<ConvexHullColliderComponent>(platform).sourcePath = prism;
    registry.emplace<RigidBodyComponent>(platform).isKinematic = true;
    const entt::entity crate = AddCrate(registry, {0.0, topPx - size.y * 0.5}, size, false);

    // Positive: daylight under the crate. Negative: sunk into the slab.
    const auto gapPx = [&] {
        const double crateBottom = Units::ToPixels(registry.get<TransformComponent>(crate).position).y + size.y * 0.5;
        const double surface = Units::ToPixels(registry.get<TransformComponent>(platform).position).y + topPx;
        return surface - crateBottom;
    };

    for (int i = 0; i < 60; ++i) PhysicsSystem::Update(registry, kStep);

    const glm::vec3 crateStart = registry.get<TransformComponent>(crate).position;
    const glm::vec3 platformStart = registry.get<TransformComponent>(platform).position;
    Rider rider;
    rider.lowGapPx = std::numeric_limits<double>::infinity();
    rider.highGapPx = -std::numeric_limits<double>::infinity();
    for (int i = 0; i < 60; ++i) {
        registry.get<TransformComponent>(platform).position += velocity * kStep;
        // Written, and ignored: the contact solve leaves a kinematic body's
        // velocity out (ARCHITECTURE.md 7m). That is F1.
        registry.get<RigidBodyComponent>(platform).velocity = velocity;
        PhysicsSystem::Update(registry, kStep);
        const double gap = gapPx();
        rider.lowGapPx = std::min(rider.lowGapPx, gap);
        rider.highGapPx = std::max(rider.highGapPx, gap);
    }
    rider.finalGapPx = gapPx();

    const glm::vec3 crateEnd = registry.get<TransformComponent>(crate).position;
    const glm::vec3 platformEnd = registry.get<TransformComponent>(platform).position;
    const glm::vec3 axis = glm::normalize(velocity);
    const float moved = glm::dot(platformEnd - platformStart, axis);
    rider.carried = moved != 0.0f ? glm::dot(crateEnd - crateStart, axis) / moved : 0.0;
    rider.onTop = std::fabs(crateEnd.x - platformEnd.x) * Units::kPixelsPerMetre < halfWidthPx &&
                  std::fabs(rider.finalGapPx) < size.y * 0.5;
    return rider;
}

// ---- 4. Triggers ----------------------------------------------------------

// On how many of ten ticks the exit's trigger reports the player, standing on
// the slab under it and inside its box.
int MeasureTrigger(const Tscn::Scene& scene, const Paths& paths, const PlayerBody& body, bool kinematic) {
    entt::registry registry;
    LoadLevel(registry, scene, paths, true);
    const Tscn::Node* door = ByEntityName(scene, "door.ent");
    std::string error;
    const entt::entity trigger = LevelBuilder::AddTriggerFromMetadata(scene, door->name, registry, error);
    if (trigger == entt::null) Die(error);

    const double standY = TopPx(scene, "platform_ent_966") - body.heightPx * 0.5;
    const entt::entity player = AddPlayer(registry, {PositionPx(*door).x, standY}, body, kinematic, true);

    std::vector<PhysicsSystem::Contact> contacts;
    int ticks = 0;
    for (int tick = 0; tick < 10; ++tick) {
        PhysicsSystem::Update(registry, kStep, &contacts);
        for (const PhysicsSystem::Contact& contact : contacts) {
            const bool pair = (contact.a == player && contact.b == trigger) || (contact.a == trigger && contact.b == player);
            if (pair && contact.isTrigger) {
                ++ticks;
                break;
            }
        }
    }
    return ticks;
}

// ---- 5. Portal ------------------------------------------------------------

struct Traversal {
    glm::dvec2 exitVelocityPx{0.0};
    glm::dvec2 exitPositionPx{0.0};
    double velocityErrorMs = 0.0;
    double positionErrorPx = 0.0;
};

// Two portals of the spike's own, in open air in level30 - it has no static
// portal, and a player's placed ones exist only in play. The traveller arrives
// heading right at 100 px/s and the exit is turned a quarter clockwise on screen.
// What is measured is whether the solver keeps what the traversal wrote.
Traversal MeasurePortal(const Tscn::Scene& scene, const Paths& paths, const Portal::Transit& transit) {
    entt::registry registry;
    LoadLevel(registry, scene, paths, true);
    const glm::dvec2 entry(300.0, 60.0);
    const glm::dvec2 exitAt(500.0, 60.0);
    const double entryRotation = 0.0;
    const double exitRotation = 1.5707963267948966;
    const glm::dvec2 arriving(100.0, 0.0);
    const entt::entity crate = AddCrate(registry, entry, BoxSizePx(scene, "crate_small_ent_973"), false);

    Traversal traversal;
    traversal.exitVelocityPx = Portal::ExitVelocity(arriving, entryRotation, exitRotation, transit);
    traversal.exitPositionPx = Portal::ExitPosition(exitAt, traversal.exitVelocityPx, transit);

    auto& transform = registry.get<TransformComponent>(crate);
    auto& rigid = registry.get<RigidBodyComponent>(crate);
    transform.position = Units::ToWorld(traversal.exitPositionPx.x, traversal.exitPositionPx.y);
    rigid.velocity = glm::vec3(Units::ToMetres(traversal.exitVelocityPx.x), Units::ToMetres(-traversal.exitVelocityPx.y), 0.0f);
    const glm::vec3 written = rigid.velocity;
    const glm::vec3 placed = transform.position;

    PhysicsSystem::Update(registry, kStep);

    const glm::vec3 expected = written + glm::vec3(0.0f, -static_cast<float>(Units::kGravity) * kStep, 0.0f);
    const auto& after = registry.get<TransformComponent>(crate);
    traversal.velocityErrorMs = glm::length(registry.get<RigidBodyComponent>(crate).velocity - expected);
    traversal.positionErrorPx = glm::length(after.position - (placed + expected * kStep)) * Units::kPixelsPerMetre;
    return traversal;
}

} // namespace

int main(int argc, char** argv) {
    Paths paths;
    std::vector<std::string> positional;
    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];
        if (arg == "--unlocked") g_lockToPlane = false;
        else positional.push_back(arg);
    }
    paths.levels = positional.size() > 0 ? positional[0] : MAGICPORTALS_LEVELS_DIR;
    paths.data = positional.size() > 1 ? positional[1] : MAGICPORTALS_DATA_DIR;
    paths.prisms = std::filesystem::temp_directory_path() / "supersonic-mp-spike";

    Tscn::Scene scene;
    std::string error;
    if (!Tscn::Load(paths.levels + "/level30.tscn", scene, error)) Die(error);
    const PlayerBody body = LoadPlayerBody(paths.data);
    Portal::Transit transit;
    if (!Portal::LoadTransit(paths.data + "/portals.json", transit, error)) Die(error);

    entt::registry census;
    const LevelBuilder::Built built = LoadLevel(census, scene, paths, true);

    std::printf("Magic Portals spike - level30 through the engine's solver\n");
    std::printf("  built with %s\n", Toolchain());
    std::printf("  level     %s/level30.tscn: %d static, %d rigid, %d area bodies; %d hull prisms\n",
                paths.levels.c_str(), built.statics, built.rigids, built.areas, built.hulls);
    std::printf("  world     %.0f px/m, gravity %.1f m/s^2 (the original's Box2D; the engine default is 9.81), step 1/60 s\n",
                Units::kPixelsPerMetre, Units::kGravity);
    std::printf("  depth     static %.1f m, moving %.1f m; bodies friction %.1f, restitution %.1f\n",
                LevelBuilder::kStaticDepthMetres, LevelBuilder::kBodyDepthMetres,
                static_cast<double>(LevelBuilder::kBodyFriction), static_cast<double>(LevelBuilder::kBodyRestitution));
    std::printf("  player    capsule %.0f x %.0f px, dynamic, rotation frozen (size from player.json, marked _guess)\n",
                body.widthPx, body.heightPx);
    std::printf("  portals   %s, speed x%.2f, exit offset %.1f px (portals.json, every value marked _guess)\n",
                transit.momentumMode.c_str(), transit.exitSpeedScale, transit.exitOffsetPx);
    std::printf("  locks     %s\n", g_lockToPlane ? "every body locked to the plane: position z, rotation x and y (S1)"
                                                  : "NONE - --unlocked, the solver as the first run measured it");

    std::printf("\n1. Out-of-plane drift over %d ticks\n", kDriftTicks);
    const char* drives[] = {"resting bodies", "crate pushed across the seam", "player walking"};
    for (bool sleep : {true, false}) {
        for (int d = 0; d < 3; ++d) {
            const Drift drift = MeasureDrift(scene, paths, body, static_cast<Drive>(d), sleep);
            const std::string what = std::string("drift, ") + drives[d] + (sleep ? ", sleep on" : ", sleep off");
            const std::string measured =
                "z " + Fixed(drift.zPx, 4) + " px, tilt " + Fixed(drift.tiltRad, 5) + " (" + drift.worst + ", " +
                std::to_string(drift.asleep) + "/" + std::to_string(drift.watched) + " asleep" +
                (drift.firstBreach > 0 ? ", over at tick " + std::to_string(drift.firstBreach) : std::string()) + ")";
            const bool pass = drift.zPx < kDriftLimitPx && drift.tiltRad < kTiltLimitRad;
            Row(what, measured, "< 1 px, < 0.01 rad", pass ? "PASS" : "FAIL");
        }
    }

    std::printf("\n2. Landing\n");
    const double surface = TopPx(scene, "platform_ent_895"); // the platform under main_char
    const double expectedRest = surface - body.heightPx * 0.5;
    const Landing landing = MeasureLanding(scene, paths, body, true);
    Row("landing, player dropped at main_char",
        "tick " + std::to_string(landing.tick) + ", rests at " + Fixed(landing.restYPx, 2) + " px (want " +
            Fixed(expectedRest, 2) + ")",
        "by tick 45, within 1 px",
        landing.tick >= 1 && landing.tick <= kLandingTicks &&
                std::fabs(landing.restYPx - expectedRest) <= kRestTolerancePx
            ? "PASS"
            : "FAIL");
    const Landing stripped = MeasureLanding(scene, paths, body, false);
    Row("landing mutation, every StaticBody2D removed",
        stripped.tick < 0 ? "never lands, at " + Fixed(stripped.restYPx, 0) + " px by tick 120"
                          : "LANDED at tick " + std::to_string(stripped.tick),
        "must not land", stripped.tick < 0 ? "PASS" : "FAIL");

    std::printf("\n3. Riders on a kinematic mover (F1) - measured, not judged\n");
    const struct {
        const char* what;
        glm::vec3 velocity;
    } moves[] = {
        {"rider, slab rising 1 m/s for 1 s", glm::vec3(0.0f, kDriveSpeed, 0.0f)},
        {"rider, slab sinking 1 m/s for 1 s", glm::vec3(0.0f, -kDriveSpeed, 0.0f)},
        {"rider, slab sliding sideways 1 m/s for 1 s", glm::vec3(kDriveSpeed, 0.0f, 0.0f)},
    };
    for (const auto& move : moves) {
        const Rider rider = MeasureRider(scene, paths, move.velocity);
        Row(move.what,
            "carried " + Fixed(rider.carried, 3) + ", gap " + Fixed(rider.lowGapPx, 2) + ".." +
                Fixed(rider.highGapPx, 2) + " px" + (rider.onTop ? "" : ", fell off"),
            "1.0 is carried", "measured");
    }

    std::printf("\n4. Triggers against the player (F2)\n");
    const int dynamicTicks = MeasureTrigger(scene, paths, body, false);
    Row("exit trigger, player dynamic", "reported on " + std::to_string(dynamicTicks) + " of 10 ticks", "> 0",
        dynamicTicks > 0 ? "PASS" : "FAIL");
    const int kinematicTicks = MeasureTrigger(scene, paths, body, true);
    Row("exit trigger, player kinematic", "reported on " + std::to_string(kinematicTicks) + " of 10 ticks",
        "0 if F2 holds", "measured");

    std::printf("\n5. Velocity through a portal\n");
    const Traversal traversal = MeasurePortal(scene, paths, transit);
    Row("traversal, from portals.json's numbers",
        "out at (" + Fixed(traversal.exitPositionPx.x, 1) + ", " + Fixed(traversal.exitPositionPx.y, 1) +
            ") px, v (" + Fixed(traversal.exitVelocityPx.x, 2) + ", " + Fixed(traversal.exitVelocityPx.y, 2) + ")",
        "data, _guess", "measured");
    Row("solver keeps the written velocity, one tick",
        "error " + Fixed(traversal.velocityErrorMs, 7) + " m/s, " + Fixed(traversal.positionErrorPx, 4) + " px",
        "< 1e-4 m/s", traversal.velocityErrorMs < kPortalToleranceMs ? "PASS" : "FAIL");

    std::printf("\n%d threshold(s) failed\n", g_failures);
    return 0;
}
