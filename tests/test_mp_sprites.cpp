// A level's own art: the sprites the remake's converter wrote, as sim/Sprites
// reads them.
//
// What the converter decides is pinned exactly: which texture each entity node
// shows, its offset, its z_index, its blend, and the order Godot draws a canvas
// in - by z_index, then as the file lists them. The sizes are the images' own,
// read from the files the converter copied, so they are pinned too.
//
// The reader is first tested on a scene and images this suite writes, which
// runs anywhere. The rest reads the converted levels from outside this
// repository, and is skipped, saying where it looked, when they are absent.

#include "TestHarness.hpp"

#include "sim/Art.hpp"
#include "sim/Sprites.hpp"
#include "sim/Tscn.hpp"

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
const std::string kOriginal = MAGICPORTALS_ORIGINAL_DIR;

std::filesystem::path Scratch() {
    const std::filesystem::path dir = std::filesystem::temp_directory_path() / "supersonic-test-mp-sprites";
    std::error_code ec;
    std::filesystem::create_directories(dir / "assets", ec);
    return dir;
}

void Write(const std::filesystem::path& path, const std::vector<unsigned char>& bytes) {
    std::ofstream file(path, std::ios::binary | std::ios::trunc);
    file.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
}

// A PNG's first 26 bytes: the signature, then the IHDR chunk's length and tag,
// the width and height, the bit depth and the colour type.
std::vector<unsigned char> PngHeader(uint32_t width, uint32_t height) {
    std::vector<unsigned char> bytes = {0x89, 'P', 'N', 'G', 0x0D, 0x0A, 0x1A, 0x0A, 0, 0, 0, 13, 'I', 'H', 'D', 'R'};
    for (const uint32_t v : {width, height}) {
        for (int shift = 24; shift >= 0; shift -= 8) bytes.push_back(static_cast<unsigned char>(v >> shift));
    }
    bytes.push_back(8);
    bytes.push_back(6);
    return bytes;
}

// A BMP's file header and the start of a 40-byte info header.
std::vector<unsigned char> BmpHeader(int32_t width, int32_t height) {
    std::vector<unsigned char> bytes = {'B', 'M', 0, 0, 0, 0, 0, 0, 0, 0, 54, 0, 0, 0, 40, 0, 0, 0};
    for (const int32_t v : {width, height}) {
        const uint32_t u = static_cast<uint32_t>(v);
        for (int shift = 0; shift < 32; shift += 8) bytes.push_back(static_cast<unsigned char>(u >> shift));
    }
    return bytes;
}

bool Near(double a, double b) { return std::fabs(a - b) < 1e-6; }

// ---- the reader, on files this suite writes -------------------------------------

void AnImageSaysItsSizeInItsHeader() {
    const std::filesystem::path dir = Scratch();
    int w = 0;
    int h = 0;
    std::string error;

    Write(dir / "sky.png", PngHeader(455, 256));
    CHECK_MSG(Sprites::ImageSize((dir / "sky.png").string(), w, h, error), error);
    CHECK_EQ(w, 455);
    CHECK_EQ(h, 256);

    // Stored top down, which a BMP says with a negative height.
    Write(dir / "black.bmp", BmpHeader(40, -100));
    CHECK_MSG(Sprites::ImageSize((dir / "black.bmp").string(), w, h, error), error);
    CHECK_EQ(w, 40);
    CHECK_EQ(h, 100);

    // The old OS/2 header keeps its sizes in 16 bits.
    Write(dir / "old.bmp", {'B', 'M', 0, 0, 0, 0, 0, 0, 0, 0, 26, 0, 0, 0, 12, 0, 0, 0, 16, 0, 8, 0, 1, 0, 24, 0});
    CHECK_MSG(Sprites::ImageSize((dir / "old.bmp").string(), w, h, error), error);
    CHECK_EQ(w, 16);
    CHECK_EQ(h, 8);
}

