// The original's lighting, as the remake's converter carries it: sim/Lighting.
//
// The converter writes every lighting input a level file holds - the scene's
// ambient and light intensity, each entity's depth, flags, emissive, colour,
// normal map, baked lightmap and light - as quoted metadata/eth_* strings, and
// copies the files they name beside the art (the remake's b572fec). Nothing
// draws them yet; out/parity/specs/lighting/design_port.md step G1 is only that
// the port reads them, strictly, and that what it reads is what the converter
// reported.
//
// The reader is first tested on a scene and images this suite writes, with
// invented values, which runs anywhere. The rest reads the converted levels from
// outside this repository, and is skipped, saying where it looked, when they are
// absent. Its totals are the converter's own report for the four worlds on
// 14 September 2026, which an independent parser of the emitted files agreed
// with: 730 lightmaps, 72 lights (68 on static owners), 71 halos, 1,720 normal
// maps, 2,140 non-zero emissives, 1,533 non-zero depths.
//
// And, since step 45 (the design's G3), what the original's script sets over a
// level file: the port's lighting.json, the ambient light a level is drawn with
// as its torch is lit and put out, and the factor every sprite is multiplied by.
// Those run anywhere too, on the port's own data and states this suite builds.
//
// And, since step 49 (G5), which light reaches which sprite, and the colours a
// light and its halo are drawn in: arithmetic, which runs anywhere.

#include "TestHarness.hpp"

#include "sim/Lighting.hpp"
#include "sim/Tscn.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <map>
#include <set>
#include <string>
#include <system_error>
#include <vector>

using namespace MagicPortals;

