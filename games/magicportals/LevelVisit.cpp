#include "LevelVisit.hpp"

#include <algorithm>
#include <array>
#include <cstdint>
#include <set>
#include <sstream>
#include <utility>

#include "core/Application.hpp"
#include "core/Components.hpp"
#include "core/Log.hpp"
#include "renderer/TextureRegistry.hpp"

#include "sim/Chapters.hpp"
#include "sim/Lighting.hpp"
#include "sim/Tscn.hpp"

namespace MagicPortals {

namespace {

// Frames a visit waits before it measures. The renderer frees what was retired
// MAX_FRAMES_IN_FLIGHT (2) frames after it was queued; six is that with room,
// and also lets the level's own sprites and HUD resolve and acquire first.
constexpr int kSettleFrames = 6;
// Frames the level is held, with every lightmap set taken, before the next visit.
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

Supersonic::TextureRegistry* TexturesOf(entt::registry& registry) {
    auto* const* slot = registry.ctx().find<Supersonic::TextureRegistry*>();
    return slot != nullptr ? *slot : nullptr;
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
        m_phase = Phase::Done;
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
        // The nodes are hashed; sorted, as the layer's HeldLightmaps is.
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
        m_phase = Phase::Done;
    }
}

void LevelVisitLayer::pressAndCheckRelease(entt::registry& registry, MagicPortalsLayer::MenuButton button,
                                           const std::string& what, std::size_t expectDropped) {
    Supersonic::TextureRegistry* const textures = TexturesOf(registry);
    if (textures == nullptr) {
        // Nothing to count; the measure that follows says so and ends the walk.
        m_game.PressMenu(registry, button);
        return;
    }
    const std::size_t cachedBefore = textures->MaterialSetCount();
    const std::size_t inPoolBefore = textures->MaterialSetsInPool();
    m_game.PressMenu(registry, button);
    const std::size_t cachedAfter = textures->MaterialSetCount();
    const std::size_t inPoolAfter = textures->MaterialSetsInPool();

    // Out of the cache at once, and still in the pool: the free waits for the
    // frames that may bind them. Freed here and now would be the use-after-free
    // the deferred queue exists to prevent, and nothing on screen would say so.
    // Nothing renders inside PressMenu, so the layer's unload is the only thing
    // that can have dropped a set.
    const std::size_t dropped = cachedBefore >= cachedAfter ? cachedBefore - cachedAfter : 0;
    SUPERSONIC_LOG_INFO("LevelVisit") << what << ": released " << dropped << " lightmap set(s): cached "
                                      << cachedBefore << " -> " << cachedAfter << ", in pool " << inPoolBefore
                                      << " -> " << inPoolAfter << " (" << (inPoolAfter - cachedAfter)
                                      << " waiting to be freed)" << std::endl;
    if (cachedAfter > cachedBefore || dropped != expectDropped || inPoolAfter != inPoolBefore) {
        std::ostringstream why;
        why << what << ": the layer's unload of " << expectDropped << " lightmap set(s) moved the cache from "
            << cachedBefore << " to " << cachedAfter << " and the pool from " << inPoolBefore << " to "
            << inPoolAfter << " (expected the cache down by exactly that, the pool unchanged until collected)";
        fail(why.str());
    }
}

void LevelVisitLayer::open(entt::registry& registry, const Visit& visit) {
    MagicPortalsLayer::MenuButton button;
    button.kind = MagicPortalsLayer::MenuButton::Kind::Level;
    button.level = visit.level;
    // A level opened over itself is a retry, which keeps its lightmaps; any
    // other gives back what the level being shown holds.
    const bool showing = m_game.SimLevel() != nullptr;
    const bool retry = showing && m_game.Current() != nullptr && m_game.Current()->name == visit.name;
    const std::size_t expect = showing && !retry ? m_heldSets : 0;
    pressAndCheckRelease(registry, button, "opening " + visit.label + " (" + visit.name + ")", expect);
    if (!retry) m_heldSets = 0;
    if (!m_game.LoadError().empty()) {
        SUPERSONIC_LOG_WARN("LevelVisit") << visit.name << " did not load: " << m_game.LoadError() << std::endl;
    }
}

bool LevelVisitLayer::measure(entt::registry& registry, const Visit& visit, bool counted) {
    using Supersonic::MaterialComponent;
    using Supersonic::RenderableComponent;
    Supersonic::TextureRegistry* const slot = TexturesOf(registry);
    if (slot == nullptr) {
        fail("no texture registry in the context: --visit-levels needs a renderer");
        return false;
    }
    Supersonic::TextureRegistry& textures = *slot;

    // The set an exhausted pool hands out instead. Acquired before the first
    // measurement, so it is part of every baseline rather than of one level.
    const vk::DescriptorSet fallback = textures.AcquireMaterialSet(
        textures.GetWhiteTexture(), textures.GetFlatNormalTexture(), textures.GetNeutralOrmTexture(),
        textures.GetBlackTexture());

    const std::size_t cachedBefore = textures.MaterialSetCount();
    const std::size_t inPoolBefore = textures.MaterialSetsInPool();
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

    // The layer holds this level's lightmaps, and no other level's.
    if (m_game.HeldLightmaps() != visit.lightmaps) {
        std::ostringstream why;
        why << visit.name << ": the layer holds " << m_game.HeldLightmaps().size() << " lightmap(s), the level names "
            << visit.lightmaps.size();
        fail(why.str());
    }

    // Each is the overlay of a sprite: loaded, and under the id its path has.
    // Asking for a path already loaded adds nothing, so the registry's size says
    // whether the layer's sprites had loaded every one.
    const std::size_t texturesBefore = textures.Size();
    std::set<std::string> drawnLightmaps;
    std::set<std::array<uint32_t, 4>> keys;
    for (auto [entity, material, renderable] : registry.view<MaterialComponent, RenderableComponent>().each()) {
        (void)entity;
        if (material.overlayTexturePath.empty()) continue;
        drawnLightmaps.insert(material.overlayTexturePath);
        if (renderable.overlayTextureID == textures.GetBlackTexture()) {
            fail(visit.name + ": " + material.overlayTexturePath + " did not load");
            continue;
        }
        // As data: in the port's display-encoded scene a colour map is read as
        // its file's bytes (SyncResources' decodeColour).
        if (textures.Acquire(material.overlayTexturePath, false, textures.GetBlackTexture()) !=
            renderable.overlayTextureID) {
            fail(visit.name + ": " + material.overlayTexturePath + " is drawn under an id its path does not have");
            continue;
        }
        keys.insert({renderable.albedoTextureID, renderable.normalTextureID, renderable.ormTextureID,
                     renderable.overlayTextureID});
    }
    if (textures.Size() != texturesBefore) {
        fail(visit.name + ": a lightmap a sprite names had not been loaded by the layer's sprites");
    }
    if (drawnLightmaps != std::set<std::string>(visit.lightmaps.begin(), visit.lightmaps.end())) {
        std::ostringstream why;
        why << visit.name << ": the sprites draw " << drawnLightmaps.size() << " lightmap(s), the level names "
            << visit.lightmaps.size();
        fail(why.str());
    }

    // Every such sprite's own set, as the renderer takes it when the sprite
    // comes into view. One already in the cache is one the renderer has drawn.
    std::size_t drawn = 0;
    std::size_t taken = 0;
    for (const std::array<uint32_t, 4>& key : keys) {
        const std::size_t before = textures.MaterialSetCount();
        const vk::DescriptorSet set = textures.AcquireMaterialSet(key[0], key[1], key[2], key[3]);
        if (!set || set == fallback) {
            fail(visit.name + ": a lightmapped sprite got no material set of its own (pool exhausted)");
            continue;
        }
        if (textures.MaterialSetCount() == before) {
            ++drawn;
        } else {
            ++taken;
        }
    }
    m_heldSets = drawn + taken;

    const std::size_t cachedAfter = textures.MaterialSetCount();
    const std::size_t inPoolAfter = textures.MaterialSetsInPool();
    m_result.peakInPool = std::max(m_result.peakInPool, inPoolAfter);
    const std::size_t baseline = inPoolBefore - drawn;
    if (counted) {
        m_result.lightmapSets += m_heldSets;
        m_result.drawnFirst += drawn;
        m_result.baselines.push_back(baseline);
    }
    const int pass = m_visit / static_cast<int>(m_plan.size()) + 1;
    SUPERSONIC_LOG_INFO("LevelVisit") << (counted ? "pass " + std::to_string(pass) + " visit " +
                                                        std::to_string(m_visit + 1)
                                                  : std::string("again"))
                                      << " " << visit.label << " (" << visit.name << "): cached " << cachedBefore
                                      << " in pool " << inPoolBefore << ", " << drawn
                                      << " of them the drawn lightmap sets; baseline " << baseline << "; "
                                      << visit.lightmaps.size() << " lightmap(s), " << taken
                                      << " set(s) taken for sprites not drawn yet -> cached " << cachedAfter
                                      << " in pool " << inPoolAfter << "; run total " << m_result.lightmapSets
                                      << std::endl;

    if (cachedAfter != cachedBefore + taken || inPoolAfter != inPoolBefore + taken) {
        std::ostringstream why;
        why << visit.name << ": taking " << taken << " set(s) moved the cache from " << cachedBefore << " to "
            << cachedAfter << " and the pool from " << inPoolBefore << " to " << inPoolAfter;
        fail(why.str());
    }
    return true;
}

void LevelVisitLayer::OnUpdate(entt::registry& registry, float deltaTime) {
    (void)deltaTime;
    const int total = static_cast<int>(m_plan.size()) * m_options.passes;
    switch (m_phase) {
    case Phase::Done:
        return;

    case Phase::Walking: {
        if (m_visit >= 0) ++m_age;
        if (m_visit < 0 || m_age >= kSettleFrames + kHoldFrames) {
            ++m_visit;
            m_age = 0;
            if (m_visit < total) {
                open(registry, m_plan[static_cast<std::size_t>(m_visit) % m_plan.size()]);
                ++m_result.visits;
                return;
            }
            // Out to the level grid, which unloads the last level.
            MagicPortalsLayer::MenuButton list;
            list.kind = MagicPortalsLayer::MenuButton::Kind::List;
            pressAndCheckRelease(registry, list, "leaving for the level grid", m_heldSets);
            m_heldSets = 0;
            m_phase = Phase::Leaving;
            return;
        }
        if (m_age == kSettleFrames &&
            !measure(registry, m_plan[static_cast<std::size_t>(m_visit) % m_plan.size()], true)) {
            m_phase = Phase::Done;
        }
        return;
    }

    case Phase::Leaving: {
        if (++m_age < kSettleFrames) return;
        // After the last release and the frames to collect it: nothing the
        // walk's levels held may be left in either count.
        if (Supersonic::TextureRegistry* const textures = TexturesOf(registry); textures != nullptr) {
            m_result.finalInPool = textures->MaterialSetsInPool();
            const std::size_t cached = textures->MaterialSetCount();
            SUPERSONIC_LOG_INFO("LevelVisit") << "after the last release, on the grid: cached " << cached
                                              << " in pool " << m_result.finalInPool << std::endl;
            if (m_result.finalInPool != cached) {
                std::ostringstream why;
                why << "after the last release, " << m_result.finalInPool << " set(s) in the pool and " << cached
                    << " in the cache";
                fail(why.str());
            }
        }
        // And the shutdown case, on purpose: the last level opened again and
        // held, every lightmap set taken, on the frame the run quits. The
        // layer's detach drops them after the last frame, so their frees are
        // still queued when the renderer destroys the registry - and its pool -
        // and only then flushes the queue. Validation, which fails the run, is
        // what would see a free into a destroyed pool.
        open(registry, m_plan.back());
        m_age = 0;
        m_phase = Phase::Returning;
        return;
    }

    case Phase::Returning: {
        if (++m_age < kSettleFrames) return;
        measure(registry, m_plan.back(), false);
        SUPERSONIC_LOG_INFO("LevelVisit") << "quitting with " << m_heldSets << " lightmap set(s) held by "
                                          << m_plan.back().name << ", for the layer's detach to drop" << std::endl;
        m_result.finished = true;
        m_phase = Phase::Done;
        SUPERSONIC_LOG_INFO("LevelVisit") << "done: " << m_result.visits << " visit(s), " << m_result.lightmapSets
                                          << " lightmap set(s), " << m_result.drawnFirst
                                          << " of them drawn before the measure, peak " << m_result.peakInPool
                                          << " in pool, " << m_result.failures.size() << " failure(s)" << std::endl;
        Supersonic::Application::RequestQuit();
        return;
    }
    }
}

} // namespace MagicPortals
