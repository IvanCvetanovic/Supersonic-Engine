#include "sim/Sky.hpp"

#include "sim/Roles.hpp"

#include "core/Json.hpp"

#include <algorithm>
#include <cmath>
#include <fstream>
#include <sstream>
#include <utility>

namespace MagicPortals::Sky {

namespace {

namespace Json = Supersonic::Json;

bool PositionOf(const Tscn::Node& node, glm::dvec2& out) {
    const Tscn::Value* position = node.Find("position");
    if (position == nullptr || position->kind != Tscn::Value::Kind::Vector2) return false;
    out = glm::dvec2(position->numbers[0], position->numbers[1]);
    return true;
}

// A level entity's float as ETHEntity::GetFloat answers it: the number, or 0 for
// a name the entity does not carry.
double FloatOf(const Tscn::Node& node, const std::string& key) {
    double value = 0.0;
    const Tscn::Value* found = node.Meta(key);
    if (found == nullptr || !found->AsNumber(value)) return 0.0;
    return value;
}

} // namespace

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
    const Json::Value& block = root["static_sky"];
    const auto number = [&](const char* key, double& into) -> bool {
        if (!block.Has(key) || !block[key].IsNumber()) {
            error = path + ": static_sky." + key + " is missing or not a number";
            return false;
        }
        into = block[key].AsNumber();
        return true;
    };
    const auto string = [&](const char* key, std::string& into) -> bool {
        if (!block.Has(key) || !block[key].IsString() || block[key].AsString().empty()) {
            error = path + ": static_sky." + key + " is missing, not a string or empty";
            return false;
        }
        into = block[key].AsString();
        return true;
    };

    Rules read;
    if (!string("properties_entity", read.propertiesEntity) || !string("space_bg_key", read.spaceBgKey) ||
        !string("satellite_name", read.satelliteName) || !string("scroll_key", read.scrollKey) ||
        !number("screen_pitch", read.screenPitch) || !number("frame_cap_ms", read.frameCapMs)) {
        return false;
    }
    if (!block.Has("entity_names") || !block["entity_names"].IsArray()) {
        error = path + ": static_sky.entity_names is missing or not an array";
        return false;
    }
    for (const Json::Value& one : block["entity_names"].AsArray()) {
        if (!one.IsString() || one.AsString().empty()) {
            error = path + ": static_sky.entity_names holds a value that is not a name";
            return false;
        }
        read.skyNames.push_back(one.AsString());
    }
    // Each of these would draw a sky that looks placed and is not: no names finds
    // no sky, so every level keeps the position its editor left; a pitch of 0 pins
    // the sky's centre to the view's corner; no frame cap lets one long frame
    // throw a scrolling strip past its wrap.
    if (read.skyNames.empty()) {
        error = path + ": static_sky.entity_names names at least one entity";
        return false;
    }
    if (read.screenPitch <= 0.0 || read.frameCapMs <= 0.0) {
        error = path + ": static_sky.screen_pitch and static_sky.frame_cap_ms are above zero";
        return false;
    }
    out = std::move(read);
    return true;
}