namespace {

const std::string kLevels = MAGICPORTALS_LEVELS_DIR;

std::filesystem::path Scratch() {
    const std::filesystem::path dir = std::filesystem::temp_directory_path() / "supersonic-test-mp-lighting";
    std::error_code ec;
    std::filesystem::create_directories(dir / "assets" / "entities" / "normalmaps", ec);
    std::filesystem::create_directories(dir / "assets" / "lightmaps" / "room", ec);
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
    bytes.push_back(2);
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

// The images the scene below names. Only their headers are read.
std::string WriteImages() {
    const std::filesystem::path dir = Scratch();
    Write(dir / "assets" / "entities" / "lamp.png", PngHeader(32, 64));
    Write(dir / "assets" / "entities" / "wall.png", PngHeader(64, 16));
    Write(dir / "assets" / "entities" / "ring.png", PngHeader(48, 48));
    Write(dir / "assets" / "entities" / "glow.bmp", BmpHeader(64, 64));
    Write(dir / "assets" / "entities" / "normalmaps" / "lamp_n.png", PngHeader(32, 64));
    Write(dir / "assets" / "entities" / "normalmaps" / "wall_n.png", PngHeader(64, 16));
    Write(dir / "assets" / "lightmaps" / "room" / "add7.png", PngHeader(16, 32));
    Write(dir / "assets" / "lightmaps" / "room" / "big7.png", PngHeader(32, 64));
    return dir.string();
}

// Five entities: one with every key the converter writes, a light with no
// picture and every default, a spriteless marker, a lit wall with no lightmap,
// and a node with no key at all. The values are invented.
const std::string kRoom = R"([gd_scene load_steps=4 format=3]

[ext_resource type="Texture2D" path="res://assets/entities/lamp.png" id="tex_0"]
[ext_resource type="Texture2D" path="res://assets/entities/wall.png" id="tex_1"]
[ext_resource type="Texture2D" path="res://assets/entities/ring.png" id="tex_2"]

[node name="room" type="Node2D"]
metadata/eth_ambient = "0.2 0.25 0.4"
metadata/eth_light_intensity = "2.5"

[node name="lamp_7" type="Node2D" parent="."]
position = Vector2(100, 50)
metadata/entity_name = "lamp.ent"
z_index = -18
metadata/eth_z = "-18.5"
metadata/eth_static = "1"
metadata/eth_apply_light = "1"
metadata/eth_emissive = "0.5 0.6 0.7"
metadata/eth_color = "1 0.9 0.8 0.5"
metadata/eth_normal = "res://assets/entities/normalmaps/lamp_n.png"
metadata/eth_lightmap = "res://assets/lightmaps/room/add7.png"
metadata/eth_light_offset = "2 -30 60"
metadata/eth_light_range = "750"
metadata/eth_light_color = "0.9 0.4 1.5"
metadata/eth_halo = "res://assets/entities/glow.bmp"
metadata/eth_halo_offset = "1 -15"
metadata/eth_halo_size = "180 270"
metadata/eth_halo_brightness = "0.35"

[node name="Sprite" type="Sprite2D" parent="lamp_7"]
texture = ExtResource("tex_0")

[node name="flare_8" type="Node2D" parent="."]
position = Vector2(10, 10)
metadata/eth_emissive = "1 1 1"
metadata/eth_light_range = "256"
metadata/eth_halo = "res://assets/entities/glow.bmp"

[node name="marker_9" type="Node2D" parent="."]
position = Vector2(0, 0)
z_index = 2
metadata/eth_z = "2"

[node name="wall_10" type="Node2D" parent="."]
position = Vector2(64, 200)
metadata/eth_static = "1"
metadata/eth_apply_light = "1"
metadata/eth_normal = "res://assets/entities/normalmaps/wall_n.png"

[node name="Sprite" type="Sprite2D" parent="wall_10"]
texture = ExtResource("tex_1")

[node name="Body" type="StaticBody2D" parent="wall_10"]

[node name="ring_11" type="Node2D" parent="."]
position = Vector2(300, 100)

[node name="Sprite" type="Sprite2D" parent="ring_11"]
texture = ExtResource("tex_2")
)";

bool Is(const glm::dvec3& v, double x, double y, double z) { return v.x == x && v.y == y && v.z == z; }
bool Is(const glm::dvec2& v, double x, double y) { return v.x == x && v.y == y; }
bool EndsWith(const std::string& s, const std::string& tail) { return s.ends_with(tail); }

int LineOf(const std::string& text, const std::string& needle) {
    const std::size_t at = text.find(needle);
    if (at == std::string::npos) return -1;
    return 1 + static_cast<int>(std::count(text.begin(), text.begin() + static_cast<std::ptrdiff_t>(at), '\n'));
}

// `text` with `from`, which must be there exactly once, made `to`.
std::string Replace(const std::string& text, const std::string& from, const std::string& to) {
    const std::size_t at = text.find(from);
    const bool once = at != std::string::npos && text.find(from, at + 1) == std::string::npos;
    CHECK_MSG(once, "the fixture holds '" + from + "' exactly once");
    if (!once) return text;
    std::string out = text;
    out.replace(at, from.size(), to);
    return out;
}

// ---- what the script sets: lighting.json and the ambient now ---------------------

void TheScriptsAmbientIsReadAndFollowsTheTorch() {
    Lighting::Rules rules;
    std::string error;
    const bool ok = Lighting::LoadRules(std::string(MAGICPORTALS_PORT_DATA_DIR) + "/lighting.json", rules, error);
    CHECK_MSG(ok, error);
    CHECK_MSG(Is(rules.darkestAmbient, 0.01, 0.01, 0.01), "DARKEST_AMBIENT_LIGHT, Game.angelscript 306783..306841");
    CHECK_MSG(Is(rules.torchLitAmbient, 0.1, 0.1, 0.25), "what ETHCallback_light_off's delete branch sets");

    const glm::dvec3 file(0.35, 0.3, 0.35);
    Torch::State none;
    CHECK_MSG(Lighting::Ambient(rules, file, false, none) == file, "a level that sets nothing: its file's ambient");
    CHECK_MSG(Lighting::Ambient(rules, file, true, none) == rules.darkestAmbient,
              "`darkest` REPLACES the file's ambient rather than dimming it");

    Torch::State torch;
    torch.lights.push_back(Torch::Light{});
    CHECK_MSG(Lighting::Ambient(rules, file, true, torch) == rules.darkestAmbient, "an unlit torch changes nothing");
    torch.lights[0].lit = true;
    torch.lit = 1;
    CHECK_MSG(Lighting::Ambient(rules, file, true, torch) == rules.torchLitAmbient, "lit, the torch's ambient");
    CHECK_MSG(Lighting::Ambient(rules, file, false, torch) == rules.torchLitAmbient, "on any level");
    // Put back out. The cumulative counter stays at 1, and must not be what is read.
    torch.lights[0].lit = false;
    torch.putOut = 1;
    CHECK_EQ(torch.lit, 1);
    CHECK_MSG(Lighting::Ambient(rules, file, true, torch) == rules.darkestAmbient,
              "put out: darkest again, though the torch has been lit once");
    CHECK_MSG(Lighting::Ambient(rules, file, false, torch) == rules.darkestAmbient,
              "and ETHCallback_fire_signal sets darkest on any level, not the file's back");
    torch.lights[0].lit = true;
    torch.lit = 2;
    CHECK_MSG(Lighting::Ambient(rules, file, true, torch) == rules.torchLitAmbient, "and lit again, lit again");

    // min(1, ambient + emissive), per channel.
    CHECK(Is(Lighting::AmbientTerm(glm::dvec3(0.35, 0.3, 0.35), glm::dvec3(0.0)), 0.35, 0.3, 0.35));
    CHECK(Is(Lighting::AmbientTerm(glm::dvec3(0.35, 0.3, 0.35), glm::dvec3(1.0)), 1.0, 1.0, 1.0));
    CHECK_MSG(Is(Lighting::AmbientTerm(glm::dvec3(0.25, 0.5, 0.5), glm::dvec3(0.5, 0.25, 0.75)), 0.75, 0.75, 1.0),
              "each channel on its own, and clipped at 1");

    // Refused: anything but three numbers from 0 to 1, and either light missing.
    const std::filesystem::path path = Scratch() / "lighting.json";
    const auto refused = [&](const std::string& text, const std::string& says) {
        Write(path, std::vector<unsigned char>(text.begin(), text.end()));
        Lighting::Rules read;
        std::string why;
        const bool loaded = Lighting::LoadRules(path.string(), read, why);
        CHECK_MSG(!loaded, "refused: " + text);
        CHECK_MSG(why.find(says) != std::string::npos, why);
    };
    const std::string lit = R"("torch_lit_ambient": {"value": [0.1, 0.1, 0.25]})";
    refused("{" + lit + "}", "darkest_ambient");
    refused(R"({"darkest_ambient": {"value": [0.01, 0.01]}, )" + lit + "}", "darkest_ambient");
    refused(R"({"darkest_ambient": {"value": [0.01, 0.01, 1.5]}, )" + lit + "}", "from 0 to 1");
    refused(R"({"darkest_ambient": {"value": [0.01, -0.01, 0.01]}, )" + lit + "}", "from 0 to 1");
    refused(R"({"darkest_ambient": {"value": [0.01, "0.01", 0.01]}, )" + lit + "}", "darkest_ambient");
    refused(R"({"darkest_ambient": [0.01, 0.01, 0.01], )" + lit + "}", "darkest_ambient");
    refused(R"({"darkest_ambient": {"value": [0.01, 0.01, 0.01]}})", "torch_lit_ambient");

    // Since step 49: the normal maps' green and the halo scale, each required.
    CHECK_MSG(rules.normalMapGreenDown, "hPixelLightDiff.ps's decode: green points down the image");
    CHECK_MSG(rules.haloBrightnessScale > 0.0 && rules.haloBrightnessScale <= 1.0,
              "a halo scale from above 0 to the formula's own: " + std::to_string(rules.haloBrightnessScale));
    const std::string both = R"("darkest_ambient": {"value": [0.01, 0.01, 0.01]}, )" + lit;
    const std::string green = R"(, "normal_map_green_down": {"value": true})";
    const std::string scale = R"(, "halo_brightness_scale": {"value": 0.5})";
    refused("{" + both + scale + "}", "normal_map_green_down");
    refused("{" + both + R"(, "normal_map_green_down": {"value": 1})" + scale + "}", "normal_map_green_down");
    refused("{" + both + R"(, "normal_map_green_down": true)" + scale + "}", "normal_map_green_down");
    refused("{" + both + green + "}", "halo_brightness_scale");
    refused("{" + both + green + R"(, "halo_brightness_scale": {"value": 1.5})" + "}", "from 0 to 1");
    refused("{" + both + green + R"(, "halo_brightness_scale": {"value": -0.5})" + "}", "from 0 to 1");
    refused("{" + both + green + R"(, "halo_brightness_scale": {"value": "0.5"})" + "}", "halo_brightness_scale");

    // Since step 55: the 16-bit surface, required and a bool.
    CHECK_MSG(rules.framebufferRgb565, "GL2JNIView.java:94: the original draws into 5/6/5");
    refused("{" + both + green + scale + "}", "framebuffer_rgb565");
    refused("{" + both + green + scale + R"(, "framebuffer_rgb565": {"value": 1})" + "}", "framebuffer_rgb565");
    refused("{" + both + green + scale + R"(, "framebuffer_rgb565": true)" + "}", "framebuffer_rgb565");
    {
        const std::string text = "{" + both + R"(, "normal_map_green_down": {"value": false})" + scale +
                                 R"(, "framebuffer_rgb565": {"value": false})" + "}";
        Write(path, std::vector<unsigned char>(text.begin(), text.end()));
        Lighting::Rules read;
        std::string why;
        CHECK_MSG(Lighting::LoadRules(path.string(), read, why), why);
        CHECK_MSG(!read.normalMapGreenDown && read.haloBrightnessScale == 0.5 && !read.framebufferRgb565,
                  "and read as written");
    }
}

// ---- which light reaches which sprite, and in what colour (step 49) ---------------

void EachLightReachesTheSpritesItShould() {
    // ETHEntitySpriteRenderer.cpp:70: a static sprite never takes a static light's
    // pass while lightmaps are on; BeginLightPass refuses a sprite that applies no
    // light. Two layers carry that.
    CHECK_MSG(Lighting::kLiveLights != 0 && Lighting::kStaticLights != 0 &&
                  (Lighting::kLiveLights & Lighting::kStaticLights) == 0,
              "two separate bits");
    CHECK_EQ(static_cast<int>(Lighting::ReceiverMask(false, false)), 0);
    CHECK_EQ(static_cast<int>(Lighting::ReceiverMask(true, false)), 0);
    CHECK_MSG(Lighting::ReceiverMask(true, true) == Lighting::kLiveLights,
              "a static sprite that applies light: the live lights only, its static ones are baked");
    CHECK_MSG(Lighting::ReceiverMask(false, true) == (Lighting::kLiveLights | Lighting::kStaticLights),
              "a moving sprite that applies light: every light");
    // Since step 55: once the level bakes at run time a static sprite takes the
    // static lights live too; a sprite that applies no light still takes none.
    CHECK_MSG(Lighting::ReceiverMask(true, true, true) == (Lighting::kLiveLights | Lighting::kStaticLights),
              "baked at run time: a static sprite takes every light");
    CHECK_MSG(Lighting::ReceiverMask(false, true, true) == (Lighting::kLiveLights | Lighting::kStaticLights),
              "and a moving one as before");
    CHECK_EQ(static_cast<int>(Lighting::ReceiverMask(true, false, true)), 0);
    {
        Torch::State torch;
        CHECK_MSG(!Lighting::RuntimeBake(torch), "no torch lit: the file's lightmaps");
        torch.lit = 1;
        CHECK_MSG(Lighting::RuntimeBake(torch), "a torch lit: baked at run time");
        torch.putOut = 1;
        CHECK_MSG(Lighting::RuntimeBake(torch), "and put back out, still: GenerateLightmaps ran again, with no light");
    }
    CHECK_MSG(Lighting::LightLayer(true) == Lighting::kStaticLights, "a static owner's light is static");
    CHECK_MSG(Lighting::LightLayer(false) == Lighting::kLiveLights, "anything else's is live");
    // The four pairings, as the engine's mask test reads them.
    const auto reaches = [](bool receiverStatic, bool ownerStatic) {
        return (Lighting::ReceiverMask(receiverStatic, true) & Lighting::LightLayer(ownerStatic)) != 0;
    };
    CHECK_MSG(!reaches(true, true), "a torch does not reach a wall whose lightmap holds it");
    CHECK_MSG(reaches(true, false), "a shot does");
    CHECK_MSG(reaches(false, true), "a torch reaches the player");
    CHECK_MSG(reaches(false, false), "and so does a shot");

    // active / total, and 1 with no system.
    CHECK(Lighting::ParticleRatio(9, 12) == 0.75);
    CHECK(Lighting::ParticleRatio(0, 12) == 0.0);
    CHECK(Lighting::ParticleRatio(12, 12) == 1.0);
    CHECK_MSG(Lighting::ParticleRatio(0, 0) == 1.0, "no particle system: whole");
    CHECK_MSG(Lighting::ParticleRatio(13, 12) == 1.0 && Lighting::ParticleRatio(-1, 12) == 0.0, "held to 0..1");

    // light_ent_696's light and halo. Products of decimals: to a tolerance.
    const auto Near = [](const glm::dvec3& v, double x, double y, double z) {
        return std::fabs(v.x - x) < 1e-12 && std::fabs(v.y - y) < 1e-12 && std::fabs(v.z - z) < 1e-12;
    };
    Lighting::Light torch;
    torch.colour = glm::dvec3(1.0, 0.5, 0.1);
    torch.haloBrightness = 0.7;
    CHECK_MSG(Near(Lighting::LightColour(torch, 3.0, true, 0.25), 3.0, 1.5, 0.3),
              "a static owner's light: colour x intensity, whatever its flame does");
    CHECK_MSG(Near(Lighting::LightColour(torch, 3.0, false, 0.5), 1.5, 0.75, 0.15),
              "a moving owner's: x its live share too");
    CHECK_MSG(Near(Lighting::HaloColour(torch, 0.5, 1.0), 0.35, 0.175, 0.035),
              "a halo: colour x haloBrightness x the live share, for a static owner too, and no intensity");
    CHECK_MSG(Near(Lighting::HaloColour(torch, 1.0, 0.5), 0.35, 0.175, 0.035), "x lighting.json's scale");
}

// ---- the reader, on a scene this suite writes ------------------------------------

void EveryKeyIsReadAndEveryAbsenceIsTheDefault() {
    const std::string res = WriteImages();
    Tscn::Scene scene;
    std::string error;
    const bool parsed = Tscn::Parse(kRoom, scene, error);
    CHECK_MSG(parsed, error);
    if (!parsed) return;
    Lighting::Scene look;
    const bool ok = Lighting::Read(scene, res, look, error);
    CHECK_MSG(ok, error);
    if (!ok) return;

    CHECK(Is(look.ambient, 0.2, 0.25, 0.4));
    CHECK(look.intensity == 2.5);
    CHECK_EQ(look.nodes.size(), std::size_t{5});
    CHECK_MSG(look.nodes.count("room") == 0, "the root is the scene, not one of its entities");
    CHECK_MSG(look.nodes.count("Sprite") == 0 && look.nodes.count("Body") == 0, "nor is a node below an entity");

    const auto lamp = look.nodes.find("lamp_7");
    CHECK(lamp != look.nodes.end());
    if (lamp != look.nodes.end()) {
        const Lighting::Look& l = lamp->second;
        CHECK_MSG(l.z == -18.5, "the depth unrounded, where z_index says -18");
        CHECK(l.isStatic && l.applyLight);
        CHECK(Is(l.emissive, 0.5, 0.6, 0.7));
        CHECK(l.colour == glm::dvec4(1.0, 0.9, 0.8, 0.5));
        CHECK_MSG(l.normal == res + "/assets/entities/normalmaps/lamp_n.png", l.normal);
        CHECK_MSG(l.lightmap == res + "/assets/lightmaps/room/add7.png", l.lightmap);
        CHECK(l.light.has_value());
        if (l.light) {
            CHECK(Is(l.light->offset, 2.0, -30.0, 60.0));
            CHECK(l.light->range == 750.0);
            CHECK_MSG(Is(l.light->colour, 0.9, 0.4, 1.5), "a light's colour may exceed 1");
            CHECK_MSG(l.light->halo == res + "/assets/entities/glow.bmp", l.light->halo);
            CHECK(Is(l.light->haloOffset, 1.0, -15.0));
            CHECK(Is(l.light->haloSize, 180.0, 270.0));
            CHECK(l.light->haloBrightness == 0.35);
        }
    }

    // A light with no picture, and every halo number at the engine's default.
    const auto flare = look.nodes.find("flare_8");
    CHECK(flare != look.nodes.end());
    if (flare != look.nodes.end()) {
        const Lighting::Look& l = flare->second;
        CHECK(l.z == 0.0 && !l.isStatic && !l.applyLight);
        CHECK(Is(l.emissive, 1.0, 1.0, 1.0));
        CHECK(l.normal.empty() && l.lightmap.empty());
        CHECK(l.light.has_value());
        if (l.light) {
            CHECK_MSG(Is(l.light->offset, 0.0, 0.0, 0.0) && Is(l.light->colour, 1.0, 1.0, 1.0),
                      "ETHLight.cpp:27-38: position 0, colour 1");
            CHECK(l.light->range == 256.0);
            CHECK_MSG(l.light->halo == res + "/assets/entities/glow.bmp", l.light->halo);
            CHECK_MSG(Is(l.light->haloOffset, 0.0, 0.0) && Is(l.light->haloSize, 64.0, 64.0) &&
                          l.light->haloBrightness == 1.0,
                      "haloSize 64, haloBrightness 1");
        }
    }

    // A marker with a depth and nothing else - where the port's own actors will
    // take their lighting height from.
    const auto marker = look.nodes.find("marker_9");
    CHECK(marker != look.nodes.end());
    if (marker != look.nodes.end()) {
        const Lighting::Look& l = marker->second;
        CHECK(l.z == 2.0);
        CHECK(!l.isStatic && !l.applyLight && !l.light.has_value());
        CHECK(Is(l.emissive, 0.0, 0.0, 0.0) && l.colour == glm::dvec4(1.0));
    }

    // Lit and static with no lightmap: ambient only, which is the case of 841
    // sprites in the levels.
    const auto wall = look.nodes.find("wall_10");
    CHECK(wall != look.nodes.end());
    if (wall != look.nodes.end()) {
        CHECK(wall->second.isStatic && wall->second.applyLight);
        CHECK_MSG(wall->second.normal == res + "/assets/entities/normalmaps/wall_n.png", wall->second.normal);
        CHECK(wall->second.lightmap.empty() && !wall->second.light.has_value());
    }

    // No key at all: every default.
    const auto ring = look.nodes.find("ring_11");
    CHECK(ring != look.nodes.end());
    if (ring != look.nodes.end()) {
        const Lighting::Look& l = ring->second;
        CHECK(l.z == 0.0 && !l.isStatic && !l.applyLight && !l.light.has_value());
        CHECK(Is(l.emissive, 0.0, 0.0, 0.0) && l.colour == glm::dvec4(1.0));
        CHECK(l.normal.empty() && l.lightmap.empty());
    }
}

struct Refusal {
    const char* what;
    std::string text;
    const char* says;   // a fragment the error must hold
    const char* header; // the node the error must name the line of
};

void AnythingElseIsRefusedNamingTheLine() {
    const std::string res = WriteImages();
    const std::string lamp = R"([node name="lamp_7" type="Node2D" parent="."])";
    const std::string flare = R"([node name="flare_8" type="Node2D" parent="."])";
    const std::string marker = R"([node name="marker_9" type="Node2D" parent="."])";
    const std::string wall = R"([node name="wall_10" type="Node2D" parent="."])";
    const std::string body = R"([node name="Body" type="StaticBody2D" parent="wall_10"])";
    const std::string room = R"([node name="room" type="Node2D"])";
    const std::string markerZ = "metadata/eth_z = \"2\"\n";

    const std::vector<Refusal> refusals = {
        {"an unknown key", Replace(kRoom, markerZ, markerZ + "metadata/eth_glow = \"1\"\n"),
         "eth_glow is not a lighting key the port reads", marker.c_str()},
        {"a bare number", Replace(kRoom, markerZ, "metadata/eth_z = 2\n"), "eth_z is not a quoted string",
         marker.c_str()},
        {"a Vector2", Replace(kRoom, "metadata/eth_halo_offset = \"1 -15\"", "metadata/eth_halo_offset = Vector2(1, -15)"),
         "eth_halo_offset is not a quoted string", lamp.c_str()},
        {"two numbers for three", Replace(kRoom, "\"0.5 0.6 0.7\"", "\"0.5 0.6\""),
         "eth_emissive: \"0.5 0.6\" is not three numbers", lamp.c_str()},
        {"four numbers for three", Replace(kRoom, "\"0.5 0.6 0.7\"", "\"0.5 0.6 0.7 0.8\""), "is not three numbers",
         lamp.c_str()},
        {"two spaces", Replace(kRoom, "\"0.5 0.6 0.7\"", "\"0.5  0.6 0.7\""), "is not three numbers", lamp.c_str()},
        {"a trailing space", Replace(kRoom, "\"1 -15\"", "\"1 -15 \""), "eth_halo_offset: \"1 -15 \" is not two numbers",
         lamp.c_str()},
        {"a word", Replace(kRoom, "\"2.5\"", "\"bright\""), "eth_light_intensity: \"bright\" is not one number",
         room.c_str()},
        {"an infinity", Replace(kRoom, markerZ, "metadata/eth_z = \"inf\"\n"), "eth_z: \"inf\" is not one number",
         marker.c_str()},
        {"a nan", Replace(kRoom, markerZ, "metadata/eth_z = \"nan\"\n"), "eth_z: \"nan\" is not one number",
         marker.c_str()},
        {"a colour of three", Replace(kRoom, "\"1 0.9 0.8 0.5\"", "\"1 0.9 0.8\""), "eth_color: \"1 0.9 0.8\" is not four numbers",
         lamp.c_str()},
        {"a flag that is a word", Replace(kRoom, "metadata/eth_static = \"1\"\nmetadata/eth_apply_light = \"1\"\nmetadata/eth_normal",
                                          "metadata/eth_static = \"yes\"\nmetadata/eth_apply_light = \"1\"\nmetadata/eth_normal"),
         "eth_static: \"yes\" is not \"0\" or \"1\"", wall.c_str()},
        {"a lightmap on a node that is not static",
         Replace(kRoom, "metadata/eth_z = \"-18.5\"\nmetadata/eth_static = \"1\"\n", "metadata/eth_z = \"-18.5\"\n"),
         "eth_lightmap on a node that is not static", lamp.c_str()},
        {"a lightmap on a node that does not apply light",
         Replace(kRoom, "metadata/eth_apply_light = \"1\"\nmetadata/eth_emissive", "metadata/eth_emissive"),
         "eth_lightmap on a node that does not apply light", lamp.c_str()},
        {"a lightmap on a node with no sprite",
         Replace(kRoom, "metadata/eth_light_range = \"256\"\n",
                 "metadata/eth_light_range = \"256\"\nmetadata/eth_static = \"1\"\nmetadata/eth_apply_light = \"1\"\n"
                 "metadata/eth_lightmap = \"res://assets/lightmaps/room/add7.png\"\n"),
         "eth_lightmap on a node that draws no sprite", flare.c_str()},
        {"a lightmap not half its sprite", Replace(kRoom, "room/add7.png", "room/big7.png"),
         "eth_lightmap is 32x64, and twice that is not its sprite's 32x64", lamp.c_str()},
        {"a path that is not res://", Replace(kRoom, "\"res://assets/entities/normalmaps/wall_n.png\"", "\"assets/entities/normalmaps/wall_n.png\""),
         "eth_normal: \"assets/entities/normalmaps/wall_n.png\" is not a res:// path", wall.c_str()},
        {"a path with no file", Replace(kRoom, "normalmaps/wall_n.png", "normalmaps/none.png"),
         "none.png is not a file", wall.c_str()},
        {"a halo with no file",
         Replace(kRoom, "metadata/eth_light_range = \"256\"\nmetadata/eth_halo = \"res://assets/entities/glow.bmp\"",
                 "metadata/eth_light_range = \"256\"\nmetadata/eth_halo = \"res://assets/entities/gone.bmp\""),
         "gone.bmp is not a file", flare.c_str()},
        {"a halo without the light", Replace(kRoom, "metadata/eth_light_range = \"256\"\n", ""),
         "eth_halo without eth_light_range", flare.c_str()},
        {"a light offset without the light", Replace(kRoom, "metadata/eth_light_range = \"750\"\n", ""),
         "eth_light_offset without eth_light_range", lamp.c_str()},
        {"halo numbers without the halo",
         Replace(kRoom, "metadata/eth_halo = \"res://assets/entities/glow.bmp\"\nmetadata/eth_halo_offset",
                 "metadata/eth_halo_offset"),
         "eth_halo_offset without eth_halo", lamp.c_str()},
        {"a range of zero", Replace(kRoom, "\"256\"", "\"0\""), "eth_light_range: \"0\" is not a positive range",
         flare.c_str()},
        {"a negative range", Replace(kRoom, "\"750\"", "\"-750\""), "eth_light_range: \"-750\" is not a positive range",
         lamp.c_str()},
        {"no ambient", Replace(kRoom, "metadata/eth_ambient = \"0.2 0.25 0.4\"\n", ""), "the root has no eth_ambient",
         room.c_str()},
        {"no intensity", Replace(kRoom, "metadata/eth_light_intensity = \"2.5\"\n", ""),
         "the root has no eth_light_intensity", room.c_str()},
        {"the scene's key on an entity", Replace(kRoom, markerZ, markerZ + "metadata/eth_ambient = \"1 1 1\"\n"),
         "eth_ambient is the scene's key, on an entity", marker.c_str()},
        {"an entity's key on the root", Replace(kRoom, "metadata/eth_light_intensity = \"2.5\"\n",
                                                "metadata/eth_light_intensity = \"2.5\"\nmetadata/eth_z = \"3\"\n"),
         "eth_z is an entity's key, on the root", room.c_str()},
        {"a key below an entity", Replace(kRoom, body + "\n", body + "\nmetadata/eth_z = \"1\"\n"),
         "Body: eth_z below an entity node", body.c_str()},
        {"a normal map on a node that draws nothing",
         Replace(kRoom, markerZ, markerZ + "metadata/eth_normal = \"res://assets/entities/normalmaps/wall_n.png\"\n"),
         "eth_normal on a node that neither draws a sprite nor owns a light", marker.c_str()},
        {"a static flag on a node that draws nothing", Replace(kRoom, markerZ, markerZ + "metadata/eth_static = \"1\"\n"),
         "eth_static on a node that neither draws a sprite nor owns a light", marker.c_str()},
    };

    int refused = 0;
    for (const Refusal& r : refusals) {
        Tscn::Scene scene;
        std::string error;
        const bool parsed = Tscn::Parse(r.text, scene, error);
        CHECK_MSG(parsed, std::string(r.what) + ": the scene itself parses - " + error);
        if (!parsed) continue;

        Lighting::Scene look;
        look.intensity = -1.0;
        look.nodes["stale"] = Lighting::Look{};
        error.clear();
        const bool ok = Lighting::Read(scene, res, look, error);
        CHECK_MSG(!ok, std::string(r.what) + " is refused");
        CHECK_MSG(error.find(r.says) != std::string::npos, std::string(r.what) + ": said \"" + error + "\"");
        const std::string line = "line " + std::to_string(LineOf(r.text, r.header)) + ": ";
        CHECK_MSG(error.starts_with(line), std::string(r.what) + ": wanted " + line + "..., said \"" + error + "\"");
        CHECK_MSG(look.nodes.empty() && look.intensity == 2.0, std::string(r.what) + ": what was read is not kept");
        if (!ok && error.find(r.says) != std::string::npos) ++refused;
    }
    CHECK_EQ(refused, static_cast<int>(refusals.size()));
    std::printf("  %d refusals, each naming its line\n", refused);
}

// ---- the converted levels ---------------------------------------------------------

struct Totals {
    int levels = 0;
    int lightmaps = 0;
    int lights = 0;
    int staticLights = 0;
    int halos = 0;
    int lightsWithoutHalo = 0;
    int normals = 0;
    int normalsUnlit = 0;
    int emissive = 0;
    int deep = 0; // z != 0
    int coloured = 0;
    int nodes = 0;
    int playersAboveZero = 0;
    std::set<std::string> normalFiles;
    std::map<std::string, int> lightmapsByLevel;
    std::set<std::string> lightmapFiles; // "<level>/add<id>.png", as referenced
    int lightmapsMisfiled = 0;
};

const std::string kLightmapDir = "/assets/lightmaps/";

std::string IdOf(const std::string& node) { return node.substr(node.rfind('_') + 1); }

void LevelsCarryWhatTheConverterReported() {
    std::vector<std::filesystem::path> files;
    for (const auto& entry : std::filesystem::directory_iterator(kLevels))
        if (entry.path().extension() == ".tscn") files.push_back(entry.path());
    std::sort(files.begin(), files.end());
    CHECK_EQ(files.size(), std::size_t{128});

    const std::string res = kLevels + "/..";
    Totals t;
    std::map<std::string, Lighting::Scene> kept; // the levels pinned by name below
    const std::set<std::string> keep = {"level0", "level1", "level7", "level10a", "level21c", "level8a", "level0a"};

    for (const auto& file : files) {
        const std::string level = file.stem().string();
        Tscn::Scene scene;
        Lighting::Scene look;
        std::string error;
        const bool ok = Tscn::Load(file.string(), scene, error) && Lighting::Read(scene, res, look, error);
        CHECK_MSG(ok, level + ": " + error);
        if (!ok) continue;
        ++t.levels;

        for (const Tscn::Node& node : scene.nodes) {
            const Tscn::Value* name = node.Meta("entity_name");
            const auto found = look.nodes.find(node.name);
            if (node.parent == "." && name != nullptr && name->text == "main_char" && found != look.nodes.end() &&
                found->second.z != 0.0)
                ++t.playersAboveZero;
        }

        for (const auto& [node, l] : look.nodes) {
            ++t.nodes;
            if (!l.lightmap.empty()) {
                ++t.lightmaps;
                ++t.lightmapsByLevel[level];
                // Its own level's directory, and the add<id>.png of this node's
                // own instance id: the converter kept the ids so the pair holds.
                const std::string want = res + kLightmapDir + level + "/add" + IdOf(node) + ".png";
                if (l.lightmap != want) {
                    ++t.lightmapsMisfiled;
                    CHECK_MSG(false, level + "/" + node + ": " + l.lightmap);
                }
                t.lightmapFiles.insert(level + "/add" + IdOf(node) + ".png");
            }
            if (l.light) {
                ++t.lights;
                if (l.isStatic) ++t.staticLights;
                if (l.light->halo.empty()) ++t.lightsWithoutHalo;
                else ++t.halos;
            }
            if (!l.normal.empty()) {
                ++t.normals;
                t.normalFiles.insert(std::filesystem::path(l.normal).filename().string());
                if (!l.applyLight) ++t.normalsUnlit;
            }
            if (l.emissive != glm::dvec3(0.0)) ++t.emissive;
            if (l.z != 0.0) ++t.deep;
            if (l.colour != glm::dvec4(1.0)) ++t.coloured;
        }
        if (keep.count(level) != 0) kept.emplace(level, std::move(look));
    }

    std::printf("  lighting: %d of 128 levels read, %d entity nodes; %d lightmaps in %zu levels; %d lights, %d on "
                "static owners, %d with a halo; %d normal maps (%zu files); %d emissive; %d with a depth\n",
                t.levels, t.nodes, t.lightmaps, t.lightmapsByLevel.size(), t.lights, t.staticLights, t.halos,
                t.normals, t.normalFiles.size(), t.emissive, t.deep);

    CHECK_EQ(t.levels, 128);
    CHECK_EQ(t.lightmaps, 730);
    CHECK_EQ(t.lightmapsByLevel.size(), std::size_t{67});
    CHECK_EQ(t.lightmapsMisfiled, 0);
    CHECK_EQ(t.lights, 72);
    CHECK_EQ(t.staticLights, 68);
    CHECK_EQ(t.halos, 71);
    CHECK_MSG(t.lightsWithoutHalo == 1, "level1's portal_static is the one light with no halo bitmap");
    CHECK_EQ(t.normals, 1720);
    CHECK_EQ(t.normalFiles.size(), std::size_t{27});
    CHECK_MSG(t.normalsUnlit == 1, "level0a's static_sphere names a normal map and applies no light");
    CHECK_EQ(t.emissive, 2140);
    CHECK_EQ(t.deep, 1533);
    CHECK_MSG(t.coloured == 0, "no level instance carries a <Color>");
    CHECK_MSG(t.playersAboveZero == 1, "level8a's main_char is the one player marker off z 0");

    // And none elsewhere: every lightmap the converter copied is one a node names,
    // once, so no file sits there that a level does not use.
    std::set<std::string> onDisk;
    std::set<std::string> directories;
    std::error_code ec;
    for (const auto& entry : std::filesystem::recursive_directory_iterator(res + kLightmapDir, ec)) {
        if (entry.is_directory()) directories.insert(entry.path().filename().string());
        if (entry.is_regular_file())
            onDisk.insert(entry.path().parent_path().filename().string() + "/" + entry.path().filename().string());
    }
    CHECK_MSG(!ec, "the copied lightmaps are at " + res + kLightmapDir);
    CHECK_EQ(onDisk.size(), std::size_t{730});
    CHECK_EQ(directories.size(), std::size_t{67});
    CHECK_MSG(onDisk == t.lightmapFiles, "the lightmaps on disk are exactly the ones the levels name");

    // Level 1-1: its ambient, and its torch, every key the converter writes.
    if (const auto it = kept.find("level0"); it != kept.end()) {
        const Lighting::Scene& s = it->second;
        CHECK(Is(s.ambient, 0.35, 0.3, 0.35));
        CHECK(s.intensity == 3.0);
        CHECK_EQ(t.lightmapsByLevel["level0"], 9);
        const auto torch = s.nodes.find("light_ent_696");
        CHECK(torch != s.nodes.end());
        if (torch != s.nodes.end()) {
            const Lighting::Look& l = torch->second;
            CHECK(l.z == -18.0 && l.isStatic && l.applyLight);
            CHECK(EndsWith(l.normal, "/assets/entities/normalmaps/light01_normal.png"));
            CHECK(EndsWith(l.lightmap, "/assets/lightmaps/level0/add696.png"));
            CHECK(l.light.has_value());
            if (l.light) {
                CHECK(Is(l.light->offset, 0.0, -12.0, 24.0));
                CHECK(l.light->range == 300.0);
                CHECK(Is(l.light->colour, 1.0, 0.5, 0.1));
                CHECK(EndsWith(l.light->halo, "/assets/entities/halo.bmp"));
                CHECK(Is(l.light->haloOffset, 0.0, -12.0));
                CHECK(Is(l.light->haloSize, 300.0, 300.0));
                CHECK(l.light->haloBrightness == 0.7);
            }
        }
    } else {
        CHECK_MSG(false, "level0 was read");
    }

    // 1-2's static portal: a light at the default position with no halo at all.
    if (const auto it = kept.find("level1"); it != kept.end()) {
        const auto portal = it->second.nodes.find("portal_static_582");
        CHECK(portal != it->second.nodes.end() && portal->second.light.has_value());
        if (portal != it->second.nodes.end() && portal->second.light) {
            const Lighting::Light& light = *portal->second.light;
            CHECK(Is(light.offset, 0.0, 0.0, 0.0) && light.range == 90.0 && Is(light.colour, 0.6, 0.6, 1.0));
            CHECK(light.halo.empty());
        }
    }

    // 1-8's blue light: a halo at the default brightness.
    if (const auto it = kept.find("level7"); it != kept.end()) {
        const auto blue = it->second.nodes.find("blue_light_ent_930");
        CHECK(blue != it->second.nodes.end() && blue->second.light.has_value());
        if (blue != it->second.nodes.end() && blue->second.light) {
            const Lighting::Light& light = *blue->second.light;
            CHECK(Is(light.offset, 4.0, -28.0, 28.0) && light.range == 317.5 && Is(light.colour, 0.4, 0.7, 1.0));
            CHECK(EndsWith(light.halo, "/assets/entities/spark_halo.bmp"));
            CHECK(Is(light.haloOffset, 4.0, -28.0) && Is(light.haloSize, 208.0, 208.0));
            CHECK_MSG(light.haloBrightness == 1.0, "no eth_halo_brightness: the default");
        }
    }

    // 2-11's fire agent: the engine's default range, halo size and halo offset.
    if (const auto it = kept.find("level10a"); it != kept.end()) {
        const auto fire = it->second.nodes.find("fire_agent_ent_930");
        CHECK(fire != it->second.nodes.end() && fire->second.light.has_value());
        if (fire != it->second.nodes.end() && fire->second.light) {
            const Lighting::Look& l = fire->second;
            CHECK(l.isStatic && !l.applyLight && l.normal.empty() && Is(l.emissive, 1.0, 1.0, 1.0));
            CHECK(Is(l.light->offset, 0.0, 0.0, 10.0) && Is(l.light->colour, 1.0, 0.5, 0.2));
            CHECK_MSG(l.light->range == 256.0, "the range written at the default, as every light's is");
            CHECK(EndsWith(l.light->halo, "/assets/entities/portal_halo.png"));
            CHECK_MSG(Is(l.light->haloOffset, 0.0, 0.0) && Is(l.light->haloSize, 64.0, 64.0),
                      "no eth_halo_offset or eth_halo_size: the defaults");
            CHECK(l.light->haloBrightness == 0.55);
        }
    }

    // 4-22, a darkest level: the file's own ambient, which the game replaces.
    if (const auto it = kept.find("level21c"); it != kept.end()) {
        CHECK(Is(it->second.ambient, 0.5, 0.5, 0.5));
        CHECK(it->second.intensity == 3.5);
        CHECK_EQ(t.lightmapsByLevel.count("level21c"), std::size_t{0});
    }

    // 2-09's player marker, at z 2.
    if (const auto it = kept.find("level8a"); it != kept.end()) {
        const auto player = it->second.nodes.find("main_char_29");
        CHECK(player != it->second.nodes.end() && player->second.z == 2.0);
    }

    // 2-01's sphere: a normal map on a sprite that applies no light.
    if (const auto it = kept.find("level0a"); it != kept.end()) {
        const auto sphere = it->second.nodes.find("static_sphere_790");
        CHECK(sphere != it->second.nodes.end());
        if (sphere != it->second.nodes.end()) {
            CHECK(sphere->second.isStatic && !sphere->second.applyLight);
            CHECK(EndsWith(sphere->second.normal, "/assets/entities/normalmaps/sphere_normal.png"));
        }
    }
}

} // namespace

int main() {
    TheScriptsAmbientIsReadAndFollowsTheTorch();
    EveryKeyIsReadAndEveryAbsenceIsTheDefault();
    AnythingElseIsRefusedNamingTheLine();
    EachLightReachesTheSpritesItShould();

    std::error_code ec;
    if (!std::filesystem::is_directory(kLevels, ec)) {
        std::printf("test_mp_lighting: the levels' part SKIPPED - needs the converted levels at %s.\n"
                    "  They live outside this repository; configure with -DSUPERSONIC_MAGICPORTALS_LEVELS=...\n",
                    kLevels.c_str());
        return ::test::summary("test_mp_lighting", 150);
    }
    LevelsCarryWhatTheConverterReported();
    return ::test::summary("test_mp_lighting", 350);
}
