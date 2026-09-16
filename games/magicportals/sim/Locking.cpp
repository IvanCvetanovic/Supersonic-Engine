#include "sim/Locking.hpp"

#include "core/Json.hpp"
#include "sim/UiLayer.hpp"

#include <cmath>

namespace MagicPortals::Locking {

namespace {

namespace Json = Supersonic::Json;

bool Whole(const Json::Value& block, const char* key, int least, int most, int& out, std::string& why) {
    const Json::Value& value = block[key];
    if (!value.IsNumber() || value.AsNumber() != std::floor(value.AsNumber()) ||
        value.AsNumber() < static_cast<double>(least) || value.AsNumber() > static_cast<double>(most)) {
        why = std::string("locking.") + key + " is missing or not a whole number within " + std::to_string(least) +
              ".." + std::to_string(most);
        return false;
    }
    out = static_cast<int>(value.AsNumber());
    return true;
}

} // namespace

bool LoadRules(const std::string& path, Rules& out, std::string& error) {
    Json::Value root;
    if (!UiLayer::Read::File(path, root, error)) return false;
    const Json::Value& block = root["locking"];
    if (!block.IsObject()) {
        error = path + ": locking is not an object";
        return false;
    }
    Rules read;
    std::string why;
    if (!Whole(block, "medal_max", 1, 1000, read.medalMax, why) ||
        !Whole(block, "chapter_unlock_percent", 0, 100, read.chapterUnlockPercent, why)) {
        error = path + ": " + why;
        return false;
    }
    out = read;
    return true;
}

int LevelsIn(const Chapters::Table& chapters, int world) {
    int count = 0;
    for (const Chapters::Level& level : chapters.levels) {
        if (level.world == world) ++count;
    }
    return count;
}

int ChapterCompletion(const Rules& rules, const Scores::Store& scores, const Chapters::Table& chapters, int world) {
    const int levels = LevelsIn(chapters, world);
    if (levels <= 0 || rules.medalMax <= 0) return 0;
    int sum = 0;
    for (const Chapters::Level& level : chapters.levels) {
        if (level.world == world) sum += scores.Get(world, level.index);
    }
    // The original's float arithmetic, truncated as fTOu truncates.
    const float rate = static_cast<float>(sum) / static_cast<float>(levels * rules.medalMax);
    return static_cast<int>(rate * 100.0f);
}

bool ChapterUnlocked(const Rules& rules, const Scores::Store& scores, const Chapters::Table& chapters, int world) {
    if (world == 0) return true;
    if (world < 0) return false;
    return ChapterCompletion(rules, scores, chapters, world - 1) >= rules.chapterUnlockPercent;
}

bool LevelUnlocked(const Rules& rules, const Scores::Store& scores, const Chapters::Table& chapters, int world,
                   int level) {
    if (!ChapterUnlocked(rules, scores, chapters, world)) return false;
    if (level <= 0) return true;
    return scores.Get(world, level - 1) > Scores::kUnplayed;
}

} // namespace MagicPortals::Locking
