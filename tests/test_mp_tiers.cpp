// The texture tiers and the one cut rule: tiers.json and sim/Tiers.
//
// What is pinned, and from where:
//
//   the rules    tiers.json's search, fullhd before hd, each at density 2 - the
//                original's app.enml at a 720 px screen (the owner's ruling R12) -
//                and every refusal of a malformed file.
//   the choice   ChooseSpriteVersion: the first <dir>/<tier>/<name> that exists, else
//                <name> at density 1. Only the name is tried, so a tier file with no
//                1x name is reached by no 1x name, and by its own name it is.
//   the size     int(texels / density), the truncation after the divide
//                (GLES2Sprite.cpp:435, :391).
//   the cut      int(texels / density) / columns in whole units, and the uv the
//                shader samples a frame at: stride over texels / density, column
//                times that (Sprite.cpp:119-120; default.vs:38-39).
//   the original over the APK's own tier folders: which file each 1x image draws,
//                the four skies that are wider in fullhd, the minion, ghost and
//                player sheets cut in hd, and the four orphans never chosen.
//
// The rules, the choice and the arithmetic run anywhere, on files this suite writes.
// The original's part reads its extracted assets from outside this repository and
// is skipped without them, never with 77.

#include "TestHarness.hpp"

#include "sim/Sprites.hpp"
#include "sim/Tiers.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <set>
#include <string>
#include <system_error>
#include <vector>

using namespace MagicPortals;

