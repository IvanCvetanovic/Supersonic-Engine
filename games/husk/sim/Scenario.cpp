#include "Scenario.hpp"

#include "Mission.hpp"
#include "Snapshot.hpp"

#include <algorithm>
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
    std::replace(v.begin(), v.end(), '_', ' ');
    return v;
}

// A catalog name, or `@N` for a raw id. Not `#`: that starts a comment.
template <class Def>
uint16_t kindOf(const std::string& v, const Catalog<Def>& catalog) {
    if (!v.empty() && v[0] == '@') return static_cast<uint16_t>(toU64(v.substr(1)));
    return catalog.id(catalogName(v));
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
    return std::all_of(s.begin(), s.end(), [](char ch) { return ch >= '0' && ch <= '9'; });
}

bool isOrderVerb(const std::string& v) {
    static const char* const kVerbs[] = {"point",   "attack", "patrol", "hold",  "stop",   "extract", "repair",
                                         "build",   "place",  "produce", "cancel", "rally", "cast",   "learn",
                                         "revive",  "pickup", "drop",   "use",   "research"};
    return std::any_of(std::begin(kVerbs), std::end(kVerbs), [&](const char* k) { return v == k; });
}

Affinity affinityNamed(const std::string& n) {
    if (n == "Steel") return Affinity::Steel;
    if (n == "Flora") return Affinity::Flora;
    if (n == "Volt") return Affinity::Volt;
    if (n == "Stone") return Affinity::Stone;
    if (n == "Pyre") return Affinity::Pyre;
    if (n == "Aqua") return Affinity::Aqua;
    bad("unknown affinity " + n);
}

const std::string& word(const std::vector<std::string>& w, size_t i) {
    if (i >= w.size()) bad("'" + w[0] + "' is missing an argument");
    return w[i];
}

float fw(const std::vector<std::string>& w, size_t i) {
    return toF32(word(w, i));
}

uint32_t uw(const std::vector<std::string>& w, size_t i) {
    return toU32(word(w, i));
}

Entity& entityAt(World& x, uint32_t id) {
    Entity* e = x.indexed(id);
    if (!e) bad("no entity with SimId " + std::to_string(id));
    return *e;
}

void rebuildMap(World& x) {
    x.grid = NavGrid::fromDef(x.map);
    x.flowFields.clear();
}

