// The sky the original pins to the camera: StaticSky, as sim/Sky reads it.
//
// What is pinned, and from where:
//
//   the rules    sky.json's names and numbers, each decoded (sky.json's _source):
//                `sky` and `sky.ent`, `satellite`, the `scroll` float, the 0.5
//                pitch, the 200 ms frame cap, and space_bg on `properties`.
//   the centre   a still sky's entity is placed at the camera's corner plus half
//                the view, whatever the level said - so it IS the view's centre,
//                wherever the camera is. The original's frames put it at (640, 360)
//                of 1280 x 720, to within 1 px, on 16 levels of 16 (sky.json's
//                static_sky._measured).
//   the size     view height / picture height, and m_width the last sky's width.
//   the strip    the t-indexed arithmetic of a scrolling strip, its end pivots, and
//                its scroll: moved after the frame's positions, capped at 200 ms a
//                frame, back to 0 once it has run a whole width.
//   space        a level carrying space_bg at all builds no StaticSky.
//   the census   over the 128 converted levels: 95 one sky, 19 space, 13 neither,
//                one satellite alone; no sky scrolls; every sky's scale is 1.
//   the tier     a sky is drawn from the file the original draws (tiers.json,
//                step 66): the fullhd sky.png, icy_sky.png, red_sky.png and
//                sky_purple.png are 1024 x 512 over 455 x 256 1x files, so 28
//                skies grow to 512 u wide, and stay one screen tall and centred.
//
// The arithmetic is pure and runs anywhere. The census reads the converted levels
// and their art from outside this repository, and is skipped without them, never
// with 77.

#include "TestHarness.hpp"

#include "sim/Roles.hpp"
#include "sim/Sky.hpp"
#include "sim/Sprites.hpp"
#include "sim/Tiers.hpp"
#include "sim/Tscn.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <string>
#include <system_error>
#include <vector>

using namespace MagicPortals;

namespace {

const std::string kLevels = MAGICPORTALS_LEVELS_DIR;
const std::string kPortData = MAGICPORTALS_PORT_DATA_DIR;

// The port's view at 1280 x 720: 256 px tall, and 256 * 16 / 9 wide.
constexpr double kViewHeight = 256.0;
const glm::dvec2 kView(256.0 * 1280.0 / 720.0, 256.0);

bool Near(const glm::dvec2& a, const glm::dvec2& b, double eps = 1e-9) {
    return std::fabs(a.x - b.x) <= eps && std::fabs(a.y - b.y) <= eps;
}

std::string Px(const glm::dvec2& p) {
    return "(" + std::to_string(p.x) + ", " + std::to_string(p.y) + ")";
}

Tiers::Rules TheTiers() {
    Tiers::Rules rules;
    std::string error;
    CHECK_MSG(Tiers::LoadRules(kPortData + "/tiers.json", rules, error), error);
    return rules;
}

Sky::Rules TheRules() {
    Sky::Rules rules;
    std::string error;
    CHECK_MSG(Sky::LoadRules(kPortData + "/sky.json", rules, error), error);
    return rules;
}

// A top-level entity node, as the converter writes one.
Tscn::Node Entity(const std::string& name, const std::string& entityName, const glm::dvec2& at) {
    Tscn::Node node;
    node.name = name;
    node.type = "Node2D";
    node.parent = ".";
    node.path = name;
    Tscn::Property position;
    position.key = "position";
    position.value.kind = Tscn::Value::Kind::Vector2;
    position.value.numbers = {at.x, at.y};
    node.properties.push_back(position);
    Tscn::Property entity;
    entity.key = "metadata/entity_name";
    entity.value.kind = Tscn::Value::Kind::String;
    entity.value.text = entityName;
    node.properties.push_back(entity);
    return node;
}

void Meta(Tscn::Node& node, const std::string& key, const std::string& text) {
    Tscn::Property property;
    property.key = "metadata/" + key;
    property.value.kind = Tscn::Value::Kind::String;
    property.value.text = text;
    node.properties.push_back(property);
}

Sprites::Sprite Picture(const Tscn::Node& node, const glm::dvec2& sizePx) {
    Sprites::Sprite sprite;
    sprite.node = node.name;
    sprite.sizePx = sizePx;
    sprite.atPx = glm::dvec2(node.Find("position")->numbers[0], node.Find("position")->numbers[1]);
    return sprite;
}

void TheRulesAreSkyJsons() {
    const Sky::Rules rules = TheRules();
    CHECK_EQ(rules.skyNames.size(), static_cast<std::size_t>(2));
    CHECK_MSG(rules.skyNames.size() == 2 && rules.skyNames[0] == "sky" && rules.skyNames[1] == "sky.ent",
              "GetEntityArray(name) and GetEntityArray(name + '.ent'), name 'sky'");
    CHECK(rules.satelliteName == "satellite");
    CHECK(rules.scrollKey == "scroll");
    CHECK(rules.propertiesEntity == "properties");
    CHECK(rules.spaceBgKey == "space_bg");
    CHECK(rules.screenPitch == 0.5);
    CHECK(rules.frameCapMs == 200.0);

    // A file that would pin the sky's centre to the view's corner is refused.
    const std::filesystem::path broken = std::filesystem::temp_directory_path() / "supersonic-test-mp-sky.json";
    {
        std::ofstream out(broken, std::ios::binary);
        out << R"({"static_sky": {"properties_entity": "properties", "space_bg_key": "space_bg",
                  "entity_names": ["sky"], "satellite_name": "satellite", "scroll_key": "scroll",
                  "screen_pitch": 0.0, "frame_cap_ms": 200.0}})";
    }
    Sky::Rules refused;
    std::string error;
    CHECK_MSG(!Sky::LoadRules(broken.string(), refused, error) && error.find("screen_pitch") != std::string::npos,
              "a pitch of 0 is refused: " + error);
    std::error_code ec;
    std::filesystem::remove(broken, ec);
}

