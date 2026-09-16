#pragma once

// What the original's lighting needs from a level: the converter's eth_* keys.
//
// The original lights a sprite per pixel as
//   clamp(texel * colour * min(1, ambient + emissive) + lightmap) + one term per live light,
// every value an 8-bit encoded one, and halos added on top. The remake's
// converter carries every input of that which a level file holds as quoted
// metadata/eth_* strings (the remake's docs/ethanon-formats.md, the 2026-09-14
// lighting block, holds the mapping and its engine citations), and copies the
// files they name - lightmaps, normal maps, halo bitmaps - beside the art. This
// reads them. It draws nothing: what the port does with them is
// out/parity/specs/lighting/design_port.md sections 4 and 5, steps G3 onwards.
//
// The vocabulary, and where each key may stand:
//
//   root node      eth_ambient "r g b", eth_light_intensity "LI" - both, always
//   entity nodes   eth_z "z", eth_emissive "r g b"                 - on any
//   (the root's    eth_static "1", eth_apply_light "1",
//   children)      eth_color "r g b a", eth_normal "res://..."     - only on a node that
//                  eth_lightmap "res://..."                          draws a sprite or owns
//                  eth_light_range "units" (the light itself),       a light
//                  eth_light_offset "x y z", eth_light_color "r g b",
//                  eth_halo "res://...", eth_halo_offset "x y",
//                  eth_halo_size "w h", eth_halo_brightness "b"
//
// An absent key is the ENGINE'S default, which is decoded rather than guessed
// (the defaults below, each cited in the remake's docs). The one exception is
// the root pair: every level file has a <SceneProperties>, so a root without
// both keys is refused rather than given ambient (1,1,1) and intensity 2.
//
// Strict, like Tscn and Sprites, and one level up from them: Tscn already reads
// any metadata/* string, so the tscn reader's vocabulary does not grow; the
// strictness about WHICH eth_ keys may appear, and what they may say, is here.
// Refused, each naming the node's line: an eth_ key it does not know, or one
// standing where the converter never writes it; a value that is not a quoted
// string of exactly the numbers the key takes, all finite; a flag that is not
// "0" or "1"; a path that is not res:// or names no file; a lightmap on a node
// that is not static, not applyLight or draws no sprite, or whose size times
// two is not its sprite's; a light or halo key without the key that makes the
// light (eth_light_range) or the halo (eth_halo); a range that is not positive.
//
// Renderer-free, like the rest of the simulation side. It is NOT a pure function
// of the scene's text, in the way Sprites::Find is not: it checks that each path
// names a file, and reads the headers of the lightmaps and of the sprites
// (through Sprites::Find) to compare their sizes.
//
// And what the original's SCRIPT sets over the level file: the port's
// lighting.json, and the ambient light a level is drawn with at any moment
// (Rules, Ambient). Drawn since step 45 as the design's G3: every sprite's colour
// is multiplied by AmbientTerm(Ambient(...), emissive), by the engine's 2D sprite
// path since step 47 (G4), which also adds each Look::lightmap over its sprite.
// Since step 49 (G5) each Look::light is a 2D point light and its halo an added
// quad, and which sprite takes which light is ReceiverMask and LightLayer below.

#include "sim/Torch.hpp"
#include "sim/Tscn.hpp"

#include <cstdint>
#include <optional>
#include <string>
#include <unordered_map>

#include <glm/glm.hpp>