void AnythingElseIsRefusedByName() {
    const std::filesystem::path dir = Scratch();
    int w = 0;
    int h = 0;
    std::string error;

    Write(dir / "fire.gif", {'G', 'I', 'F', '8', '9', 'a', 1, 0, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0});
    const bool gif = Sprites::ImageSize((dir / "fire.gif").string(), w, h, error);
    CHECK(!gif);
    CHECK_MSG(error.find("fire.gif") != std::string::npos, error);

    std::vector<unsigned char> cut = PngHeader(64, 64);
    cut.resize(12);
    Write(dir / "cut.png", cut);
    const bool truncated = Sprites::ImageSize((dir / "cut.png").string(), w, h, error);
    CHECK_MSG(!truncated, "a header cut short says no size");

    Write(dir / "empty.png", PngHeader(0, 64));
    const bool empty = Sprites::ImageSize((dir / "empty.png").string(), w, h, error);
    CHECK_MSG(!empty, "nor does one that says zero");

    error.clear();
    const bool missing = Sprites::ImageSize((dir / "nothing.png").string(), w, h, error);
    CHECK(!missing);
    CHECK_MSG(error.find("nothing.png") != std::string::npos, error);
}

// Three entities, written out of drawing order, with every property the reader
// takes between them.
const char* const kScene = R"([gd_scene load_steps=4 format=3]

[ext_resource type="Texture2D" path="res://assets/a.png" id="tex_0"]
[ext_resource type="Texture2D" path="res://assets/b.png" id="tex_1"]

[sub_resource type="CanvasItemMaterial" id="CanvasItemMaterial_blend1"]
blend_mode = 1

[node name="level" type="Node2D"]

[node name="front_1" type="Node2D" parent="."]
position = Vector2(10, 20)
z_index = 5

[node name="Sprite" type="Sprite2D" parent="front_1"]
texture = ExtResource("tex_0")

[node name="back_2" type="Node2D" parent="."]
position = Vector2(30, 40)
rotation = 1.5708
z_index = -3

[node name="Sprite" type="Sprite2D" parent="back_2"]
texture = ExtResource("tex_1")
offset = Vector2(4, 0)
material = SubResource("CanvasItemMaterial_blend1")

[node name="middle_3" type="Node2D" parent="."]

[node name="Sprite" type="Sprite2D" parent="middle_3"]
texture = ExtResource("tex_0")
)";

bool ParseScene(const char* text, Tscn::Scene& scene) {
    std::string error;
    const bool ok = Tscn::Parse(text, scene, error);
    CHECK_MSG(ok, error);
    return ok;
}

void TheCanvasOrderIsZThenTheFile() {
    const std::filesystem::path dir = Scratch();
    Write(dir / "assets" / "a.png", PngHeader(4, 2));
    Write(dir / "assets" / "b.png", PngHeader(8, 8));
    Tscn::Scene scene;
    if (!ParseScene(kScene, scene)) return;

    std::vector<Sprites::Sprite> sprites;
    std::string error;
    const bool found = Sprites::Find(scene, dir.string(), sprites, error);
    CHECK_MSG(found, error);
    CHECK_EQ(sprites.size(), std::size_t{3});
    if (sprites.size() != 3) return;

    CHECK(sprites[0].node == "back_2");
    CHECK(sprites[1].node == "middle_3");
    CHECK_MSG(sprites[2].node == "front_1", "the highest z_index is drawn last, in front");
    for (int i = 0; i < 3; ++i) CHECK_EQ(sprites[static_cast<std::size_t>(i)].order, i);

    const Sprites::Sprite& back = sprites[0];
    CHECK_MSG(back.texture == dir.string() + "/assets/b.png", back.texture);
    CHECK(back.sizePx == glm::dvec2(8.0, 8.0));
    CHECK_EQ(back.zIndex, -3);
    CHECK(Near(back.rotation, 1.5708));
    CHECK(back.offsetPx == glm::dvec2(4.0, 0.0));
    CHECK_MSG(back.additive, "blend_mode 1 is added");
    // A quarter turn clockwise on the screen takes the offset from +x to +y,
    // which in the level's pixels is down.
    const glm::dvec2 centre = Sprites::CentrePx(back);
    CHECK_MSG(std::fabs(centre.x - 30.0) < 1e-3 && std::fabs(centre.y - 44.0) < 1e-3,
              "turned with its node: " + std::to_string(centre.x) + ", " + std::to_string(centre.y));

    const Sprites::Sprite& front = sprites[2];
    CHECK(front.sizePx == glm::dvec2(4.0, 2.0));
    CHECK(front.atPx == glm::dvec2(10.0, 20.0));
    CHECK_MSG(!front.additive && !sprites[1].additive, "and a sprite with no material mixes");
    CHECK_EQ(sprites[1].zIndex, 0);
}

