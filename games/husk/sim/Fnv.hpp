#pragma once

// FNV-1a 64 (src/sim/hash.rs), the determinism oracle's hash. Little-endian
// byte order is part of it - Rust's to_le_bytes - so it is written out here
// rather than taken from the host's layout.

#include <cstdint>
#include <cstring>
#include <string_view>

namespace husk {

class Fnv1a {
public:
    void writeByte(uint8_t b) { m_h = (m_h ^ b) * 0x00000100000001b3ull; }

    void writeU32(uint32_t v) {
        for (int i = 0; i < 4; ++i) writeByte(static_cast<uint8_t>(v >> (8 * i)));
    }

    void writeU64(uint64_t v) {
        for (int i = 0; i < 8; ++i) writeByte(static_cast<uint8_t>(v >> (8 * i)));
    }

    // The exact bit pattern: equal hashes mean bit-identical state, not state
    // that is approximately equal.
    void writeF32(float v) {
        uint32_t bits;
        std::memcpy(&bits, &v, sizeof bits);
        writeU32(bits);
    }

    void writeBytes(std::string_view bytes) {
        for (char c : bytes) writeByte(static_cast<uint8_t>(c));
    }

    uint64_t finish() const { return m_h; }

private:
    uint64_t m_h = 0xcbf29ce484222325ull;
};

inline uint32_t floatBits(float v) {
    uint32_t bits;
    std::memcpy(&bits, &v, sizeof bits);
    return bits;
}

} // namespace husk
