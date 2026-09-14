// The converted levels, read whole.
//
// These are the remake's 128 levels as its converter wrote them to
// Magic-Portals-Remake/out/levels, at the remake's b572fec: 79af319's files
// (whose game/assets/levels copy is byte-identical to them) with the lighting
// keys inserted and not one other byte changed. They are the original game's
// level data and are NOT in this repository - main() skips, and says where it
// looked, when they are absent.
//
// The oracle is the files themselves. Every count below was taken with grep on
// 10 September 2026 (the metadata strings again on 14 September, when the
// lighting keys were added), independently of this reader, e.g.
//
//   cd Magic-Portals-Remake/out/levels
//   cat *.tscn | grep -oE '^\[node[^]]*type="[A-Za-z0-9]+"' | sort | uniq -c
//
// and the level30 values were read off level30.tscn. So a reader that dropped a
// node, a resource or a property is caught here by a count, not later by a
// level that loads with a platform missing.

#include "TestHarness.hpp"

#include "core/ConvexHullCache.hpp"
#include "sim/Prism.hpp"
#include "sim/Tscn.hpp"
#include "sim/Units.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <initializer_list>
#include <map>
#include <set>
#include <string>
#include <system_error>
#include <vector>

using namespace MagicPortals;
using Kind = Tscn::Value::Kind;

