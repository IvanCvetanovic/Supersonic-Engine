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

#include "sim/Tscn.hpp"

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

} // namespace MagicPortals::Lighting