void AStillSkyIsTheViewsCentreWhereverTheCameraIs() {
    const Sky::Rules rules = TheRules();
    // level0's sky: the level puts it 2 u right and 8 u up of the view's centre.
    Tscn::Scene scene;
    scene.nodes.push_back(Entity("sky_ent_171", "sky.ent", {229.5, 120.0}));
    const std::vector<Sprites::Sprite> sprites = {Picture(scene.nodes[0], {455.0, 256.0})};
    Sky::Controller sky;
    std::string error;
    CHECK_MSG(Sky::Build(rules, scene, sprites, kViewHeight, sky, error), error);
    CHECK(sky.running);
    CHECK_EQ(sky.skies.size(), static_cast<std::size_t>(1));
    if (sky.skies.size() != 1) return;
    CHECK(!sky.scroll);
    CHECK(sky.skies[0].scale == 1.0);
    CHECK(sky.widthPx == 455.0);
    CHECK(Sky::DrawnSizePx(sky, 0) == glm::dvec2(455.0, 256.0));
    CHECK(!sky.hasSatellite);

    // The view's centre at the level's corner, then scrolled right and down: the
    // sky's entity and its picture's centre are the view's centre every time.
    const glm::dvec2 cameras[] = {kView * 0.5, {540.5, 128.0}, {227.5 + 313.0, 128.0 + 126.0}};
    for (const glm::dvec2& camera : cameras) {
        const glm::dvec2 at = Sky::PositionPx(rules, sky, 0, camera, kView);
        CHECK_MSG(Near(at, camera), "camera " + Px(camera) + ": the sky at " + Px(at));
        CHECK_MSG(Near(Sky::DrawnCentrePx(rules, sky, 0, camera, kView), camera), "and its picture's centre too");
    }
    // At the level's corner that is 1.944 u left of and 8 u below where level0
    // puts it - which is where the original's frame of 1-01 has it.
    const glm::dvec2 moved = Sky::PositionPx(rules, sky, 0, kView * 0.5, kView) - glm::dvec2(229.5, 120.0);
    CHECK_MSG(Near(moved, {256.0 * 1280.0 / 720.0 * 0.5 - 229.5, 8.0}), "moved by " + Px(moved));
    // A still sky has nothing to advance.
    Sky::Advance(rules, sky, 1.0);
    CHECK(sky.skies[0].scrollX == 0.0);
    CHECK_MSG(Near(Sky::PositionPx(rules, sky, 0, kView * 0.5, kView), kView * 0.5), "and has not moved");
}

