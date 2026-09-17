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

#include "core/AudioClip.hpp"

#include "sim/Art.hpp"
#include "sim/Lighting.hpp"
#include "sim/Particles.hpp"
#include "sim/Sprites.hpp"
#include "sim/Tscn.hpp"

#include <stb_image.h>

#include <cctype>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <system_error>
#include <utility>
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

// A JPEG's start as a camera writes one: the start marker, a JFIF segment and a
// quantisation table, then a frame header of `marker` (0xC0 baseline) at 8 bits
// and three components - height BEFORE width, the reverse of a PNG. The scan
// that would follow is left off: the reader stops at the frame header.
std::vector<unsigned char> JpegHeader(uint16_t width, uint16_t height, unsigned char marker = 0xC0) {
    std::vector<unsigned char> bytes = {0xFF, 0xD8, 0xFF, 0xE0, 0, 16, 'J', 'F', 'I', 'F', 0, 1, 1, 0, 0, 1, 0, 1, 0, 0};
    bytes.insert(bytes.end(), {0xFF, 0xDB, 0, 67, 0});
    bytes.insert(bytes.end(), 64, static_cast<unsigned char>(1));
    bytes.insert(bytes.end(), {0xFF, marker, 0, 17, 8});
    for (const uint16_t v : {height, width}) {
        bytes.push_back(static_cast<unsigned char>(v >> 8));
        bytes.push_back(static_cast<unsigned char>(v));
    }
    bytes.insert(bytes.end(), {3, 1, 0x22, 0, 2, 0x11, 1, 3, 0x11, 1});
    return bytes;
}

// Where JpegHeader's frame header starts: after the JFIF segment and the table.
constexpr std::size_t kJpegFrameAt = 20 + 69;

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

// A JPEG's size is in its frame header, behind its other segments (K16: the
// particle systems of fireball, burn_projectile, dragon, dark_dragon and
// ghost_utility_spawn draw particles/explosion.JPG).
void AJpegSaysItsSizeInItsFrameHeader() {
    const std::filesystem::path dir = Scratch();
    int w = 0;
    int h = 0;
    std::string error;

    // Not square, so a width and a height read the wrong way round would show.
    Write(dir / "wide.jpg", JpegHeader(455, 256));
    CHECK_MSG(Sprites::ImageSize((dir / "wide.jpg").string(), w, h, error), error);
    CHECK_EQ(w, 455);
    CHECK_EQ(h, 256);

    // Progressive, with fill bytes before its frame header's marker and a byte of
    // padding after the table, both of which the renderer's decoder passes over.
    std::vector<unsigned char> padded = JpegHeader(32, 100, 0xC2);
    padded.insert(padded.begin() + static_cast<std::ptrdiff_t>(kJpegFrameAt), {0x00, 0xFF, 0xFF});
    Write(dir / "tall.jpg", padded);
    CHECK_MSG(Sprites::ImageSize((dir / "tall.jpg").string(), w, h, error), error);
    CHECK_EQ(w, 32);
    CHECK_EQ(h, 100);

    // Extended sequential, one component: a frame header of 11 bytes.
    std::vector<unsigned char> grey = JpegHeader(7, 9, 0xC1);
    grey.resize(kJpegFrameAt + 9);
    grey[kJpegFrameAt + 3] = 11;
    grey.insert(grey.end(), {1, 1, 0x11, 0}); // one component: its id, sampling and table
    Write(dir / "grey.jpg", grey);
    CHECK_MSG(Sprites::ImageSize((dir / "grey.jpg").string(), w, h, error), error);
    CHECK_EQ(w, 7);
    CHECK_EQ(h, 9);
}

// A JPEG cut short, or one the renderer's decoder (stb_image) would not read, has
// no size: a size for it would put a particle system on a texture that draws as
// the fallback.
void AJpegTheRendererCannotReadIsRefused() {
    const std::filesystem::path dir = Scratch();
    const auto refused = [&dir](const char* name, const std::vector<unsigned char>& bytes, const char* says) {
        int w = 0;
        int h = 0;
        std::string error;
        Write(dir / name, bytes);
        const bool sized = Sprites::ImageSize((dir / name).string(), w, h, error);
        CHECK_MSG(!sized, std::string(name) + " was sized " + std::to_string(w) + " x " + std::to_string(h));
        CHECK_MSG(error.find(name) != std::string::npos && error.find(says) != std::string::npos,
                  std::string(name) + ": " + error);
    };
    const std::vector<unsigned char> whole = JpegHeader(64, 32);

    std::vector<unsigned char> cut = whole;
    cut.resize(kJpegFrameAt);
    refused("cut_before_frame.jpg", cut, "cut short before its frame header");
    cut = whole;
    cut.resize(kJpegFrameAt + 6);
    refused("cut_in_frame.jpg", cut, "cut short in its frame header");
    cut = whole;
    cut.resize(40); // inside the table, whose length says 67
    refused("cut_in_table.jpg", cut, "cut short before its frame header");
    refused("start_only.jpg", {0xFF, 0xD8}, "cut short before its frame header");

    refused("lossless.jpg", JpegHeader(64, 32, 0xC3), "marker 0xC3");
    refused("arithmetic.jpg", JpegHeader(64, 32, 0xC9), "marker 0xC9");
    std::vector<unsigned char> scanFirst = whole;
    scanFirst[kJpegFrameAt + 1] = 0xDA;
    refused("scan_first.jpg", scanFirst, "marker 0xDA");

    std::vector<unsigned char> twelve = whole;
    twelve[kJpegFrameAt + 4] = 12;
    refused("twelve_bit.jpg", twelve, "12 bits a sample");
    std::vector<unsigned char> components = whole;
    components[kJpegFrameAt + 9] = 2;
    refused("two_components.jpg", components, "frame header the renderer does not decode");
    std::vector<unsigned char> delayed = whole; // the height left to a later DNL segment
    delayed[kJpegFrameAt + 5] = 0;
    delayed[kJpegFrameAt + 6] = 0;
    refused("delayed_height.jpg", delayed, "says it has no size");

    std::vector<unsigned char> shortSegment = whole;
    shortSegment[5] = 1;
    refused("short_segment.jpg", shortSegment, "shorter than its own length");
    refused("no_marker.jpg", {0xFF, 0xD8, 0x00, 0xFF, 0xC0}, "no marker after its start");
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

    // Chapter 1's boss and its spikes (step 11b): the .ent files' facts, and the
    // pulse bounce() gives it in each thing it does, decoded from its script.
    const Art::Beholder& beholder = rules.beholder;
    CHECK_MSG(beholder.sprite == "beholder.png" && !beholder.additive && beholder.columns == 2 && beholder.rows == 1,
              "beholder.ent: its sheet, cut 2 x 1, mixed");
    const auto pulse = [](const Art::Pulse& p, glm::dvec2 from, glm::dvec2 to, double strideMs) {
        return p.fromScale == from && p.toScale == to && p.strideMs == strideMs;
    };
    CHECK_MSG(pulse(beholder.seeking, {1.0, 1.02}, {1.02, 1.0}, 400.0), "seeking, it breathes across");
    CHECK_MSG(pulse(beholder.hurt, {1.0, 1.0}, {1.25, 1.25}, 600.0), "hurt, it swells");
    CHECK_MSG(pulse(beholder.dead, {1.1, 1.1}, {1.25, 1.25}, 400.0), "dead, it throbs");
    CHECK_MSG(rules.spike.sprite == "beholder_spike.png" && !rules.spike.additive && rules.spike.Frames() == 1 &&
                  rules.spike.pivotXPx == 0.0 && rules.spike.pivotYPx == 10.0,
              "beholder_spike.ent: one frame, its pivot 10 px below the middle");

    // Each .ent's <EmissiveColor>, which the layer draws it with (step 45). Only
    // the shot's is below one, so only the shot is ever dimmed by a level's ambient.
    CHECK_MSG(rules.portal.emissive == glm::dvec3(1.0), "portal.ent: emissive 1");
    CHECK_MSG(rules.shot.emissive == glm::dvec3(0.6, 0.6, 1.0), "projectile.ent: emissive (0.6, 0.6, 1)");
    CHECK_MSG(mage.emissive == glm::dvec3(1.0), "dark_mage.ent: emissive 1");
    CHECK_MSG(beholder.emissive == glm::dvec3(1.0), "beholder.ent: emissive 1");
    CHECK_MSG(rules.spike.emissive == glm::dvec3(1.0), "beholder_spike.ent: emissive 1");

    // And their lighting (step 49). Only the player applies light: not static,
    // through normalmap_77.png. Only the shot has a light: projectile.ent's, live,
    // range 70, (0.6, 0.6, 1) 12 below it, with halo.bmp at 50 and 0.65.
    CHECK_MSG(!mage.isStatic && mage.applyLight && mage.normal == "normalmap_77.png" && !mage.light,
              "dark_mage.ent: not static, applies light through normalmap_77.png, no light of its own");
    CHECK_MSG(!rules.shot.isStatic && !rules.shot.applyLight && rules.shot.normal.empty(),
              "projectile.ent: not static, applies no light");
    CHECK_MSG(rules.shot.light.has_value(), "projectile.ent has a light");
    if (rules.shot.light) {
        const Lighting::Light& light = *rules.shot.light;
        CHECK_MSG(light.range == 70.0 && light.offset == glm::dvec3(0.0, 0.0, -12.0) &&
                      light.colour == glm::dvec3(0.6, 0.6, 1.0),
                  "range 70, offset (0, 0, -12), colour (0.6, 0.6, 1)");
        CHECK_MSG(light.halo == "halo.bmp" && light.haloOffset == glm::dvec2(0.0) &&
                      light.haloSize == glm::dvec2(50.0) && light.haloBrightness == 0.65,
                  "halo.bmp, centred, 50 units, brightness 0.65");
        CHECK_MSG(rules.shot.z == 0.0, "measured from the shot's depth, 0");
    }
    for (const Art::Picture* picture : {static_cast<const Art::Picture*>(&rules.portal),
                                        static_cast<const Art::Picture*>(&beholder),
                                        static_cast<const Art::Picture*>(&rules.spike)}) {
        CHECK_MSG(!picture->isStatic && !picture->applyLight && picture->normal.empty() && !picture->light,
                  picture->sprite + ": not static, applies no light, no normal map, no light");
    }

    // A static portal as ETHCallback_portal_static redraws it (bytes 360571..361404):
    // Scale(0.8f) once (ops 14-16), then SetColor by its `color` (ops 31-79), the
    // vector3's last argument pushed first - so 'red' is (1, 0.3, 0.3) and anything
    // else, an absent colour included, (0.3, 0.3, 1).
    const Art::StaticPortal& statics = rules.staticPortal;
    CHECK_MSG(statics.entity == "portal_static" && statics.scale == 0.8 && statics.red == "red",
              "portal_static, scaled by 0.8, red by the colour 'red'");
    CHECK_MSG(statics.tintRed == glm::dvec3(1.0, 0.3, 0.3) && statics.tintOtherwise == glm::dvec3(0.3, 0.3, 1.0),
              "tinted (1, 0.3, 0.3) when red, (0.3, 0.3, 1) otherwise");
    CHECK_MSG(statics.TintFor("red") == statics.tintRed && statics.TintFor("blue") == statics.tintOtherwise &&
                  statics.TintFor("") == statics.tintOtherwise,
              "GetString('color') == 'red': a blue portal and one with no colour both take the other tint");
}

