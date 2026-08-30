#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace Supersonic {

// Decoded PCM audio.
//
// The engine previously linked no audio library of any kind, so there was
// nothing to decode into and nothing to play. This is a deliberately small
// uncompressed-WAV reader: enough to make AudioSourceComponent mean something
// without pulling in a codec dependency.
struct AudioClip {
    uint16_t channels{0};
    uint32_t sampleRate{0};
    uint16_t bitsPerSample{0};
    std::vector<uint8_t> pcm;   // interleaved, little-endian, as stored

    // BIT DEPTH INCLUDED, and it was not. A clip with samples, a rate and
    // channels but no bitsPerSample passed this and was cached by AddClip -
    // and then never made a sound, because the backend computes its block
    // alignment as channels * (bitsPerSample / 8) and gets zero, which
    // CreateSourceVoice rejects.
    //
    // Nothing on the LoadWav path could reach it: a decoder that got as far as
    // samples had already read the format chunk. It is reachable only by a game
    // that SYNTHESISES its audio and forgets one field - which is the whole
    // reason AddClip exists, and a silent sound is the hardest kind of bug to
    // find. Refused at the door instead.
    bool valid() const {
        return channels > 0 && sampleRate > 0 && bitsPerSample > 0 && !pcm.empty();
    }

    float durationSeconds() const {
        if (!valid() || bitsPerSample == 0) return 0.0f;
        const uint32_t bytesPerFrame = channels * (bitsPerSample / 8u);
        if (bytesPerFrame == 0) return 0.0f;
        return static_cast<float>(pcm.size() / bytesPerFrame) / static_cast<float>(sampleRate);
    }

    // Parses a RIFF/WAVE file containing uncompressed PCM or IEEE float.
    // Returns false and leaves the clip empty on anything it does not support,
    // rather than guessing.
    static bool LoadWav(const std::string& path, AudioClip& out, std::string& error);
};

} // namespace Supersonic
