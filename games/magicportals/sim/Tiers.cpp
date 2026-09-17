#include "sim/Tiers.hpp"

#include "core/Json.hpp"

#include <filesystem>
#include <fstream>
#include <sstream>
#include <system_error>
#include <utility>

namespace MagicPortals::Tiers {

namespace {

namespace Json = Supersonic::Json;

// A folder is joined to a path as one name, so a separator or a step up would
// send the search somewhere the original never looks.
bool PlainFolder(const std::string& folder) {
    return !folder.empty() && folder != "." && folder != ".." && folder.find('/') == std::string::npos &&
           folder.find('\\') == std::string::npos;
}

} // namespace

bool LoadRules(const std::string& path, Rules& out, std::string& error) {
    std::ifstream file(path, std::ios::binary);
    if (!file) {
        error = path + ": cannot read";
        return false;
    }
    std::ostringstream text;
    text << file.rdbuf();

    // Named, not a temporary: the parser keeps a reference to what it reads.
    const std::string content = text.str();
    Json::Parser parser(content);
    Json::Value root;
    if (!parser.Parse(root)) {
        error = path + ": " + parser.Error();
        return false;
    }
    const Json::Value& block = root["density_tiers"];
    if (!block.IsObject()) {
        error = path + ": density_tiers is missing or not an object";
        return false;
    }
    if (!block["search"].IsArray() || block["search"].AsArray().empty()) {
        error = path + ": density_tiers.search is missing, not an array or empty";
        return false;
    }
    const Json::Value& densities = block["density"];
    if (!densities.IsObject()) {
        error = path + ": density_tiers.density is missing or not an object";
        return false;
    }

    Rules read;
    for (const Json::Value& one : block["search"].AsArray()) {
        if (!one.IsString() || !PlainFolder(one.AsString())) {
            error = path + ": density_tiers.search holds a value that is not one folder name";
            return false;
        }
        const std::string folder = one.AsString();
        for (const Tier& earlier : read.search) {
            if (earlier.folder == folder) {
                error = path + ": density_tiers.search names '" + folder + "' twice";
                return false;
            }
        }
        // No default: a tier drawn at the wrong density is drawn at the wrong size,
        // and ETHSpriteDensityManager's own defaults (fullhd 4) are not the game's.
        if (!densities.Has(folder) || !densities[folder].IsNumber() || !(densities[folder].AsNumber() > 0.0)) {
            error = path + ": density_tiers.density." + folder + " is missing, not a number or not above zero";
            return false;
        }
        read.search.push_back({folder, densities[folder].AsFloat()});
    }
    // A density no search reaches is a number nothing reads, which is how a folder
    // meant to be searched goes missing from the list unnoticed. Notes (_source and
    // the like) are not densities.
    for (const auto& entry : densities.AsObject()) {
        if (entry.first.starts_with('_')) continue;
        bool searched = false;
        for (const Tier& tier : read.search) searched = searched || tier.folder == entry.first;
        if (!searched) {
            error = path + ": density_tiers.density names '" + entry.first + "', which search does not try";
            return false;
        }
    }
    out = std::move(read);
    return true;
}

Resolved Resolve(const Rules& rules, const std::string& named) {
    // AssembleResourceName: the directory with its trailing separator, then the
    // tier's folder, then the file's own name.
    const std::size_t slash = named.find_last_of("/\\");
    const std::string parent = slash == std::string::npos ? std::string() : named.substr(0, slash + 1);
    const std::string name = slash == std::string::npos ? named : named.substr(slash + 1);
    for (const Tier& tier : rules.search) {
        const std::string candidate = parent + tier.folder + "/" + name;
        std::error_code ec;
        if (std::filesystem::is_regular_file(candidate, ec)) {
            return {candidate, tier.folder, tier.density};
        }
    }
    return {named, std::string(), 1.0f};
}

glm::ivec2 Units(const glm::ivec2& texels, float density) {
    // Float, then truncated toward zero, as SetSpriteDensityValue and GetBitmapSize do.
    return glm::ivec2(static_cast<int>(static_cast<float>(texels.x) / density),
                      static_cast<int>(static_cast<float>(texels.y) / density));
}

bool FrameCut(const glm::ivec2& texels, float density, int columns, int rows, Cut& out, std::string& error) {
    if (texels.x <= 0 || texels.y <= 0 || !(density > 0.0f) || columns <= 0 || rows <= 0) {
        error = "a cut needs texels, a density, columns and rows above zero";
        return false;
    }
    Cut cut;
    cut.units = Units(texels, density);
    cut.frameUnits = glm::ivec2(cut.units.x / columns, cut.units.y / rows);
    if (cut.frameUnits.x <= 0 || cut.frameUnits.y <= 0) {
        error = "a " + std::to_string(texels.x) + " x " + std::to_string(texels.y) + " image at density " +
                std::to_string(density) + " is " + std::to_string(cut.units.x) + " x " + std::to_string(cut.units.y) +
                " units, too small to cut into " + std::to_string(columns) + " x " + std::to_string(rows);
        return false;
    }
    cut.frameTexels = glm::dvec2(cut.frameUnits) * static_cast<double>(density);
    // rectSize / bitmapSize, with bitmapSize the float texels / density.
    const glm::dvec2 bitmapUnits = glm::dvec2(static_cast<double>(static_cast<float>(texels.x) / density),
                                              static_cast<double>(static_cast<float>(texels.y) / density));
    cut.uvScale = glm::dvec2(cut.frameUnits) / bitmapUnits;
    out = cut;
    return true;
}

glm::dvec2 UvOffset(const Cut& cut, int column, int row) {
    return glm::dvec2(static_cast<double>(column), static_cast<double>(row)) * cut.uvScale;
}

} // namespace MagicPortals::Tiers