// The static portal's script is not optional: without it the port draws the white,
// unscaled halo the footage refuses (step 59's ring-excess row).
void AStaticPortalWithoutItsScriptIsRefused() {
    std::ifstream file(std::string(MAGICPORTALS_PORT_DATA_DIR) + "/art.json", std::ios::binary);
    std::ostringstream buffer;
    buffer << file.rdbuf();
    const std::string real = buffer.str();
    CHECK(real.find("\"static_portal\"") != std::string::npos);
    const auto refused = [&real](const std::string& from, const std::string& to, const std::string& name) {
        std::string text = real;
        const std::size_t at = text.find(from);
        CHECK_MSG(at != std::string::npos, from);
        if (at == std::string::npos) return;
        text.replace(at, from.size(), to);
        const std::filesystem::path path = Scratch() / ("art-static-portal-" + name + ".json");
        Write(path, std::vector<unsigned char>(text.begin(), text.end()));
        Art::Rules rules;
        std::string error;
        const bool ok = Art::LoadRules(path.string(), rules, error);
        CHECK_MSG(!ok, "refused: " + name);
        CHECK_MSG(error.find("static_portal needs") != std::string::npos, name + ": " + error);
    };
    refused("\"static_portal\"", "\"static_portal_gone\"", "absent");
    refused("\"scale\": 0.8", "\"scale\": 0", "scale 0");
    refused("\"tint_red\": [1.0, 0.3, 0.3]", "\"tint_red\": [1.0, 0.3]", "two numbers");
    refused("\"tint_otherwise\": [0.3, 0.3, 1.0]", "\"tint_otherwise\": [0.3, -0.3, 1.0]", "below zero");
    refused("\"red\": \"red\"", "\"red\": \"\"", "no red");
}

// The dial behind a timed crystal (art.json timer): timer.ent's facts, pinned. The
// clock it runs by is test_mp_timed's.
void TheTimerIsTimerEnt() {
    Art::Rules rules;
    std::string error;
    const bool ok = Art::LoadRules(std::string(MAGICPORTALS_PORT_DATA_DIR) + "/art.json", rules, error);
    CHECK_MSG(ok, error);
    const Art::Timer& timer = rules.timer;
    CHECK_MSG(timer.sprite == "timer.png" && !timer.additive && timer.columns == 4 && timer.rows == 2,
              "timer.ent: timer.png, cut 4 x 2, mixed");
    CHECK_MSG(timer.frames == 8 && timer.frames == timer.Frames(), "the time in eight cells, every cell of the cut");
    CHECK_MSG(timer.emissive == glm::dvec3(1.0) && !timer.isStatic && !timer.applyLight && timer.normal.empty() &&
                  !timer.light,
              "emissive 1, not static, applies no light, no normal map, no light");
    CHECK_MSG(timer.framesPerSecond == 0.0, "its cell is chosen by the time, never played");
    CHECK_MSG(timer.alpha == 0.5 && timer.zOffset == -2, "SetAlpha(0.5f), 2 behind its crystal");
}

// Without the timer the port would draw no dial - or the remake's fade, which the
// original never had.
void ATimerWithoutItsClockIsRefused() {
    std::ifstream file(std::string(MAGICPORTALS_PORT_DATA_DIR) + "/art.json", std::ios::binary);
    std::ostringstream buffer;
    buffer << file.rdbuf();
    const std::string real = buffer.str();
    const auto refused = [&real](const std::string& from, const std::string& to, const std::string& name,
                                 const std::string& says) {
        std::string text = real;
        const std::size_t at = text.find(from);
        CHECK_MSG(at != std::string::npos, from);
        if (at == std::string::npos) return;
        text.replace(at, from.size(), to);
        const std::filesystem::path path = Scratch() / ("art-timer-" + name + ".json");
        Write(path, std::vector<unsigned char>(text.begin(), text.end()));
        Art::Rules rules;
        std::string error;
        const bool ok = Art::LoadRules(path.string(), rules, error);
        CHECK_MSG(!ok, "refused: " + name);
        CHECK_MSG(error.find(says) != std::string::npos, name + ": " + error);
    };
    refused("\"timer\": {", "\"timer_gone\": {", "absent", "timer is an object");
    refused("\"sprite\": \"timer.png\",", "", "no sprite", "timer needs sprite and additive");
    refused("\"frames\": 8,", "\"frames\": 9,", "more frames than cells", "timer needs frames");
    refused("\"alpha\": 0.5,", "\"alpha\": 0,", "alpha 0", "timer needs frames");
    refused("\"z_offset\": -2,", "\"z_offset\": -2.5,", "a z_offset between depths", "timer needs frames");
    refused("\"pulse_to\": 1.15,", "", "no pulse_to", "timer needs frames");
    refused("\"pulse_min_leg_ms\": 400.0,", "\"pulse_min_leg_ms\": 0,", "a leg of 0", "timer needs frames");
    refused("\"shrink_per_frame\": 0.9,", "\"shrink_per_frame\": 1.0,", "no shrink", "timer needs frames");
    refused("\"gone_below_scale\": 0.1", "\"gone_below_scale\": 0", "never gone", "timer needs frames");
    refused("\"screen_px_per_unit\": 2.8125,", "\"screen_px_per_unit\": 0,", "no scale factor", "timer needs frames");
}

