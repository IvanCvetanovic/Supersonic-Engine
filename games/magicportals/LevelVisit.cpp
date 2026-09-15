#include "LevelVisit.hpp"

#include <algorithm>
#include <sstream>
#include <utility>

#include "core/Application.hpp"
#include "core/Log.hpp"
#include "renderer/TextureRegistry.hpp"

#include "sim/Chapters.hpp"
#include "sim/Lighting.hpp"
#include "sim/Tscn.hpp"

namespace MagicPortals {

namespace {

// Frames a visit waits before it measures. The renderer frees what was retired
// MAX_FRAMES_IN_FLIGHT (2) frames after it was queued; six is that with room,
// and also lets the level's own sprites and HUD acquire their sets first.
constexpr int kSettleFrames = 6;
// Frames the level's lightmap sets are held before the next visit.
constexpr int kHoldFrames = 2;

std::vector<std::string> SplitNames(const std::string& text) {
    std::vector<std::string> out;
    std::stringstream stream(text);
    std::string item;
    while (std::getline(stream, item, ',')) {
        if (!item.empty()) out.push_back(item);
    }
    return out;
}

} // namespace

LevelVisitLayer::LevelVisitLayer(MagicPortalsLayer& game, MagicPortalsLayer::Paths paths, Options options,
                                 Result& result)
    : m_game(game), m_paths(std::move(paths)), m_options(std::move(options)), m_result(result) {}

void LevelVisitLayer::fail(std::string why) {
    SUPERSONIC_LOG_ERROR("LevelVisit") << why << std::endl;
    m_result.failures.push_back(std::move(why));
}

void LevelVisitLayer::OnAttach(entt::registry& registry) {
    (void)registry;
    Chapters::Table chapters;
    std::string error;
    if (!Chapters::Load(m_paths.chapters, chapters, error)) {
        fail("chapters: " + error);
        m_done = true;
        return;
    }

    const bool everyLevel = m_options.levels == "all";
    const bool lightmapped = m_options.levels == "lightmapped";
    std::vector<int> chosen;
    if (everyLevel || lightmapped) {
        for (int i = 0; i < static_cast<int>(chapters.levels.size()); ++i) chosen.push_back(i);
    } else {
        for (const std::string& name : SplitNames(m_options.levels)) {
            const int at = chapters.Find(name);
            if (at < 0) {
                fail(name + " is not a level of " + m_paths.chapters);
                continue;
            }
            chosen.push_back(at);
        }
    }

    for (const int at : chosen) {
        const Chapters::Level& entry = chapters.levels[static_cast<std::size_t>(at)];
        Visit visit;
        visit.level = at;
        visit.name = entry.name;
        visit.label = Chapters::Label(entry);

        Tscn::Scene scene;
        Lighting::Scene lighting;
        if (!Tscn::Load(m_paths.levels + "/" + entry.name + ".tscn", scene, error) ||
            !Lighting::Read(scene, m_paths.art, lighting, error)) {
            fail(entry.name + "'s lighting: " + error);
            continue;
        }
        for (const auto& [node, look] : lighting.nodes) {
            if (!look.lightmap.empty()) visit.lightmaps.push_back(look.lightmap);
        }
        // The nodes are hashed; sorted, so two runs acquire in the same order.
        std::sort(visit.lightmaps.begin(), visit.lightmaps.end());
        visit.lightmaps.erase(std::unique(visit.lightmaps.begin(), visit.lightmaps.end()), visit.lightmaps.end());
        if (lightmapped && visit.lightmaps.empty()) continue;
        m_plan.push_back(std::move(visit));
    }

    std::size_t perPass = 0;
    for (const Visit& visit : m_plan) perPass += visit.lightmaps.size();
    SUPERSONIC_LOG_INFO("LevelVisit") << "plan: " << m_plan.size() << " level(s), " << perPass
                                      << " lightmap(s) per pass, " << m_options.passes << " pass(es)" << std::endl;
    if (m_plan.empty() || m_options.passes < 1) {
        fail("nothing to visit");
        m_done = true;
    }
}

void LevelVisitLayer::release(entt::registry& registry) {
    auto* const* slot = registry.ctx().find<Supersonic::TextureRegistry*>();
    if (slot == nullptr || *slot == nullptr || m_held.empty()) return;
    Supersonic::TextureRegistry& textures = **slot;
    const std::size_t cachedBefore = textures.MaterialSetCount();
    const std::size_t inPoolBefore = textures.MaterialSetsInPool();
    for (const std::string& path : m_held) textures.Invalidate(path);

    // Out of the cache at once, and still in the pool: the free waits for the
    // frames that may bind them. Freed here and now would be the use-after-free
    // the deferred queue exists to prevent, and nothing on screen would say so.
    const std::size_t cachedAfter = textures.MaterialSetCount();
    const std::size_t inPoolAfter = textures.MaterialSetsInPool();
    SUPERSONIC_LOG_INFO("LevelVisit") << "released " << m_held.size() << " lightmap(s): cached " << cachedBefore
                                      << " -> " << cachedAfter << ", in pool " << inPoolBefore << " -> "
                                      << inPoolAfter << " (" << (inPoolAfter - cachedAfter)
                                      << " waiting to be freed)" << std::endl;
    if (cachedBefore - cachedAfter != m_heldSets || inPoolAfter != inPoolBefore) {
        std::ostringstream why;
        why << "releasing " << m_heldSets << " set(s) dropped " << (cachedBefore - cachedAfter)
            << " from the cache and moved the pool from " << inPoolBefore << " to " << inPoolAfter
            << " (expected unchanged until collected)";
        fail(why.str());
    }
    m_held.clear();
    m_heldSets = 0;
}

void LevelVisitLayer::measureAndAcquire(entt::registry& registry) {
    auto* const* slot = registry.ctx().find<Supersonic::TextureRegistry*>();
    if (slot == nullptr || *slot == nullptr) {
        fail("no texture registry in the context: --visit-levels needs a renderer");
        m_done = true;
        return;
    }
    Supersonic::TextureRegistry& textures = **slot;
    const Visit& visit = m_plan[static_cast<std::size_t>(m_visit) % m_plan.size()];
    const int pass = m_visit / static_cast<int>(m_plan.size()) + 1;

    // The set an exhausted pool hands out instead. Acquired before the first
    // measurement, so it is part of every baseline rather than of one level.
    const vk::DescriptorSet fallback = textures.AcquireMaterialSet(
        textures.GetWhiteTexture(), textures.GetFlatNormalTexture(), textures.GetNeutralOrmTexture(),
        textures.GetBlackTexture());

    const std::size_t cachedBefore = textures.MaterialSetCount();
    const std::size_t inPoolBefore = textures.MaterialSetsInPool();
    m_result.baselines.push_back(inPoolBefore);
    // THE CHECK THAT MATTERS. Every set the last level released has been freed
    // by now, so the pool holds exactly what the cache hands out. A registry
    // that forgot sets without freeing them passes every other line here and
    // fails this one, by the number it forgot.
    if (inPoolBefore != cachedBefore) {
        std::ostringstream why;
        why << visit.name << ": " << inPoolBefore << " set(s) in the pool, " << cachedBefore << " in the cache, "
            << kSettleFrames << " frames after the last release";
        fail(why.str());
    }

    std::size_t acquired = 0;
    for (const std::string& path : visit.lightmaps) {
        const uint32_t id = textures.Acquire(path, false, textures.GetWhiteTexture());
        if (id == textures.GetWhiteTexture()) {
            fail(visit.name + ": " + path + " did not load");
            continue;
        }
        m_held.push_back(path);
        const vk::DescriptorSet set = textures.AcquireMaterialSet(
            id, textures.GetFlatNormalTexture(), textures.GetNeutralOrmTexture(), textures.GetBlackTexture());
        if (!set || set == fallback) {
            fail(visit.name + ": " + path + " got no material set of its own (pool exhausted)");
            continue;
        }
        ++acquired;
    }
    m_heldSets = acquired;
    m_result.lightmapSets += acquired;

    const std::size_t cachedAfter = textures.MaterialSetCount();
    const std::size_t inPoolAfter = textures.MaterialSetsInPool();
    m_result.peakInPool = std::max(m_result.peakInPool, inPoolAfter);
    SUPERSONIC_LOG_INFO("LevelVisit") << "pass " << pass << " visit " << (m_visit + 1) << " " << visit.label << " ("
                                      << visit.name << "): baseline cached " << cachedBefore << " in pool "
                                      << inPoolBefore << "; " << visit.lightmaps.size() << " lightmap(s) -> cached "
                                      << cachedAfter << " in pool " << inPoolAfter << "; run total "
                                      << m_result.lightmapSets << std::endl;

    if (cachedAfter != cachedBefore + acquired || inPoolAfter != inPoolBefore + acquired) {
        std::ostringstream why;
        why << visit.name << ": " << acquired << " lightmap set(s) moved the cache from " << cachedBefore << " to "
            << cachedAfter << " and the pool from " << inPoolBefore << " to " << inPoolAfter;
        fail(why.str());
    }
}

void LevelVisitLayer::finish(entt::registry& registry) {
    // After the last release and the frames to collect it: nothing of this
    // layer's may be left in either count.
    if (auto* const* slot = registry.ctx().find<Supersonic::TextureRegistry*>(); slot != nullptr && *slot != nullptr) {
        Supersonic::TextureRegistry& textures = **slot;
        m_result.finalInPool = textures.MaterialSetsInPool();
        const std::size_t cached = textures.MaterialSetCount();
        SUPERSONIC_LOG_INFO("LevelVisit") << "after the last release: cached " << cached << " in pool "
                                          << m_result.finalInPool << std::endl;
        if (m_result.finalInPool != cached) {
            std::ostringstream why;
            why << "after the last release, " << m_result.finalInPool << " set(s) in the pool and " << cached
                << " in the cache";
            fail(why.str());
        }

        // And the shutdown case, on purpose: the last level's lightmaps taken
        // again and dropped on the frame the run quits, so their frees are still
        // queued when the renderer destroys the registry - and its pool - and
        // only then flushes the queue. Validation, which fails the run, is what
        // would see a free into a destroyed pool; the queued free has to find
        // the pool gone and do nothing.
        const Visit& last = m_plan.back();
        for (const std::string& path : last.lightmaps) {
            const uint32_t id = textures.Acquire(path, false, textures.GetWhiteTexture());
            if (id != textures.GetWhiteTexture()) {
                textures.AcquireMaterialSet(id, textures.GetFlatNormalTexture(), textures.GetNeutralOrmTexture(),
                                            textures.GetBlackTexture());
            }
            textures.Invalidate(path);
        }
        SUPERSONIC_LOG_INFO("LevelVisit") << "quitting with " << (textures.MaterialSetsInPool() - textures.MaterialSetCount())
                                          << " set(s) still waiting to be freed" << std::endl;
    }
    m_result.finished = true;
    m_done = true;
    SUPERSONIC_LOG_INFO("LevelVisit") << "done: " << m_result.visits << " visit(s), " << m_result.lightmapSets
                                      << " lightmap set(s), peak " << m_result.peakInPool << " in pool, "
                                      << m_result.failures.size() << " failure(s)" << std::endl;
    Supersonic::Application::RequestQuit();
}

void LevelVisitLayer::OnUpdate(entt::registry& registry, float deltaTime) {
    (void)deltaTime;
    if (m_done) return;

    const int total = static_cast<int>(m_plan.size()) * m_options.passes;
    if (m_closing) {
        if (++m_age == kSettleFrames) finish(registry);
        return;
    }
    if (m_visit >= 0) ++m_age;

    const bool start = m_visit < 0 || m_age >= kSettleFrames + kHoldFrames;
    if (start) {
        // What the layer's unload does since step 45, for the sets this layer took.
        release(registry);
        ++m_visit;
        m_age = 0;
        if (m_visit >= total) {
            m_closing = true;
            return;
        }
        const Visit& visit = m_plan[static_cast<std::size_t>(m_visit) % m_plan.size()];
        MagicPortalsLayer::MenuButton button;
        button.kind = MagicPortalsLayer::MenuButton::Kind::Level;
        button.level = visit.level;
        m_game.PressMenu(registry, button);
        ++m_result.visits;
        if (!m_game.LoadError().empty()) {
            SUPERSONIC_LOG_WARN("LevelVisit") << visit.name << " did not load: " << m_game.LoadError() << std::endl;
        }
        return;
    }
    if (m_age == kSettleFrames) measureAndAcquire(registry);
}

} // namespace MagicPortals
