#include "sim/Sprites.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <fstream>
#include <iterator>
#include <string_view>

namespace MagicPortals::Sprites {

namespace {

constexpr unsigned char kPngSignature[8] = {0x89, 'P', 'N', 'G', 0x0D, 0x0A, 0x1A, 0x0A};
constexpr std::string_view kRes = "res://";

// Godot's CanvasItemMaterial blend modes that the converter can write, and the
// two the port draws: 0 mixes (the default), 1 adds.
constexpr double kBlendMix = 0.0;
constexpr double kBlendAdd = 1.0;

uint32_t BigEndian32(const unsigned char* p) {
    return (uint32_t{p[0]} << 24) | (uint32_t{p[1]} << 16) | (uint32_t{p[2]} << 8) | uint32_t{p[3]};
}

int64_t LittleEndian32(const unsigned char* p) {
    const uint32_t u = uint32_t{p[0]} | (uint32_t{p[1]} << 8) | (uint32_t{p[2]} << 16) | (uint32_t{p[3]} << 24);
    return static_cast<int64_t>(static_cast<int32_t>(u));
}

int64_t LittleEndian16(const unsigned char* p) { return static_cast<int64_t>(uint32_t{p[0]} | (uint32_t{p[1]} << 8)); }

std::string At(const Tscn::Node& node) { return "line " + std::to_string(node.line) + ": "; }

// A JPEG keeps its size in its frame header, which may stand behind any number
// of tables and application segments, so the segments are walked to it. The
// marker walk and the frame header's layout are held to what stb_image, the
// renderer's decoder (TextureRegistry::Acquire), accepts:
//  - before the frame header only the tables (DQT, DHT, DRI) and the application
//    and comment segments, each skipped by its length (stb_image.h:3099-3199);
//    padding between segments and fill bytes before a marker are passed over
//    (:2919-2927, :3375-3382);
//  - the frame header is SOF0, SOF1 or SOF2 alone (:3360): lossless, hierarchical
//    and arithmetic-coded frames are refused;
//  - its length is 8 + 3 a component, of 1, 3 or 4 components, at 8 bits a sample
//    (:3262-3274); the height comes before the width.
// Not checked: the tables' contents and each component's sampling factors and
// table index (:3290-3293). A file stb_image refuses for those is still sized
// here, and the renderer draws it as the fallback.
// `file` stands just past the start marker (0xFF 0xD8). An empty answer is a size.
std::string JpegSize(std::istream& file, int64_t& width, int64_t& height) {
    const auto next = [&file](int& byte) {
        byte = file.get();
        return byte != std::char_traits<char>::eof();
    };
    const auto bigEndian16 = [](const unsigned char* p) { return (int{p[0]} << 8) | int{p[1]}; };
    bool first = true;
    for (;;) {
        int byte = 0;
        if (!next(byte)) return "is cut short before its frame header";
        // stb_image scans for the next marker only after a segment it has read.
        if (byte != 0xFF && first) return "has no marker after its start";
        while (byte != 0xFF) {
            if (!next(byte)) return "is cut short before its frame header";
        }
        while (byte == 0xFF) {
            if (!next(byte)) return "is cut short before its frame header";
        }
        first = false;
        const int marker = byte;

        if (marker == 0xC0 || marker == 0xC1 || marker == 0xC2) {
            unsigned char frame[8] = {}; // length, precision, height, width, components
            if (!file.read(reinterpret_cast<char*>(frame), sizeof frame)) return "is cut short in its frame header";
            const int length = bigEndian16(frame);
            const int components = frame[7];
            if (frame[2] != 8) {
                return "is a JPEG of " + std::to_string(frame[2]) + " bits a sample, which the renderer does not decode";
            }
            if ((components != 1 && components != 3 && components != 4) || length != 8 + 3 * components) {
                return "has a frame header the renderer does not decode";
            }
            height = bigEndian16(frame + 3);
            width = bigEndian16(frame + 5);
            return {};
        }
        const bool table = marker == 0xDB || marker == 0xC4 || marker == 0xDD;
        const bool application = (marker >= 0xE0 && marker <= 0xEF) || marker == 0xFE;
        if (!table && !application) {
            const char digits[] = "0123456789ABCDEF";
            return std::string("is a JPEG with marker 0x") + digits[marker >> 4] + digits[marker & 0xF] +
                   " before its frame header, which the renderer does not decode";
        }
        unsigned char size[2] = {};
        if (!file.read(reinterpret_cast<char*>(size), sizeof size)) return "is cut short before its frame header";
        const int length = bigEndian16(size);
        if (length < 2) return "has a segment shorter than its own length";
        file.seekg(length - 2, std::ios::cur);
    }
}

} // namespace

bool ImageSize(const std::string& path, int& width, int& height, std::string& error) {
    std::ifstream file(path, std::ios::binary);
    if (!file) {
        error = path + " cannot be read";
        return false;
    }
    unsigned char header[26] = {};
    file.read(reinterpret_cast<char*>(header), sizeof header);
    const std::streamsize got = file.gcount();

    int64_t w = 0;
    int64_t h = 0;
    if (got >= 24 && std::equal(std::begin(kPngSignature), std::end(kPngSignature), header) &&
        std::string_view(reinterpret_cast<const char*>(header + 12), 4) == "IHDR") {
        // The IHDR chunk comes first, and opens with the width and the height.
        w = BigEndian32(header + 16);
        h = BigEndian32(header + 20);
    } else if (got >= 26 && header[0] == 'B' && header[1] == 'M') {
        // After the 14-byte file header, the DIB header says its own size. The
        // old OS/2 one (12 bytes) keeps the sizes in 16 bits; every later one in
        // 32, with a negative height for an image stored top down.
        if (LittleEndian32(header + 14) == 12) {
            w = LittleEndian16(header + 18);
            h = LittleEndian16(header + 20);
        } else {
            w = LittleEndian32(header + 18);
            h = std::llabs(LittleEndian32(header + 22));
        }
    } else if (got >= 2 && header[0] == 0xFF && header[1] == 0xD8) {
        // A short file has set the stream's end; the walk starts again after the
        // start marker.
        file.clear();
        file.seekg(2);
        if (const std::string why = JpegSize(file, w, h); !why.empty()) {
            error = path + " " + why;
            return false;
        }
    } else {
        error = path + " is not a PNG, a BMP or a JPEG";
        return false;
    }
    if (w <= 0 || h <= 0 || w > INT32_MAX || h > INT32_MAX) {
        error = path + " says it has no size";
        return false;
    }
    width = static_cast<int>(w);
    height = static_cast<int>(h);
    return true;
}

glm::dvec2 CentrePx(const Sprite& sprite) {
    const double c = std::cos(sprite.rotation);
    const double s = std::sin(sprite.rotation);
    const glm::dvec2& o = sprite.offsetPx;
    return sprite.atPx + glm::dvec2(o.x * c - o.y * s, o.x * s + o.y * c);
}

bool Find(const Tscn::Scene& scene, const std::string& resRoot, std::vector<Sprite>& out, std::string& error) {
    out.clear();
    std::vector<Sprite> found;
    for (const Tscn::Node& node : scene.nodes) {
        if (node.type != "Sprite2D") continue;

        const Tscn::Node* owner = scene.FindNode(node.parent);
        if (owner == nullptr || owner->parent != ".") {
            error = At(node) + "a sprite under " + node.parent + ", which is not one of the level's entities";
            return false;
        }
        Sprite sprite;
        sprite.node = owner->name;

        const Tscn::Value* texture = node.Find("texture");
        if (texture == nullptr || texture->kind != Tscn::Value::Kind::ExtResource) {
            error = At(node) + owner->name + "'s sprite has no texture";
            return false;
        }
        const Tscn::Resource* image = scene.External(texture->text);
        if (image == nullptr || image->type != "Texture2D" || image->path.rfind(kRes, 0) != 0) {
            error = At(node) + owner->name + "'s texture is not a res:// image";
            return false;
        }
        sprite.texture = resRoot + "/" + image->path.substr(kRes.size());
        int width = 0;
        int height = 0;
        std::string why;
        if (!ImageSize(sprite.texture, width, height, why)) {
            error = At(node) + owner->name + "'s texture: " + why;
            return false;
        }
        sprite.sizePx = glm::dvec2(width, height);

        if (const Tscn::Value* offset = node.Find("offset"); offset != nullptr) {
            if (offset->kind != Tscn::Value::Kind::Vector2) {
                error = At(node) + owner->name + "'s sprite offset is not a Vector2";
                return false;
            }
            sprite.offsetPx = glm::dvec2(offset->numbers[0], offset->numbers[1]);
        }

        if (const Tscn::Value* material = node.Find("material"); material != nullptr) {
            const Tscn::Resource* canvas =
                material->kind == Tscn::Value::Kind::SubResource ? scene.Embedded(material->text) : nullptr;
            const Tscn::Value* mode = canvas != nullptr ? canvas->Find("blend_mode") : nullptr;
            double blend = -1.0;
            if (canvas == nullptr || canvas->type != "CanvasItemMaterial" || mode == nullptr ||
                !mode->AsNumber(blend) || (blend != kBlendMix && blend != kBlendAdd)) {
                error = At(node) + owner->name + "'s sprite has a material whose blend_mode the port does not draw";
                return false;
            }
            sprite.additive = blend == kBlendAdd;
        }

        if (const Tscn::Value* at = owner->Find("position"); at != nullptr) {
            if (at->kind != Tscn::Value::Kind::Vector2) {
                error = At(*owner) + owner->name + "'s position is not a Vector2";
                return false;
            }
            sprite.atPx = glm::dvec2(at->numbers[0], at->numbers[1]);
        }
        if (const Tscn::Value* turn = owner->Find("rotation"); turn != nullptr && !turn->AsNumber(sprite.rotation)) {
            error = At(*owner) + owner->name + "'s rotation is not a number";
            return false;
        }
        if (const Tscn::Value* z = owner->Find("z_index"); z != nullptr) {
            double zIndex = 0.0;
            if (!z->AsNumber(zIndex) || zIndex != std::floor(zIndex)) {
                error = At(*owner) + owner->name + "'s z_index is not a whole number";
                return false;
            }
            sprite.zIndex = static_cast<int>(zIndex);
        }
        found.push_back(std::move(sprite));
    }

    // Godot's canvas order: z_index first, then the tree, which is the file's
    // order - so a stable sort of what was found in file order.
    std::stable_sort(found.begin(), found.end(),
                     [](const Sprite& a, const Sprite& b) { return a.zIndex < b.zIndex; });
    for (std::size_t i = 0; i < found.size(); ++i) found[i].order = static_cast<int>(i);
    out = std::move(found);
    return true;
}

} // namespace MagicPortals::Sprites