void APulseGoesThereAndBack() {
    // bounce(): from one scale to the other in a stride, eased by smoothEnd, and
    // back in the next.
    Art::Pulse p;
    p.fromScale = glm::dvec2(1.0, 1.0);
    p.toScale = glm::dvec2(1.25, 1.5);
    p.strideMs = 600.0;
    const auto near = [](const glm::dvec2& got, double x, double y) {
        return std::fabs(got.x - x) < 1e-5 && std::fabs(got.y - y) < 1e-5;
    };
    const double half = std::sin(3.14159265358979 / 4.0);
    CHECK(near(p.ScaleAt(0.0), 1.0, 1.0));
    CHECK(near(p.ScaleAt(300.0), 1.0 + 0.25 * half, 1.0 + 0.5 * half));
    CHECK(near(p.ScaleAt(600.0), 1.25, 1.5));
    CHECK(near(p.ScaleAt(900.0), 1.25 - 0.25 * (1.0 - half), 1.5 - 0.5 * (1.0 - half)));
    CHECK(near(p.ScaleAt(1200.0), 1.0, 1.0));
}

void ASheetThatDoesNotSayHowFastIsRefused() {
    const std::filesystem::path path = Scratch() / "art.json";
    // Everything else in order, so the shot's missing rate is what is refused.
    const std::string text = R"({"portal": {"sprite": "a.png", "additive": true, "emissive": [1, 1, 1],
                                            "static": false, "apply_light": false},
                                 "shot": {"sprite": "b.png", "additive": true, "columns": 6, "emissive": [1, 1, 1],
                                          "static": false, "apply_light": false},
                                 "torch_light": {"sprite": "d.png", "additive": false, "emissive": [0, 0, 0],
                                                 "static": true, "apply_light": true},
                                 "character": {"sprite": "c.png", "additive": false, "columns": 4, "rows": 4,
                                               "emissive": [1, 1, 1], "static": false, "apply_light": true,
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

void APictureWithoutItsEmissiveIsRefused() {
    // The emissive decides how dark a picture is drawn, and a default nobody
    // decoded would be the ambient alone: a black player on a dark level.
    const std::filesystem::path path = Scratch() / "art-no-emissive.json";
    for (const std::string& emissive : {std::string(), std::string(R"(, "emissive": [1, 1])"),
                                        std::string(R"(, "emissive": [1, -0.5, 1])")}) {
        const std::string text = R"({"portal": {"sprite": "a.png", "additive": true, "static": false,
                                                "apply_light": false)" + emissive + R"(},
                                     "shot": {"sprite": "b.png", "additive": true, "emissive": [1, 1, 1],
                                              "static": false, "apply_light": false},
                                     "torch_light": {"sprite": "d.png", "additive": false, "emissive": [0, 0, 0],
                                                     "static": true, "apply_light": true},
                                     "character": {"sprite": "c.png", "additive": false, "emissive": [1, 1, 1],
                                                   "static": false, "apply_light": true}})";
        Write(path, std::vector<unsigned char>(text.begin(), text.end()));
        Art::Rules rules;
        std::string error;
        const bool ok = Art::LoadRules(path.string(), rules, error);
        CHECK_MSG(!ok, "refused: portal" + emissive);
        CHECK_MSG(error.find("portal's emissive") != std::string::npos, error);
    }
}

void APictureWithoutItsLightingIsRefused() {
    // Which lights reach a picture decides whether the player is lit at all, and
    // a light's every number is its .ent's: none of it is defaulted.
    const std::filesystem::path path = Scratch() / "art-lighting.json";
    const auto refused = [&](const std::string& portal, const std::string& shot, const std::string& says) {
        const std::string text = R"({"portal": {"sprite": "a.png", "additive": true, "emissive": [1, 1, 1])" + portal +
                                 R"(}, "shot": {"sprite": "b.png", "additive": true, "emissive": [1, 1, 1],
                                                "static": false, "apply_light": false)" + shot +
                                 R"(}, "torch_light": {"sprite": "d.png", "additive": false, "emissive": [0, 0, 0],
                                                       "static": true, "apply_light": true},
                                     "character": {"sprite": "c.png", "additive": false, "emissive": [1, 1, 1],
                                                     "static": false, "apply_light": true}})";
        Write(path, std::vector<unsigned char>(text.begin(), text.end()));
        Art::Rules rules;
        std::string error;
        const bool ok = Art::LoadRules(path.string(), rules, error);
        CHECK_MSG(!ok, "refused: portal" + portal + " shot" + shot);
        CHECK_MSG(error.find(says) != std::string::npos, error);
    };
    const std::string flags = R"(, "static": false, "apply_light": false)";
    const std::string light = R"(, "light": {"range": 70, "offset": [0, 0, -12], "colour": [0.6, 0.6, 1],
                                  "halo": "halo.bmp", "halo_offset": [0, 0], "halo_size": [50, 50],
                                  "halo_brightness": 0.65}, "z": {"value": 0})";
    refused("", "", "portal needs static and apply_light");
    refused(R"(, "static": false)", "", "portal needs static and apply_light");
    refused(R"(, "static": 0, "apply_light": false)", "", "portal needs static and apply_light");
    refused(flags + R"(, "normal": "")", "", "portal's normal is not a file name");
    // The shot's light, with each of its numbers taken out or broken in turn.
    const auto without = [&light](const std::string& from, const std::string& to) {
        std::string changed = light;
        const std::size_t at = changed.find(from);
        CHECK_MSG(at != std::string::npos, from);
        if (at != std::string::npos) changed.replace(at, from.size(), to);
        return changed;
    };
    refused(flags, without(R"("range": 70, )", ""), "shot's light needs a range above 0");
    refused(flags, without(R"("range": 70)", R"("range": 0)"), "shot's light needs a range above 0");
    refused(flags, without(R"([0, 0, -12])", "[0, -12]"), "shot's light needs");
    refused(flags, without(R"([0.6, 0.6, 1])", "[0.6, -0.6, 1]"), "shot's light needs");
    refused(flags, without(R"("halo": "halo.bmp", )", ""), "shot's light needs");
    refused(flags, without(R"([50, 50])", "[50, 0]"), "shot's light needs");
    refused(flags, without(R"("halo_brightness": 0.65)", R"("halo_brightness": "0.65")"), "shot's light needs");
    refused(flags, without(R"(, "z": {"value": 0})", ""), "shot has a light and no z.value");
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
    const bool beholder = Sprites::ImageSize(kOriginal + "/entities/beholder.png", w, h, error);
    CHECK_MSG(beholder && w == 2 * 128 && h == 128, "beholder.png is two frames of 128 x 128: " + error);
    const bool spike = Sprites::ImageSize(kOriginal + "/entities/beholder_spike.png", w, h, error);
    CHECK_MSG(spike && w == 16 && h == 32, "beholder_spike.png is 16 x 32: " + error);
    // The player's normal map is sampled with the sheet's own coordinates, so it is
    // cut 4 x 4 like the sheet: sixteen cells of 32 x 32 (step 49). And the shot's
    // halo is halo.bmp, an 8-bit BMP the header reader sizes.
    const bool normal = Sprites::ImageSize(kOriginal + "/entities/normalmaps/normalmap_77.png", w, h, error);
    CHECK_MSG(normal && w == 4 * 32 && h == 4 * 32, "normalmap_77.png is sixteen cells of 32 x 32: " + error);
    const bool halo2 = Sprites::ImageSize(kOriginal + "/entities/halo.bmp", w, h, error);
    CHECK_MSG(halo2 && w == 64 && h == 64, "halo.bmp is 64 x 64: " + error);
    // The timed crystal's dial: eight cells of 32 x 32, and no hd or fullhd twin, so
    // the 1x picture is the one every screen draws.
    const bool timer = Sprites::ImageSize(kOriginal + "/entities/timer.png", w, h, error);
    CHECK_MSG(timer && w == 4 * 32 && h == 2 * 32, "timer.png is eight cells of 32 x 32: " + error);
    std::error_code ec;
    CHECK_MSG(!std::filesystem::exists(kOriginal + "/entities/hd/timer.png", ec) &&
                  !std::filesystem::exists(kOriginal + "/entities/fullhd/timer.png", ec),
              "timer.png has no hd or fullhd twin");
    // The one JPEG a particle system names (K16): its frame header, after an Exif
    // segment and two tables, says 32 x 32.
    const bool explosion = Sprites::ImageSize(kOriginal + "/particles/explosion.JPG", w, h, error);
    CHECK_MSG(explosion && w == 32 && h == 32, "explosion.JPG is 32 x 32: " + error);
}