void TheSkyIsAsTallAsTheView() {
    const Sky::Rules rules = TheRules();
    Tscn::Scene scene;
    scene.nodes.push_back(Entity("sky_1", "sky", {256.0, 128.0}));
    // A 512 x 256 picture (satellite_sky.png) under a view twice the level's
    // height: scale 2, and m_width the scaled width.
    const std::vector<Sprites::Sprite> sprites = {Picture(scene.nodes[0], {512.0, 256.0})};
    Sky::Controller sky;
    std::string error;
    CHECK_MSG(Sky::Build(rules, scene, sprites, 512.0, sky, error), error);
    if (sky.skies.size() != 1) {
        CHECK(false);
        return;
    }
    CHECK(sky.skies[0].scale == 2.0);
    CHECK(sky.widthPx == 1024.0);
    CHECK(Sky::DrawnSizePx(sky, 0) == glm::dvec2(1024.0, 512.0));
}

void ASpaceLevelBuildsNoStaticSky() {
    const Sky::Rules rules = TheRules();
    // readProperties asks only whether space_bg is there: "0" counts as set.
    for (const char* value : {"1", "0"}) {
        Tscn::Scene scene;
        scene.nodes.push_back(Entity("properties_843", "properties", {31.0, 24.0}));
        Meta(scene.nodes[0], "space_bg", value);
        scene.nodes.push_back(Entity("sky_1", "sky", {227.5, 128.0}));
        const std::vector<Sprites::Sprite> sprites = {Picture(scene.nodes[1], {455.0, 256.0})};
        Sky::Controller sky;
        sky.running = true;
        std::string error;
        CHECK_MSG(Sky::Build(rules, scene, sprites, kViewHeight, sky, error), error);
        CHECK_MSG(!sky.running && sky.skies.empty(), std::string("space_bg = ") + value + ": SpaceSky's, not this");
    }
    // Without it, the same scene's sky is StaticSky's.
    Tscn::Scene scene;
    scene.nodes.push_back(Entity("properties_843", "properties", {31.0, 24.0}));
    Meta(scene.nodes[0], "scrolling_sky", "1");
    scene.nodes.push_back(Entity("sky_1", "sky", {227.5, 128.0}));
    const std::vector<Sprites::Sprite> sprites = {Picture(scene.nodes[1], {455.0, 256.0})};
    Sky::Controller sky;
    std::string error;
    CHECK_MSG(Sky::Build(rules, scene, sprites, kViewHeight, sky, error), error);
    CHECK_MSG(sky.running && sky.skies.size() == 1, "a level without space_bg builds StaticSky");
    // scrolling_sky is a different variable from a sky's own scroll, and nothing
    // in the original reads it.
    CHECK_MSG(!sky.scroll, "properties' scrolling_sky does not scroll the sky");
}

void OnlyTheExactNamesAreSkies() {
    const Sky::Rules rules = TheRules();
    Tscn::Scene scene;
    scene.nodes.push_back(Entity("space_sky_2002", "space_sky", {256.0, 128.0}));
    scene.nodes.push_back(Entity("sky_dark_1", "sky_dark.ent", {227.5, 128.0}));
    scene.nodes.push_back(Entity("wall_1", "wall04.ent", {218.0, 6.0}));
    std::vector<Sprites::Sprite> sprites;
    for (const Tscn::Node& node : scene.nodes) sprites.push_back(Picture(node, {455.0, 256.0}));
    Sky::Controller sky;
    std::string error;
    CHECK_MSG(Sky::Build(rules, scene, sprites, kViewHeight, sky, error), error);
    CHECK_MSG(!sky.running && sky.skies.empty(), "space_sky, sky_dark.ent and a wall are not StaticSky's");
}

void ASkyWithoutAPictureIsRefused() {
    const Sky::Rules rules = TheRules();
    Tscn::Scene scene;
    scene.nodes.push_back(Entity("sky_1", "sky", {227.5, 128.0}));
    Sky::Controller sky;
    std::string error;
    CHECK_MSG(!Sky::Build(rules, scene, {}, kViewHeight, sky, error) && error.find("sky_1") != std::string::npos,
              "a sky with no picture cannot be sized: " + error);
}

