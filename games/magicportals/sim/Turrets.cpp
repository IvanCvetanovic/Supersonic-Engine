#include "sim/Turrets.hpp"

#include "sim/Trigger.hpp"
#include "sim/Units.hpp"

#include "core/Json.hpp"

#include <cmath>
#include <fstream>
#include <sstream>
#include <vector>

namespace MagicPortals::Turrets {

namespace {

namespace Json = Supersonic::Json;

// A [x, y] pair, as turrets.json writes both of its.
bool ReadPair(const Json::Value& value, glm::dvec2& out) {
    if (!value.IsArray() || value.AsArray().size() != 2) return false;
    if (!value.AsArray()[0].IsNumber() || !value.AsArray()[1].IsNumber()) return false;
    out = glm::dvec2(value.AsArray()[0].AsNumber(0.0), value.AsArray()[1].AsNumber(0.0));
    return true;
}

// A node's metadata as a number, or `fallback`. The converter writes metadata as
// STRINGS - metadata/dirX = "1.2" - which Tscn::Value::AsNumber reads.
double Meta(const Tscn::Node& node, const char* key, double fallback) {
    const Tscn::Value* value = node.Meta(key);
    double read = 0.0;
    if (value == nullptr || !value->AsNumber(read)) return fallback;
    return read;
}

} // namespace

bool LoadRules(const std::string& path, Rules& out, std::string& error) {
    out = Rules{};

    std::ifstream file(path, std::ios::binary);
    if (!file) {
        error = path + ": cannot open";
        return false;
    }
    std::ostringstream buffer;
    buffer << file.rdbuf();

    // Named, not a temporary: Json::Parser holds its text by reference.
    const std::string text = buffer.str();
    Json::Parser parser(text);
    Json::Value root;
    if (!parser.Parse(root)) {
        error = path + ": " + parser.Error();
        return false;
    }

    const Json::Value& fireball = root["fireball"];
    const Json::Value& stride = root["stride"];
    const Json::Value& cull = root["cull"];
    if (!fireball.IsObject() || !stride.IsObject() || !cull.IsObject()) {
        error = path + ": fireball, stride and cull are each an object";
        return false;
    }

    Rules read;
    read.speedPx = fireball["speed_px_s"].AsNumber(0.0);
    read.defaultStrideMs = stride["default_ms"].AsNumber(0.0);
    if (!ReadPair(fireball["hit_px"], read.hitPx) || !ReadPair(cull["margin_px"], read.cullMarginPx)) {
        error = path + ": hit_px and margin_px are each two numbers";
        return false;
    }
    // A speed of nothing is a fireball that never leaves the gargoyle's mouth,
    // and a stride of nothing is a carranca that fires every tick. Both would
    // look like a level that works.
    if (read.speedPx <= 0.0 || read.defaultStrideMs <= 0.0) {
        error = path + ": speed_px_s and default_ms are both above zero";
        return false;
    }
    if (read.hitPx.x <= 0.0 || read.hitPx.y <= 0.0) {
        error = path + ": hit_px is above zero in both axes";
        return false;
    }

    out = read;
    return true;
}

std::vector<Fireball> State::Fire(float dt) {
    std::vector<Fireball> now;
    const double ms = static_cast<double>(dt) * 1000.0;
    for (Turret& turret : turrets) {
        turret.elapsedMs += ms;
        // PASSES the stride, as carrancaCallback's CMPu does, rather than
        // reaching it.
        if (turret.elapsedMs <= turret.strideMs) continue;

        // RESET, not a subtraction. The original writes zero, so a long frame's
        // overshoot is not carried into the next shot; subtracting the stride
        // would make a carranca fire twice in quick succession to catch up.
        turret.elapsedMs = 0.0;

        Fireball made;
        made.name = turret.name + "#" + std::to_string(++turret.fired);
        // At the carranca's own position. The remake spawns one 24 px along the
        // direction (hazards.gd:365); addFireball uses GetPositionXY and nothing
        // else, so that offset is the remake's own.
        made.atPx = turret.atPx;
        made.velocityPx = turret.directionPx * rules.speedPx;
        fireballs.push_back(made);
        now.push_back(made);
    }
    return now;
}

void State::Tick(entt::registry& registry, entt::entity player, float dt) {
    const glm::dvec2 lowPx = -rules.cullMarginPx;
    const glm::dvec2 highPx = boundsPx + rules.cullMarginPx;

    for (Fireball& ball : fireballs) ball.atPx += ball.velocityPx * static_cast<double>(dt);
    std::erase_if(fireballs, [&lowPx, &highPx](const Fireball& ball) {
        return ball.atPx.x < lowPx.x || ball.atPx.y < lowPx.y || ball.atPx.x > highPx.x || ball.atPx.y > highPx.y;
    });

    if (player == entt::null || !registry.valid(player) || playerKilled) return;

    // Against the player's own collider rather than a box around its centre:
    // Trigger is exact in the plane, and a capsule is what the character has.
    const glm::vec2 half(Units::ToMetres(rules.hitPx.x * 0.5), Units::ToMetres(rules.hitPx.y * 0.5));
    for (const Fireball& ball : fireballs) {
        // addFireball's killMainCharacter. A fire diamond's conversion passes it
        // clear and the contact callback bails out on a character when it is, so
        // one of these kills and the other cannot. Turrets.hpp has the decode.
        if (!ball.killsPlayer) continue;
        Trigger::Box box;
        const glm::vec3 centre = Units::ToWorld(ball.atPx.x, ball.atPx.y);
        box.centre = glm::vec2(centre.x, centre.y);
        box.half = half;
        if (!Trigger::Overlaps(registry, player, box)) continue;
        playerKilled = true;
        killedBy = ball.name;
        return;
    }
}

bool Find(const Tscn::Scene& scene, const Roles::Table& roles, const Rules& rules, State& out, std::string& error) {
    out = State{};
    out.rules = rules;

    for (const Tscn::Node& node : scene.nodes) {
        if (node.parent != ".") continue;
        const std::string role = Roles::RoleOf(roles, node);
        const Tscn::Value* position = node.Find("position");
        const bool placed = position != nullptr && position->kind == Tscn::Value::Kind::Vector2;
        const glm::dvec2 atPx = placed ? glm::dvec2(position->numbers[0], position->numbers[1]) : glm::dvec2(0.0);

        if (role == Roles::kLevelBounds && placed) out.boundsPx = atPx;
        if (role != Roles::kTurret) continue;

        Turret turret;
        turret.name = node.name;
        turret.atPx = atPx;

        // The remake's defaults for a node that names neither (hazards.gd:340-344).
        glm::dvec2 direction(Meta(node, "dirX", -1.0), Meta(node, "dirY", 0.0));
        if (direction.x == 0.0 && direction.y == 0.0) direction = glm::dvec2(-1.0, 0.0);
        const double length = std::sqrt(direction.x * direction.x + direction.y * direction.y);
        // Normalised, and turrets.json records what the magnitude might have
        // meant: level0a's carranca carries a dirX of 1.2, and nothing decoded
        // reads that back.
        turret.directionPx = direction / length;

        turret.strideMs = Meta(node, "stride", rules.defaultStrideMs);
        if (turret.strideMs <= 0.0) {
            error = node.name + " has a stride of " + std::to_string(turret.strideMs);
            return false;
        }
        turret.elapsedMs = Meta(node, "startStride", 0.0);

        out.turrets.push_back(turret);
    }
    return true;
}

} // namespace MagicPortals::Turrets