void applyLine(std::unique_ptr<World>& w, const std::vector<std::string>& words, const Scenario& s) {
    const std::string& v = words[0];

    // Save and restore into a fresh world with another seed: the round trip
    // the game's snapshot tests make (through a file there, through memory
    // here - the state is the same either way).
    if (v == "snapshot") {
        auto fresh = std::make_unique<World>(w->catalogs, 999);
        restore(*fresh, capture(*w, CampaignState{}));
        w = std::move(fresh);
        return;
    }

    World& x = *w;
    const Catalogs& c = x.cat();
    if (v == "setup") {
        const std::string& which = word(words, 1);
        if (which == "m0") {
            spawnM0Scenario(x);
        } else if (which == "macro") {
            spawnM2MacroScenario(x);
        } else {
            bad("unknown setup " + which);
        }
    } else if (v == "mission") {
        if (auto err = loadMission(x, word(words, 1))) bad(*err);
    } else if (v == "install") {
        if (auto err = installMission(x, parseMissionDefFile(s.dir / word(words, 1)))) bad(*err);
    } else if (v == "squad") {
        const auto m = keyValues(words, 1);
        std::vector<std::pair<uint16_t, uint32_t>> comp;
        std::stringstream ss(at(m, "comp"));
        std::string part;
        while (std::getline(ss, part, ',')) {
            const size_t colon = part.find(':');
            if (colon == std::string::npos) bad("bad comp " + part);
            comp.emplace_back(c.units.id(catalogName(part.substr(0, colon))), toU32(part.substr(colon + 1)));
        }
        spawnSquad(x, static_cast<uint8_t>(toU32(at(m, "team"))), Vec2(toF32(at(m, "x")), toF32(at(m, "y"))), comp);
    } else if (v == "map") {
        x.map = MapDef{};
        x.map.half = fw(words, 1);
        rebuildMap(x);
    } else if (v == "plateau") {
        x.map.plateaus.emplace_back(Rect2::make(fw(words, 1), fw(words, 2), fw(words, 3), fw(words, 4)),
                                    static_cast<uint8_t>(uw(words, 5)));
        rebuildMap(x);
    } else if (v == "ramp") {
        x.map.ramps.push_back(Rect2::make(fw(words, 1), fw(words, 2), fw(words, 3), fw(words, 4)));
        rebuildMap(x);
    } else if (v == "obstacle") {
        x.map.obstacles.push_back(Rect2::make(fw(words, 1), fw(words, 2), fw(words, 3), fw(words, 4)));
        rebuildMap(x);
    } else if (v == "poly") {
        std::vector<Vec2> verts;
        for (size_t i = 2; i < words.size(); ++i) {
            const size_t comma = words[i].find(',');
            if (comma == std::string::npos) bad("bad vertex " + words[i]);
            verts.emplace_back(toF32(words[i].substr(0, comma)), toF32(words[i].substr(comma + 1)));
        }
        x.map.plateauPolys.emplace_back(std::move(verts), static_cast<uint8_t>(uw(words, 1)));
        rebuildMap(x);
    } else if (v == "unit") {
        spawnUnit(x, c.units.id(catalogName(word(words, 1))), static_cast<uint8_t>(uw(words, 2)),
                  Vec2(fw(words, 3), fw(words, 4)));
    } else if (v == "building") {
        const bool complete = words.size() <= 5 || words[5] != "site";
        spawnBuilding(x, c.buildings.id(catalogName(word(words, 1))), static_cast<uint8_t>(uw(words, 2)),
                      Vec2(fw(words, 3), fw(words, 4)), complete);
    } else if (v == "source") {
        const uint32_t id = spawnSource(x, c.sources.id(catalogName(word(words, 1))), Vec2(fw(words, 2), fw(words, 3)));
        if (words.size() > 4 && words[4] == "husk") x.get(id)->source.essence = 0.0f;
    } else if (v == "item") {
        spawnItem(x, c.items.id(catalogName(word(words, 1))), Vec2(fw(words, 2), fw(words, 3)));
    } else if (v == "hero") {
        spawnHero(x, Vec2(fw(words, 1), fw(words, 2)));
    } else if (v == "essence") {
        x.economy.essence = fw(words, 1);
    } else if (v == "anima") {
        x.economy.anima = fw(words, 1);
    } else if (v == "affinity") {
        x.affinity.cumulative[static_cast<size_t>(affinityNamed(word(words, 1)))] = fw(words, 2);
    } else if (v == "researched") {
        const uint16_t id = c.upgrades.id(catalogName(word(words, 1)));
        if (x.research.levels.size() < c.upgrades.defs.size()) x.research.levels.resize(c.upgrades.defs.size(), 0);
        x.research.levels[id] = static_cast<uint8_t>(uw(words, 2));
    } else if (v == "hp") {
        entityAt(x, uw(words, 1)).health.cur = fw(words, 2);
    } else if (v == "herolevel") {
        Entity& e = entityAt(x, uw(words, 1));
        if (!e.hero) bad("herolevel on an entity without a Hero");
        e.hero->level = static_cast<uint8_t>(uw(words, 2));
        e.hero->xp = fw(words, 3);
        e.hero->points = static_cast<uint8_t>(uw(words, 4));
    } else if (v == "inventory") {
        const uint16_t item = c.items.id(catalogName(word(words, 3)));
        Entity& e = entityAt(x, uw(words, 1));
        if (!e.hero) bad("inventory on an entity without a Hero");
        e.hero->inventory.at(uw(words, 2)) = item;
    } else if (v == "queue") {
        const uint16_t kind = c.units.id(catalogName(word(words, 2)));
        entityAt(x, uw(words, 1)).building.queue.push_back(kind);
    } else if (v == "hook") {
        if (word(words, 1) != "essence_drip") bad("unknown hook " + words[1]);
        x.missionHooks.push_back([](World& world) { world.economy.essence += 1.0f; });
    } else {
        bad("unknown line " + v);
    }
}

} // namespace

std::filesystem::path goldenDir() {
    return std::filesystem::path(HUSK_GOLDEN_DIR);
}

Scenario parseScenario(const std::filesystem::path& path) {
    std::ifstream in(path);
    if (!in) bad("failed to read " + path.generic_string());
    Scenario s;
    s.dir = path.parent_path();
    std::string line;
    while (std::getline(in, line)) {
        line = line.substr(0, line.find('#'));
        std::stringstream ss(line);
        std::vector<std::string> words;
        for (std::string w; ss >> w;) words.push_back(w);
        if (words.empty()) continue;
        if (words[0] == "seed") {
            s.seed = toU64(word(words, 1));
        } else if (words[0] == "ticks") {
            s.ticks = toU64(word(words, 1));
        } else if (words[0] == "difficulty") {
            const std::string& d = word(words, 1);
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
    if (kind == "place") return OrderMsg::placeBuilding(toU32(at(m, "builder")), kindOf(at(m, "kind"), c.buildings), xy());
    if (kind == "produce") return OrderMsg::produce(toU32(at(m, "building")), kindOf(at(m, "kind"), c.units));
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
    if (kind == "research") return OrderMsg::research(toU32(at(m, "building")), kindOf(at(m, "upgrade"), c.upgrades));
    bad("unknown order " + kind);
}

std::unique_ptr<World> startScenario(const Scenario& s, std::shared_ptr<const Catalogs> catalogs) {
    auto w = std::make_unique<World>(std::move(catalogs), s.seed);
    // before setup: difficulty scales enemy hp and squad size at spawn
    w->difficulty = s.difficulty;
    for (const auto& words : s.setup) applyLine(w, words, s);
    return w;
}

void applyScriptTick(std::unique_ptr<World>& w, const Scenario& s, uint64_t tick) {
    for (const auto& [atTick, words] : s.script) {
        if (atTick != tick) continue;
        if (isOrderVerb(words[0])) {
            w->orderQueue.push_back(scenarioOrder(*w, words));
        } else {
            applyLine(w, words, s);
        }
    }
}

} // namespace husk