// The header reader and the renderer's decoder agree on every image the original
// ships: the same size where stb_image reads a header (stbi_info), and a refusal
// where it does not. So no particle system or sprite is sized for a texture that
// draws as the fallback, nor refused for one that would draw.
void EveryOriginalImageIsSizedAsTheRendererReadsIt() {
    int images = 0;
    int jpegs = 0;
    int disagree = 0;
    std::error_code ec;
    for (std::filesystem::recursive_directory_iterator it(kOriginal, ec), end; !ec && it != end; it.increment(ec)) {
        if (!it->is_regular_file(ec)) continue;
        std::string extension = it->path().extension().string();
        for (char& c : extension) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        if (extension != ".png" && extension != ".bmp" && extension != ".jpg" && extension != ".jpeg") continue;
        ++images;
        if (extension == ".jpg" || extension == ".jpeg") ++jpegs;
        const std::string path = it->path().string();
        int w = 0;
        int h = 0;
        std::string error;
        const bool sized = Sprites::ImageSize(path, w, h, error);
        int sw = 0;
        int sh = 0;
        int channels = 0;
        const bool read = stbi_info(path.c_str(), &sw, &sh, &channels) != 0;
        if (sized != read || (sized && (w != sw || h != sh))) {
            ++disagree;
            CHECK_MSG(false, path + ": the reader says " + (sized ? std::to_string(w) + " x " + std::to_string(h) : error) +
                                 ", stb_image " + (read ? std::to_string(sw) + " x " + std::to_string(sh) : "no size"));
        }
    }
    std::printf("  the original's images: %d (%d JPEG), %d sized otherwise than stb_image reads them\n", images, jpegs,
                disagree);
    CHECK_MSG(images > 1000 && jpegs == 2, "the original ships over 1,000 images, two of them JPEGs: " +
                                               std::to_string(images) + ", " + std::to_string(jpegs));
    CHECK_EQ(disagree, 0);
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

// ---- the original's sounds, decoded -------------------------------------------

// One of the original's own mp3s, through the engine's decoder.
//
// Here rather than in test_audio because the file is the ORIGINAL's: this suite
// already knows where its assets are and already skips when they are absent,
// and test_audio links the engine alone and has no path to them.
//
// Asserts the SHAPE rather than particular numbers - channels, a rate, 16-bit,
// a duration above zero - because the engine's contract is what matters here
// and a sample rate copied out of a file I had not measured would be a pin on
// nothing. What it really proves is that the decode path runs at all: both
// toolchains build it, and no suite in the tree had ever decoded a byte.
void TheOriginalsMp3sDecode() {
    const std::string path = kOriginal + "/soundfx/door_open.mp3";
    std::error_code ec;
    if (!std::filesystem::is_regular_file(path, ec)) {
        std::printf("test_mp_sprites: the mp3 decode SKIPPED - no %s.\n", path.c_str());
        return;
    }

    Supersonic::AudioClip clip;
    std::string error;
    const bool ok = Supersonic::AudioClip::Load(path, clip, error);

#if defined(_WIN32)
    CHECK_MSG(ok, "door_open.mp3 did not decode: " + error);
    if (!ok) return;
    CHECK(clip.valid());
    CHECK_MSG(clip.channels >= 1 && clip.channels <= 2,
              "channels " + std::to_string(clip.channels));
    CHECK(clip.sampleRate > 0);
    CHECK_EQ(static_cast<int>(clip.bitsPerSample), 16);
    CHECK_MSG(clip.durationSeconds() > 0.0f,
              "duration " + std::to_string(clip.durationSeconds()));
#else
    // Media Foundation is Windows only, so elsewhere this must fail with a
    // reason rather than half-decode or crash.
    CHECK_MSG(!ok, "mp3 must not decode without Media Foundation");
    CHECK(!error.empty());
    CHECK(!clip.valid());
#endif

    // And a sound the engine does not read is refused by name, on every
    // platform, rather than being guessed at.
    Supersonic::AudioClip other;
    std::string otherError;
    CHECK(!Supersonic::AudioClip::Load(kOriginal + "/soundfx/door_open.flac", other, otherError));
    CHECK(!otherError.empty());
}

// ---- the particle systems the .ent files carry --------------------------------

// crystal.ent and fire32.ent, read out of the original's own files. Two
// systems chosen because they differ in every way that matters: one sparkle on
// a still 1x1 bitmap that plays its sheet by age, and thirty-two flames on a
// four-frame cut that pick a frame at random and live a fixed number of times.
void TheOriginalsParticlesAreRead() {
    std::vector<Particles::System> systems;
    std::string error;
    CHECK_MSG(Particles::Load(kOriginal + "/entities/crystal.ent", systems, error), error);
    CHECK_EQ(static_cast<int>(systems.size()), 1);
    if (systems.size() == 1) {
        const Particles::System& crystal = systems.front();
        CHECK_MSG(crystal.bitmap == "sparkles.bmp", "its bitmap is " + crystal.bitmap);
        CHECK_EQ(crystal.count, 2);
        CHECK(crystal.additive);            // alphaMode 1, AM_ADD
        CHECK_EQ(crystal.animationMode, 1); // PLAY_ANIMATION
        CHECK_EQ(crystal.repeat, 0);
        CHECK(!crystal.allAtOnce);
        CHECK_NEAR(static_cast<float>(crystal.lifeTimeMs), 1700.0f);
        CHECK_NEAR(static_cast<float>(crystal.randomLifeTimeMs), 1050.0f);
        CHECK_NEAR(static_cast<float>(crystal.size), 12.0f);
        CHECK_NEAR(static_cast<float>(crystal.randomizeSize), 5.0f);
        CHECK_NEAR(static_cast<float>(crystal.growth), -0.05f);
        CHECK_NEAR(static_cast<float>(crystal.maxSize), 1000.0f);
        CHECK_NEAR(static_cast<float>(crystal.angleDir), 2.0f);
        CHECK_NEAR(static_cast<float>(crystal.randAngle), 2.0f);
        CHECK_NEAR(static_cast<float>(crystal.randAngleStart), 360.0f);
        CHECK_NEAR(static_cast<float>(crystal.startPoint.y), -2.5f);
        CHECK_NEAR(static_cast<float>(crystal.randStartPoint.x), 12.0f);
        CHECK_NEAR(static_cast<float>(crystal.randStartPoint.y), 7.0f);
        CHECK_NEAR(static_cast<float>(crystal.colour0.b), 1.0f);
        CHECK_NEAR(static_cast<float>(crystal.colour1.a), 0.0f);
        CHECK_EQ(crystal.Frames(), 1);
    }

    CHECK_MSG(Particles::Load(kOriginal + "/entities/fire32.ent", systems, error), error);
    CHECK_EQ(static_cast<int>(systems.size()), 1);
    if (systems.size() == 1) {
        const Particles::System& fire = systems.front();
        CHECK_MSG(fire.bitmap == "fire.png", "its bitmap is " + fire.bitmap);
        CHECK_EQ(fire.count, 32);
        CHECK_EQ(fire.animationMode, 2); // PICK_RANDOM_FRAME
        CHECK_EQ(fire.repeat, 2);
        CHECK_EQ(fire.columns, 4);
        CHECK_EQ(fire.rows, 1);
        CHECK_EQ(fire.Frames(), 4);
        CHECK_NEAR(static_cast<float>(fire.lifeTimeMs), 450.0f);
        CHECK_NEAR(static_cast<float>(fire.size), 64.0f);
        CHECK_NEAR(static_cast<float>(fire.growth), -2.5f);
        CHECK_NEAR(static_cast<float>(fire.direction.y), -2.3f);
        CHECK_NEAR(static_cast<float>(fire.randomizeDir.x), 0.7f);
        CHECK_NEAR(static_cast<float>(fire.randAngle), 11.8f);
    }

    // An entity may carry more than one, and a chapter-1 emitter does:
    // portal_static holds two, so a reader that took only the first would draw
    // half of every static portal and say nothing about the rest.
    CHECK_MSG(Particles::Load(kOriginal + "/entities/portal_static.ent", systems, error), error);
    CHECK_EQ(static_cast<int>(systems.size()), 2);
    for (const Particles::System& system : systems) {
        CHECK(!system.bitmap.empty());
        CHECK(system.count > 0);
    }
}

// Every .ent of the original's reads, 102 of them carry a system, and every
// bitmap one names is really there.
//
// The last of those is the check worth having: the particle bitmaps live in
// the original's `particles/` directory, a THIRD place beside its entities and
// its sprites, and looking in the wrong one is a mistake this port has already
// made twice with art that then went missing in silence.
void AnEntityWithoutParticlesSaysSoWithoutFailing() {
    int files = 0;
    int withSystem = 0; // files carrying at least one
    int systemsRead = 0;
    int missingBitmaps = 0;
    std::string firstError;
    std::string firstMissing;
    std::error_code ec;
    for (const auto& entry : std::filesystem::directory_iterator(kOriginal + "/entities", ec)) {
        if (!entry.is_regular_file() || entry.path().extension() != ".ent") continue;
        ++files;
        std::vector<Particles::System> systems;
        std::string error;
        if (!Particles::Load(entry.path().string(), systems, error)) {
            if (firstError.empty()) firstError = error;
            continue;
        }
        if (systems.empty()) continue; // 109 of them carry none, which is no error
        ++withSystem;
        systemsRead += static_cast<int>(systems.size());
        for (const Particles::System& system : systems) {
            const std::string bitmap = kOriginal + "/particles/" + system.bitmap;
            if (!std::filesystem::is_regular_file(bitmap, ec)) {
                ++missingBitmaps;
                if (firstMissing.empty()) firstMissing = system.bitmap;
            }
        }
    }
    CHECK_MSG(firstError.empty(), "an .ent would not read: " + firstError);
    CHECK_EQ(files, 190);
    // 81 files carry systems and there are 102 of them: 21 entities hold two,
    // which is why the reader returns all of an entity's rather than its first.
    CHECK_EQ(withSystem, 81);
    CHECK_EQ(systemsRead, 102);
    CHECK_MSG(missingBitmaps == 0,
              "the original's particles/ is missing " + std::to_string(missingBitmaps) + " bitmap(s), first " +
                  firstMissing);

    // And a file that is not the original's is refused rather than mangled.
    const std::filesystem::path scratch = Scratch() / "not-utf16.ent";
    Write(scratch, {'<', 'E', 't', 'h', 'a', 'n', 'o', 'n', '>'});
    std::vector<Particles::System> systems;
    std::string error;
    CHECK(!Particles::Load(scratch.string(), systems, error));
    CHECK(!error.empty());
    CHECK(systems.empty());
    CHECK(!Particles::Load((Scratch() / "no-such-file.ent").string(), systems, error));
}

// ---- a particle, moved and drawn as ETHParticleManager moves and draws it --------
//
// Synthetic systems and a generator that always answers the middle of its range,
// so every spread is zero and every number below is the arithmetic alone.

const Particles::Random kMiddle = [](double from, double to) { return (from + to) * 0.5; };

bool NearD(double a, double b, double eps = 1e-9) {
    return std::fabs(a - b) <= eps;
}

bool NearP(const glm::dvec2& a, const glm::dvec2& b, double eps = 1e-9) {
    return NearD(a.x, b.x, eps) && NearD(a.y, b.y, eps);
}

std::string ShowP(const glm::dvec2& p) {
    return "(" + std::to_string(p.x) + ", " + std::to_string(p.y) + ")";
}

// light.ent's flame, as the file states it (the reader pins the real one above).
Particles::System TorchFlame() {
    Particles::System s;
    s.bitmap = "fire.png";
    s.count = 12;
    s.alphaMode = Particles::kAlphaAdd;
    s.additive = true;
    s.animationMode = 2;
    s.lifeTimeMs = 450.0;
    s.size = 28.0;
    s.growth = -1.4;
    s.maxSize = 1000.0;
    s.direction = glm::dvec2(0.0, -1.6);
    s.startPoint = glm::dvec2(0.0, -12.0);
    s.colour0 = glm::dvec4(1.0, 0.4, 0.2, 1.0);
    s.colour1 = s.colour0;
    s.luminance = glm::dvec3(0.0);
    s.columns = 4;
    return s;
}

// 2a: a particle starts at its entity's POSITION plus its start point, turned by
// the entity's angle - not at the centre of the entity's picture. The torch of
// 1-1 stands at (288, 64) with its picture hung 16 below (offset (0, 16)), so its
// flame starts at (288, 52), where the port used to start it at (288, 68).
void AParticleStartsAtItsEntitysPosition() {
    const Particles::System flame = TorchFlame();
    Particles::Particle p;
    Particles::Reset(flame, p, Particles::Owner{glm::dvec2(288.0, 64.0), 0.0}, kMiddle);
    CHECK_MSG(NearP(p.atPx, glm::dvec2(288.0, 52.0)) && NearP(p.bornPx, p.atPx),
              "the flame starts 12 above the node: " + ShowP(p.atPx));
    CHECK_MSG(NearP(p.velocityPx, glm::dvec2(0.0, -1.6)), "and rises: " + ShowP(p.velocityPx));

    // The turn is Multiply(v, RotateZ(a)): (x cos + y sin, -x sin + y cos) in
    // +y-down pixels. At 90 degrees right becomes up and up becomes left, which is
    // counter-clockwise on the screen.
    CHECK_MSG(NearP(Particles::Turn(glm::dvec2(10.0, 0.0), 90.0), glm::dvec2(0.0, -10.0), 1e-12),
              "right turned 90 is up: " + ShowP(Particles::Turn(glm::dvec2(10.0, 0.0), 90.0)));
    CHECK_MSG(NearP(Particles::Turn(glm::dvec2(0.0, -10.0), 90.0), glm::dvec2(-10.0, 0.0), 1e-12),
              "up turned 90 is left");
    CHECK_MSG(Particles::Turn(glm::dvec2(3.25, -12.0), 0.0) == glm::dvec2(3.25, -12.0), "unturned is exact");

    // An entity at 90 (the one light_wall of the 128 levels that is turned): the
    // start point, its spread and the direction turn with it; gravity does not.
    Particles::System turned = flame;
    turned.startPoint = glm::dvec2(12.0, 0.0);
    turned.gravity = glm::dvec2(0.0, 0.5);
    turned.angleStart = 10.0;
    Particles::Particle q;
    Particles::Reset(turned, q, Particles::Owner{glm::dvec2(100.0, 100.0), 90.0}, kMiddle);
    CHECK_MSG(NearP(q.atPx, glm::dvec2(100.0, 88.0), 1e-12), "(12, 0) turned 90 is 12 above: " + ShowP(q.atPx));
    CHECK_MSG(NearP(q.velocityPx, glm::dvec2(-1.6, 0.0), 1e-12), "rising turned 90 is leftward: " + ShowP(q.velocityPx));
    CHECK_MSG(NearD(q.angleDeg, 100.0), "its angle starts at angleStart plus the entity's: " + std::to_string(q.angleDeg));
    const bool active = Particles::Step(turned, q, 0, 1, Particles::Owner{glm::dvec2(100.0, 100.0), 90.0}, 1000.0 / 60.0,
                                        false, kMiddle);
    CHECK_MSG(!active, "a particle not yet released is not active");
    CHECK_MSG(q.released, "released on its first frame, index 0 of 1");
    CHECK_MSG(NearP(q.velocityPx, glm::dvec2(-1.6, 0.5), 1e-12), "gravity is added unturned: " + ShowP(q.velocityPx));
}

// 2b: the angle turns counter-clockwise on the screen when it grows, and the
// engine's +z turn is counter-clockwise too, so the rotation the quad takes is
// +angle. The port used to hand it over negated, and 1-1's portals spun the
// wrong way (plan_port gap H: the original at -95 to -119 degrees a second,
// clockwise positive).
void AParticleTurnsCounterClockwise() {
    Particles::System ring;
    ring.count = 3;
    ring.lifeTimeMs = 900.0;
    ring.size = 110.0;
    ring.growth = -2.0;
    ring.minSize = 2.0;
    ring.maxSize = 9100.0;
    ring.angleDir = 1.8; // portal_static.ent's rings
    ring.colour0 = glm::dvec4(0.0, 0.0, 0.0, 1.0);
    ring.colour1 = glm::dvec4(0.5, 0.4, 1.0, 0.0);
    Particles::Particle p;
    const Particles::Owner owner{glm::dvec2(74.0, 166.0), 0.0};
    Particles::Reset(ring, p, owner, kMiddle);
    Particles::Step(ring, p, 0, 3, owner, 1000.0 / 60.0, false, kMiddle); // released; turns 1.8
    const float first = Particles::WorldRotation(p);
    for (int frame = 0; frame < 30; ++frame) Particles::Step(ring, p, 0, 3, owner, 1000.0 / 60.0, false, kMiddle);
    const float later = Particles::WorldRotation(p);
    const double perSecond = static_cast<double>(later - first) / 0.5 * 180.0 / 3.14159265358979323846;
    CHECK_MSG(NearD(perSecond, 108.0, 1e-3), "108 degrees a second, counter-clockwise: " + std::to_string(perSecond));
    CHECK_MSG(later > first, "the engine's rotation grows: counter-clockwise on the screen");
    // The quad's own axis: +x turned by the rotation points up the screen (+y in
    // the engine) for a quarter turn.
    Particles::Particle quarter;
    quarter.angleDeg = 90.0;
    CHECK_MSG(NearD(Particles::WorldRotation(quarter), 3.14159265358979323846 / 2.0, 1e-6),
              "a quarter turn is +pi/2 about +z: " + std::to_string(Particles::WorldRotation(quarter)));
}

// 2c: an added particle is drawn with alpha 1 and its colour lerped; an
// alpha-blended one keeps its alpha and takes min(1, luminance + ambient).
void AnAddedParticleIgnoresItsAlpha() {
    Particles::System sparkle; // crystal.ent's
    sparkle.count = 2;
    sparkle.lifeTimeMs = 1000.0;
    sparkle.size = 12.0;
    sparkle.maxSize = 1000.0;
    sparkle.colour0 = glm::dvec4(0.8, 0.8, 1.0, 1.0);
    sparkle.colour1 = glm::dvec4(0.0, 0.0, 0.0, 0.0);
    Particles::Particle p;
    p.released = true;
    p.size = 12.0;
    p.colour = glm::dvec4(0.4, 0.4, 0.5, 0.5);
    glm::dvec4 c = Particles::DrawColour(sparkle, p, glm::dvec3(0.01));
    CHECK_MSG(NearD(c.r, 0.4) && NearD(c.g, 0.4) && NearD(c.b, 0.5) && c.a == 1.0,
              "added: the lerped colour, alpha 1, no ambient: " + std::to_string(c.a));
    CHECK_MSG(Particles::Drawn(sparkle, p, false), "and drawn");

    Particles::System water = sparkle; // gutter_mouth.ent's
    water.alphaMode = Particles::kAlphaPixel;
    water.additive = false;
    water.luminance = glm::dvec3(0.45);
    water.colour0 = glm::dvec4(1.0);
    c = Particles::DrawColour(water, p, glm::dvec3(0.3, 0.3, 0.35));
    CHECK_MSG(NearD(c.r, 0.4 * 0.75) && NearD(c.b, 0.5 * 0.8) && NearD(c.a, 0.5),
              "mixed: times min(1, 0.45 + ambient), its alpha kept: " + std::to_string(c.r) + " " + std::to_string(c.a));
    c = Particles::DrawColour(water, p, glm::dvec3(0.9));
    CHECK_MSG(NearD(c.r, 0.4) && NearD(c.a, 0.5), "and never lifted past 1");

    // Not drawn once its colour's alpha is spent - in either blend (:375).
    p.colour.a = 0.0;
    CHECK(!Particles::Drawn(water, p, false));
    CHECK(!Particles::Drawn(sparkle, p, false));
    p.colour.a = 0.5;
    p.size = 0.0;
    CHECK_MSG(!Particles::Drawn(sparkle, p, false), "nor at no size");
    p.size = 12.0;
    p.elapsedMs = 1200.0;
    p.lifeMs = 1000.0;
    CHECK_MSG(Particles::Drawn(sparkle, p, false) && !Particles::Drawn(sparkle, p, true),
              "past its life it is drawn only while its system is not killed");
    sparkle.repeat = 1;
    p.repeats = 1;
    CHECK_MSG(!Particles::Drawn(sparkle, p, false), "and never once its lives are spent");

    CHECK_MSG(Particles::Drawable(sparkle) && Particles::Drawable(water), "added and mixed are drawn");
    Particles::System modulate = sparkle;
    modulate.alphaMode = Particles::kAlphaModulate;
    modulate.additive = false;
    CHECK_MSG(!Particles::Drawable(modulate), "a multiply is refused, not mixed");
}

// 2d: a system draws in its entity's own slot, after the halo's quarter and
// before the half the layer puts what stands between two slots at, one after
// another in the file's order.
void ASystemDrawsInsideItsEntitysSlot() {
    CHECK_MSG(NearD(Particles::SlotFraction(0, 1), 0.375), "one system: 3/8");
    CHECK_MSG(NearD(Particles::SlotFraction(0, 2), 1.0 / 3.0) && NearD(Particles::SlotFraction(1, 2), 5.0 / 12.0),
              "two: 1/3 and 5/12");
    for (int n = 1; n <= 8; ++n) {
        double last = 0.25;
        for (int t = 0; t < n; ++t) {
            const double f = Particles::SlotFraction(t, n);
            CHECK_MSG(f > last && f < 0.5, "system " + std::to_string(t) + " of " + std::to_string(n) + " at " +
                                               std::to_string(f) + ", after " + std::to_string(last));
            last = f;
        }
    }
}

// 2e: the quad is square whatever the cell is; tesla_shock_black_bg.png is cut
// 5 x 2 from 256 x 64, a 51.2 x 32 cell, and is still drawn size by size.
void AParticlesQuadIsSquare() {
    Particles::Particle p;
    p.size = 40.0;
    CHECK_MSG(Particles::QuadPx(p) == glm::dvec2(40.0, 40.0), "40 by 40: " + ShowP(Particles::QuadPx(p)));
}

// ETHEntity::Scale on a system (ETHParticleSystem.cpp:27-40): every length times the
// scale, and no time, angle or colour. And the layer's shortcut - making the pool
// from the scaled system, where the original scales a pool it has made and run
// for a frame - gives the same particles for a system whose start point, spreads
// and direction are 0, as portal_static's rings are: sizes and their growth are
// linear in the scale, and the draws are the same draws over scaled ranges.
void AScaledSystemIsEthanonsScale() {
    Particles::System s = TorchFlame();
    s.gravity = glm::dvec2(0.5, -0.25);
    s.randomizeDir = glm::dvec2(2.0, 4.0);
    s.randStartPoint = glm::dvec2(6.0, 8.0);
    s.randomizeSize = 10.0;
    s.minSize = 2.0;
    s.randomLifeTimeMs = 30.0;
    s.angleStart = 15.0;
    s.randAngleStart = 245.0;
    s.angleDir = 1.8;
    s.randAngle = 0.5;
    Particles::System scaled = s;
    Particles::Scale(scaled, 0.8);
    CHECK_MSG(NearP(scaled.gravity, s.gravity * 0.8) && NearP(scaled.direction, s.direction * 0.8) &&
                  NearP(scaled.randomizeDir, s.randomizeDir * 0.8) && NearP(scaled.startPoint, s.startPoint * 0.8) &&
                  NearP(scaled.randStartPoint, s.randStartPoint * 0.8),
              "gravity, direction, their spreads and the start point scale");
    CHECK_MSG(NearD(scaled.size, s.size * 0.8) && NearD(scaled.randomizeSize, s.randomizeSize * 0.8) &&
                  NearD(scaled.growth, s.growth * 0.8) && NearD(scaled.minSize, s.minSize * 0.8) &&
                  NearD(scaled.maxSize, s.maxSize * 0.8),
              "the size, its spread, its growth and its bounds scale");
    CHECK_MSG(scaled.lifeTimeMs == s.lifeTimeMs && scaled.randomLifeTimeMs == s.randomLifeTimeMs &&
                  scaled.angleStart == s.angleStart && scaled.randAngleStart == s.randAngleStart &&
                  scaled.angleDir == s.angleDir && scaled.randAngle == s.randAngle && scaled.colour0 == s.colour0 &&
                  scaled.colour1 == s.colour1 && scaled.luminance == s.luminance && scaled.count == s.count &&
                  scaled.columns == s.columns && scaled.rows == s.rows,
              "no time, angle, colour, count or cut does");

    // portal_static.ent's rings, and the same rings as its script leaves them.
    Particles::System ring;
    ring.count = 3;
    ring.lifeTimeMs = 900.0;
    ring.randomLifeTimeMs = 300.0;
    ring.size = 110.0;
    ring.growth = -2.0;
    ring.minSize = 2.0;
    ring.maxSize = 9100.0;
    ring.angleDir = 1.8;
    ring.randAngleStart = 245.0;
    ring.colour0 = glm::dvec4(0.0, 0.0, 0.0, 1.0);
    ring.colour1 = glm::dvec4(0.5, 0.4, 1.0, 0.0);
    Particles::System scaledRing = ring;
    Particles::Scale(scaledRing, 0.8);
    CHECK_MSG(scaledRing.size == 88.0 && NearD(scaledRing.growth, -1.6) && NearD(scaledRing.minSize, 1.6),
              "the rings are born at 88 and shrink 1.6 a frame to 1.6");

    // A generator whose k-th draw lies (k mod 16) / 16 along its range: one each, counted.
    const auto sequence = [](int& k) {
        return [&k](double from, double to) {
            ++k;
            return from + (to - from) * static_cast<double>(k % 16) / 16.0;
        };
    };
    int drawsA = 0;
    int drawsB = 0;
    const Particles::Random a = sequence(drawsA);
    const Particles::Random b = sequence(drawsB);
    const Particles::Owner owner{glm::dvec2(334.0, 200.0), 0.0};
    const double tick = 1000.0 / 60.0;
    // The original's order: the pool made and run a frame at the file's scale, then
    // scaled, particle and system (ETHParticleManager.cpp:444-451, .h:160-164).
    std::vector<Particles::Particle> original = Particles::MakePool(ring, 3, owner, a);
    for (int i = 0; i < 3; ++i) Particles::Step(ring, original[static_cast<std::size_t>(i)], i, 3, owner, tick, false, a);
    for (Particles::Particle& p : original) {
        p.size *= 0.8;
        p.velocityPx *= 0.8;
    }
    // The layer's: made from the scaled system, run the same frame.
    std::vector<Particles::Particle> port = Particles::MakePool(scaledRing, 3, owner, b);
    for (int i = 0; i < 3; ++i) Particles::Step(scaledRing, port[static_cast<std::size_t>(i)], i, 3, owner, tick, false, b);
    bool same = drawsA == drawsB;
    for (int frame = 0; frame < 240 && same; ++frame) {
        for (int i = 0; i < 3; ++i) {
            const Particles::Particle& o = original[static_cast<std::size_t>(i)];
            const Particles::Particle& q = port[static_cast<std::size_t>(i)];
            if (!NearD(o.size, q.size, 1e-9) || !NearP(o.atPx, q.atPx, 1e-9) || !NearD(o.angleDeg, q.angleDeg, 1e-9) ||
                o.released != q.released || o.repeats != q.repeats || o.colour != q.colour) {
                same = false;
            }
        }
        for (int i = 0; i < 3; ++i) {
            Particles::Step(scaledRing, original[static_cast<std::size_t>(i)], i, 3, owner, tick, false, a);
            Particles::Step(scaledRing, port[static_cast<std::size_t>(i)], i, 3, owner, tick, false, b);
        }
        same = same && drawsA == drawsB;
    }
    CHECK_MSG(same, "four seconds of the rings: scaling the made pool and making the pool scaled agree, draw for draw");
}

// And the loop's own order, which the port once had wrong: a renewed particle is
// drawn in its colour at birth and at frame 0 of a played sheet on the frame it is
// renewed; it counts as active by what the frame before left; the motion is capped
// at 250 ms and the age is not; a killed system still releases, and renews nothing.
void TheLoopKeepsEthanonsOrder() {
    Particles::System s;
    s.count = 4;
    s.lifeTimeMs = 110.0;
    s.size = 10.0;
    s.maxSize = 100.0;
    s.direction = glm::dvec2(1.0, 0.0);
    s.colour0 = glm::dvec4(1.0, 0.0, 0.0, 1.0);
    s.colour1 = glm::dvec4(0.0, 0.0, 1.0, 1.0);
    s.columns = 4;
    s.animationMode = 1;
    const Particles::Owner owner{glm::dvec2(0.0), 0.0};
    const double tick = 1000.0 / 60.0;

    std::vector<Particles::Particle> pool = Particles::MakePool(s, 4, owner, kMiddle);
    CHECK_EQ(static_cast<int>(pool.size()), 4);
    // Staggered: index i is released once its age passes (110 + 0) * i / 4.
    int releasedAfterOne = 0;
    for (int i = 0; i < 4; ++i) {
        Particles::Step(s, pool[static_cast<std::size_t>(i)], i, 4, owner, tick, false, kMiddle);
        if (pool[static_cast<std::size_t>(i)].released) ++releasedAfterOne;
    }
    CHECK_MSG(releasedAfterOne == 1, "one frame of 16.7 ms releases index 0 only: " + std::to_string(releasedAfterOne));

    Particles::Particle p = pool[0];
    CHECK_MSG(NearD(p.elapsedMs, 0.0) && NearP(p.atPx, glm::dvec2(1.0, 0.0)), "released at 0 and moved one step");
    // Five more frames: 83.3 ms of a life of 110, at 3/4 of the sheet.
    for (int f = 0; f < 5; ++f) CHECK(Particles::Step(s, p, 0, 4, owner, tick, false, kMiddle));
    CHECK_MSG(p.frame == 3 && NearD(p.colour.r, 1.0 - 83.3333333333 / 110.0, 1e-6), "frame 3, colour lerped by age");
    // The sixth: 100 ms is inside its life; the seventh, 116.7, renews it.
    CHECK(Particles::Step(s, p, 0, 4, owner, tick, false, kMiddle));
    CHECK_MSG(p.repeats == 0, "inside its life it is not renewed");
    CHECK(Particles::Step(s, p, 0, 4, owner, tick, false, kMiddle));
    CHECK_MSG(p.repeats == 1 && p.elapsedMs == 0.0 && p.frame == 0 && p.colour == s.colour0 &&
                  NearP(p.atPx, glm::dvec2(0.0)),
              "renewed: colour at birth, frame 0, back at its start: frame " + std::to_string(p.frame) + " at " +
                  ShowP(p.atPx));

    // A frame of a whole second moves 250 ms' worth and ages the whole second.
    Particles::Particle slow = pool[1];
    slow.released = true;
    slow.elapsedMs = 0.0;
    slow.lifeMs = 5000.0;
    slow.atPx = glm::dvec2(0.0);
    Particles::Step(s, slow, 1, 4, owner, 1000.0, false, kMiddle);
    CHECK_MSG(NearP(slow.atPx, glm::dvec2(15.0, 0.0)) && NearD(slow.elapsedMs, 1000.0),
              "capped motion, whole age: " + ShowP(slow.atPx));

    // Size 0 as the last frame left it is not active, whatever this frame does.
    Particles::Particle shrunk = slow;
    shrunk.size = 0.0;
    CHECK_MSG(!Particles::Step(s, shrunk, 1, 4, owner, tick, false, kMiddle), "no size, not active");

    // Killed: an unreleased particle is still released; a life that ends is not renewed.
    Particles::Particle waiting = pool[3];
    Particles::Step(s, waiting, 3, 4, owner, 200.0, true, kMiddle);
    CHECK_MSG(waiting.released, "a killed system still releases");
    Particles::Particle ending = slow;
    ending.elapsedMs = 4990.0;
    const bool counted = Particles::Step(s, ending, 1, 4, owner, tick, true, kMiddle);
    CHECK_MSG(counted && ending.repeats == 1 && ending.elapsedMs > ending.lifeMs && !Particles::Drawn(s, ending, true),
              "killed at the end of its life: counted, spent, not renewed, not drawn");
}

// The random numbers are drawn in the original's statement order: ResetParticle's
// turn rate, life, size, direction x then y (ETHParticleManager.cpp:483-488), then
// PositionParticle's angle, start x then y (:517-519), then the frame (:502). kMiddle
// answers every draw alike, so it cannot see the order; this generator answers the
// k-th draw (k + 1) / 16 of the way along its range, so a y drawn before its x
// would put different numbers on both.
void TheRandomNumbersAreDrawnInEthanonsOrder() {
    Particles::System s;
    s.count = 1;
    s.lifeTimeMs = 500.0;
    s.size = 20.0;
    s.maxSize = 100.0;
    s.direction = glm::dvec2(1.0, -1.0);
    s.startPoint = glm::dvec2(10.0, 20.0);
    s.randAngle = 2.0;
    s.randomLifeTimeMs = 4.0;
    s.randomizeSize = 6.0;
    s.randomizeDir = glm::dvec2(8.0, 10.0);
    s.randAngleStart = 12.0;
    s.randStartPoint = glm::dvec2(14.0, 16.0);
    s.columns = 2;
    s.animationMode = 2; // PICK_RANDOM_FRAME: one more draw, last
    const Particles::Owner owner{glm::dvec2(100.0, 200.0), 0.0};

    std::vector<std::pair<double, double>> asked;
    const Particles::Random sequence = [&asked](double from, double to) {
        const double along = static_cast<double>(asked.size() + 1) / 16.0;
        asked.emplace_back(from, to);
        return from + (to - from) * along;
    };

    Particles::Particle p;
    Particles::Reset(s, p, owner, sequence);
    const std::vector<std::pair<double, double>> order{{-1.0, 1.0}, {-2.0, 2.0}, {-3.0, 3.0}, {-4.0, 4.0}, {-5.0, 5.0},
                                                       {0.0, 12.0}, {-7.0, 7.0}, {-8.0, 8.0}, {0.0, 2.0}};
    CHECK_MSG(asked == order, "Reset asks for its nine ranges in the original's order, " +
                                  std::to_string(asked.size()) + " asked");
    CHECK_MSG(NearD(p.angleDirDeg, -0.875) && NearD(p.lifeMs, 498.5) && NearD(p.size, 18.125),
              "turn rate, life and size take draws 1 to 3");
    CHECK_MSG(NearP(p.velocityPx, glm::dvec2(-1.0, -2.875)),
              "direction x takes draw 4 and y draw 5: " + ShowP(p.velocityPx));
    CHECK_MSG(NearD(p.angleDeg, 4.5), "the start angle takes draw 6: " + std::to_string(p.angleDeg));
    CHECK_MSG(NearP(p.atPx, glm::dvec2(109.125, 220.0)), "start x takes draw 7 and y draw 8: " + ShowP(p.atPx));
    CHECK_MSG(p.frame == 1, "and the frame draw 9: " + std::to_string(p.frame));

    // A first release positions only: the angle, then x, then y.
    asked.clear();
    Particles::Release(s, p, owner, sequence);
    const std::vector<std::pair<double, double>> released{{0.0, 12.0}, {-7.0, 7.0}, {-8.0, 8.0}};
    CHECK_MSG(asked == released, "Release asks for three ranges, angle then x then y");
    CHECK_MSG(NearD(p.angleDeg, 0.75), "the angle takes the first draw: " + std::to_string(p.angleDeg));
    CHECK_MSG(NearP(p.atPx, glm::dvec2(104.75, 215.0)), "x takes the second draw and y the third: " + ShowP(p.atPx));
}

// The census the blend rules rest on, over all 190 files: 102 systems, 75 added,
// 23 mixed and 4 multiplied; every added bitmap without alpha, which is what makes
// an added particle's alpha of 1 exact rather than close; every mixed bitmap with
// it; and every system with a <Luminance>.
int PngColourType(const std::string& path) {
    std::ifstream file(path, std::ios::binary);
    std::vector<unsigned char> head(26, 0);
    if (!file.read(reinterpret_cast<char*>(head.data()), 26)) return -1;
    if (head[0] != 0x89 || head[1] != 'P') return -1;
    return head[25];
}

void EveryAddedBitmapIsWithoutAlpha() {
    int systemsRead = 0;
    int added = 0;
    int mixed = 0;
    int multiplied = 0;
    int addedWithAlpha = 0;
    int mixedWithoutAlpha = 0;
    std::string firstWrong;
    std::error_code ec;
    for (const auto& entry : std::filesystem::directory_iterator(kOriginal + "/entities", ec)) {
        if (!entry.is_regular_file() || entry.path().extension() != ".ent") continue;
        std::vector<Particles::System> systems;
        std::string error;
        if (!Particles::Load(entry.path().string(), systems, error)) continue;
        for (const Particles::System& system : systems) {
            ++systemsRead;
            const std::string bitmap = kOriginal + "/particles/" + system.bitmap;
            const std::string ext = std::filesystem::path(system.bitmap).extension().string();
            // A PNG says in its IHDR (colour types 4 and 6 carry alpha, and none of
            // the game's is paletted); the game's BMPs are 8-bit grey and its JPG
            // cannot carry any.
            int type = PngColourType(bitmap);
            const bool alpha = type == 4 || type == 6;
            if (type < 0 && ext != ".bmp" && ext != ".JPG" && ext != ".jpg") {
                if (firstWrong.empty()) firstWrong = system.bitmap + " is none of PNG, BMP or JPG";
            }
            if (system.alphaMode == Particles::kAlphaAdd) {
                ++added;
                if (alpha) {
                    ++addedWithAlpha;
                    if (firstWrong.empty()) firstWrong = system.bitmap + " is added and carries alpha";
                }
            } else if (system.alphaMode == Particles::kAlphaPixel) {
                ++mixed;
                if (!alpha) {
                    ++mixedWithoutAlpha;
                    if (firstWrong.empty()) firstWrong = system.bitmap + " is mixed and carries none";
                }
            } else if (system.alphaMode == Particles::kAlphaModulate) {
                ++multiplied;
            }
        }
    }
    CHECK_EQ(systemsRead, 102);
    CHECK_EQ(added, 75);
    CHECK_EQ(mixed, 23);
    CHECK_EQ(multiplied, 4);
    CHECK_MSG(addedWithAlpha == 0 && mixedWithoutAlpha == 0, firstWrong);

    // And the three the layer draws differently, read from the files.
    std::vector<Particles::System> systems;
    std::string error;
    CHECK_MSG(Particles::Load(kOriginal + "/entities/portal_static.ent", systems, error) && systems.size() == 2, error);
    if (systems.size() == 2) {
        CHECK(systems[0].alphaMode == Particles::kAlphaAdd && systems[0].luminance == glm::dvec3(1.0));
        CHECK_MSG(systems[1].alphaMode == Particles::kAlphaPixel && !systems[1].additive && systems[1].bitmap == "portal.png",
                  "portal_static's iris is mixed");
    }
    CHECK_MSG(Particles::Load(kOriginal + "/entities/gutter_mouth.ent", systems, error) && systems.size() == 1, error);
    if (systems.size() == 1) {
        CHECK_MSG(systems[0].alphaMode == Particles::kAlphaPixel &&
                      NearP(glm::dvec2(systems[0].luminance.r, systems[0].luminance.b), glm::dvec2(0.45), 1e-9),
                  "gutter_mouth's water is mixed at luminance 0.45");
    }
    CHECK_MSG(Particles::Load(kOriginal + "/entities/light.ent", systems, error) && systems.size() == 1, error);
    if (systems.size() == 1) {
        CHECK(systems[0].luminance == glm::dvec3(0.0));
        CHECK_MSG(NearP(systems[0].startPoint, glm::dvec2(0.0, -12.0)) && systems[0].randStartPoint == glm::dvec2(0.0),
                  "the torch's flame starts 12 above its entity, with no spread");
    }
}

int main() {
    AnImageSaysItsSizeInItsHeader();
    AnythingElseIsRefusedByName();
    AJpegSaysItsSizeInItsFrameHeader();
    AJpegTheRendererCannotReadIsRefused();
    TheCanvasOrderIsZThenTheFile();
    WhatTheReaderDoesNotDrawIsNamed();
    ThePortalAndTheShotAreTheirEnts();
    AStaticPortalWithoutItsScriptIsRefused();
    TheTimerIsTimerEnt();
    ATimerWithoutItsClockIsRefused();
    APulseGoesThereAndBack();
    ASheetThatDoesNotSayHowFastIsRefused();
    APictureWithoutItsEmissiveIsRefused();
    APictureWithoutItsLightingIsRefused();
    AParticleStartsAtItsEntitysPosition();
    AParticleTurnsCounterClockwise();
    AnAddedParticleIgnoresItsAlpha();
    ASystemDrawsInsideItsEntitysSlot();
    AParticlesQuadIsSquare();
    AScaledSystemIsEthanonsScale();
    TheLoopKeepsEthanonsOrder();
    TheRandomNumbersAreDrawnInEthanonsOrder();

    std::error_code original;
    if (std::filesystem::is_directory(kOriginal + "/entities", original)) {
        TheOriginalsImagesAreCutAsTheEntsSay();
        EveryOriginalImageIsSizedAsTheRendererReadsIt();
        TheOriginalsParticlesAreRead();
        AnEntityWithoutParticlesSaysSoWithoutFailing();
        EveryAddedBitmapIsWithoutAlpha();
        TheOriginalsMp3sDecode();
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