bool Build(const Rules& rules, const Tscn::Scene& scene, const std::vector<Sprites::Sprite>& sprites,
           double viewHeightPx, Controller& out, std::string& error) {
    Controller built;

    // Game::preLoop's branch: a space level's sky is SpaceSky's, and StaticSky is
    // never made. readProperties asks only whether the first `properties` carries
    // space_bg at all (CheckCustomData != 0), not what it holds.
    for (const Tscn::Node& node : scene.nodes) {
        if (node.parent != "." || Roles::EntityName(node) != rules.propertiesEntity) continue;
        if (node.Meta(rules.spaceBgKey) != nullptr) {
            out = Controller{};
            return true;
        }
        break;
    }

    // GetEntityArray for each name in turn, each exact. The original walks its
    // buckets; the port walks the file. No level places two skies, so the two
    // orders never differ in play.
    for (const std::string& name : rules.skyNames) {
        for (const Tscn::Node& node : scene.nodes) {
            if (node.parent != "." || Roles::EntityName(node) != name) continue;
            const auto sprite = std::find_if(sprites.begin(), sprites.end(),
                                             [&node](const Sprites::Sprite& s) { return s.node == node.name; });
            if (sprite == sprites.end() || sprite->sizePx.y <= 0.0) {
                error = "the sky " + node.name + " has no picture to size";
                return false;
            }
            Layer sky;
            sky.node = node.name;
            sky.imagePx = sprite->sizePx;
            sky.scrollValue = FloatOf(node, rules.scrollKey);
            built.skies.push_back(std::move(sky));
        }
    }

    // scaleSky, in its own order: the scale, then m_width and m_scroll from the
    // sky just scaled - one member each, so the last sky's stand - then the pivot
    // a scrolling strip's ends take.
    const std::size_t count = built.skies.size();
    for (std::size_t t = 0; t < count; ++t) {
        Layer& sky = built.skies[t];
        sky.scale = viewHeightPx / sky.imagePx.y;
        built.widthPx = sky.imagePx.x * sky.scale;
        built.scrollValue = sky.scrollValue;
        built.scroll = sky.scrollValue > 0.0;
        sky.scrollX = 0.0;
        if (!built.scroll) continue;
        // SetPivotAdjust(size * (+-1, 0)) with the size read BEFORE the scale. It
        // divides by the new scale and the draw multiplies by it again, so what
        // moves the picture is that unscaled width. The last call wins on a lone
        // sky, which is both the first and the last.
        if (t == 0) sky.pivotX = sky.imagePx.x;
        if (t + 1 == count) sky.pivotX = -sky.imagePx.x;
    }

    for (const Tscn::Node& node : scene.nodes) {
        if (node.parent != "." || Roles::EntityName(node) != rules.satelliteName) continue;
        glm::dvec2 atPx(0.0);
        if (!PositionOf(node, atPx)) continue;
        built.hasSatellite = true;
        built.satelliteNode = node.name;
        built.satelliteOriginalPx = atPx;
        break;
    }

    built.running = count > 0 || built.hasSatellite;
    out = std::move(built);
    return true;
}

glm::dvec2 PositionPx(const Rules& rules, const Controller& sky, std::size_t t, const glm::dvec2& viewCentrePx,
                      const glm::dvec2& viewPx) {
    const Layer& layer = sky.skies[t];
    const glm::dvec2 size = layer.imagePx * layer.scale;
    glm::dvec2 pitch = size * rules.screenPitch;
    if (!sky.scroll) {
        pitch = viewPx * rules.screenPitch;
    } else {
        if (t == 0) pitch.x += size.x;
        if (t + 1 == sky.skies.size()) pitch.x -= size.x;
    }
    const glm::dvec2 originalPos = pitch + glm::dvec2(sky.widthPx * static_cast<double>(t), 0.0);
    // GetCameraPos() is the corner; the port's camera keeps its centre.
    const glm::dvec2 cornerPx = viewCentrePx - viewPx * 0.5;
    return cornerPx + originalPos + glm::dvec2(layer.scrollX, 0.0);
}

glm::dvec2 DrawnCentrePx(const Rules& rules, const Controller& sky, std::size_t t, const glm::dvec2& viewCentrePx,
                         const glm::dvec2& viewPx) {
    return PositionPx(rules, sky, t, viewCentrePx, viewPx) - glm::dvec2(sky.skies[t].pivotX, 0.0);
}

glm::dvec2 DrawnSizePx(const Controller& sky, std::size_t t) {
    const Layer& layer = sky.skies[t];
    return layer.imagePx * layer.scale;
}

glm::dvec2 SatellitePx(const Controller& sky, const glm::dvec2& viewCentrePx, const glm::dvec2& viewPx) {
    return viewCentrePx - viewPx * 0.5 + sky.satelliteOriginalPx;
}

void Advance(const Rules& rules, Controller& sky, double dtS) {
    if (!sky.scroll) return;
    // scaledUnitsPerSecond(-m_scrollValue): the frame capped at 200 ms, in the
    // level's own units once g_scale is folded away.
    const double frameMs = std::min(rules.frameCapMs, dtS * 1000.0);
    const double move = -sky.scrollValue * frameMs / 1000.0;
    for (Layer& layer : sky.skies) {
        layer.scrollX += move;
        if (std::fabs(layer.scrollX) >= sky.widthPx) layer.scrollX = 0.0;
    }
}

} // namespace MagicPortals::Sky