// One entity node and its sprite, with whatever goes between.
std::string OneSprite(const std::string& resources, const std::string& between, const std::string& sprite) {
    return "[gd_scene format=3]\n\n" + resources + "\n[node name=\"level\" type=\"Node2D\"]\n\n"
           "[node name=\"wall_1\" type=\"Node2D\" parent=\".\"]\n\n" + between + sprite;
}

void WhatTheReaderDoesNotDrawIsNamed() {
    const std::filesystem::path dir = Scratch();
    Write(dir / "assets" / "a.png", PngHeader(4, 2));
    const std::string texture = "[ext_resource type=\"Texture2D\" path=\"res://assets/a.png\" id=\"tex_0\"]\n";
    const std::string sprite = "[node name=\"Sprite\" type=\"Sprite2D\" parent=\"wall_1\"]\ntexture = ExtResource(\"tex_0\")\n";

    const auto refused = [&dir](const std::string& text, const char* expect, const char* what) {
        Tscn::Scene scene;
        if (!ParseScene(text.c_str(), scene)) return;
        std::vector<Sprites::Sprite> sprites;
        std::string error;
        const bool found = Sprites::Find(scene, dir.string(), sprites, error);
        CHECK_MSG(!found, what);
        CHECK_MSG(error.find(expect) != std::string::npos, std::string(what) + ": " + error);
    };

    // A texture the converter named and did not copy.
    refused(OneSprite("[ext_resource type=\"Texture2D\" path=\"res://assets/gone.png\" id=\"tex_0\"]\n", "", sprite),
            "gone.png", "a missing image is refused, by its file");

    // A blend the port does not draw: Godot's 2 subtracts.
    refused(OneSprite(texture + "\n[sub_resource type=\"CanvasItemMaterial\" id=\"m\"]\nblend_mode = 2\n", "",
                      sprite + "material = SubResource(\"m\")\n"),
            "blend_mode", "a subtracting sprite is refused");

    // A sprite that is not an entity's own.
    refused(OneSprite(texture, "[node name=\"Body\" type=\"StaticBody2D\" parent=\"wall_1\"]\n\n",
                      "[node name=\"Sprite\" type=\"Sprite2D\" parent=\"wall_1/Body\"]\ntexture = ExtResource(\"tex_0\")\n"),
            "wall_1/Body", "a sprite under a body is refused");
}

// ---- art.json: what no level pictures ------------------------------------------

void ThePortalAndTheShotAreTheirEnts() {
    // The .ent files' facts, pinned. How fast the shot's sheet plays is a guess
    // and only has to be something.
    Art::Rules rules;
    std::string error;
    const bool ok = Art::LoadRules(std::string(MAGICPORTALS_PORT_DATA_DIR) + "/art.json", rules, error);
    CHECK_MSG(ok, error);
    CHECK_MSG(rules.portal.sprite == "portal_halo.png" && rules.portal.additive && rules.portal.Frames() == 1,
              "portal.ent: its halo, added");
    CHECK_MSG(rules.shot.sprite == "projectile.png" && rules.shot.additive, "projectile.ent: its sheet, added");
    CHECK_MSG(rules.shot.columns == 6 && rules.shot.rows == 1, "cut 6 x 1, as its SpriteCut says");
    CHECK_MSG(rules.shot.framesPerSecond > 0.0, "and a six-frame sheet says how fast it plays");

    const Art::Character& mage = rules.character;
    CHECK_MSG(mage.sprite == "magic_portals_hd.png" && !mage.additive && mage.columns == 4 && mage.rows == 4,
              "dark_mage.ent: its sheet, cut 4 x 4, mixed");
    CHECK_MSG(mage.startFrame == 4 && mage.pivotXPx == 0.0 && mage.pivotYPx == 2.0,
              "starting on frame 4, its pivot 2 px below the middle");
    CHECK_MSG(mage.leftRow == 1 && mage.rightRow == 2, "the rows the decoded DIRECTION enum gives left and right");
    CHECK_MSG(mage.startFrame / mage.columns == mage.leftRow, "and the start frame stands on the left row");
}