namespace MagicPortals::Lighting {

// One <Light>, already scaled as the engine scales it: the light's position and
// range by the owner's scale.y (ETHEntityRenderingManager.cpp:180-181), the halo
// centred on the unscaled position and sized by the scale per axis
// (ETHRenderEntity.cpp:378-385). Defaults are ETHLight.cpp:27-38.
struct Light {
    glm::dvec3 offset{0.0};      // units, from the owner; lighting only
    glm::dvec3 colour{1.0};      // a multiplier, may exceed 1; the scene's intensity is NOT folded in
    double range = 256.0;        // units
    std::string halo;            // the bitmap on disk, res:// resolved; empty = no halo
    glm::dvec2 haloOffset{0.0};  // units, from the owner, unscaled
    glm::dvec2 haloSize{64.0};   // units
    double haloBrightness = 1.0;
};

// One entity node's lighting facts. A node with no eth_ key at all has exactly
// these defaults (ETHEntityProperties.cpp:79-89, ETHEntity.cpp:96).
struct Look {
    double z = 0.0;              // the original's depth, unrounded; z_index only orders
    bool isStatic = false;
    bool applyLight = false;
    glm::dvec3 emissive{0.0};
    glm::dvec4 colour{1.0};      // the instance colour
    std::string normal;          // the normal map on disk; empty = none
    std::string lightmap;        // the baked lightmap on disk; empty = none
    std::optional<Light> light;
};

struct Scene {
    glm::dvec3 ambient{1.0};     // ETHSceneProperties.cpp:88-94 - but never defaulted, see above
    double intensity = 2.0;      // lightIntensity
    // Every entity node of the level - the root's children - by node name,
    // including those that carry no key.
    std::unordered_map<std::string, Look> nodes;
};

// Replaces `out`. `resRoot` is the directory res:// stands for, as for
// Sprites::Find. False, with `error` reading "line N: <node>: why", for anything
// the reader refuses; `out` is then empty.
bool Read(const Tscn::Scene& scene, const std::string& resRoot, Scene& out, std::string& error);

// The port's lighting.json: the two ambient lights the original's script sets
// in place of a level file's own, and what the lights and halos need that no
// level file says.
struct Rules {
    glm::dvec3 darkestAmbient{0.0};  // DARKEST_AMBIENT_LIGHT, for a level that sets `darkest`
    glm::dvec3 torchLitAmbient{0.0}; // what a lit torch sets
    // The normal maps' green channel points DOWN the image: hPixelLightDiff.ps
    // decodes a texel as -(2 (c - 0.5)) against a light vector in Ethanon's y-down
    // world (the remake's engine_math.md 4.5).
    bool normalMapGreenDown = true;
    // What every halo's formula colour is multiplied by. 1 is the formula itself;
    // the value in the file is a _guess the design leaves to the halo gate
    // (design_port.md 5.5, 8), with its measurements beside it.
    double haloBrightnessScale = 1.0;
};

// False, with `error`, unless both lights are three finite numbers from 0 to 1,
// normal_map_green_down is a bool, and halo_brightness_scale a finite number from
// 0 to 1. The original's ambients are well inside that, and an ambient above 1 is
// clipped by AmbientTerm anyway, so a value outside it is a typing mistake, not a
// light; a halo scale above the formula's own brightness is not one the design
// considers.
bool LoadRules(const std::string& path, Rules& out, std::string& error);

// ---- which light reaches which sprite -------------------------------------------
//
// The engine lights a 2D sprite with every Light2DComponent whose layers share a
// bit with the sprite's light mask. These are the two layers, and the rule that
// hands them out: ETHEntitySpriteRenderer.cpp:70 skips a light's pass on a sprite
// when both are static and lightmaps are on, because a static light is already
// baked into a static sprite's add<id>.png - and it skips it even where no file
// was baked (the design's section 5.2: the in-range static sprites without one
// render at the ambient alone).
inline constexpr std::uint8_t kLiveLights = 1u << 0;   // owned by an entity that is not static
inline constexpr std::uint8_t kStaticLights = 1u << 1; // owned by a static entity

// The mask a sprite takes: nothing unless it applies light (BeginLightPass,
// ETHShaderManager.cpp:138); only the live lights when it is static; both when it
// is not. Lightmaps are always on in the port until the design's step G6 bakes at
// run time.
std::uint8_t ReceiverMask(bool isStatic, bool applyLight);

// The layer a light is on: a light is as static as its owner
// (ETHEntityProperties.cpp:343-348).
std::uint8_t LightLayer(bool ownerStatic);

// How many of a particle system's particles are live, as a share of all of them:
// ETHParticleManager counts a particle released and bigger than nothing
// (ETHParticleManager.cpp:208-213) against the system's particle count. 1 when
// there is no system (total 0), which is how both callers below read an owner
// with no system in its first slot.
double ParticleRatio(int active, int total);

// A light's colour as the shader adds it: <Color> x the scene's lightIntensity x
// the owner's live particle ratio when the owner is not static, and x 1 when it
// is (ETHSpriteEntity::ComputeLightIntensity, ETHSpriteEntity.cpp:595-609;
// ETHPixelLightDiffuseSpecular.cpp:136-146 multiplies in the intensity).
glm::dvec3 LightColour(const Light& light, double intensity, bool ownerStatic, double ratio);

// A halo's colour: <Color> x haloBrightness x the owner's live particle ratio,
// for a static owner too, and NOT the scene's intensity (ETHRenderEntity::DrawHalo,
// ETHRenderEntity.cpp:354-388); then Rules::haloBrightnessScale.
glm::dvec3 HaloColour(const Light& light, double ratio, double scale);

// The ambient light the original draws a level with now, from its events in
// their order:
//   - it starts at the level file's <Ambient> (Scene::ambient), or at
//     darkestAmbient when the level sets `darkest`, which REPLACES the file's
//     (Game's level-properties reader: SetAmbientLight(DARKEST_AMBIENT_LIGHT));
//   - a torch lit sets torchLitAmbient (ETHCallback_light_off's `delete` branch);
//   - a torch put back out sets darkestAmbient (ETHCallback_fire_signal), on any
//     level.
// So the last event decides. A torch is lit now when any of the level's lights
// is (Torch::State::Light::lit), NOT when the cumulative `lit` counter is above
// zero: that one never comes down. Every level places at most one torch
// (torch.json's census), so "any lit now, else put out before, else the start"
// is exactly the last event.
//
// One frame early against the original, which sets the lit ambient on the frame
// AFTER the shot; the port has it from the tick the torch is shot.
glm::dvec3 Ambient(const Rules& rules, const glm::dvec3& fileAmbient, bool darkest, const Torch::State& torch);

// What a sprite's colour is multiplied by before anything is added:
// min(1, ambient + emissive), per channel (ETHRenderEntity.cpp:113-117), for
// every sprite whether or not it applies light, and whatever its blend. The
// remake's fit holds it to 0.28 of 255 over 1,359 unlit blocks (fit.md 4).
glm::dvec3 AmbientTerm(const glm::dvec3& ambient, const glm::dvec3& emissive);

} // namespace MagicPortals::Lighting
