#include "sim/Lighting.hpp"

#include "sim/Sprites.hpp"

#include "core/Json.hpp"

#include <algorithm>
#include <array>
#include <charconv>
#include <cmath>
#include <cstddef>
#include <filesystem>
#include <fstream>
#include <initializer_list>
#include <sstream>
#include <string_view>
#include <system_error>
#include <utility>
#include <vector>

namespace MagicPortals::Lighting {

namespace {

constexpr std::string_view kMetaPrefix = "metadata/";
constexpr std::string_view kEthPrefix = "eth_";
constexpr std::string_view kRes = "res://";

enum class Id : std::size_t {
    Ambient,
    Intensity,
    Z,
    Emissive,
    Static,
    ApplyLight,
    Colour,
    Normal,
    Lightmap,
    LightRange,
    LightOffset,
    LightColour,
    Halo,
    HaloOffset,
    HaloSize,
    HaloBrightness,
    Count,
};

// Where the converter writes a key (its tscn.py, _entity_lighting and
// _UNGATED_ENTITY_KEYS). Anywhere: on every entity node whose value is not the
// default. Drawn: only on an entity node that draws a sprite or owns a light.
enum class Place { Root, Anywhere, Drawn };

struct Key {
    std::string_view name;
    Id id;
    Place place;
};

// The whole vocabulary, as the converter wrote it on 14 September 2026 (the
// remake's b572fec). A key not listed here is refused, never skipped: a lighting
// input the port silently ignored would be a level drawn wrong with nothing
// saying why.
constexpr Key kKeys[] = {
    {"eth_ambient", Id::Ambient, Place::Root},
    {"eth_light_intensity", Id::Intensity, Place::Root},
    {"eth_z", Id::Z, Place::Anywhere},
    {"eth_emissive", Id::Emissive, Place::Anywhere},
    {"eth_static", Id::Static, Place::Drawn},
    {"eth_apply_light", Id::ApplyLight, Place::Drawn},
    {"eth_color", Id::Colour, Place::Drawn},
    {"eth_normal", Id::Normal, Place::Drawn},
    {"eth_lightmap", Id::Lightmap, Place::Drawn},
    {"eth_light_range", Id::LightRange, Place::Drawn},
    {"eth_light_offset", Id::LightOffset, Place::Drawn},
    {"eth_light_color", Id::LightColour, Place::Drawn},
    {"eth_halo", Id::Halo, Place::Drawn},
    {"eth_halo_offset", Id::HaloOffset, Place::Drawn},
    {"eth_halo_size", Id::HaloSize, Place::Drawn},
    {"eth_halo_brightness", Id::HaloBrightness, Place::Drawn},
};

constexpr std::size_t kIdCount = static_cast<std::size_t>(Id::Count);

const Key* Find(std::string_view name) {
    for (const Key& key : kKeys)
        if (key.name == name) return &key;
    return nullptr;
}

std::string_view NameOf(Id id) {
    for (const Key& key : kKeys)
        if (key.id == id) return key.name;
    return {};
}

std::string At(const Tscn::Node& node) {
    return "line " + std::to_string(node.line) + ": " + node.name + ": ";
}

// Exactly `count` finite numbers with one space between each, which is how the
// converter's _nums writes them. from_chars rather than strtod, which takes its
// decimal point from the C locale.
bool Numbers(std::string_view text, std::size_t count, double* out) {
    std::size_t got = 0;
    std::size_t from = 0;
    while (true) {
        const std::size_t space = text.find(' ', from);
        const std::string_view item =
            text.substr(from, space == std::string_view::npos ? std::string_view::npos : space - from);
        if (item.empty() || got == count) return false;
        double value = 0.0;
        const char* last = item.data() + item.size();
        const auto [ptr, ec] = std::from_chars(item.data(), last, value);
        if (ec != std::errc() || ptr != last || !std::isfinite(value)) return false;
        out[got++] = value;
        if (space == std::string_view::npos) break;
        from = space + 1;
    }
    return got == count;
}

std::string CountWord(std::size_t count) {
    switch (count) {
    case 1: return "one number";
    case 2: return "two numbers";
    case 3: return "three numbers";
    case 4: return "four numbers";
    default: return std::to_string(count) + " numbers";
    }
}

bool Vector(const std::string& text, glm::dvec3& out, std::string& why) {
    double v[3] = {};
    if (!Numbers(text, 3, v)) {
        why = "\"" + text + "\" is not " + CountWord(3);
        return false;
    }
    out = glm::dvec3(v[0], v[1], v[2]);
    return true;
}

bool Vector(const std::string& text, glm::dvec2& out, std::string& why) {
    double v[2] = {};
    if (!Numbers(text, 2, v)) {
        why = "\"" + text + "\" is not " + CountWord(2);
        return false;
    }
    out = glm::dvec2(v[0], v[1]);
    return true;
}

bool Scalar(const std::string& text, double& out, std::string& why) {
    if (!Numbers(text, 1, &out)) {
        why = "\"" + text + "\" is not " + CountWord(1);
        return false;
    }
    return true;
}

bool Flag(const std::string& text, bool& out, std::string& why) {
    if (text != "0" && text != "1") {
        why = "\"" + text + "\" is not \"0\" or \"1\"";
        return false;
    }
    out = text == "1";
    return true;
}

// A res:// path, resolved against resRoot, that names a file.
bool Path(const std::string& text, const std::string& resRoot, std::string& out, std::string& why) {
    if (!text.starts_with(kRes)) {
        why = "\"" + text + "\" is not a res:// path";
        return false;
    }
    out = resRoot + "/" + text.substr(kRes.size());
    std::error_code ec;
    if (!std::filesystem::is_regular_file(out, ec)) {
        why = out + " is not a file";
        return false;
    }
    return true;
}

} // namespace

bool Read(const Tscn::Scene& scene, const std::string& resRoot, Scene& out, std::string& error) {
    out = Scene{};

    // The sprites' own sizes, for the lightmap's. Sprites::Find is the reader the
    // layer draws with, so a lightmap is checked against the size the layer will
    // stretch it over rather than against a second reading of the same file.
    std::vector<Sprites::Sprite> sprites;
    if (!Sprites::Find(scene, resRoot, sprites, error)) return false;
    std::unordered_map<std::string, glm::dvec2> spriteSizePx;
    for (const Sprites::Sprite& sprite : sprites) spriteSizePx.emplace(sprite.node, sprite.sizePx);

    Scene read;
    const Tscn::Node* root = nullptr;
    bool haveAmbient = false;
    bool haveIntensity = false;
    std::string why;
    const auto fail = [&](const Tscn::Node& node, const std::string& message) {
        error = At(node) + message;
        return false;
    };

    for (const Tscn::Node& node : scene.nodes) {
        const bool isRoot = node.path == ".";
        const bool isEntity = node.parent == ".";
        if (isRoot) root = &node;

        Look look;
        Light light;
        std::array<bool, kIdCount> has{};

        for (const Tscn::Property& property : node.properties) {
            if (!property.key.starts_with(kMetaPrefix)) continue;
            const std::string name = property.key.substr(kMetaPrefix.size());
            if (!name.starts_with(kEthPrefix)) continue;

            const Key* key = Find(name);
            if (key == nullptr) return fail(node, name + " is not a lighting key the port reads");
            if (!isRoot && !isEntity)
                return fail(node, name + " below an entity node, where the converter writes no lighting");
            if (isRoot && key->place != Place::Root) return fail(node, name + " is an entity's key, on the root");
            if (!isRoot && key->place == Place::Root) return fail(node, name + " is the scene's key, on an entity");
            if (property.value.kind != Tscn::Value::Kind::String)
                return fail(node, name + " is not a quoted string");
            has[static_cast<std::size_t>(key->id)] = true;

            const std::string& text = property.value.text;
            bool ok = true;
            switch (key->id) {
            case Id::Ambient: ok = Vector(text, read.ambient, why); haveAmbient = ok; break;
            case Id::Intensity: ok = Scalar(text, read.intensity, why); haveIntensity = ok; break;
            case Id::Z: ok = Scalar(text, look.z, why); break;
            case Id::Emissive: ok = Vector(text, look.emissive, why); break;
            case Id::Static: ok = Flag(text, look.isStatic, why); break;
            case Id::ApplyLight: ok = Flag(text, look.applyLight, why); break;
            case Id::Colour: {
                double v[4] = {};
                ok = Numbers(text, 4, v);
                if (ok) look.colour = glm::dvec4(v[0], v[1], v[2], v[3]);
                else why = "\"" + text + "\" is not " + CountWord(4);
                break;
            }
            case Id::Normal: ok = Path(text, resRoot, look.normal, why); break;
            case Id::Lightmap: ok = Path(text, resRoot, look.lightmap, why); break;
            case Id::LightRange:
                ok = Scalar(text, light.range, why);
                // The engine keeps a light only if its range is positive
                // (ETHEntityProperties.cpp:343), so a converter that wrote one
                // that is not has lost track of which lights exist.
                if (ok && !(light.range > 0.0)) {
                    ok = false;
                    why = "\"" + text + "\" is not a positive range";
                }
                break;
            case Id::LightOffset: ok = Vector(text, light.offset, why); break;
            case Id::LightColour: ok = Vector(text, light.colour, why); break;
            case Id::Halo: ok = Path(text, resRoot, light.halo, why); break;
            case Id::HaloOffset: ok = Vector(text, light.haloOffset, why); break;
            case Id::HaloSize: ok = Vector(text, light.haloSize, why); break;
            case Id::HaloBrightness: ok = Scalar(text, light.haloBrightness, why); break;
            case Id::Count: break;
            }
            if (!ok) return fail(node, name + ": " + why);
        }

        if (!isEntity) continue;

        const auto present = [&](Id id) { return has[static_cast<std::size_t>(id)]; };
        const bool drawsSprite = spriteSizePx.count(node.name) != 0;
        const bool ownsLight = present(Id::LightRange);

        // A light is identified by its range, which the converter always writes;
        // a halo by its bitmap. The rest of either on its own describes nothing.
        for (const Id id : {Id::LightOffset, Id::LightColour, Id::Halo, Id::HaloOffset, Id::HaloSize, Id::HaloBrightness}) {
            if (present(id) && !ownsLight)
                return fail(node, std::string(NameOf(id)) + " without eth_light_range, which is what makes a light");
        }
        for (const Id id : {Id::HaloOffset, Id::HaloSize, Id::HaloBrightness}) {
            if (present(id) && !present(Id::Halo))
                return fail(node, std::string(NameOf(id)) + " without eth_halo, which is what makes a halo");
        }
        for (const Key& key : kKeys) {
            if (key.place == Place::Drawn && present(key.id) && !drawsSprite && !ownsLight)
                return fail(node, std::string(key.name) + " on a node that neither draws a sprite nor owns a light");
        }

        if (present(Id::Lightmap)) {
            // Only a static, lit entity with a picture is baked
            // (ETHLightmapGen.cpp:42), and the converter attaches no other.
            if (!look.isStatic) return fail(node, "eth_lightmap on a node that is not static");
            if (!look.applyLight) return fail(node, "eth_lightmap on a node that does not apply light");
            if (!drawsSprite) return fail(node, "eth_lightmap on a node that draws no sprite");

            // Every shipped lightmap is half its 1x sprite on both axes (730 of
            // 730), and the lightmap is sampled at the sprite's own coordinates,
            // so any other ratio means the pair is not what the original baked.
            int width = 0;
            int height = 0;
            if (!Sprites::ImageSize(look.lightmap, width, height, why)) return fail(node, "eth_lightmap: " + why);
            const glm::dvec2 sizePx = spriteSizePx.at(node.name);
            if (2.0 * width != sizePx.x || 2.0 * height != sizePx.y) {
                return fail(node, "eth_lightmap is " + std::to_string(width) + "x" + std::to_string(height) +
                                      ", and twice that is not its sprite's " +
                                      std::to_string(static_cast<int>(sizePx.x)) + "x" +
                                      std::to_string(static_cast<int>(sizePx.y)));
            }
        }

        if (ownsLight) look.light = std::move(light);
        read.nodes.emplace(node.name, std::move(look));
    }

    // Tscn::Parse refuses a scene with no nodes, so the root is always there.
    if (root == nullptr) {
        error = "the scene has no root node";
        return false;
    }
    if (!haveAmbient) return fail(*root, "the root has no eth_ambient, and every level file declares one");
    if (!haveIntensity) return fail(*root, "the root has no eth_light_intensity, and every level file declares one");

    out = std::move(read);
    return true;
}

bool LoadRules(const std::string& path, Rules& out, std::string& error) {
    namespace Json = Supersonic::Json;
    std::ifstream file(path, std::ios::binary);
    if (!file) {
        error = path + ": cannot open";
        return false;
    }
    std::ostringstream buffer;
    buffer << file.rdbuf();
    // Named, not a temporary: the parser keeps a reference to what it reads.
    const std::string text = buffer.str();
    Json::Parser parser(text);
    Json::Value root;
    if (!parser.Parse(root)) {
        error = path + ": " + parser.Error();
        return false;
    }
    if (!root.IsObject()) {
        error = path + ": not an object";
        return false;
    }
    const auto light = [&](const char* key, glm::dvec3& into) {
        const Json::Value& value = root[key]["value"];
        bool ok = root.Has(key) && root[key].IsObject() && value.IsArray() && value.AsArray().size() == 3;
        for (std::size_t i = 0; ok && i < 3; ++i) {
            const Json::Value& channel = value.AsArray()[i];
            ok = channel.IsNumber() && std::isfinite(channel.AsNumber()) && channel.AsNumber() >= 0.0 &&
                 channel.AsNumber() <= 1.0;
            if (ok) into[static_cast<glm::length_t>(i)] = channel.AsNumber();
        }
        if (!ok) error = path + ": " + key + ".value is not three numbers from 0 to 1";
        return ok;
    };
    Rules read;
    if (!light("darkest_ambient", read.darkestAmbient) || !light("torch_lit_ambient", read.torchLitAmbient)) {
        return false;
    }
    out = read;
    return true;
}

glm::dvec3 Ambient(const Rules& rules, const glm::dvec3& fileAmbient, bool darkest, const Torch::State& torch) {
    const bool litNow = std::any_of(torch.lights.begin(), torch.lights.end(),
                                    [](const Torch::Light& light) { return light.lit; });
    if (litNow) return rules.torchLitAmbient;
    if (torch.putOut > 0 || darkest) return rules.darkestAmbient;
    return fileAmbient;
}

glm::dvec3 AmbientTerm(const glm::dvec3& ambient, const glm::dvec3& emissive) {
    return glm::min(glm::dvec3(1.0), ambient + emissive);
}

} // namespace MagicPortals::Lighting