void ASheetThatDoesNotSayHowFastIsRefused() {
    const std::filesystem::path path = Scratch() / "art.json";
    // Everything else in order, so the shot's missing rate is what is refused.
    const std::string text = R"({"portal": {"sprite": "a.png", "additive": true},
                                 "shot": {"sprite": "b.png", "additive": true, "columns": 6},
                                 "character": {"sprite": "c.png", "additive": false, "columns": 4, "rows": 4,
                                               "start_frame": 4, "pivot_px": [0, 2],
                                               "rows_by_direction": {"left": 1, "right": 2},
                                               "animation": {"frames_per_second": 10, "idle_column": 0}}})";
    Write(path, std::vector<unsigned char>(text.begin(), text.end()));
    Art::Rules rules;
    std::string error;
    const bool ok = Art::LoadRules(path.string(), rules, error);
    CHECK(!ok);
    CHECK_MSG(error.find("frames_per_second") != std::string::npos, error);
}

void TheOriginalsImagesAreCutAsTheEntsSay() {
    int w = 0;
    int h = 0;
    std::string error;
    const bool halo = Sprites::ImageSize(kOriginal + "/entities/portal_halo.png", w, h, error);
    CHECK_MSG(halo && w == 64 && h == 64, "portal_halo.png: " + error);
    const bool sheet = Sprites::ImageSize(kOriginal + "/entities/projectile.png", w, h, error);
    CHECK_MSG(sheet && w == 6 * 64 && h == 64, "projectile.png is six frames of 64 x 64: " + error);
    const bool mage = Sprites::ImageSize(kOriginal + "/entities/magic_portals_hd.png", w, h, error);
    CHECK_MSG(mage && w == 4 * 40 && h == 4 * 56, "magic_portals_hd.png is sixteen frames of 40 x 56: " + error);
}

// ---- the converted levels -----------------------------------------------------

bool LoadLevel(const std::string& level, std::vector<Sprites::Sprite>& out) {
    Tscn::Scene scene;
    std::string error;
    const bool ok = Tscn::Load(kLevels + "/" + level + ".tscn", scene, error) &&
                    Sprites::Find(scene, kLevels + "/..", out, error);
    CHECK_MSG(ok, level + ": " + error);
    return ok;
}

const Sprites::Sprite* Of(const std::vector<Sprites::Sprite>& sprites, const std::string& node) {
    for (const Sprites::Sprite& sprite : sprites) {
        if (sprite.node == node) return &sprite;
    }
    return nullptr;
}

std::string FileOf(const Sprites::Sprite* sprite) {
    return sprite != nullptr ? std::filesystem::path(sprite->texture).filename().string() : std::string();
}

void Level8sArt() {
    std::vector<Sprites::Sprite> sprites;
    if (!LoadLevel("level8", sprites)) return;
    CHECK_EQ(sprites.size(), std::size_t{23});

    bool inOrder = true;
    bool present = true;
    int added = 0;
    for (std::size_t i = 0; i < sprites.size(); ++i) {
        if (sprites[i].order != static_cast<int>(i) || (i > 0 && sprites[i - 1].zIndex > sprites[i].zIndex)) {
            inOrder = false;
        }
        std::error_code ec;
        if (!std::filesystem::is_regular_file(sprites[i].texture, ec)) present = false;
        if (sprites[i].additive) ++added;
    }
    CHECK_MSG(inOrder, "numbered from the back, and never a lower z_index in front of a higher");
    CHECK_MSG(present, "every image the level names is where res:// says");
    CHECK_MSG(added == 0, "level8 has no glow");
    CHECK_MSG(!sprites.empty() && sprites.front().node == "sky_629", "the sky, at z_index -100, is drawn first");

    const Sprites::Sprite* sky = Of(sprites, "sky_629");
    CHECK(sky != nullptr && FileOf(sky) == "icy_sky.png" && sky->sizePx == glm::dvec2(455.0, 256.0) &&
          sky->atPx == glm::dvec2(227.5, 128.0) && sky->zIndex == -100);
    const Sprites::Sprite* wall = Of(sprites, "wall_w3_ent_832");
    CHECK(wall != nullptr && FileOf(wall) == "wall_w3.png" && wall->sizePx == glm::dvec2(256.0, 256.0) &&
          wall->atPx == glm::dvec2(640.0, 128.0) && wall->zIndex == -20 && wall->offsetPx == glm::dvec2(0.0));
    const Sprites::Sprite* torch = Of(sprites, "light_ent_648");
    CHECK(torch != nullptr && FileOf(torch) == "torch_small.png" && torch->sizePx == glm::dvec2(32.0, 64.0) &&
          torch->offsetPx == glm::dvec2(0.0, 16.0) && torch->zIndex == -16);
    const Sprites::Sprite* doorway = Of(sprites, "door_bg_ent_596");
    CHECK_MSG(doorway != nullptr && FileOf(doorway) == "black.bmp" && doorway->sizePx == glm::dvec2(40.0, 100.0) &&
                  doorway->zIndex == -18,
              "the one BMP the converter copies, sized from its header");
    const Sprites::Sprite* wallToBreak = Of(sprites, "breakable_wall_625");
    CHECK(wallToBreak != nullptr && FileOf(wallToBreak) == "breakable_wall_sand.png" &&
          wallToBreak->sizePx == glm::dvec2(32.0, 128.0) && wallToBreak->zIndex == -2);
    const Sprites::Sprite* crystal = Of(sprites, "crystal_ent_790");
    CHECK(crystal != nullptr && FileOf(crystal) == "crystal.png" && crystal->sizePx == glm::dvec2(32.0, 32.0) &&
          crystal->atPx == glm::dvec2(83.0, 32.0) && crystal->zIndex == 0);

    // Two platforms at one z_index: the file's order decides.
    const Sprites::Sprite* first = Of(sprites, "moving_platform_single_ent_604");
    const Sprites::Sprite* second = Of(sprites, "moving_platform_single_ent_603");
    CHECK_MSG(first != nullptr && second != nullptr && first->zIndex == second->zIndex && first->order < second->order,
              "at one z_index, the one written first is drawn first");
}