void TheSatelliteKeepsItsPlaceOnTheScreen() {
    const Sky::Rules rules = TheRules();
    // level14b's: a satellite and no sky.
    Tscn::Scene scene;
    scene.nodes.push_back(Entity("satellite_1166", "satellite", {140.0, 182.0}));
    const std::vector<Sprites::Sprite> sprites = {Picture(scene.nodes[0], {128.0, 128.0})};
    Sky::Controller sky;
    std::string error;
    CHECK_MSG(Sky::Build(rules, scene, sprites, kViewHeight, sky, error), error);
    CHECK(sky.running && sky.skies.empty());
    CHECK(sky.hasSatellite && sky.satelliteNode == "satellite_1166");
    CHECK(sky.satelliteOriginalPx == glm::dvec2(140.0, 182.0));
    // The camera's corner plus where the level put it.
    CHECK_MSG(Near(Sky::SatellitePx(sky, kView * 0.5, kView), {140.0, 182.0}), "at the level's corner, unmoved");
    const glm::dvec2 camera(800.0, 300.0);
    const glm::dvec2 at = Sky::SatellitePx(sky, camera, kView);
    CHECK_MSG(Near(at, camera - kView * 0.5 + glm::dvec2(140.0, 182.0)), "moved with the camera: " + Px(at));
    // SeekEntity: the first, and not `satellite.ent`.
    Tscn::Scene two;
    two.nodes.push_back(Entity("satellite_ent_1", "satellite.ent", {10.0, 10.0}));
    two.nodes.push_back(Entity("satellite_2", "satellite", {20.0, 20.0}));
    two.nodes.push_back(Entity("satellite_3", "satellite", {30.0, 30.0}));
    Sky::Controller other;
    CHECK_MSG(Sky::Build(rules, two, {}, kViewHeight, other, error), error);
    CHECK_MSG(other.hasSatellite && other.satelliteNode == "satellite_2", "the first entity named exactly satellite");
}

// A three-sky strip, which no level places, as StaticSky would lay it out.
Sky::Controller Strip(const Sky::Rules& rules, const char* scroll) {
    Tscn::Scene scene;
    std::vector<Sprites::Sprite> sprites;
    for (int i = 0; i < 3; ++i) {
        scene.nodes.push_back(Entity("sky_" + std::to_string(i), "sky", {100.0 * i, 0.0}));
        if (scroll != nullptr) Meta(scene.nodes.back(), "scroll", scroll);
    }
    for (const Tscn::Node& node : scene.nodes) sprites.push_back(Picture(node, {455.0, 256.0}));
    Sky::Controller sky;
    std::string error;
    CHECK_MSG(Sky::Build(rules, scene, sprites, kViewHeight, sky, error), error);
    return sky;
}

void AStillStripSitsSideBySide() {
    const Sky::Rules rules = TheRules();
    const Sky::Controller sky = Strip(rules, nullptr);
    CHECK_EQ(sky.skies.size(), static_cast<std::size_t>(3));
    if (sky.skies.size() != 3) return;
    const glm::dvec2 camera(600.0, 200.0);
    for (std::size_t t = 0; t < 3; ++t) {
        // originalPos = view * 0.5 + (m_width * t, 0), from the corner.
        const glm::dvec2 want = camera + glm::dvec2(455.0 * static_cast<double>(t), 0.0);
        CHECK_MSG(Near(Sky::PositionPx(rules, sky, t, camera, kView), want), "still sky " + std::to_string(t));
        CHECK(sky.skies[t].pivotX == 0.0);
    }
}

