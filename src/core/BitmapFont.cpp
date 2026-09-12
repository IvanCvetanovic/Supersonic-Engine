#include "core/BitmapFont.hpp"

#include "core/Log.hpp"

#include <cstdlib>
#include <fstream>
#include <sstream>

namespace Supersonic {

namespace {

// The directory `path` sits in, with its separator, or empty for a bare name.
// Page images are named RELATIVE to the descriptor, so this is what resolves
// them - a font loaded by absolute path from outside the project must find its
// pages beside itself rather than beside the working directory.
std::string DirectoryOf(const std::string& path) {
    const std::size_t slash = path.find_last_of("/\\");
    return slash == std::string::npos ? std::string() : path.substr(0, slash + 1);
}

// BMFont writes `key=value` with values sometimes quoted and sometimes not, and
// the quoted ones may contain spaces (`face="Matura MT Script Capitals"`). So a
// line cannot be split on whitespace alone.
//
// Parsed into a map per line rather than positionally, because writers differ in
// which optional keys they emit and in what order. Reading by name is what makes
// a file from another tool acceptable instead of a refusal nobody can explain.
void ParsePairs(const std::string& line, std::unordered_map<std::string, std::string>& out) {
    out.clear();
    std::size_t i = 0;
    while (i < line.size()) {
        while (i < line.size() && std::isspace(static_cast<unsigned char>(line[i]))) ++i;
        const std::size_t keyStart = i;
        while (i < line.size() && line[i] != '=' && !std::isspace(static_cast<unsigned char>(line[i]))) ++i;
        if (i >= line.size() || line[i] != '=') {
            // A bare token - the record's name, like `char` - or trailing
            // rubbish. Neither is a pair; skip to the next whitespace.
            while (i < line.size() && !std::isspace(static_cast<unsigned char>(line[i]))) ++i;
            continue;
        }
        const std::string key = line.substr(keyStart, i - keyStart);
        ++i; // '='

        std::string value;
        if (i < line.size() && line[i] == '"') {
            ++i;
            const std::size_t valueStart = i;
            while (i < line.size() && line[i] != '"') ++i;
            value = line.substr(valueStart, i - valueStart);
            if (i < line.size()) ++i; // closing quote
        } else {
            const std::size_t valueStart = i;
            while (i < line.size() && !std::isspace(static_cast<unsigned char>(line[i]))) ++i;
            value = line.substr(valueStart, i - valueStart);
        }
        out.emplace(key, std::move(value));
    }
}

// The first word of a line: `info`, `common`, `page`, `chars`, `char`, `kerning`.
std::string RecordOf(const std::string& line) {
    std::size_t i = 0;
    while (i < line.size() && std::isspace(static_cast<unsigned char>(line[i]))) ++i;
    const std::size_t start = i;
    while (i < line.size() && !std::isspace(static_cast<unsigned char>(line[i]))) ++i;
    return line.substr(start, i - start);
}

bool IntOf(const std::unordered_map<std::string, std::string>& pairs, const char* key, int& out) {
    const auto it = pairs.find(key);
    if (it == pairs.end()) return false;
    const std::string& text = it->second;
    if (text.empty()) return false;
    char* end = nullptr;
    const long value = std::strtol(text.c_str(), &end, 10);
    if (end == text.c_str() || *end != '\0') return false;
    out = static_cast<int>(value);
    return true;
}

} // namespace

bool BitmapFont::Load(const std::string& fntPath, std::string& error) {
    std::ifstream file(fntPath);
    if (!file) {
        error = fntPath + ": cannot open";
        return false;
    }

    m_glyphs.clear();
    m_pages.clear();
    m_lineHeight = 0;
    m_base = 0;
    m_scaleW = 0;
    m_scaleH = 0;

    const std::string directory = DirectoryOf(fntPath);
    std::unordered_map<std::string, std::string> pairs;
    std::string line;
    bool sawCommon = false;

    while (std::getline(file, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        const std::string record = RecordOf(line);
        if (record.empty()) continue;

        if (record == "common") {
            ParsePairs(line, pairs);
            // Without these a glyph cannot be placed on a line or on its page,
            // so a descriptor missing them is refused rather than defaulted:
            // a guessed lineHeight is text that looks almost right.
            if (!IntOf(pairs, "lineHeight", m_lineHeight) || !IntOf(pairs, "base", m_base) ||
                !IntOf(pairs, "scaleW", m_scaleW) || !IntOf(pairs, "scaleH", m_scaleH)) {
                error = fntPath + ": its common line needs lineHeight, base, scaleW and scaleH";
                return false;
            }
            if (m_lineHeight <= 0 || m_scaleW <= 0 || m_scaleH <= 0) {
                error = fntPath + ": lineHeight and the page size are above zero";
                return false;
            }
            sawCommon = true;
            continue;
        }

        if (record == "page") {
            ParsePairs(line, pairs);
            int id = 0;
            const auto fileIt = pairs.find("file");
            if (!IntOf(pairs, "id", id) || id < 0 || fileIt == pairs.end() || fileIt->second.empty()) {
                error = fntPath + ": a page line needs an id and a file";
                return false;
            }
            // Pages are addressed by id, and ids need not arrive in order.
            if (static_cast<std::size_t>(id) >= m_pages.size()) m_pages.resize(static_cast<std::size_t>(id) + 1);
            m_pages[static_cast<std::size_t>(id)] = directory + fileIt->second;
            continue;
        }

        if (record == "char") {
            ParsePairs(line, pairs);
            int id = 0;
            FontGlyph glyph;
            if (!IntOf(pairs, "id", id) || !IntOf(pairs, "x", glyph.x) || !IntOf(pairs, "y", glyph.y) ||
                !IntOf(pairs, "width", glyph.width) || !IntOf(pairs, "height", glyph.height) ||
                !IntOf(pairs, "xoffset", glyph.xoffset) || !IntOf(pairs, "yoffset", glyph.yoffset) ||
                !IntOf(pairs, "xadvance", glyph.xadvance)) {
                error = fntPath + ": a char line is missing one of id, x, y, width, height, xoffset, "
                                  "yoffset or xadvance";
                return false;
            }
            if (id < 0) continue; // BMFont writes -1 for an unused slot
            IntOf(pairs, "page", glyph.page);
            m_glyphs.emplace(static_cast<uint32_t>(id), glyph);
            continue;
        }
        // info, chars, kerning and anything else: read for nothing. Kerning is
        // deliberately not applied - none of the fonts here carry any, and
        // pretending to honour it would be a claim this does not keep.
    }

    if (!sawCommon) {
        error = fntPath + ": no common line, so it is not a BMFont descriptor";
        return false;
    }
    if (m_glyphs.empty()) {
        error = fntPath + ": describes no characters";
        return false;
    }
    if (m_pages.empty()) {
        error = fntPath + ": names no page image";
        return false;
    }
    for (std::size_t i = 0; i < m_pages.size(); ++i) {
        if (m_pages[i].empty()) {
            error = fntPath + ": page " + std::to_string(i) + " is named by a glyph but never described";
            return false;
        }
    }
    return true;
}

const FontGlyph* BitmapFont::Find(uint32_t codepoint) const {
    const auto it = m_glyphs.find(codepoint);
    return it == m_glyphs.end() ? nullptr : &it->second;
}

glm::vec2 BitmapFont::Measure(const std::string& text) const {
    if (!IsLoaded()) return glm::vec2(0.0f);

    float widest = 0.0f;
    float pen = 0.0f;
    int lines = 1;
    for (const char c : text) {
        if (c == '\n') {
            widest = pen > widest ? pen : widest;
            pen = 0.0f;
            ++lines;
            continue;
        }
        if (const FontGlyph* glyph = Find(static_cast<uint32_t>(static_cast<unsigned char>(c)))) {
            pen += static_cast<float>(glyph->xadvance);
        }
    }
    widest = pen > widest ? pen : widest;
    return glm::vec2(widest, static_cast<float>(lines * m_lineHeight));
}

bool BitmapFont::BuildText(const std::string& text, int page, MeshData& out) const {
    out.clear();
    if (!IsLoaded()) return false;

    const float pageW = static_cast<float>(m_scaleW);
    const float pageH = static_cast<float>(m_scaleH);

    float pen = 0.0f;
    float lineTop = 0.0f; // descends by lineHeight per newline
    uint32_t emitted = 0;

    for (const char c : text) {
        if (c == '\n') {
            pen = 0.0f;
            lineTop -= static_cast<float>(m_lineHeight);
            continue;
        }
        const FontGlyph* glyph = Find(static_cast<uint32_t>(static_cast<unsigned char>(c)));
        if (glyph == nullptr) continue; // a character this font has no glyph for
        if (glyph->page != page) {
            // Not this mesh's page. The pen still advances, so the glyphs that
            // DO belong here stay in the right places and the two meshes line
            // up when drawn together.
            pen += static_cast<float>(glyph->xadvance);
            continue;
        }
        if (glyph->width > 0 && glyph->height > 0) {
            // BMFont's y runs DOWN from the top of the line; this engine's
            // world y runs up. Flipped once, here, so no caller has to.
            const float left = pen + static_cast<float>(glyph->xoffset);
            const float top = lineTop - static_cast<float>(glyph->yoffset);
            const float right = left + static_cast<float>(glyph->width);
            const float bottom = top - static_cast<float>(glyph->height);

            const float u0 = static_cast<float>(glyph->x) / pageW;
            const float v0 = static_cast<float>(glyph->y) / pageH;
            const float u1 = static_cast<float>(glyph->x + glyph->width) / pageW;
            const float v1 = static_cast<float>(glyph->y + glyph->height) / pageH;

            const auto base = static_cast<uint32_t>(out.vertices.size());
            const glm::vec3 normal(0.0f, 0.0f, 1.0f);
            const glm::vec3 white(1.0f);
            out.vertices.push_back({{left, bottom, 0.0f}, normal, white, {u0, v1}});
            out.vertices.push_back({{right, bottom, 0.0f}, normal, white, {u1, v1}});
            out.vertices.push_back({{right, top, 0.0f}, normal, white, {u1, v0}});
            out.vertices.push_back({{left, top, 0.0f}, normal, white, {u0, v0}});

            out.indices.push_back(base + 0);
            out.indices.push_back(base + 1);
            out.indices.push_back(base + 2);
            out.indices.push_back(base + 2);
            out.indices.push_back(base + 3);
            out.indices.push_back(base + 0);
            ++emitted;
        }
        pen += static_cast<float>(glyph->xadvance);
    }

    // The bounds and the tangent basis, for the reason GenerateQuad now states
    // in a comment paid for the hard way: MeshData's bounds default to a POINT,
    // and a mesh that does not compute them is culled against its own centre.
    out.computeTangents();
    out.computeBounds();
    return emitted > 0;
}

} // namespace Supersonic
