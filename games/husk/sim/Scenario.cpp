#include "Scenario.hpp"

#include "Mission.hpp"

#include <charconv>
#include <fstream>
#include <map>
#include <sstream>
#include <stdexcept>

namespace husk {

namespace {

[[noreturn]] void bad(const std::string& what) {
    throw std::runtime_error("scenario: " + what);
}

std::map<std::string, std::string> keyValues(const std::vector<std::string>& words, size_t from) {
    std::map<std::string, std::string> out;
    for (size_t i = from; i < words.size(); ++i) {
        const size_t eq = words[i].find('=');
        if (eq != std::string::npos) out[words[i].substr(0, eq)] = words[i].substr(eq + 1);
    }
    return out;
}

const std::string& at(const std::map<std::string, std::string>& m, const std::string& key) {
    auto it = m.find(key);
    if (it == m.end()) bad("missing " + key + "=");
    return it->second;
}

// Rust's str::parse::<f32>: correctly rounded, locale-free - from_chars.
float toF32(const std::string& text) {
    std::string_view s = text;
    if (!s.empty() && s.front() == '+') s.remove_prefix(1);
    float out = 0.0f;
    const auto r = std::from_chars(s.data(), s.data() + s.size(), out);
    if (r.ec != std::errc() || r.ptr != s.data() + s.size()) bad("bad float '" + text + "'");
    return out;
}

uint64_t toU64(const std::string& text) {
    uint64_t out = 0;
    const auto r = std::from_chars(text.data(), text.data() + text.size(), out);
    if (r.ec != std::errc() || r.ptr != text.data() + text.size()) bad("bad integer '" + text + "'");
    return out;
}

uint32_t toU32(const std::string& text) {
    return static_cast<uint32_t>(toU64(text));
}

std::string catalogName(std::string v) {
    for (char& ch : v) {
        if (ch == '_') ch = ' ';
    }
    return v;
}

std::vector<uint32_t> idList(const std::string& v) {
    std::vector<uint32_t> out;
    std::stringstream ss(v);
    std::string part;
    while (std::getline(ss, part, ',')) {
        const size_t dash = part.find('-');
        if (dash != std::string::npos) {
            const uint32_t lo = toU32(part.substr(0, dash));
            const uint32_t hi = toU32(part.substr(dash + 1));
            for (uint32_t i = lo; i <= hi; ++i) out.push_back(i);
        } else {
            out.push_back(toU32(part));
        }
    }
    return out;
}

bool flag(const std::map<std::string, std::string>& m, const std::string& key) {
    auto it = m.find(key);
    return it != m.end() && it->second == "1";
}

bool allDigits(const std::string& s) {
    if (s.empty()) return false;
    for (char ch : s) {
        if (ch < '0' || ch > '9') return false;
    }
    return true;
}

} // namespace

std::filesystem::path goldenDir() {
    return std::filesystem::path(HUSK_GOLDEN_DIR);
}

Scenario parseScenario(const std::filesystem::path& path) {
    std::ifstream in(path);
    if (!in) bad("failed to read " + path.generic_string());
    Scenario s;
    std::string line;
    while (std::getline(in, line)) {
        line = line.substr(0, line.find('#'));
        std::stringstream ss(line);
        std::vector<std::string> words;
        for (std::string w; ss >> w;) words.push_back(w);
        if (words.empty()) continue;
        if (words[0] == "seed") {
            s.seed = toU64(words.at(1));
        } else if (words[0] == "ticks") {
            s.ticks = toU64(words.at(1));
        } else if (words[0] == "difficulty") {
            const std::string& d = words.at(1);
            if (d == "story") {
                s.difficulty = Difficulty::Story;
            } else if (d == "normal") {
                s.difficulty = Difficulty::Normal;
            } else if (d == "hard") {
                s.difficulty = Difficulty::Hard;
            } else {
                bad("unknown difficulty " + d);
            }
        } else if (allDigits(words[0])) {
            s.script.emplace_back(toU64(words[0]), std::vector<std::string>(words.begin() + 1, words.end()));
        } else {
            s.setup.push_back(std::move(words));
        }
    }
    return s;
}

OrderMsg scenarioOrder(const World& w, const std::vector<std::string>& words) {
    if (words.empty()) bad("empty order");
    const auto m = keyValues(words, 1);
    const std::string& kind = words[0];
    const Catalogs& c = w.cat();
    auto xy = [&] { return Vec2(toF32(at(m, "x")), toF32(at(m, "y"))); };
    if (kind == "point") return OrderMsg::point(idList(at(m, "units")), flag(m, "attack"), xy(), flag(m, "queued"));
    if (kind == "attack") return OrderMsg::attackUnit(idList(at(m, "units")), toU32(at(m, "target")), flag(m, "queued"));
    if (kind == "patrol") return OrderMsg::patrol(idList(at(m, "units")), xy());
    if (kind == "hold") return OrderMsg::hold(idList(at(m, "units")));
    if (kind == "stop") return OrderMsg::stop(idList(at(m, "units")));
    if (kind == "extract") return OrderMsg::extract(idList(at(m, "units")), toU32(at(m, "source")), flag(m, "queued"));
    if (kind == "repair") return OrderMsg::repair(idList(at(m, "units")), toU32(at(m, "target")), flag(m, "queued"));
    if (kind == "build") return OrderMsg::build(idList(at(m, "units")), toU32(at(m, "site")), flag(m, "queued"));
    if (kind == "place") {
        return OrderMsg::placeBuilding(toU32(at(m, "builder")), c.buildings.id(catalogName(at(m, "kind"))), xy());
    }
    if (kind == "produce") return OrderMsg::produce(toU32(at(m, "building")), c.units.id(catalogName(at(m, "kind"))));
    if (kind == "cancel") return OrderMsg::cancelProduce(toU32(at(m, "building")));
    if (kind == "rally") return OrderMsg::setRally(toU32(at(m, "building")), xy());
    if (kind == "cast") {
        const std::string t = at(m, "target");
        CastTarget target;
        if (t == "none") {
            target = CastTarget::none();
        } else if (t.rfind("id:", 0) == 0) {
            target = CastTarget::ofId(toU32(t.substr(3)));
        } else if (t.rfind("point:", 0) == 0) {
            const std::string p = t.substr(6);
            const size_t comma = p.find(',');
            if (comma == std::string::npos) bad("bad cast point " + t);
            target = CastTarget::ofPoint(Vec2(toF32(p.substr(0, comma)), toF32(p.substr(comma + 1))));
        } else {
            bad("bad cast target " + t);
        }
        return OrderMsg::castAbility(toU32(at(m, "hero")), static_cast<uint8_t>(toU32(at(m, "ability"))), target,
                                     flag(m, "queued"));
    }
    if (kind == "learn") return OrderMsg::learn(toU32(at(m, "hero")), static_cast<uint8_t>(toU32(at(m, "ability"))));
    if (kind == "revive") return OrderMsg::revive();
    if (kind == "pickup") return OrderMsg::pickup(toU32(at(m, "hero")), toU32(at(m, "item")), flag(m, "queued"));
    if (kind == "drop") return OrderMsg::dropItem(toU32(at(m, "hero")), static_cast<uint8_t>(toU32(at(m, "slot"))));
    if (kind == "use") return OrderMsg::useItem(toU32(at(m, "hero")), static_cast<uint8_t>(toU32(at(m, "slot"))));
    if (kind == "research") {
        return OrderMsg::research(toU32(at(m, "building")), c.upgrades.id(catalogName(at(m, "upgrade"))));
    }
    bad("unknown order " + kind);
}

std::unique_ptr<World> startScenario(const Scenario& s, std::shared_ptr<const Catalogs> catalogs) {
    auto w = std::make_unique<World>(std::move(catalogs), s.seed);
    // before setup: difficulty scales enemy hp and squad size at spawn
    w->difficulty = s.difficulty;
    for (const auto& words : s.setup) {
        if (words[0] == "setup" && words.size() > 1 && words[1] == "m0") {
            spawnM0Scenario(*w);
        } else if (words[0] == "setup" && words.size() > 1 && words[1] == "macro") {
            spawnM2MacroScenario(*w);
        } else if (words[0] == "mission" && words.size() > 1) {
            if (auto err = loadMission(*w, words[1])) bad(*err);
        } else if (words[0] == "squad") {
            const auto m = keyValues(words, 1);
            std::vector<std::pair<uint16_t, uint32_t>> comp;
            std::stringstream ss(at(m, "comp"));
            std::string part;
            while (std::getline(ss, part, ',')) {
                const size_t colon = part.find(':');
                if (colon == std::string::npos) bad("bad comp " + part);
                comp.emplace_back(w->cat().units.id(catalogName(part.substr(0, colon))), toU32(part.substr(colon + 1)));
            }
            spawnSquad(*w, static_cast<uint8_t>(toU32(at(m, "team"))), Vec2(toF32(at(m, "x")), toF32(at(m, "y"))), comp);
        } else {
            bad("unknown setup line " + words[0]);
        }
    }
    return w;
}

void pushScriptedOrders(World& w, const Scenario& s, uint64_t tick) {
    for (const auto& [atTick, words] : s.script) {
        if (atTick == tick) w.orderQueue.push_back(scenarioOrder(w, words));
    }
}

} // namespace husk
