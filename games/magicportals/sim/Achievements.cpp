#include "sim/Achievements.hpp"

#include "core/Json.hpp"

#include <algorithm>
#include <cmath>
#include <fstream>
#include <sstream>
#include <utility>

namespace MagicPortals::Achievements {

namespace {

namespace Json = Supersonic::Json;

bool Integer(const Json::Value& object, const char* key, int& out) {
    const Json::Value& value = object[key];
    if (!value.IsNumber() || value.AsNumber() != std::floor(value.AsNumber()) || std::fabs(value.AsNumber()) > 1.0e6) {
        return false;
    }
    out = static_cast<int>(value.AsNumber());
    return true;
}

bool String(const Json::Value& object, const char* key, std::string& out) {
    const Json::Value& value = object[key];
    if (!value.IsString()) return false;
    out = value.AsString();
    return true;
}

bool Bool(const Json::Value& object, const char* key, bool& out) {
    const Json::Value& value = object[key];
    if (!value.IsBool()) return false;
    out = value.AsBool();
    return true;
}

} // namespace

bool Load(const std::string& path, Content& out, std::string& error) {
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
    if (!root.IsObject() || !root["format"].IsNumber() || root["format"].AsNumber() != 1.0 ||
        !root["achievements"].IsArray() || !root["secret"].IsObject() || !root["unlock"].IsObject() ||
        !root["unlock"]["from_completion"].IsArray() || !root["unlock"]["from_level_medal"].IsArray()) {
        error = path + ": not tools/asbc/achievements.py's format 1 (achievements, secret, unlock)";
        return false;
    }

    Content read;
    for (const Json::Value& item : root["achievements"].AsArray()) {
        Entry entry;
        if (!item.IsObject() || !Integer(item, "id", entry.id) || !Integer(item, "world", entry.world) ||
            !Integer(item, "level", entry.level) || !Integer(item, "points", entry.points) ||
            !String(item, "title", entry.title) || !String(item, "description", entry.description) ||
            !String(item, "icon", entry.icon) || !Bool(item, "is_new", entry.isNew) ||
            !Bool(item, "is_secret", entry.isSecret) || entry.world < 0 || entry.level < -1) {
            error = path + ": achievement " + std::to_string(read.entries.size()) +
                    " lacks an id, a world, a level of -1 or more, points, a title, a description, an icon, "
                    "is_new or is_secret";
            return false;
        }
        read.entries.push_back(std::move(entry));
    }

    const Json::Value& secret = root["secret"];
    if (!String(secret, "title", read.secret.title) ||
        !String(secret, "description_prefix", read.secret.descriptionPrefix) ||
        !String(secret, "description_separator", read.secret.descriptionSeparator) ||
        !String(secret, "icon", read.secret.icon)) {
        error = path + ": secret lacks a title, description_prefix, description_separator or icon";
        return false;
    }

    for (const Json::Value& item : root["unlock"]["from_completion"].AsArray()) {
        CompletionRule rule;
        if (!item.IsObject() || !Integer(item, "world", rule.world) || !Integer(item, "min_percent", rule.minPercent) ||
            !Integer(item, "id", rule.id)) {
            error = path + ": a from_completion rule lacks a world, a min_percent or an id";
            return false;
        }
        read.fromCompletion.push_back(rule);
    }
    for (const Json::Value& item : root["unlock"]["from_level_medal"].AsArray()) {
        MedalRule rule;
        if (!item.IsObject() || !Integer(item, "world", rule.world) || !Integer(item, "level", rule.level) ||
            !Integer(item, "min_medal", rule.minMedal) || !Integer(item, "id", rule.id)) {
            error = path + ": a from_level_medal rule lacks a world, a level, a min_medal or an id";
            return false;
        }
        read.fromLevelMedal.push_back(rule);
    }
    out = std::move(read);
    return true;
}

std::vector<int> Unlocked(const Content& content, const Locking::Rules& locking, const Scores::Store& scores,
                          const Chapters::Table& chapters) {
    std::vector<int> ids;
    for (const CompletionRule& rule : content.fromCompletion) {
        if (Locking::ChapterCompletion(locking, scores, chapters, rule.world) >= rule.minPercent) ids.push_back(rule.id);
    }
    for (const MedalRule& rule : content.fromLevelMedal) {
        if (scores.Get(rule.world, rule.level) >= rule.minMedal) ids.push_back(rule.id);
    }
    std::sort(ids.begin(), ids.end());
    ids.erase(std::unique(ids.begin(), ids.end()), ids.end());
    return ids;
}

bool IsUnlocked(const std::vector<int>& unlocked, int id) {
    return std::binary_search(unlocked.begin(), unlocked.end(), id);
}

int Points(const Content& content, const std::vector<int>& unlocked) {
    int points = 0;
    for (const Entry& entry : content.entries) {
        if (IsUnlocked(unlocked, entry.id)) points += entry.points;
    }
    return points;
}

std::string TitleOf(const Content& content, const Entry& entry, bool unlocked) {
    return !entry.isSecret || unlocked ? entry.title : content.secret.title;
}

std::string DescriptionOf(const Content& content, const Entry& entry, bool unlocked) {
    if (!entry.isSecret || unlocked) return entry.description;
    return content.secret.descriptionPrefix + std::to_string(entry.world + 1) + content.secret.descriptionSeparator +
           std::to_string(entry.level + 1);
}

std::string IconOf(const Content& content, const Entry& entry, bool unlocked) {
    return !entry.isSecret || unlocked ? entry.icon : content.secret.icon;
}

} // namespace MagicPortals::Achievements