namespace {

const std::string kLevels = MAGICPORTALS_LEVELS_DIR;

// Every distinct collision polygon across the levels, gathered by AllLevels.
std::set<std::vector<double>> g_polygons;

struct Inventory {
    std::map<std::string, int> nodes;
    std::map<std::string, int> external;
    std::map<std::string, int> embedded;
    std::map<std::string, int> keys; // non-metadata properties, nodes and resources both
    int metaStrings = 0;
    int metaVectors = 0;
    int metaNumbers = 0;
    int metaNumbersOutsideJoints = 0;
    std::map<std::size_t, int> polygonPoints;
};

void Tally(const Tscn::Scene& scene, Inventory& inv) {
    for (const Tscn::Resource& r : scene.external) ++inv.external[r.type];
    for (const Tscn::Resource& r : scene.embedded) {
        ++inv.embedded[r.type];
        for (const Tscn::Property& p : r.properties) ++inv.keys[p.key];
    }
    for (const Tscn::Node& n : scene.nodes) {
        ++inv.nodes[n.type];
        for (const Tscn::Property& p : n.properties) {
            if (!p.key.starts_with("metadata/")) {
                ++inv.keys[p.key];
                if (p.key == "polygon") ++inv.polygonPoints[p.value.numbers.size() / 2];
                continue;
            }
            switch (p.value.kind) {
            case Kind::String: ++inv.metaStrings; break;
            case Kind::Vector2: ++inv.metaVectors; break;
            case Kind::Number:
                ++inv.metaNumbers;
                if (!p.key.starts_with("metadata/joint_")) ++inv.metaNumbersOutsideJoints;
                break;
            default: break;
            }
        }
    }
}

bool Is(const Tscn::Value* v, Kind kind, std::initializer_list<double> expected) {
    if (v == nullptr || v->kind != kind || v->numbers.size() != expected.size()) return false;
    return std::equal(expected.begin(), expected.end(), v->numbers.begin());
}

bool Text(const Tscn::Value* v, const std::string& expected) {
    return v != nullptr && v->kind == Kind::String && v->text == expected;
}

// The embedded shape behind <entity>/Body/Shape, or null.
const Tscn::Resource* ShapeOf(const Tscn::Scene& scene, const std::string& entity) {
    const Tscn::Node* shape = scene.FindNode(entity + "/Body/Shape");
    const Tscn::Value* ref = shape ? shape->Find("shape") : nullptr;
    return ref && ref->kind == Kind::SubResource ? scene.Embedded(ref->text) : nullptr;
}

// level30 is the spike's level: it has rigid bodies and movers, where level0
// has neither.
void Level30() {
    Tscn::Scene scene;
    std::string error;
    const bool ok = Tscn::Load(kLevels + "/level30.tscn", scene, error);
    CHECK_MSG(ok, error);
    if (!ok) return;

    Inventory inv;
    Tally(scene, inv);
    CHECK_EQ(inv.nodes["Node2D"], 37);
    CHECK_EQ(inv.nodes["Sprite2D"], 30);
    CHECK_EQ(inv.nodes["StaticBody2D"], 15);
    CHECK_EQ(inv.nodes["RigidBody2D"], 3);
    CHECK_EQ(inv.nodes["Area2D"], 3);
    CHECK_EQ(inv.nodes["CollisionShape2D"], 12);
    CHECK_EQ(inv.nodes["CollisionPolygon2D"], 9);
    CHECK_EQ(scene.nodes.size(), std::size_t{109});
    CHECK_EQ(inv.external["Texture2D"], 15);
    CHECK_EQ(scene.external.size(), std::size_t{15});
    CHECK_EQ(inv.embedded["RectangleShape2D"], 12);
    CHECK_EQ(scene.embedded.size(), std::size_t{12});
    CHECK_EQ(scene.format, 3);
    CHECK_EQ(scene.loadSteps, 28);

    const Tscn::Node* root = scene.FindNode(".");
    CHECK(root && root->name == "level30");
    const Tscn::Node* player = scene.FindNode("main_char_29");
    CHECK(player && Text(player->Meta("entity_name"), "main_char"));
    CHECK(player && Is(player->Find("position"), Kind::Vector2, {182, 203}));

    // The three bodies with mass. "crate" and "crate.ent" are different entities
    // in the original, which is the role table's reason for never stripping the
    // suffix, and teleportable is set per instance.
    struct Crate {
        const char* node;
        const char* entity;
        const char* teleportable;
        double x, y, w, h;
    };
    const Crate crates[] = {
        {"crate_969", "crate", "0", 352, 192, 58, 58},
        {"crate_ent_968", "crate.ent", "1", 416, 192, 58, 58},
        {"crate_small_ent_973", "crate_small.ent", "1", 670, 208, 30, 30},
    };
    for (const Crate& c : crates) {
        const Tscn::Node* n = scene.FindNode(c.node);
        CHECK_MSG(n != nullptr, c.node);
        if (!n) continue;
        CHECK_MSG(Text(n->Meta("entity_name"), c.entity), c.node);
        CHECK_MSG(Text(n->Meta("teleportable"), c.teleportable), c.node);
        CHECK_MSG(Is(n->Find("position"), Kind::Vector2, {c.x, c.y}), c.node);
        const Tscn::Node* body = scene.Child(*n, "Body");
        CHECK_MSG(body && body->type == "RigidBody2D", c.node);
        const Tscn::Resource* rect = ShapeOf(scene, c.node);
        CHECK_MSG(rect && rect->type == "RectangleShape2D" && Is(rect->Find("size"), Kind::Vector2, {c.w, c.h}), c.node);
    }

    // The three door lifts: static in the file, made movers by the remake's
    // LevelRuntime, keyed to buttons by switchIdx. Quoted numbers, both keys.
    struct Lift {
        const char* node;
        double stride, switchIdx, x, y;
    };
    const Lift lifts[] = {
        {"door_lift_978", 3000, 1, 600, 74},
        {"door_lift_977", 3000, 2, 632, 74},
        {"door_lift_976", 1000, 0, 592, 192},
    };
    for (const Lift& l : lifts) {
        const Tscn::Node* n = scene.FindNode(l.node);
        CHECK_MSG(n != nullptr, l.node);
        if (!n) continue;
        double stride = -1.0, index = -1.0;
        CHECK_MSG(n->Meta("stride") && n->Meta("stride")->kind == Kind::String, l.node);
        CHECK_MSG(n->Meta("stride") && n->Meta("stride")->AsNumber(stride) && stride == l.stride, l.node);
        CHECK_MSG(n->Meta("switchIdx") && n->Meta("switchIdx")->AsNumber(index) && index == l.switchIdx, l.node);
        CHECK_MSG(Is(n->Find("position"), Kind::Vector2, {l.x, l.y}), l.node);
        CHECK_MSG(scene.Child(*n, "Body") && scene.Child(*n, "Body")->type == "StaticBody2D", l.node);
        const Tscn::Resource* rect = ShapeOf(scene, l.node);
        CHECK_MSG(rect && Is(rect->Find("size"), Kind::Vector2, {30, 126}), l.node);
    }

    // The three buttons, the only Area2D bodies in the file.
    const char* buttons[] = {"button_975", "button_980", "button_982"};
    for (int i = 0; i < 3; ++i) {
        const Tscn::Node* n = scene.FindNode(buttons[i]);
        CHECK_MSG(n && Text(n->Meta("idx"), std::to_string(i)) && Text(n->Meta("stride"), "300"), buttons[i]);
        CHECK_MSG(n && scene.Child(*n, "Body") && scene.Child(*n, "Body")->type == "Area2D", buttons[i]);
        const Tscn::Resource* rect = ShapeOf(scene, buttons[i]);
        CHECK_MSG(rect && Is(rect->Find("size"), Kind::Vector2, {26, 16}), buttons[i]);
    }

    // The exit: its trigger box is metadata, turned into an Area2D at runtime.
    const Tscn::Node* door = scene.FindNode("door_ent_728");
    CHECK(door && Text(door->Meta("entity_name"), "door.ent"));
    CHECK(door && Is(door->Meta("trigger_size"), Kind::Vector2, {8, 24}));
    CHECK(door && Is(door->Meta("trigger_offset"), Kind::Vector2, {0, 37}));
    CHECK(door && Is(door->Find("z_index"), Kind::Number, {-16}));

    // Chamfered platforms: eight-point polygons, exact to the file's decimals.
    const Tscn::Node* block = scene.FindNode("single_block_plat_ent_974/Body/Shape");
    CHECK(block && block->type == "CollisionPolygon2D");
    CHECK(block && Is(block->Find("polygon"), Kind::Points,
                      {-22.4, 16, -32, 12.8, -32, -12.8, -22.4, -16, 22.4, -16, 32, -12.8, 32, 12.8, 22.4, 16}));
    const Tscn::Node* platform = scene.FindNode("platform_ent_966/Body/Shape");
    CHECK(platform && Is(platform->Find("polygon"), Kind::Points,
                         {-121.6, 16, -128, 12.8, -128, -12.8, -121.6, -16, 121.6, -16, 128, -12.8, 128, 12.8, 121.6, 16}));

    // The level's walls, and the background drawn furthest back.
    const Tscn::Node* ceiling = scene.FindNode("collision_768x128_ent_940");
    CHECK(ceiling && Is(ceiling->Find("position"), Kind::Vector2, {384, -64}));
    const Tscn::Resource* ceilingRect = ShapeOf(scene, "collision_768x128_ent_940");
    CHECK(ceilingRect && Is(ceilingRect->Find("size"), Kind::Vector2, {768, 128}));
    const Tscn::Node* sky = scene.FindNode("sky_576");
    CHECK(sky && Is(sky->Find("position"), Kind::Vector2, {228.5, 128}) && Is(sky->Find("z_index"), Kind::Number, {-100}));
    const Tscn::Node* skySprite = scene.FindNode("sky_576/Sprite");
    const Tscn::Value* skyTex = skySprite ? skySprite->Find("texture") : nullptr;
    const Tscn::Resource* skyFile = skyTex ? scene.External(skyTex->text) : nullptr;
    CHECK(skyFile && skyFile->path == "res://assets/entities/icy_sky.png");
}

void AllLevels() {
    std::vector<std::filesystem::path> files;
    for (const auto& entry : std::filesystem::directory_iterator(kLevels))
        if (entry.path().extension() == ".tscn") files.push_back(entry.path());
    std::sort(files.begin(), files.end());
    CHECK_EQ(files.size(), std::size_t{128});

    Inventory total;
    int parsed = 0;
    int stepsAgree = 0;
    for (const auto& file : files) {
        Tscn::Scene scene;
        std::string error;
        if (!Tscn::Load(file.string(), scene, error)) {
            CHECK_MSG(false, error);
            continue;
        }
        ++parsed;
        Tally(scene, total);
        for (const Tscn::Node& node : scene.nodes)
            if (const Tscn::Value* polygon = node.type == "CollisionPolygon2D" ? node.Find("polygon") : nullptr)
                g_polygons.insert(polygon->numbers);
        // Godot's load_steps is the resource count plus one. Holding it in every
        // level says no resource line was lost or doubled on the way in.
        if (scene.loadSteps == static_cast<int>(scene.external.size() + scene.embedded.size()) + 1) ++stepsAgree;
    }
    CHECK_EQ(parsed, 128);
    CHECK_EQ(stepsAgree, 128);

    CHECK_EQ(total.nodes["Node2D"], 4193);
    CHECK_EQ(total.nodes["Sprite2D"], 2883);
    CHECK_EQ(total.nodes["StaticBody2D"], 1601);
    CHECK_EQ(total.nodes["CollisionPolygon2D"], 972);
    CHECK_EQ(total.nodes["CollisionShape2D"], 866);
    CHECK_EQ(total.nodes["RigidBody2D"], 146);
    CHECK_EQ(total.nodes["Area2D"], 91);
    CHECK_EQ(total.nodes.size(), std::size_t{7});

    CHECK_EQ(total.external["Texture2D"], 1757);
    CHECK_EQ(total.external.size(), std::size_t{1});
    CHECK_EQ(total.embedded["RectangleShape2D"], 806);
    CHECK_EQ(total.embedded["CanvasItemMaterial"], 64);
    CHECK_EQ(total.embedded["CircleShape2D"], 60);
    CHECK_EQ(total.embedded.size(), std::size_t{3});

    CHECK_EQ(total.keys["position"], 4095);
    CHECK_EQ(total.keys["texture"], 2883);
    CHECK_EQ(total.keys["z_index"], 1533);
    CHECK_EQ(total.keys["polygon"], 972);
    CHECK_EQ(total.keys["shape"], 866);
    CHECK_EQ(total.keys["size"], 806);
    CHECK_EQ(total.keys["rotation"], 143);
    CHECK_EQ(total.keys["material"], 99);
    CHECK_EQ(total.keys["offset"], 88);
    CHECK_EQ(total.keys["blend_mode"], 64);
    CHECK_EQ(total.keys["radius"], 60);
    CHECK_EQ(total.keys.size(), std::size_t{11});

    // Metadata by spelling. The 80 bare numbers are the ten hinges' eight
    // joint_* keys, and nothing else is written bare.
    //
    // The strings were 5,700 until the remake's b572fec, whose converter writes
    // the original's lighting as metadata/eth_* strings. The levels were
    // regenerated with it on 14 September 2026 and every file came out as the
    // one before plus those lines: 5,700 + 11,234 = 16,934, the converter's own
    // report of the lines it added, and what
    //   cat *.tscn | grep -cE '^metadata/[^ ]+ = "'
    // counts before and after. sim/Lighting reads them; test_mp_lighting pins
    // what they say.
    CHECK_EQ(total.metaStrings, 16934);
    CHECK_EQ(total.metaVectors, 837);
    CHECK_EQ(total.metaNumbers, 80);
    CHECK_EQ(total.metaNumbersOutsideJoints, 0);

    // The collision polygons: 956 eight-point chamfered boxes and 16 triangles.
    CHECK_EQ(total.polygonPoints[8], 956);
    CHECK_EQ(total.polygonPoints[3], 16);
    CHECK_EQ(total.polygonPoints.size(), std::size_t{2});
}

// The S2 route across every level. Each distinct collision polygon becomes an
// OBJ prism, and each prism a hull that must be exactly the prism: one piece,
// every point a vertex, the polygon's area times the depth for a volume, and
// nothing invented. A wrong axis, winding or scale in Prism.hpp fails here, on
// the converter's own shapes rather than on ones written to pass.
void EveryPolygonIsAnExactHull() {
    CHECK_EQ(g_polygons.size(), std::size_t{13});
    const std::filesystem::path directory = std::filesystem::temp_directory_path() / "supersonic-test-mp-levels";
    constexpr double depth = 2.0;
    Supersonic::ConvexHullCache cache;
    int exact = 0;
    for (const std::vector<double>& points : g_polygons) {
        std::string error;
        const std::string path = Prism::Write(points, depth, directory, error);
        if (path.empty()) {
            CHECK_MSG(false, error);
            continue;
        }
        const Supersonic::ConvexDecomposition* hull = cache.Get("", path);
        const double volume = Prism::AreaPx(points) / (Units::kPixelsPerMetre * Units::kPixelsPerMetre) * depth;
        const bool ok = hull != nullptr && hull->pieces().size() == 1 &&
                        hull->pieces().front().vertices().size() == points.size() &&
                        std::fabs(hull->meshVolume() - volume) <= 1e-4 * volume && hull->invented() <= 1e-4 * volume;
        CHECK_MSG(ok, path + ": not exactly its prism");
        if (ok) ++exact;
    }
    CHECK_EQ(exact, 13);
}

void runTests() {
    Level30();
    AllLevels();
    EveryPolygonIsAnExactHull();
}

} // namespace

int main() {
    std::error_code ec;
    if (!std::filesystem::is_directory(kLevels, ec)) {
        std::printf("test_mp_levels: SKIPPED - no converted levels at %s\n"
                    "  They are the original game's data and live outside this repository. Convert\n"
                    "  them with the remake's tools/converter, or configure with\n"
                    "  -DSUPERSONIC_MAGICPORTALS_LEVELS=<Magic-Portals-Remake>/out/levels\n",
                    kLevels.c_str());
        return 77;
    }
    runTests();
    return ::test::summary("test_mp_levels", 125);
}
