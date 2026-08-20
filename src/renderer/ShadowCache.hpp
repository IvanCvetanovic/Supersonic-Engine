#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

namespace Supersonic {

// Which of the eighteen depth passes still have to be recorded this frame.
//
// A shadow map is a function of the light's transform and the casters it can
// see. Render it twice from the same inputs and you get the same image twice,
// so a pass whose inputs have not changed since it was last recorded need not
// be recorded again - and in a scene where nothing is moving, which is most of
// the time an editor is open, that is every pass.
//
// Kept apart from the renderer because the decision is the part that can be
// wrong and the recording is not. A cache that never invalidates renders a
// perfectly plausible frame; it is simply the wrong one, and it looks right in
// a screenshot. So the rule lives here where it can be tested directly.
class ShadowCache {
public:
    // Every cascade, every cube face of every point slot, every spot slot.
    static constexpr std::size_t kMaxPasses = 32;

    // True when the pass must be recorded. Remembers the signature either way,
    // so the next frame is compared against what this one decided.
    //
    // The FIRST call for a pass is always true, whatever the signature. That is
    // not defensiveness: a shadow image is created in an undefined layout and
    // only reaches a readable one by being rendered through the pass, so a slot
    // that has never been recorded is not merely stale, it is unusable - and
    // since the cube maps are one descriptor array, one untouched slot makes
    // the whole array invalid. Tracked as its own flag rather than as a
    // reserved signature value, because there is no number a real signature
    // cannot take.
    bool NeedsRender(std::size_t pass, uint64_t signature) {
        if (pass >= kMaxPasses) return true;

        if (!m_seen[pass] || m_signatures[pass] != signature) {
            m_seen[pass] = true;
            m_signatures[pass] = signature;
            return true;
        }
        return false;
    }

    // Forgets everything, so every pass is recorded again.
    //
    // For anything that invalidates the images themselves rather than their
    // contents - a resize, a device loss, a shadow map rebuilt at a new
    // resolution. Those give the passes new images in an undefined layout again,
    // and a cache that remembered the old signatures would skip straight past
    // them.
    void Invalidate() {
        m_seen.fill(false);
        m_signatures.fill(0);
    }

private:
    std::array<uint64_t, kMaxPasses> m_signatures{};
    std::array<bool, kMaxPasses> m_seen{};
};

} // namespace Supersonic
