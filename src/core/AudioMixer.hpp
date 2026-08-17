#pragma once

#include <cstdint>
#include <mutex>
#include <unordered_map>
#include <vector>

#include "core/AudioClip.hpp"

namespace Supersonic {

// Software mixer: many voices in, one interleaved stereo stream out.
//
// XAudio2 does this itself - you hand it a source voice per sound and it mixes
// them onto the master. ALSA and CoreAudio do not: they give you one output
// stream and expect you to have already summed everything into it. So the
// non-Windows backends need a mixer, and there is no reason for each of them to
// have its own. This is that mixer, with no platform header in sight, which is
// also what makes it testable without an audio device.
//
// Semantics deliberately match the XAudio2 path so a sound behaves the same on
// every platform: volume clamped to [0, 1], pitch as a frequency ratio clamped
// to [0.5, 2], and constant-power panning.
class AudioMixer {
public:
    using VoiceId = uint32_t;
    static constexpr VoiceId kInvalidVoice = 0xFFFFFFFFu;

    AudioMixer(uint32_t outputSampleRate, uint16_t outputChannels);

    // The clip must outlive the voice. AudioEngine owns its clips in a map
    // whose nodes are address-stable, which is what makes that safe to promise
    // while the audio thread is reading.
    VoiceId Add(const AudioClip& clip, bool loop, float volume, float pitch);

    void Remove(VoiceId voice);
    void SetParameters(VoiceId voice, float volume, float pitch, float pan);

    // False once a non-looping voice has run past its last frame. Matches
    // XAudio2's BuffersQueued check, which is what AudioSystem uses to notice a
    // one-shot has finished.
    bool IsPlaying(VoiceId voice) const;

    size_t VoiceCount() const;

    // Writes `frames` interleaved output frames, overwriting whatever was in
    // the buffer. Called from the audio thread; every other method may be
    // called from the game thread at the same time.
    //
    // Output is clamped to [-1, 1]. Summing several voices can exceed that, and
    // wrapping instead of clamping turns a loud moment into a burst of noise.
    void Mix(float* out, size_t frames);

    // Same, as 16-bit signed - what most ALSA and CoreAudio configurations
    // want, and doing the conversion here keeps it out of both backends.
    void MixInt16(int16_t* out, size_t frames);

private:
    struct Voice {
        const AudioClip* clip{nullptr};

        // In source frames. Fractional because pitch and sample-rate conversion
        // are the same operation: step through the source faster or slower and
        // interpolate. An integer cursor would quantise every pitch to a ratio
        // the source rate happens to allow.
        double cursor{0.0};
        double step{1.0};

        float volume{1.0f};
        float pan{0.0f};
        bool loop{false};
        bool finished{false};
    };

    // Reads one channel of one source frame as a normalised float, whatever the
    // clip's storage format is.
    static float sampleAt(const AudioClip& clip, size_t frame, uint16_t channel);

    // Linear interpolation between the two frames the cursor sits between.
    static float sampleInterpolated(const AudioClip& clip, double cursor, uint16_t channel);

    double stepFor(const AudioClip& clip, float pitch) const;

    uint32_t m_outputSampleRate;
    uint16_t m_outputChannels;

    mutable std::mutex m_mutex;
    std::unordered_map<VoiceId, Voice> m_voices;
    VoiceId m_nextId{1};

    std::vector<float> m_scratch;
};

} // namespace Supersonic
