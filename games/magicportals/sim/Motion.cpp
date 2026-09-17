#include "sim/Motion.hpp"

#include "core/Json.hpp"
#include "sim/Tscn.hpp"

#include <algorithm>
#include <cmath>
#include <fstream>
#include <sstream>
#include <utility>

namespace MagicPortals::Motion {

namespace {

namespace Json = Supersonic::Json;

double Sign(double value) {
    // AngelScript's sign(): -1, 0 or 1.
    return value > 0.0 ? 1.0 : (value < 0.0 ? -1.0 : 0.0);
}

bool ReadRow(const std::string& path, const Json::Value& root, const char* name, Row& out, std::string& error) {
    const std::string where = path + ": " + name;
    if (!root.Has(name) || !root[name].IsObject()) {
        error = where + " is missing or not an object";
        return false;
    }
    const Json::Value& block = root[name];
    const auto number = [&](const char* key, double& into) -> bool {
        if (!block.Has(key) || !block[key].IsNumber()) {
            error = where + "." + key + " is missing or not a number";
            return false;
        }
        into = block[key].AsNumber();
        return true;
    };
    Row read;
    if (!number("speed", read.speed) || !number("stride", read.stride) || !number("axis_deg", read.axisDeg) ||
        !number("start_angle_from", read.startFrom) || !number("start_angle_to", read.startTo)) {
        return false;
    }
    if (!block.Has("vertical") || !block["vertical"].IsBool()) {
        error = where + ".vertical is missing or not true or false";
        return false;
    }
    read.vertical = block["vertical"].AsBool();
    if (!block.Has("entities") || !block["entities"].IsArray()) {
        error = where + ".entities is missing or not an array";
        return false;
    }
    for (const Json::Value& one : block["entities"].AsArray()) {
        // Without the extension: the callback is ETHCallback_ + the name less .ent,
        // so both spellings of a placement run it.
        if (!one.IsString() || one.AsString().empty() || one.AsString().find('.') != std::string::npos) {
            error = where + ".entities holds a value that is not a bare entity name";
            return false;
        }
        read.entities.push_back(one.AsString());
    }
    // Each of these would draw a motion that is not the script's: no entity moves
    // nothing; a speed of 0 never moves (sign(0) is 0 too); a stride at or below 0
    // moves nothing or turns the swing over; a range upside down is no range.
    if (read.entities.empty()) {
        error = where + ".entities names at least one entity";
        return false;
    }
    if (read.speed == 0.0 || !(read.stride > 0.0)) {
        error = where + ": speed is not 0 and stride is above 0";
        return false;
    }
    if (!(read.startTo >= read.startFrom)) {
        error = where + ": start_angle_to is not below start_angle_from";
        return false;
    }
    out = std::move(read);
    return true;
}

bool ReadPlaced(const std::string& path, const Json::Value& root, const Rules& rules, std::vector<Placed>& out,
                std::string& error) {
    if (!root.Has("placed") || !root["placed"].IsArray()) {
        error = path + ": placed is missing or not an array";
        return false;
    }
    std::vector<Placed> read;
    for (const Json::Value& block : root["placed"].AsArray()) {
        const std::string where = path + ": placed[" + std::to_string(read.size()) + "]";
        if (!block.IsObject()) {
            error = where + " is not an object";
            return false;
        }
        Placed row;
        row.entity = block["entity"].AsString();
        // Without the extension, as the crystal's and key's rows: both spellings of
        // a placement run ETHCallback_ + the name less .ent.
        if (row.entity.empty() || row.entity.find('.') != std::string::npos) {
            error = where + ".entity is not a bare entity name";
            return false;
        }
        // One callback an entity: a second row, or one the crystal or key row already
        // names, would give a placement two scripts where the original runs one.
        const bool taken = rules.crystal.Names(row.entity) || rules.key.Names(row.entity) ||
                           std::any_of(read.begin(), read.end(),
                                       [&row](const Placed& other) { return other.entity == row.entity; });
        if (taken) {
            error = where + ": " + row.entity + " already has a row";
            return false;
        }
        const Json::Value& linear = block["linear_motion"];
        if (linear.IsObject()) {
            row.moves = true;
            if (!linear.Has("vertical") || !linear["vertical"].IsBool()) {
                error = where + ".linear_motion.vertical is missing or not true or false";
                return false;
            }
            row.vertical = linear["vertical"].AsBool();
            const std::string axis = linear["axis"].AsString();
            if (axis != "node" && axis != "fixed") {
                error = where + ".linear_motion.axis is neither \"node\" nor \"fixed\"";
                return false;
            }
            row.axisFromNode = axis == "node";
            for (const auto& [key, into] : {std::pair<const char*, double*>{"axis_add_deg", &row.axisAddDeg},
                                            std::pair<const char*, double*>{"start_angle", &row.startAngle}}) {
                if (!linear.Has(key) || !linear[key].IsNumber()) {
                    error = where + ".linear_motion." + key + " is missing or not a number";
                    return false;
                }
                *into = linear[key].AsNumber();
            }
        } else if (!block.Has("linear_motion") || linear.GetType() != Json::Type::Null) {
            error = where + ".linear_motion is missing, or neither an object nor null";
            return false;
        }
        const Json::Value& alpha = block["set_alpha"];
        if (alpha.IsNumber()) {
            row.setsAlpha = true;
            row.alpha = alpha.AsNumber();
            if (!(row.alpha >= 0.0 && row.alpha <= 1.0)) {
                error = where + ".set_alpha is not within 0..1";
                return false;
            }
        } else if (!block.Has("set_alpha") || alpha.GetType() != Json::Type::Null) {
            error = where + ".set_alpha is missing, or neither a number nor null";
            return false;
        }
        if (!block.Has("spin_deg_s") || !block["spin_deg_s"].IsNumber()) {
            error = where + ".spin_deg_s is missing or not a number";
            return false;
        }
        row.spinDegPerS = block["spin_deg_s"].AsNumber();
        // A row that neither moves, nor sets an alpha, nor turns is no callback the
        // port has to run: most likely a typo for one that does.
        if (!row.moves && !row.setsAlpha && row.spinDegPerS == 0.0) {
            error = where + ": " + row.entity + " neither moves, sets an alpha nor turns";
            return false;
        }
        read.push_back(std::move(row));
    }
    out = std::move(read);
    return true;
}

} // namespace

bool Row::Names(const std::string& bareEntity) const {
    return std::find(entities.begin(), entities.end(), bareEntity) != entities.end();
}

const Placed* Rules::FindPlaced(const std::string& bareEntity) const {
    for (const Placed& row : placed) {
        if (row.entity == bareEntity) return &row;
    }
    return nullptr;
}

bool LoadRules(const std::string& path, Rules& out, std::string& error) {
    std::ifstream file(path, std::ios::binary);
    if (!file) {
        error = path + ": cannot read";
        return false;
    }
    std::ostringstream text;
    text << file.rdbuf();

    // Named, not a temporary: the parser keeps a reference to what it reads.
    const std::string content = text.str();
    Json::Parser parser(content);
    Json::Value root;
    if (!parser.Parse(root)) {
        error = path + ": " + parser.Error();
        return false;
    }
    Rules read;
    const Json::Value& linear = root["linear_motion"];
    for (const auto& [key, into] : {std::pair<const char*, double*>{"frame_cap_ms", &read.frameCapMs},
                                    std::pair<const char*, double*>{"wrap_rad", &read.wrapRad}}) {
        if (!linear.Has(key) || !linear[key].IsNumber()) {
            error = path + ": linear_motion." + key + " is missing or not a number";
            return false;
        }
        *into = linear[key].AsNumber();
    }
    // No cap lets one long frame throw a swing anywhere on its circle; no turn to
    // wrap at lets the angle grow for ever.
    if (!(read.frameCapMs > 0.0) || !(read.wrapRad > 0.0)) {
        error = path + ": linear_motion.frame_cap_ms and linear_motion.wrap_rad are above zero";
        return false;
    }
    if (!ReadRow(path, root, "crystal", read.crystal, error) || !ReadRow(path, root, "key", read.key, error) ||
        !ReadPlaced(path, root, read, read.placed, error)) {
        return false;
    }
    out = std::move(read);
    return true;
}

Linear Start(const Row& row, double startAngle) {
    Linear motion;
    motion.speed = row.speed;
    motion.stride = row.stride;
    motion.vertical = row.vertical;
    motion.axisDeg = row.axisDeg;
    motion.startAngle = startAngle;
    return motion;
}

void Advance(const Rules& rules, Linear& motion, double frameMs) {
    // linearMotion ins 1-39: the first call writes originalPos and the start angle.
    if (!motion.started) {
        motion.angle = motion.startAngle;
        motion.started = true;
    }
    // Ins 53-68: unitsPerSecond(speed), which is speed x min(200, frame) / 1000.
    motion.angle += motion.speed * std::min(rules.frameCapMs, std::max(0.0, frameMs)) / 1000.0;
    // Ins 80-98: CMPf angle, PI x 2f; JNP: only when strictly above it, and once.
    if (motion.angle > rules.wrapRad) motion.angle -= rules.wrapRad;
}

glm::dvec2 OffsetPx(const Linear& motion) {
    if (!motion.started) return glm::dvec2(0.0);
    // Ins 112-129: cos(angle) x stride x sign(speed).
    const double offset = std::cos(motion.angle) * motion.stride * Sign(motion.speed);
    // Ins 131-178: the vector along its axis, times rotateZ(degreeToRadian(angle))
    // as multiply(vector3, matrix4x4) takes it: (x cos a + y sin a, -x sin a + y cos a)
    // (Ethanon GameMath.h RotateZ and Multiply(Vector3, Matrix4x4)). So a positive
    // angle turns the axis counter-clockwise on a +y-down screen.
    const glm::dvec2 along = motion.vertical ? glm::dvec2(0.0, offset) : glm::dvec2(offset, 0.0);
    const double a = motion.axisDeg * 3.14159265358979323846 / 180.0;
    const double c = std::cos(a);
    const double s = std::sin(a);
    // Ins 180-208: x getScale(), which is 1 in the port's units.
    return glm::dvec2(along.x * c + along.y * s, -along.x * s + along.y * c);
}

bool FromNode(const Placed& row, const Tscn::Node& node, Linear& out, std::string& error) {
    Linear read;
    // linearMotion ins 41-51 and 100-110: GetFloat('speed') and GetFloat('stride'),
    // the node's own custom data.
    const Tscn::Value* speed = node.Meta("speed");
    const Tscn::Value* stride = node.Meta("stride");
    if (speed == nullptr || !speed->AsNumber(read.speed) || stride == nullptr || !stride->AsNumber(read.stride)) {
        error = node.name + " has no speed and stride for its linearMotion";
        return false;
    }
    double rotation = 0.0;
    if (const Tscn::Value* turned = node.Find("rotation");
        turned != nullptr && (turned->kind != Tscn::Value::Kind::Number || !turned->AsNumber(rotation))) {
        error = node.name + "'s rotation is not a number";
        return false;
    }
    read.vertical = row.vertical;
    // GetAngle() is Ethanon's angle, which the converter wrote negated in radians.
    const double nodeAngleDeg = -rotation * 180.0 / 3.14159265358979323846;
    read.axisDeg = (row.axisFromNode ? nodeAngleDeg : 0.0) + row.axisAddDeg;
    read.startAngle = row.startAngle;
    out = read;
    return true;
}

void Advance(const Rules& rules, Turn& turn, double frameMs) {
    // AddToAngle(g_timeManager.unitsPerSecond(degPerS)): the frame capped, x m_factor.
    turn.turnedDeg += turn.degPerS * std::min(rules.frameCapMs, std::max(0.0, frameMs)) / 1000.0;
    // Ethanon's m_angle is never wrapped; one turn off draws the same angle and keeps
    // the float the quad is handed small.
    if (turn.turnedDeg >= 360.0 || turn.turnedDeg <= -360.0) turn.turnedDeg = std::fmod(turn.turnedDeg, 360.0);
}

std::uint32_t LevelSeed(std::uint32_t base, const std::string& level) {
    std::uint32_t hash = 2166136261u;
    for (const char c : level) {
        hash ^= static_cast<std::uint8_t>(c);
        hash *= 16777619u;
    }
    return base ^ hash;
}

double Phases::Next(const Row& row) {
    if (!(row.startTo > row.startFrom)) return row.startFrom;
    std::uniform_real_distribution<double> spread(row.startFrom, row.startTo);
    return spread(m_engine);
}

} // namespace MagicPortals::Motion