void AScrollingStripAndItsEnds() {
    const Sky::Rules rules = TheRules();
    Sky::Controller sky = Strip(rules, "60");
    CHECK(sky.scroll);
    CHECK(sky.scrollValue == 60.0);
    if (sky.skies.size() != 3) {
        CHECK(false);
        return;
    }
    // The pivots: + the width on the first, - on the last, none between.
    CHECK(sky.skies[0].pivotX == 455.0);
    CHECK(sky.skies[1].pivotX == 0.0);
    CHECK(sky.skies[2].pivotX == -455.0);
    const glm::dvec2 camera(600.0, 200.0);
    const glm::dvec2 corner = camera - kView * 0.5;
    // position(t): size * 0.5, + size.x on the first, - size.x on the last, + m_width * t.
    const double half = 227.5;
    const double positions[] = {half + 455.0, half + 455.0, half - 455.0 + 910.0};
    const double drawn[] = {half, half + 455.0, half + 910.0};
    for (std::size_t t = 0; t < 3; ++t) {
        const glm::dvec2 at = Sky::PositionPx(rules, sky, t, camera, kView);
        CHECK_MSG(Near(at, corner + glm::dvec2(positions[t], 128.0)), "scrolling sky " + std::to_string(t) + " at " + Px(at));
        // Drawn less its pivot: the three pictures edge to edge from the corner.
        const glm::dvec2 centre = Sky::DrawnCentrePx(rules, sky, t, camera, kView);
        CHECK_MSG(Near(centre, corner + glm::dvec2(drawn[t], 128.0)), "drawn at " + Px(centre));
    }

    // scaledUnitsPerSecond(-60) a tick, every sky alike.
    Sky::Advance(rules, sky, 1.0 / 60.0);
    for (const Sky::Layer& layer : sky.skies) CHECK(std::fabs(layer.scrollX + 1.0) < 1e-12);
    CHECK_MSG(Near(Sky::DrawnCentrePx(rules, sky, 0, camera, kView), corner + glm::dvec2(half - 1.0, 128.0)),
              "and the next frame draws it a unit left");
    // A long frame counts 200 ms, not a second.
    Sky::Advance(rules, sky, 1.0);
    CHECK(std::fabs(sky.skies[0].scrollX + 13.0) < 1e-9);
    // Back to 0 once it has run a whole width: 455 u at 60 u/s.
    Sky::Controller wrap = Strip(rules, "60");
    int ticks = 0;
    double least = 0.0;
    while (ticks < 1000) {
        Sky::Advance(rules, wrap, 1.0 / 60.0);
        ++ticks;
        least = std::min(least, wrap.skies[0].scrollX);
        if (wrap.skies[0].scrollX == 0.0) break;
    }
    CHECK_MSG(ticks == 455 || ticks == 456, "wrapped after " + std::to_string(ticks) + " ticks");
    CHECK_MSG(least > -455.0, "and never drew a whole width off: " + std::to_string(least));
}

void ALoneScrollingSkyIsBothEnds() {
    const Sky::Rules rules = TheRules();
    Tscn::Scene scene;
    scene.nodes.push_back(Entity("sky_1", "sky", {227.5, 128.0}));
    Meta(scene.nodes[0], "scroll", "30");
    const std::vector<Sprites::Sprite> sprites = {Picture(scene.nodes[0], {455.0, 256.0})};
    Sky::Controller sky;
    std::string error;
    CHECK_MSG(Sky::Build(rules, scene, sprites, kViewHeight, sky, error), error);
    if (sky.skies.size() != 1) {
        CHECK(false);
        return;
    }
    // The last SetPivotAdjust wins; the pitch takes both.
    CHECK(sky.skies[0].pivotX == -455.0);
    const glm::dvec2 corner(0.0);
    CHECK(Near(Sky::PositionPx(rules, sky, 0, kView * 0.5, kView), corner + glm::dvec2(227.5, 128.0)));
    CHECK(Near(Sky::DrawnCentrePx(rules, sky, 0, kView * 0.5, kView), corner + glm::dvec2(227.5 + 455.0, 128.0)));
    // m_scroll and m_width are one member each: a still sky after a scrolling one
    // leaves the strip still.
    Tscn::Scene mixed;
    mixed.nodes.push_back(Entity("sky_a", "sky", {0.0, 0.0}));
    Meta(mixed.nodes[0], "scroll", "30");
    mixed.nodes.push_back(Entity("sky_b", "sky.ent", {0.0, 0.0}));
    const std::vector<Sprites::Sprite> two = {Picture(mixed.nodes[0], {455.0, 256.0}),
                                              Picture(mixed.nodes[1], {512.0, 256.0})};
    Sky::Controller last;
    CHECK_MSG(Sky::Build(rules, mixed, two, kViewHeight, last, error), error);
    CHECK_MSG(!last.scroll && last.widthPx == 512.0, "the last sky's scroll and width stand");
    CHECK_MSG(last.skies.size() == 2 && last.skies[0].pivotX == 455.0,
              "though the first took its pivot while it was the one scaled");
}

