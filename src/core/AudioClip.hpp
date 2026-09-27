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
//
// It has since grown two compressed formats, each decoded whole into the same
// PCM so nothing downstream learns a new format: MP3 through the platform, and
// Ogg Vorbis through stb_vorbis - the one vendored codec, public domain, a
// single file beside stb_image.
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

    // Decodes an MP3 to 16-bit PCM.
    //
    // ON WINDOWS ONLY, through Media Foundation, which ships with the system -
    // so this adds no third-party dependency, exactly as the XAudio2 backend
    // adds none. Everywhere else it fails with a reason saying so, which is the
    // same bargain AudioEngine already strikes for output: a real backend on
    // Windows, a documented no-op elsewhere.
    //
    // Why at all: the one game in the tree that has recorded audio is Magic
    // Portals, and the original ships 46 mp3s. Converting them would leave
    // derived copies of somebody else's assets lying about; decoding them where
    // they are does not.
    static bool LoadMp3(const std::string& path, AudioClip& out, std::string& error);

    // Decodes an Ogg Vorbis file to interleaved 16-bit PCM.
    //
    // Through stb_vorbis rather than the platform, so unlike LoadMp3 it works
    // the same on every platform the engine builds for and needs no system
    // decoder to be present. The price is the one vendored codec; it is a single
    // public-domain file, compiled in its own TU (StbVorbisImplementation.cpp).
    //
    // Why at all: Penumbra, the second Ethanon port on the engine, ships its
    // sound effects as .ogg, and decoding them where they are keeps the bargain
    // LoadMp3 keeps - no derived copies of somebody else's assets.
    //
    // The whole file is read and decoded here, on the calling thread; Vorbis
    // is real work, not a copy, so a game with long clips loads them before it
    // plays them. One or two channels, as LoadWav and LoadMp3 accept. What
    // stb_vorbis cannot read is refused with a reason: an Ogg stream that is
    // not Vorbis (Opus, FLAC), and floor-0 files from before 2004. A chained
    // file decodes only its first stream, and a file cut short part way
    // through the audio decodes as far as it goes - stb_vorbis reports the end
    // of the data, not why it ended.
    static bool LoadOgg(const std::string& path, AudioClip& out, std::string& error);

    // Decodes by the file's extension: .wav through LoadWav, .mp3 through
    // LoadMp3, .ogg through LoadOgg, anything else refused by name.
    // Case-insensitive.
    static bool Load(const std::string& path, AudioClip& out, std::string& error);
};

} // namespace Supersonic
