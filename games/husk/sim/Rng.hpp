#pragma once

// HUSK's PCG32 (src/sim/rng.rs), the sim's only source of randomness. The game
// owns its generator so that no library update can change a replay, and the
// port inherits that by copying it exactly: wrapping arithmetic, the masked
// rotate, and a 24-bit float that is exact by construction.

#include <cstdint>
#include <utility>

namespace husk {

class Pcg32 {
public:
    Pcg32() = default;

    Pcg32(uint64_t seed, uint64_t seq) : m_inc((seq << 1) | 1u) {
        nextU32();
        m_state += seed;
        nextU32();
    }

    static Pcg32 fromState(uint64_t state, uint64_t inc) {
        Pcg32 r;
        r.m_state = state;
        r.m_inc = inc;
        return r;
    }

    std::pair<uint64_t, uint64_t> saveState() const { return {m_state, m_inc}; }

    uint32_t nextU32() {
        const uint64_t old = m_state;
        m_state = old * 6364136223846793005ull + m_inc;
        const uint32_t xorshifted = static_cast<uint32_t>(((old >> 18) ^ old) >> 27);
        const uint32_t rot = static_cast<uint32_t>(old >> 59);
        // Masked: a shift by 32 is undefined in C++ even though rot is 0..31.
        return (xorshifted >> rot) | (xorshifted << ((32u - rot) & 31u));
    }

    // Uniform in [0, 1): 24 bits, so the conversion and the scale are exact.
    float nextF32() { return static_cast<float>(nextU32() >> 8) * (1.0f / 16777216.0f); }

    float rangeF32(float lo, float hi) { return lo + nextF32() * (hi - lo); }

private:
    uint64_t m_state = 0;
    uint64_t m_inc = 0;
};

// SimRng::new: the game's fixed stream selector.
inline Pcg32 makeSimRng(uint64_t seed) { return Pcg32(seed, 0xda3e39cb94b95bdbull); }

} // namespace husk
