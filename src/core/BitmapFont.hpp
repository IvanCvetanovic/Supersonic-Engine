#pragma once

// A bitmap font, as BMFont writes it: a .fnt descriptor beside one or more page
// images, and the glyph quads a string turns into.
//
// WHY THIS EXISTS. The engine's only text is ImGui's - UISystem says so in as
// many words, and says why: ImGui already owns a rasterised atlas and writing a
// second glyph rasteriser to draw the same fonts into the same window would be
// a lot of code to arrive back where it starts. That is true of a HUD drawn
// over the game. It is not true of text that belongs IN the world: a number
// that floats off a damaged unit, a sign on a wall, a score that sits in a
// level's own coordinates and pans with the camera. None of that could be drawn
// at all, in any game on this engine, because ImGui draws in screen space
// against its own atlas and nothing else could draw a glyph.
//
// So this deliberately does NOT rasterise anything and does not know about
// ImGui. It reads a font somebody already made, and turns a string into
// geometry - positions and texture coordinates over the font's page - which the
// caller uploads through MeshRegistry and draws like any other textured quad,
// with the page as its albedo. No pipeline, no pass, no layout engine.
//
// The format is BMFont's text variant, which is what Magic Portals' Matura
// fonts are: `info`, `common`, `page` and `char` lines of `key=value` pairs.
// The binary variant is not read; nothing needs it yet and guessing at a
// format nobody has a sample of is how a decoder acquires bugs.

#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

#include <glm/glm.hpp>

#include "core/MeshData.hpp"

namespace Supersonic {

// One glyph's place on a page, and how it sits on the line.
//
// All of it in the font's own pixels, exactly as the .fnt states it. Scaling
// belongs to whoever draws it, because a font used at two sizes in one scene is
// one font and two scales, not two fonts.
struct FontGlyph {
    // Where it is on its page.
    int x = 0;
    int y = 0;
    int width = 0;
    int height = 0;

    // Where it sits relative to the pen. yoffset is DOWN from the line's top,
    // which is the direction BMFont writes and the opposite of this engine's
    // world +y - BuildText does that flip once, so callers never think about
    // it.
    int xoffset = 0;
    int yoffset = 0;

    // How far the pen moves after it. Not the same as `width`: a glyph may
    // overhang its advance in either direction, which is most of what makes a
    // script face look like handwriting rather than a grid.
    int xadvance = 0;

    int page = 0;
};

class BitmapFont {
public:
    // False, with `error`, for a file that will not open, a descriptor with no
    // `common` line, a page the descriptor names but does not describe, or a
    // char line missing the fields a glyph cannot be placed without.
    //
    // A char line with an UNKNOWN extra key is fine: BMFont writers differ and
    // refusing a file over a field nobody reads would be refusing it for being
    // written by the wrong tool.
    bool Load(const std::string& fntPath, std::string& error);

    bool IsLoaded() const { return m_lineHeight > 0 && !m_glyphs.empty(); }

    // The page images this font needs, in page-id order, as the paths they
    // resolve to beside the descriptor. The caller uploads them; this class
    // never touches a texture.
    const std::vector<std::string>& Pages() const { return m_pages; }

    int LineHeight() const { return m_lineHeight; }
    int Base() const { return m_base; }
    glm::ivec2 PageSize() const { return glm::ivec2(m_scaleW, m_scaleH); }

    // Null for a character the font has no glyph for.
    const FontGlyph* Find(uint32_t codepoint) const;

    // How wide and tall `text` is, in the font's own pixels, honouring newlines.
    //
    // The width is the widest LINE, not the sum of every advance, or a string
    // with a newline in it would measure as though it were one long line - and
    // centred text would then sit off to the left by half of everything after
    // the break.
    glm::vec2 Measure(const std::string& text) const;

    // `text` as quads over the font's pages, in the font's own pixels, with the
    // pen starting at the origin and running +x, lines descending -y.
    //
    // ONE PAGE AT A TIME, because a mesh is drawn with one texture: `page`
    // selects which page's glyphs are emitted, and a string whose glyphs span
    // two pages is two meshes and two draws. Almost every font here is a single
    // page, so the common case costs nothing; the alternative - silently
    // dropping the glyphs that live elsewhere - is a word with letters missing
    // and nothing to say why.
    //
    // The texture coordinates are normalised against the page size, so the
    // caller needs no knowledge of the atlas.
    bool BuildText(const std::string& text, int page, MeshData& out) const;

private:
    std::unordered_map<uint32_t, FontGlyph> m_glyphs;
    std::vector<std::string> m_pages;
    int m_lineHeight = 0;
    int m_base = 0;
    int m_scaleW = 0;
    int m_scaleH = 0;
};

} // namespace Supersonic