// A FULLHD SKY (step 66): the level names sky.png, 455 x 256; the original draws
// fullhd/sky.png, 1024 x 512 at density 2, so 512 x 256 units. scaleSky's scale
// is still the view's height over the picture's, 1, and the sky is 512 u wide and
// drawn at the view's centre wherever the camera is: the wider picture changes
// its width and nothing about where it stands.
void AFullhdSkyIsWiderAndStillCentred() {
    const std::filesystem::path dir = std::filesystem::temp_directory_path() / "supersonic-test-mp-sky";
    std::error_code ec;
    std::filesystem::create_directories(dir / "assets" / "entities" / "fullhd", ec);
    const auto png = [](const std::filesystem::path& path, uint32_t width, uint32_t height) {
        std::vector<unsigned char> bytes = {0x89, 'P', 'N', 'G', 0x0D, 0x0A, 0x1A, 0x0A, 0, 0, 0, 13, 'I', 'H', 'D', 'R'};
        for (const uint32_t v : {width, height}) {
            for (int shift = 24; shift >= 0; shift -= 8) bytes.push_back(static_cast<unsigned char>(v >> shift));
        }
        bytes.push_back(8);
        bytes.push_back(6);
        std::ofstream file(path, std::ios::binary | std::ios::trunc);
        file.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
    };
    png(dir / "assets" / "entities" / "sky.png", 455, 256);
    png(dir / "assets" / "entities" / "fullhd" / "sky.png", 1024, 512);

    Tscn::Scene scene;
    std::string error;
    CHECK_MSG(Tscn::Parse(R"([gd_scene format=3]

[ext_resource type="Texture2D" path="res://assets/entities/sky.png" id="tex_0"]

[node name="level" type="Node2D"]

[node name="sky_629" type="Node2D" parent="."]
position = Vector2(227.5, 128)
z_index = -100
metadata/entity_name = "sky"

[node name="Sprite" type="Sprite2D" parent="sky_629"]
texture = ExtResource("tex_0")
)",
                          scene, error),
              error);
    const Sky::Rules rules = TheRules();
    for (const bool withTiers : {true, false}) {
        std::vector<Sprites::Sprite> sprites;
        const bool found = Sprites::Find(scene, dir.string(), withTiers ? TheTiers() : Tiers::Rules{}, sprites, error);
        CHECK_MSG(found && sprites.size() == 1, error);
        if (!found || sprites.size() != 1) return;
        Sky::Controller sky;
        CHECK_MSG(Sky::Build(rules, scene, sprites, kViewHeight, sky, error), error);
        if (sky.skies.size() != 1) {
            CHECK(false);
            return;
        }
        const double width = withTiers ? 512.0 : 455.0;
        CHECK_MSG(sky.skies[0].imagePx == glm::dvec2(width, 256.0) && sky.skies[0].scale == 1.0 &&
                      sky.widthPx == width && Sky::DrawnSizePx(sky, 0) == glm::dvec2(width, 256.0),
                  std::string(withTiers ? "the fullhd sky" : "the 1x sky") + " is " + std::to_string(width) +
                      " x 256 u at scale 1, drawn " + Px(Sky::DrawnSizePx(sky, 0)));
        for (const glm::dvec2& camera : {kView * 0.5, glm::dvec2(1000.0, 300.0), glm::dvec2(-40.0, 612.5)}) {
            CHECK_MSG(Near(Sky::DrawnCentrePx(rules, sky, 0, camera, kView), camera),
                      "and its centre is the view's: " + Px(Sky::DrawnCentrePx(rules, sky, 0, camera, kView)) +
                          " for " + Px(camera));
        }
    }
}

// ---- the converted levels ---------------------------------------------------

