#pragma once

// Which file of an image the original draws, how big it is, and how its sheet is
// cut: the texture tiers (hd, fullhd) and the one cut rule. data/tiers.json.
//
// WHICH FILE. The original loads every image - a level's sprites, the entities the
// script adds, their normal maps, halos and lightmaps, particle bitmaps - through
// ETHGraphicResourceManager::AddFile (ETHResourceManager.cpp:133-144), which asks
// ETHSpriteDensityManager::ChooseSpriteVersion (ETHSpriteDensityManager.cpp:88-139)
// for the file: <dir>/fullhd/<file>, then <dir>/hd/<file>, each tried only when
// the screen is tall enough and taken when FileExists says so, else <file> itself
// (the folder and name split by AssembleResourceName, :81-86). Only the name is
// tried: a tier file whose 1x name no one gives is never chosen - the original's
// particles/hd/tesla_shock_.png beside particles/tesla_shock.png - and a named file
// with no 1x copy is still found in its tier. The port fixes the choice at 720 px
// (the owner's ruling R12; tiers.json's _ruling), so `search` is the list the
// original tries there.
//
// HOW BIG. SetSpriteDensity hands the tier's density to
// GLES2Sprite::SetSpriteDensityValue (GLES2Sprite.cpp:429-438): the bitmap is
// texels / density units, a float, and SetupSpriteRects(1, 1) runs at once.
// SetupSpriteRects (gs2d/src/Sprite.cpp:104-136) reads GetBitmapSize, which casts
// each axis to int (GLES2Sprite.cpp:389-392), and strides the grid in whole units,
// int(size) / columns (Sprite.cpp:119-120). An entity is then as big as its frame
// (ETHSpriteEntity.cpp:113 cuts it; GetCurrentSize, :652-683, is
// Sprite::GetFrameSize, Sprite.cpp:227-230, the rect). So the truncation comes
// AFTER the divide: an image's units are int(texels / D), and a frame's are
// int(texels / D) / columns, never (texels / columns) / D. The two differ on the
// sheets whose hd width is not a multiple of 2 x columns: minion.png, 310 texels
// over 4 columns at D 2, is 38 u (76 texels) a frame, not 38.75; ghost.png, 973
// at D 2, is 486 u and 121 u a frame (242 texels), not 121.625.
//
// WHERE A FRAME IS SAMPLED. The sprite shader maps a quad's 0..1 coordinate to
// texCoord * (rectSize / bitmapSize) + rectPos / bitmapSize (gs2d
// projects/Android/GS2D/assets/shaders/default/default.vs:38-39), with bitmapSize
// the float texels / D. So a frame's uv scale is its stride in units over
// texels / D, which is its stride in texels over the texels, and column c starts
// at c times that. An even split (the engine's SpriteAnimationComponent,
// CellTransform) samples column 3 of the hd minion.png 4.5 texels off.
//
// Level art draws through it since step 66 (00_order 2.2): Sprites::Find resolves
// every image a level names and sizes it in units. The port's own art and particles
// (2.3) and the sheets cut unevenly (5.1, 12.2) take it in the steps that follow;
// the order and its decisions are the planning doc's step 61.
//
// Renderer-free: a tier file is found by asking the filesystem whether it exists,
// as the original does, and nothing is decoded.

#include <string>
#include <vector>

#include <glm/glm.hpp>

namespace MagicPortals::Tiers {

struct Tier {
    std::string folder;   // the folder beside the image: "fullhd", "hd"
    float density = 1.0f; // texels per unit in that folder
};

// tiers.json's density_tiers: the folders tried, in the order they are tried.
struct Rules {
    std::vector<Tier> search;
};

// False, with `error`, for an empty search, a folder named twice, a folder with no
// density or a density at or below zero, or a folder that is not one plain name.
bool LoadRules(const std::string& path, Rules& out, std::string& error);

// The file the original draws for `named`, and at what density.
struct Resolved {
    std::string path;     // parent(named)/<folder>/filename(named), or `named` itself
    std::string tier;     // the folder it was found in; empty when it is `named`
    float density = 1.0f; // 1 when it is `named`
};

// ChooseSpriteVersion over the fixed search: the first
// parent(named)/<folder>/filename(named) that exists as a file, else `named` at
// density 1 (whether `named` exists is the caller's to find out). `named` is split
// at its last '/' or '\', as AssembleResourceName splits a path that has one. A
// name with neither is looked for in <folder>/<name> from the working directory,
// where the original finds no tier: GetFileDirectory returns the whole name when
// it has no separator (gs2d/src/Platform/Platform.cpp:40-50), so it tries
// <name><folder>/<name>. Its callers pass a path: Sprites::Find passes res:// resolved
// against the levels' parent, always with '/'.
Resolved Resolve(const Rules& rules, const std::string& named);

// An uncut image's size in units: int(texels / density), per axis. The float
// divide and the int cast are the original's (GLES2Sprite.cpp:435, :391).
glm::ivec2 Units(const glm::ivec2& texels, float density);

// A sheet cut into columns x rows, as SetupSpriteRects cuts it.
struct Cut {
    glm::ivec2 units{0};       // the whole image: Units(texels, density)
    glm::ivec2 frameUnits{0};  // one frame: units / (columns, rows), whole units
    glm::dvec2 frameTexels{0.0}; // one frame in the file's texels: frameUnits * density
    glm::dvec2 uvScale{0.0};   // one frame's share of the texture: frameUnits / (texels / density)
};

// False, with `error`, for texels, a density, columns or rows at or below zero, and
// for a sheet whose frame would be 0 units on an axis: the original's rect is then
// empty on that axis, and on both GetFrameSize falls back to the whole bitmap
// (Sprite.cpp:229). Neither is a frame of the sheet.
bool FrameCut(const glm::ivec2& texels, float density, int columns, int rows, Cut& out, std::string& error);

// Where frame (column, row) starts in the texture: (column, row) * uvScale, the
// shader's rectPos / bitmapSize for the rect SetupSpriteRects gives it.
glm::dvec2 UvOffset(const Cut& cut, int column, int row);

} // namespace MagicPortals::Tiers