namespace {

const std::string kOriginal = MAGICPORTALS_ORIGINAL_DIR;
const std::string kPortData = MAGICPORTALS_PORT_DATA_DIR;

std::filesystem::path Scratch() {
    const std::filesystem::path dir = std::filesystem::temp_directory_path() / "supersonic-test-mp-tiers";
    std::error_code ec;
    std::filesystem::remove_all(dir, ec);
    std::filesystem::create_directories(dir, ec);
    return dir;
}

void Touch(const std::filesystem::path& path) {
    std::error_code ec;
    std::filesystem::create_directories(path.parent_path(), ec);
    std::ofstream file(path, std::ios::binary | std::ios::trunc);
    file << "x";
}

bool Near(double a, double b, double eps = 1e-9) {
    return std::fabs(a - b) <= eps;
}

std::string Str(const glm::ivec2& v) {
    return std::to_string(v.x) + " x " + std::to_string(v.y);
}

std::string Str(const glm::dvec2& v) {
    return std::to_string(v.x) + " x " + std::to_string(v.y);
}

Tiers::Rules TheRules() {
    Tiers::Rules rules;
    std::string error;
    CHECK_MSG(Tiers::LoadRules(kPortData + "/tiers.json", rules, error), error);
    return rules;
}

// ---- the rules ----------------------------------------------------------------

void TheRulesAreTiersJsons() {
    const Tiers::Rules rules = TheRules();
    CHECK_EQ(rules.search.size(), std::size_t{2});
    if (rules.search.size() != 2) return;
    CHECK_MSG(rules.search[0].folder == "fullhd", "fullhd is tried first: " + rules.search[0].folder);
    CHECK_MSG(rules.search[1].folder == "hd", "hd is tried second: " + rules.search[1].folder);
    CHECK(rules.search[0].density == 2.0f);
    CHECK(rules.search[1].density == 2.0f);
}

void AMalformedTiersFileIsRefused() {
    const std::filesystem::path dir = Scratch();
    const auto refused = [&](const std::string& json, const std::string& why) {
        const std::filesystem::path path = dir / "tiers.json";
        {
            std::ofstream file(path, std::ios::binary | std::ios::trunc);
            file << json;
        }
        Tiers::Rules rules;
        rules.search.push_back({"kept", 3.0f});
        std::string error;
        const bool loaded = Tiers::LoadRules(path.string(), rules, error);
        CHECK_MSG(!loaded, why + ": loaded");
        CHECK_MSG(error.find(why) != std::string::npos, "expected '" + why + "', got '" + error + "'");
        CHECK_MSG(rules.search.size() == 1 && rules.search[0].folder == "kept", why + ": the rules were overwritten");
    };
    refused(R"({})", "density_tiers is missing");
    refused(R"({"density_tiers": {"search": [], "density": {}}})", "search is missing, not an array or empty");
    refused(R"({"density_tiers": {"search": ["hd"]}})", "density is missing or not an object");
    refused(R"({"density_tiers": {"search": ["hd", "hd"], "density": {"hd": 2}}})", "names 'hd' twice");
    refused(R"({"density_tiers": {"search": ["fullhd", "hd"], "density": {"hd": 2}}})", "density.fullhd is missing");
    refused(R"({"density_tiers": {"search": ["hd"], "density": {"hd": 0}}})", "density.hd is missing");
    refused(R"({"density_tiers": {"search": ["hd"], "density": {"hd": -2}}})", "density.hd is missing");
    refused(R"({"density_tiers": {"search": ["hd"], "density": {"hd": "2"}}})", "density.hd is missing");
    refused(R"({"density_tiers": {"search": ["hd/x"], "density": {"hd/x": 2}}})", "not one folder name");
    refused(R"({"density_tiers": {"search": [".."], "density": {"..": 2}}})", "not one folder name");
    refused(R"({"density_tiers": {"search": [""], "density": {}}})", "not one folder name");
    refused(R"({"density_tiers": {"search": [2], "density": {}}})", "not one folder name");
    refused(R"({"density_tiers": {"search": ["hd"], "density": {"hd": 2, "fullhd": 2}}})",
            "names 'fullhd', which search does not try");

    // A note beside the densities is not a density.
    const std::filesystem::path path = dir / "noted.json";
    {
        std::ofstream file(path, std::ios::binary | std::ios::trunc);
        file << R"({"density_tiers": {"search": ["hd"], "density": {"hd": 2, "_source": "x"}}})";
    }
    Tiers::Rules rules;
    std::string error;
    CHECK_MSG(Tiers::LoadRules(path.string(), rules, error), error);
    CHECK(rules.search.size() == 1 && rules.search[0].folder == "hd" && rules.search[0].density == 2.0f);
    CHECK_MSG(!Tiers::LoadRules((dir / "absent.json").string(), rules, error), "a missing file loads");
}

// ---- the choice -----------------------------------------------------------------

void FullhdIsTriedBeforeHd() {
    const Tiers::Rules rules = TheRules();
    const std::filesystem::path dir = Scratch() / "entities";
    Touch(dir / "both.png");
    Touch(dir / "hd" / "both.png");
    Touch(dir / "fullhd" / "both.png");
    Touch(dir / "half.png");
    Touch(dir / "hd" / "half.png");
    Touch(dir / "wide.png");
    Touch(dir / "fullhd" / "wide.png");
    Touch(dir / "plain.png");

    const std::string root = dir.generic_string() + "/";
    const Tiers::Resolved both = Tiers::Resolve(rules, root + "both.png");
    CHECK_MSG(both.tier == "fullhd" && both.density == 2.0f && both.path == root + "fullhd/both.png",
              "both tiers: fullhd wins, got " + both.path);
    const Tiers::Resolved half = Tiers::Resolve(rules, root + "half.png");
    CHECK_MSG(half.tier == "hd" && half.density == 2.0f && half.path == root + "hd/half.png", half.path);
    const Tiers::Resolved wide = Tiers::Resolve(rules, root + "wide.png");
    CHECK_MSG(wide.tier == "fullhd" && wide.path == root + "fullhd/wide.png", wide.path);

    // No tier file: the name itself, at density 1.
    const Tiers::Resolved plain = Tiers::Resolve(rules, root + "plain.png");
    CHECK_MSG(plain.tier.empty() && plain.density == 1.0f && plain.path == root + "plain.png", plain.path);

    // A folder of that name is not a file (FileExists).
    std::error_code ec;
    std::filesystem::create_directories(dir / "hd" / "folder.png", ec);
    const Tiers::Resolved folder = Tiers::Resolve(rules, root + "folder.png");
    CHECK_MSG(folder.tier.empty() && folder.density == 1.0f, "a directory is not a tier file: " + folder.path);

    // AssembleResourceName splits at the last separator, either kind.
    const std::string back = dir.string() + "\\half.png";
    const Tiers::Resolved backslash = Tiers::Resolve(rules, back);
    CHECK_MSG(backslash.tier == "hd" && backslash.path == dir.string() + "\\hd/half.png", backslash.path);
}

// Only the name is tried: a tier file whose 1x name nobody gives is reached by no
// 1x name - the APK's particles/hd/tesla_shock_.png beside particles/tesla_shock.png
// - and by its own name it is found, whether or not a 1x copy exists.
void OnlyTheNameIsTried() {
    const Tiers::Rules rules = TheRules();
    const std::filesystem::path dir = Scratch() / "particles";
    Touch(dir / "shock.png");
    Touch(dir / "hd" / "shock_.png");
    const std::string root = dir.generic_string() + "/";

    const Tiers::Resolved named = Tiers::Resolve(rules, root + "shock.png");
    CHECK_MSG(named.tier.empty() && named.density == 1.0f && named.path == root + "shock.png",
              "shock.png draws itself, never hd/shock_.png: " + named.path);
    const Tiers::Resolved orphan = Tiers::Resolve(rules, root + "shock_.png");
    CHECK_MSG(orphan.tier == "hd" && orphan.density == 2.0f && orphan.path == root + "hd/shock_.png",
              "named, the orphan is found in its tier with no 1x copy: " + orphan.path);
}

// ---- the size and the cut -------------------------------------------------------

void AnImagesUnitsAreTruncatedAfterTheDivide() {
    CHECK_MSG(Tiers::Units({1024, 512}, 2.0f) == glm::ivec2(512, 256), Str(Tiers::Units({1024, 512}, 2.0f)));
    CHECK_MSG(Tiers::Units({455, 256}, 1.0f) == glm::ivec2(455, 256), "a 1x file is its texels");
    // 973 / 2 = 486.5, cast to 486: the ghost's hd sheet is as wide as its 1x file.
    CHECK_MSG(Tiers::Units({973, 256}, 2.0f) == glm::ivec2(486, 128), Str(Tiers::Units({973, 256}, 2.0f)));
    CHECK_MSG(Tiers::Units({310, 434}, 2.0f) == glm::ivec2(155, 217), Str(Tiers::Units({310, 434}, 2.0f)));
    CHECK_MSG(Tiers::Units({3, 5}, 2.0f) == glm::ivec2(1, 2), Str(Tiers::Units({3, 5}, 2.0f)));
}

void ASheetIsCutInWholeUnits() {
    std::string error;
    Tiers::Cut cut;

    // minion.png: 155 x 217 at 1x, 310 x 434 in hd, cut 4 x 4. 155 / 4 = 38 u
    // (not 38.75) and 217 / 4 = 54 u (not 54.25), which in hd is 76 x 108 texels.
    CHECK_MSG(Tiers::FrameCut({310, 434}, 2.0f, 4, 4, cut, error), error);
    CHECK_MSG(cut.units == glm::ivec2(155, 217), Str(cut.units));
    CHECK_MSG(cut.frameUnits == glm::ivec2(38, 54), Str(cut.frameUnits));
    CHECK_MSG(cut.frameTexels == glm::dvec2(76.0, 108.0), Str(cut.frameTexels));
    CHECK_MSG(Near(cut.uvScale.x, 38.0 / 155.0) && Near(cut.uvScale.y, 54.0 / 217.0), Str(cut.uvScale));
    // Column 3 starts at 3 x 38 = 114 u = 228 texels; an even split of 310 puts it
    // at 232.5, 4.5 texels off (00_order E1).
    const glm::dvec2 third = Tiers::UvOffset(cut, 3, 2);
    CHECK_MSG(Near(third.x * 310.0, 228.0) && Near(third.y * 434.0, 216.0), Str(third * glm::dvec2(310.0, 434.0)));
    CHECK(Near(3.0 * 310.0 / 4.0 - third.x * 310.0, 4.5));
    // The 1x file cuts to the same units, one texel each.
    CHECK_MSG(Tiers::FrameCut({155, 217}, 1.0f, 4, 4, cut, error), error);
    CHECK_MSG(cut.frameUnits == glm::ivec2(38, 54) && cut.frameTexels == glm::dvec2(38.0, 54.0), Str(cut.frameTexels));

    // ghost.png: 973 x 256 in hd, cut 4 x 1. int(973 / 2) = 486, then 486 / 4 = 121 u
    // = 242 texels - not 973 / 4 / 2 = 121.625. The uv is 121 over the float 486.5.
    CHECK_MSG(Tiers::FrameCut({973, 256}, 2.0f, 4, 1, cut, error), error);
    CHECK_MSG(cut.units == glm::ivec2(486, 128), Str(cut.units));
    CHECK_MSG(cut.frameUnits == glm::ivec2(121, 128), Str(cut.frameUnits));
    CHECK_MSG(cut.frameTexels == glm::dvec2(242.0, 256.0), Str(cut.frameTexels));
    CHECK_MSG(Near(cut.uvScale.x, 121.0 / 486.5) && Near(cut.uvScale.y, 1.0), Str(cut.uvScale));
    CHECK(Near(Tiers::UvOffset(cut, 3, 0).x * 973.0, 726.0));

    // magic_portals_hd.png in hd: 320 x 448 cut 4 x 4 is 40 x 56 u, evenly.
    CHECK_MSG(Tiers::FrameCut({320, 448}, 2.0f, 4, 4, cut, error), error);
    CHECK_MSG(cut.frameUnits == glm::ivec2(40, 56) && cut.frameTexels == glm::dvec2(80.0, 112.0), Str(cut.frameUnits));
    CHECK(Near(cut.uvScale.x, 0.25) && Near(cut.uvScale.y, 0.25));
    CHECK(Near(Tiers::UvOffset(cut, 1, 3).x, 0.25) && Near(Tiers::UvOffset(cut, 1, 3).y, 0.75));

    // The fullhd sky, uncut: 1024 x 512 is 512 x 256 u, the whole texture.
    CHECK_MSG(Tiers::FrameCut({1024, 512}, 2.0f, 1, 1, cut, error), error);
    CHECK_MSG(cut.frameUnits == glm::ivec2(512, 256), Str(cut.frameUnits));
    CHECK(Near(cut.uvScale.x, 1.0) && Near(cut.uvScale.y, 1.0));
    CHECK(Tiers::UvOffset(cut, 0, 0) == glm::dvec2(0.0));
}

void ACutThatIsNoFrameIsRefused() {
    Tiers::Cut cut;
    cut.frameUnits = {7, 7};
    std::string error;
    CHECK_MSG(!Tiers::FrameCut({64, 64}, 2.0f, 0, 1, cut, error), "0 columns");
    CHECK_MSG(!Tiers::FrameCut({64, 64}, 2.0f, 1, -1, cut, error), "-1 rows");
    CHECK_MSG(!Tiers::FrameCut({64, 64}, 0.0f, 1, 1, cut, error), "density 0");
    CHECK_MSG(!Tiers::FrameCut({0, 64}, 2.0f, 1, 1, cut, error), "0 texels");
    // 3 texels at density 2 is 1 u, which 2 columns cut to frames of 0.
    error.clear();
    CHECK_MSG(!Tiers::FrameCut({3, 64}, 2.0f, 2, 1, cut, error), "a frame of 0 units");
    CHECK_MSG(error.find("too small to cut into 2 x 1") != std::string::npos, error);
    CHECK_MSG(cut.frameUnits == glm::ivec2(7, 7), "a refused cut leaves its output alone");
}

// ---- the original ---------------------------------------------------------------

struct Sized {
    Tiers::Resolved drawn;
    glm::ivec2 texels{0};
};

Sized Drawn(const Tiers::Rules& rules, const std::string& relative) {
    Sized out;
    out.drawn = Tiers::Resolve(rules, kOriginal + "/" + relative);
    std::string error;
    const bool sized = Sprites::ImageSize(out.drawn.path, out.texels.x, out.texels.y, error);
    CHECK_MSG(sized, relative + ": " + error);
    return out;
}

void TheOriginalsSheetsAreCutInTheirTier() {
    const Tiers::Rules rules = TheRules();
    std::string error;
    Tiers::Cut cut;

    const Sized sky = Drawn(rules, "entities/sky.png");
    CHECK_MSG(sky.drawn.tier == "fullhd" && sky.drawn.density == 2.0f, "sky.png draws fullhd: " + sky.drawn.path);
    CHECK_MSG(sky.texels == glm::ivec2(1024, 512), Str(sky.texels));
    CHECK_MSG(Tiers::Units(sky.texels, sky.drawn.density) == glm::ivec2(512, 256),
              "the fullhd sky is 512 u wide, not the 1x file's 455");

    const Sized minion = Drawn(rules, "entities/minion.png");
    CHECK_MSG(minion.drawn.tier == "hd" && minion.texels == glm::ivec2(310, 434), minion.drawn.path);
    CHECK_MSG(Tiers::FrameCut(minion.texels, minion.drawn.density, 4, 4, cut, error), error);
    CHECK_MSG(cut.frameUnits == glm::ivec2(38, 54) && cut.frameTexels == glm::dvec2(76.0, 108.0), Str(cut.frameTexels));

    const Sized ghost = Drawn(rules, "entities/ghost.png");
    CHECK_MSG(ghost.drawn.tier == "hd" && ghost.texels == glm::ivec2(973, 256), ghost.drawn.path);
    CHECK_MSG(Tiers::FrameCut(ghost.texels, ghost.drawn.density, 4, 1, cut, error), error);
    CHECK_MSG(cut.frameUnits == glm::ivec2(121, 128) && cut.frameTexels == glm::dvec2(242.0, 256.0),
              Str(cut.frameTexels));

    const Sized mage = Drawn(rules, "entities/magic_portals_hd.png");
    CHECK_MSG(mage.drawn.tier == "hd" && mage.texels == glm::ivec2(320, 448), mage.drawn.path);
    CHECK_MSG(Tiers::FrameCut(mage.texels, mage.drawn.density, 4, 4, cut, error), error);
    CHECK_MSG(cut.frameUnits == glm::ivec2(40, 56), Str(cut.frameUnits));

    // The shot's sheet and the portal halo have no tier file: 1x, density 1.
    const Sized shot = Drawn(rules, "entities/projectile.png");
    CHECK_MSG(shot.drawn.tier.empty() && shot.drawn.density == 1.0f && shot.texels == glm::ivec2(384, 64),
              shot.drawn.path);
    const Sized halo = Drawn(rules, "entities/portal_halo.png");
    CHECK_MSG(halo.drawn.tier.empty() && halo.drawn.density == 1.0f, halo.drawn.path);

    const Sized crystal = Drawn(rules, "particles/crystal_particle.png");
    CHECK_MSG(crystal.drawn.tier == "hd" && crystal.texels == glm::ivec2(64, 64), crystal.drawn.path);
}

// Every image in the original's four tiered folders, resolved by its own 1x name: how
// many draw a tier, which draw at a size other than their 1x file's, and that none
// reaches a tier file of another name. Then the orphans themselves, by their names.
void EveryOriginalImageDrawsTheTierItsNameFinds() {
    const Tiers::Rules rules = TheRules();
    struct Folder {
        const char* dir;
        int fullhd;
        int hd;
        int oneX;
    };
    // Counted from the APK (step 61): no folder but entities/ has a fullhd/.
    const Folder folders[] = {
        {"entities", 7, 53, 50},
        {"particles", 0, 2, 41},
        {"sprites", 0, 67, 13},
        {"ETHFramework/sprites", 0, 1, 15},
    };
    std::set<std::string> reached;
    std::vector<std::string> resized;
    int otherName = 0;
    for (const Folder& folder : folders) {
        int fullhd = 0;
        int hd = 0;
        int oneX = 0;
        std::error_code ec;
        const std::filesystem::path dir = std::filesystem::path(kOriginal) / folder.dir;
        for (std::filesystem::directory_iterator it(dir, ec), end; !ec && it != end; it.increment(ec)) {
            if (!it->is_regular_file(ec)) continue;
            std::string extension = it->path().extension().string();
            for (char& c : extension) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
            if (extension != ".png" && extension != ".bmp" && extension != ".jpg" && extension != ".jpeg") continue;
            const std::string name = it->path().filename().string();
            const std::string named = std::string(folder.dir) + "/" + name;
            const Sized drawn = Drawn(rules, named);
            if (drawn.drawn.tier.empty()) {
                ++oneX;
                continue;
            }
            (drawn.drawn.tier == "fullhd" ? fullhd : hd) += 1;
            if (std::filesystem::path(drawn.drawn.path).filename().string() != name) ++otherName;
            reached.insert(std::filesystem::path(drawn.drawn.path).lexically_normal().generic_string());
            int w = 0;
            int h = 0;
            std::string error;
            CHECK_MSG(Sprites::ImageSize(it->path().string(), w, h, error), named + ": " + error);
            if (Tiers::Units(drawn.texels, drawn.drawn.density) != glm::ivec2(w, h)) {
                resized.push_back(name + " " + std::to_string(w) + " x " + std::to_string(h) + " -> " +
                                  Str(Tiers::Units(drawn.texels, drawn.drawn.density)));
            }
        }
        std::printf("  %s: %d fullhd, %d hd, %d 1x\n", folder.dir, fullhd, hd, oneX);
        CHECK_EQ(fullhd, folder.fullhd);
        CHECK_EQ(hd, folder.hd);
        CHECK_EQ(oneX, folder.oneX);
    }
    CHECK_EQ(otherName, 0);

    // Drawn at another size than the 1x file: the four skies, wider in fullhd, and one
    // menu button whose hd file is not twice its 1x one. ghost.png is not among them:
    // int(973 / 2) is its 1x 486.
    std::vector<std::string> expected = {
        "icy_sky.png 455 x 256 -> 512 x 256",
        "red_sky.png 455 x 256 -> 512 x 256",
        "sky.png 455 x 256 -> 512 x 256",
        "sky_purple.png 455 x 256 -> 512 x 256",
        "download_full_game_popup_button.png 130 x 34 -> 128 x 32",
    };
    std::sort(expected.begin(), expected.end());
    std::sort(resized.begin(), resized.end());
    std::string list;
    for (const std::string& one : resized) list += "\n    " + one;
    std::printf("  drawn at another size than the 1x file:%s\n", list.c_str());
    CHECK_MSG(resized == expected, "the resized images:" + list);

    // The orphans: tier files with no 1x name. No 1x name reached them; by their own
    // names the loader would still find them.
    const char* const orphans[] = {
        "particles/hd/tesla_shock_.png",
        "particles/hd/exclamation_mark_.png",
        "sprites/hd/game_main_title_.png",
        "sprites/hd/world_icon3_.png",
    };
    for (const char* orphan : orphans) {
        const std::filesystem::path path = std::filesystem::path(kOriginal) / orphan;
        std::error_code ec;
        CHECK_MSG(std::filesystem::is_regular_file(path, ec), std::string(orphan) + " is in the APK");
        CHECK_MSG(reached.count(path.lexically_normal().generic_string()) == 0,
                  std::string(orphan) + " is chosen for a 1x name");
        const std::filesystem::path oneX = path.parent_path().parent_path() / path.filename();
        CHECK_MSG(!std::filesystem::exists(oneX, ec), std::string(orphan) + " has a 1x name");
        const Tiers::Resolved byName = Tiers::Resolve(rules, oneX.generic_string());
        CHECK_MSG(byName.tier == "hd" && byName.density == 2.0f, std::string(orphan) + " by its own name: " + byName.path);
    }
    const Tiers::Resolved shock = Tiers::Resolve(rules, kOriginal + "/particles/tesla_shock.png");
    CHECK_MSG(shock.tier.empty() && shock.density == 1.0f && shock.path == kOriginal + "/particles/tesla_shock.png",
              "tesla_shock.png draws its 1x file: " + shock.path);
}

} // namespace

int main() {
    TheRulesAreTiersJsons();
    AMalformedTiersFileIsRefused();
    FullhdIsTriedBeforeHd();
    OnlyTheNameIsTried();
    AnImagesUnitsAreTruncatedAfterTheDivide();
    ASheetIsCutInWholeUnits();
    ACutThatIsNoFrameIsRefused();

    std::error_code ec;
    if (!std::filesystem::is_directory(kOriginal + "/entities", ec)) {
        std::printf("test_mp_tiers: the original's tier folders SKIPPED - needs its extracted assets at %s.\n",
                    kOriginal.c_str());
        return ::test::summary("test_mp_tiers", 60);
    }
    TheOriginalsSheetsAreCutInTheirTier();
    EveryOriginalImageDrawsTheTierItsNameFinds();
    return ::test::summary("test_mp_tiers", 300);
}