void EveryLevelsSky() {
    const Sky::Rules rules = TheRules();
    const Tiers::Rules tiers = TheTiers();
    int one = 0, space = 0, neither = 0, satelliteAlone = 0, levels = 0, named = 0, namedEnt = 0;
    int wide = 0, narrow = 0, otherWidth = 0; // skies of 512 u (fullhd), 455 u (1x), and any other
    int scrolling = 0, scaledOtherThanOne = 0, notTheViewsCentre = 0, scrollingSkyKey = 0;
    std::string firstWrong;
    std::error_code ec;
    for (const auto& entry : std::filesystem::directory_iterator(kLevels, ec)) {
        if (entry.path().extension() != ".tscn") continue;
        ++levels;
        const std::string level = entry.path().stem().string();
        Tscn::Scene scene;
        std::vector<Sprites::Sprite> sprites;
        std::string error;
        if (!Tscn::Load(entry.path().string(), scene, error) || !Sprites::Find(scene, kLevels + "/..", tiers, sprites, error)) {
            CHECK_MSG(false, level + ": " + error);
            continue;
        }
        Sky::Controller sky;
        CHECK_MSG(Sky::Build(rules, scene, sprites, kViewHeight, sky, error), level + ": " + error);
        for (const Tscn::Node& node : scene.nodes) {
            if (node.parent != ".") continue;
            if (Roles::EntityName(node) == "sky") ++named;
            if (Roles::EntityName(node) == "sky.ent") ++namedEnt;
            if (node.Meta("scrolling_sky") != nullptr) ++scrollingSkyKey;
        }
        bool spaceBg = false;
        for (const Tscn::Node& node : scene.nodes) {
            if (node.parent == "." && Roles::EntityName(node) == "properties" && node.Meta("space_bg") != nullptr) {
                spaceBg = true;
            }
        }
        if (spaceBg) {
            ++space;
            CHECK_MSG(!sky.running, level + ": a space level builds no StaticSky");
            continue;
        }
        if (sky.skies.size() == 1) ++one;
        if (sky.skies.empty() && sky.hasSatellite) ++satelliteAlone;
        if (!sky.running) ++neither;
        CHECK_MSG(sky.skies.size() <= 1, level + ": " + std::to_string(sky.skies.size()) + " skies");
        if (sky.scroll) ++scrolling;
        for (std::size_t t = 0; t < sky.skies.size(); ++t) {
            if (sky.skies[t].scale != 1.0) ++scaledOtherThanOne;
            if (sky.skies[t].imagePx == glm::dvec2(512.0, 256.0)) {
                ++wide;
            } else if (sky.skies[t].imagePx == glm::dvec2(455.0, 256.0)) {
                ++narrow;
            } else {
                ++otherWidth;
            }
            const glm::dvec2 camera(500.0, 300.0);
            if (!Near(Sky::DrawnCentrePx(rules, sky, t, camera, kView), camera)) {
                ++notTheViewsCentre;
                if (firstWrong.empty()) firstWrong = level;
            }
        }
    }
    std::printf("  %d levels: %d one sky (%d 'sky', %d 'sky.ent'), %d space, %d neither, %d satellite alone; "
                "%d scrolling_sky keys, %d scrolling\n",
                levels, one, named, namedEnt, space, neither, satelliteAlone, scrollingSkyKey, scrolling);
    CHECK_EQ(levels, 128);
    CHECK_EQ(one, 95);
    CHECK_EQ(named, 93);
    CHECK_EQ(namedEnt, 2);
    CHECK_EQ(space, 19);
    CHECK_EQ(neither, 13);
    CHECK_EQ(satelliteAlone, 1);
    // The key 37 levels carry, and no sky that scrolls.
    CHECK_EQ(scrollingSkyKey, 37);
    CHECK_EQ(scrolling, 0);
    CHECK_EQ(scaledOtherThanOne, 0);
    // Drawn from its tier. 51 skies are 512 u wide: the 28 whose 455 x 256 file
    // has a 1024 x 512 fullhd copy (sky 3, red_sky 7, icy_sky 14, sky_purple 4),
    // wider than their 1x files, and the 23 satellite_sky.png, whose 512 x 256
    // file is already that wide and whose fullhd copy is exactly twice it. The
    // other 44 are dark_sky.png, with no tier file, 455 u.
    std::printf("  skies by drawn width: %d at 512 u, %d at 455 u, %d other\n", wide, narrow, otherWidth);
    CHECK_EQ(wide, 51);
    CHECK_EQ(narrow, 44);
    CHECK_EQ(otherWidth, 0);
    CHECK_MSG(notTheViewsCentre == 0, "every sky at the view's centre, first wrong " + firstWrong);
}

} // namespace

int main() {
    TheRulesAreSkyJsons();
    AStillSkyIsTheViewsCentreWhereverTheCameraIs();
    TheSkyIsAsTallAsTheView();
    ASpaceLevelBuildsNoStaticSky();
    OnlyTheExactNamesAreSkies();
    ASkyWithoutAPictureIsRefused();
    TheSatelliteKeepsItsPlaceOnTheScreen();
    AStillStripSitsSideBySide();
    AScrollingStripAndItsEnds();
    ALoneScrollingSkyIsBothEnds();
    AFullhdSkyIsWiderAndStillCentred();

    std::error_code ec;
    if (!std::filesystem::is_directory(kLevels, ec)) {
        std::printf("test_mp_sky: the levels' census SKIPPED - needs the converted levels at %s.\n"
                    "  They live outside this repository; configure with -DSUPERSONIC_MAGICPORTALS_LEVELS=...\n",
                    kLevels.c_str());
        return ::test::summary("test_mp_sky", 60);
    }
    EveryLevelsSky();
    return ::test::summary("test_mp_sky", 70);
}
