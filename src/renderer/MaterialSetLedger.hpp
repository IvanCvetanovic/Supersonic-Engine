#pragma once

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace Supersonic {

// What TextureRegistry decides about its material descriptor sets, kept apart
// from the Vulkan calls that carry it out - the way ShadowCache is kept apart
// from the passes it skips, and for its reason: this is the part that can be
// wrong while every frame still looks right, and no suite can build the
// registry itself, because it needs a device.
//
// It was wrong twice, and neither showed. The registry never gave a set back:
// dropping one from its cache on an Invalidate left it allocated in the pool,
// so the cache's size - which the capacity check read - undercounted what the
// pool held, and the allocation after the real limit threw
// ErrorOutOfPoolMemory out of the middle of a frame. And a session that only
// ever acquired would reach the cap and draw every new material with the white
// fallback's maps, saying so once per call in a log nobody reads.
namespace MaterialSets {

// The ids a set is actually built from, in binding order.
//
// Each slot that names no texture - an id past the registry's `textureCount` -
// falls back to ITS OWN neutral, in `fallbacks`, never to a shared one. A
// missing albedo is a mistake worth seeing and gets the checkerboard; a missing
// normal, packed or overlay map is the ordinary case and gets the value that
// changes nothing: a flat normal, a neutral ORM of 1, an overlay of black that
// adds 0. The overlay's fallback is the reason this is out here: the slot a
// caller most often has no texture for is the one whose wrong neutral - white -
// would add a full-bright copy of nothing over every sprite.
template <std::size_t N>
std::array<uint32_t, N> ResolveKey(const std::array<uint32_t, N>& ids, uint32_t textureCount,
                                   const std::array<uint32_t, N>& fallbacks) {
    std::array<uint32_t, N> key = ids;
    for (std::size_t i = 0; i < N; ++i) {
        if (key[i] >= textureCount) key[i] = fallbacks[i];
    }
    return key;
}

// A set is a few bytes of pool: five image-sampler descriptors. 1024 is twice
// the old cap, with the margin the Magic Portals port asks for - one set per
// lightmapped sprite, at most 21 lightmaps in one level - once sets are given
// back when a level lets its textures go.
inline constexpr uint32_t kMaxSets = 1024;

// Sets taken from the pool and not yet given back to it.
//
// "Given back" is when the free actually runs, not when the cache forgets the
// set. Between the two - two frames, while a command buffer already recorded
// may still bind it - the set is out of the cache and still in the pool, and
// counting it as free is exactly the undercount described above.
class Ledger {
public:
    explicit Ledger(uint32_t capacity) : m_capacity(capacity) {}

    bool HasRoom() const { return m_live < m_capacity; }

    void Taken() { ++m_live; }

    // False, changing nothing, when there is nothing to give back. A set given
    // back twice is a bug worth reporting, not a count worth wrapping to four
    // billion - after which HasRoom would say no for the rest of the process.
    bool GivenBack() {
        if (m_live == 0) return false;
        --m_live;
        return true;
    }

    uint32_t Live() const { return m_live; }
    uint32_t Capacity() const { return m_capacity; }

private:
    uint32_t m_capacity;
    uint32_t m_live{0};
};

// Removes from `sets` every entry whose key names any of `ids` in any binding,
// and returns what was removed, in key order.
//
// One function for both of the registry's reasons to drop a set - a texture
// invalidated, a texture's pixels replaced - so neither can drift into
// checking two named bindings of three, and so what was dropped is in hand to
// be given back rather than erased and forgotten.
//
// `Map` is an ordered map from an array of texture ids to a set handle; a
// template so the policy is testable with plain numbers for handles.
template <typename Map>
std::vector<typename Map::mapped_type> TakeNaming(Map& sets, const std::vector<uint32_t>& ids) {
    std::vector<typename Map::mapped_type> taken;
    if (ids.empty()) return taken;
    for (auto it = sets.begin(); it != sets.end();) {
        const bool names = std::any_of(it->first.begin(), it->first.end(), [&ids](uint32_t id) {
            return std::find(ids.begin(), ids.end(), id) != ids.end();
        });
        if (names) {
            taken.push_back(it->second);
            it = sets.erase(it);
        } else {
            ++it;
        }
    }
    return taken;
}

} // namespace MaterialSets

} // namespace Supersonic