void Level0sStaticPortalsAreAdded() {
    std::vector<Sprites::Sprite> sprites;
    if (!LoadLevel("level0", sprites)) return;
    CHECK_EQ(sprites.size(), std::size_t{18});
    int added = 0;
    for (const Sprites::Sprite& sprite : sprites) {
        if (sprite.additive) ++added;
    }
    CHECK_EQ(added, 4);
    for (const char* name : {"portal_static_604", "portal_static_603", "portal_static_582", "portal_static_583"}) {
        const Sprites::Sprite* portal = Of(sprites, name);
        CHECK_MSG(portal != nullptr && portal->additive && FileOf(portal) == "portal_halo.png" &&
                      portal->sizePx == glm::dvec2(64.0, 64.0),
                  std::string(name) + " is the halo, added");
    }
}

void AnOffsetAndATurn() {
    std::vector<Sprites::Sprite> boss;
    if (LoadLevel("level31a", boss)) {
        const Sprites::Sprite* dragon = Of(boss, "dragon_ent_1279");
        CHECK_MSG(dragon != nullptr && dragon->offsetPx == glm::dvec2(48.0, -7.0) && dragon->zIndex == 4,
                  "the one sprite offset sideways");
    }
    std::vector<Sprites::Sprite> tutorial;
    if (LoadLevel("level1", tutorial)) {
        const Sprites::Sprite* block = Of(tutorial, "block00_ent_598");
        CHECK_MSG(block != nullptr && Near(block->rotation, 0.7854), "an eighth of a turn, as its node has");
    }
}

} // namespace

int main() {
    AnImageSaysItsSizeInItsHeader();
    AnythingElseIsRefusedByName();
    TheCanvasOrderIsZThenTheFile();
    WhatTheReaderDoesNotDrawIsNamed();
    ThePortalAndTheShotAreTheirEnts();
    ASheetThatDoesNotSayHowFastIsRefused();

    std::error_code original;
    if (std::filesystem::is_directory(kOriginal + "/entities", original)) {
        TheOriginalsImagesAreCutAsTheEntsSay();
    } else {
        std::printf("test_mp_sprites: the original's images SKIPPED - needs its extracted assets at %s.\n",
                    kOriginal.c_str());
    }

    std::error_code ec;
    if (!std::filesystem::is_directory(kLevels, ec)) {
        std::printf("test_mp_sprites: the levels' part SKIPPED - needs the converted levels at %s.\n"
                    "  They live outside this repository; configure with -DSUPERSONIC_MAGICPORTALS_LEVELS=...\n",
                    kLevels.c_str());
        return ::test::summary("test_mp_sprites", 40);
    }
    Level8sArt();
    Level0sStaticPortalsAreAdded();
    AnOffsetAndATurn();
    return ::test::summary("test_mp_sprites", 60);
}
