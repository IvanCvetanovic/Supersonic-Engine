#include "core/AudioClip.hpp"

#include <cstring>
#include <fstream>

namespace Engine {

namespace {

constexpr uint16_t kFormatPcm = 0x0001;
constexpr uint16_t kFormatFloat = 0x0003;
constexpr uint16_t kFormatExtensible = 0xFFFE;

uint16_t readU16(const uint8_t* p) {
    return static_cast<uint16_t>(p[0] | (p[1] << 8));
}

uint32_t readU32(const uint8_t* p) {
    return static_cast<uint32_t>(p[0]) | (static_cast<uint32_t>(p[1]) << 8)
         | (static_cast<uint32_t>(p[2]) << 16) | (static_cast<uint32_t>(p[3]) << 24);
}

} // namespace

bool AudioClip::LoadWav(const std::string& path, AudioClip& out, std::string& error) {
    out = AudioClip{};

    std::ifstream file(path, std::ios::binary | std::ios::ate);
    if (!file.is_open()) {
        error = "cannot open " + path;
        return false;
    }

    const auto size = static_cast<size_t>(file.tellg());
    if (size < 44) {
        error = path + " is too small to be a WAV file";
        return false;
    }

    std::vector<uint8_t> bytes(size);
    file.seekg(0);
    file.read(reinterpret_cast<char*>(bytes.data()), static_cast<std::streamsize>(size));
    if (!file) {
        error = "read error on " + path;
        return false;
    }

    if (std::memcmp(bytes.data(), "RIFF", 4) != 0 || std::memcmp(bytes.data() + 8, "WAVE", 4) != 0) {
        error = path + " is not a RIFF/WAVE file";
        return false;
    }

    // Walk the chunk list rather than assuming the canonical 44-byte layout:
    // real files routinely carry LIST/fact chunks before the data.
    size_t offset = 12;
    bool haveFormat = false;
    uint16_t formatTag = 0;

    while (offset + 8 <= size) {
        const char* id = reinterpret_cast<const char*>(bytes.data() + offset);
        const uint32_t chunkSize = readU32(bytes.data() + offset + 4);
        const size_t body = offset + 8;

        if (body + chunkSize > size) break; // truncated chunk; stop cleanly

        if (std::memcmp(id, "fmt ", 4) == 0 && chunkSize >= 16) {
            const uint8_t* f = bytes.data() + body;
            formatTag = readU16(f);
            out.channels = readU16(f + 2);
            out.sampleRate = readU32(f + 4);
            out.bitsPerSample = readU16(f + 14);

            if (formatTag == kFormatExtensible && chunkSize >= 40) {
                // The real format lives in the sub-format GUID's first two bytes.
                formatTag = readU16(f + 24);
            }
            haveFormat = true;
        } else if (std::memcmp(id, "data", 4) == 0) {
            out.pcm.assign(bytes.begin() + static_cast<std::ptrdiff_t>(body),
                           bytes.begin() + static_cast<std::ptrdiff_t>(body + chunkSize));
        }

        offset = body + chunkSize + (chunkSize & 1u); // chunks are word-aligned
    }

    if (!haveFormat) {
        error = path + " has no fmt chunk";
        return false;
    }
    if (formatTag != kFormatPcm && formatTag != kFormatFloat) {
        error = path + " uses unsupported compressed format tag " + std::to_string(formatTag);
        return false;
    }
    if (out.channels == 0 || out.channels > 2) {
        error = path + " has unsupported channel count " + std::to_string(out.channels);
        return false;
    }
    if (out.bitsPerSample != 8 && out.bitsPerSample != 16 && out.bitsPerSample != 32) {
        error = path + " has unsupported bit depth " + std::to_string(out.bitsPerSample);
        return false;
    }
    if (out.pcm.empty()) {
        error = path + " has no data chunk";
        return false;
    }

    return true;
}

} // namespace Engine
