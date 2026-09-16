#pragma once

// A level's own art: every Sprite2D the remake's converter wrote, as the port
// draws it.
//
// The converter gives each entity node that has a picture one Sprite2D child,
// and only ever says four things about it: the texture, an offset, and - on
// the entity node above it - a z_index and, for a glow, a CanvasItemMaterial
// whose blend_mode is 1. Godot draws a Sprite2D centred and at its image's own
// size (no scale is ever written), and orders a canvas by z_index and then by
// the order of the tree. That is all this reads, and it reads it strictly: a
// sprite that is not a child of an entity node, a texture that is not a
// res:// file, or a blend the port does not draw is an error naming the line.
//
// res:// is the directory ABOVE the levels: the converter writes the levels to
// out/levels and the art they name to out/assets, so resolving against the
// levels' parent needs no path of its own. The art, like the levels, is the
// original's and stays outside this repository.
//
// Renderer-free, like the rest of the simulation side. An image's size comes
// from its header (ImageSize) rather than from decoding it.

#include "sim/Tscn.hpp"

#include <string>
#include <vector>

#include <glm/glm.hpp>

namespace MagicPortals::Sprites {

struct Sprite {
    std::string node;          // the entity node it belongs to, e.g. "wall_w3_ent_832"
    std::string texture;       // the image on disk: its res:// path resolved against resRoot
    glm::dvec2 sizePx{0.0};    // the image's own size
    glm::dvec2 atPx{0.0};      // the entity node's position
    double rotation = 0.0;     // the entity node's, in Godot's radians: clockwise on the screen
    glm::dvec2 offsetPx{0.0};  // the sprite's offset, in the entity node's frame
    int zIndex = 0;            // the entity node's z_index
    int order = 0;             // its place in the level's drawing order, from the back
    bool additive = false;     // blend_mode 1: added to what is behind it rather than mixed
};

// Every sprite of a level, in drawing order: by z_index, then as the file lists
// them. `resRoot` is the directory res:// stands for. False, with `error`, for
// anything the reader does not know, and for an image it cannot size.
bool Find(const Tscn::Scene& scene, const std::string& resRoot, std::vector<Sprite>& out, std::string& error);

// Where a sprite's centre is, in the level's pixels: the node's position plus
// the offset, turned with the node.
glm::dvec2 CentrePx(const Sprite& sprite);

// An image's size, from its header: a PNG or a BMP, the two kinds of file the
// converter copies, or a JPEG, which five of the original's entities name for a
// particle system (particles/explosion.JPG). A JPEG is sized only when its
// segments and frame header are laid out as the renderer's decoder reads them;
// the tables and the components' sampling are left to the renderer. False, with
// `error` naming the file, for anything else.
bool ImageSize(const std::string& path, int& width, int& height, std::string& error);

} // namespace MagicPortals::Sprites
