#include "core/AudioMixer.hpp"

#include <algorithm>
#include <cmath>
#include <cstring>

namespace Supersonic {

namespace {

constexpr float kMinPitch = 0.5f;
constexpr float kMaxPitch = 2.0f;

size_t frameCount(const AudioClip& clip) {
    const uint32_t bytesPerFrame = clip.channels * (clip.bitsPerSample / 8u);
    if (bytesPerFrame == 0) return 0;
    return clip.pcm.size() / bytesPerFrame;
}

} // namespace

AudioMixer::AudioMixer(uint32_t outputSampleRate, uint16_t outputChannels)
    : m_outputSampleRate(outputSampleRate ? outputSampleRate : 48000u),
      m_outputChannels(outputChannels ? outputChannels : uint16_t{2}) {}

float AudioMixer::sampleAt(const AudioClip& clip, size_t frame, uint16_t channel) {
    const uint16_t bytesPerSample = clip.bitsPerSample / 8u;
    if (bytesPerSample == 0 || clip.channels == 0) return 0.0f;

    // A mono clip asked for its right channel returns its only channel, which
    // is what lets the mix loop treat every clip as if it had as many channels
    // as the output.
    const uint16_t sourceChannel = channel < clip.channels ? channel : uint16_t{0};

    const size_t offset = (frame * clip.channels + sourceChannel) * bytesPerSample;
    if (offset + bytesPerSample > clip.pcm.size()) return 0.0f;

    const uint8_t* p = clip.pcm.data() + offset;

    switch (clip.bitsPerSample) {
    case 8: {
        // 8-bit WAV is unsigned with 128 as silence, unlike every other depth.
        return (static_cast<float>(*p) - 128.0f) / 128.0f;
    }
    case 16: {
        int16_t v = 0;
        std::memcpy(&v, p, sizeof(v));
        return static_cast<float>(v) / 32768.0f;
    }
    case 24: {
        // Sign-extend the top byte into a 32-bit value.
        const int32_t v = (static_cast<int32_t>(static_cast<int8_t>(p[2])) << 16) |
                          (static_cast<int32_t>(p[1]) << 8) |
                           static_cast<int32_t>(p[0]);
        return static_cast<float>(v) / 8388608.0f;
    }
    case 32: {
        // LoadWav only accepts 32-bit as IEEE float, so this is not an int.
        float v = 0.0f;
        std::memcpy(&v, p, sizeof(v));
        return v;
    }
    default:
        return 0.0f;
    }
}

float AudioMixer::sampleInterpolated(const AudioClip& clip, double cursor, uint16_t channel) {
    const size_t frames = frameCount(clip);
    if (frames == 0) return 0.0f;

    const double floored = std::floor(cursor);
    const auto index = static_cast<size_t>(floored < 0.0 ? 0.0 : floored);
    if (index >= frames) return 0.0f;

    const float a = sampleAt(clip, index, channel);
    // The last frame has nothing to interpolate toward. Holding it is right for
    // a one-shot about to end; for a loop the wrap is handled by the caller
    // resetting the cursor, so the discontinuity here is one frame at most.
    const float b = (index + 1 < frames) ? sampleAt(clip, index + 1, channel) : a;

    const auto t = static_cast<float>(cursor - floored);
    return a + (b - a) * t;
}

double AudioMixer::stepFor(const AudioClip& clip, float pitch) const {
    const double rateRatio = static_cast<double>(clip.sampleRate) /
                             static_cast<double>(m_outputSampleRate);
    return rateRatio * static_cast<double>(std::clamp(pitch, kMinPitch, kMaxPitch));
}

AudioMixer::VoiceId AudioMixer::Add(const AudioClip& clip, bool loop, float volume, float pitch) {
    if (!clip.valid() || frameCount(clip) == 0) return kInvalidVoice;

    Voice voice;
    voice.clip = &clip;
    voice.cursor = 0.0;
    voice.step = stepFor(clip, pitch);
    voice.volume = std::clamp(volume, 0.0f, 1.0f);
    voice.loop = loop;

    std::lock_guard<std::mutex> lock(m_mutex);
    const VoiceId id = m_nextId++;
    m_voices.emplace(id, voice);
    return id;
}

void AudioMixer::Remove(VoiceId voice) {
    std::lock_guard<std::mutex> lock(m_mutex);
    m_voices.erase(voice);
}

void AudioMixer::SetParameters(VoiceId voice, float volume, float pitch, float pan) {
    std::lock_guard<std::mutex> lock(m_mutex);
    auto it = m_voices.find(voice);
    if (it == m_voices.end() || !it->second.clip) return;

    it->second.volume = std::clamp(volume, 0.0f, 1.0f);
    it->second.pan = std::clamp(pan, -1.0f, 1.0f);
    // Only the rate changes; the cursor stays where it is, so a pitch change
    // mid-playback bends the sound rather than restarting it.
    it->second.step = stepFor(*it->second.clip, pitch);
}

bool AudioMixer::IsPlaying(VoiceId voice) const {
    std::lock_guard<std::mutex> lock(m_mutex);
    auto it = m_voices.find(voice);
    if (it == m_voices.end()) return false;
    return !it->second.finished;
}

size_t AudioMixer::VoiceCount() const {
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_voices.size();
}

void AudioMixer::Mix(float* out, size_t frames) {
    if (!out || frames == 0) return;

    const size_t samples = frames * m_outputChannels;
    std::fill(out, out + samples, 0.0f);

    std::lock_guard<std::mutex> lock(m_mutex);
    if (m_voices.empty()) return;

    for (auto& [id, voice] : m_voices) {
        (void)id;
        if (!voice.clip || voice.finished) continue;

        const AudioClip& clip = *voice.clip;
        const size_t sourceFrames = frameCount(clip);
        if (sourceFrames == 0) { voice.finished = true; continue; }

        // Constant-power pan, matching the XAudio2 output matrix. Centre gives
        // both sides 1/sqrt(2), so panning does not change perceived loudness.
        const float left  = std::sqrt(0.5f * (1.0f - voice.pan));
        const float right = std::sqrt(0.5f * (1.0f + voice.pan));

        for (size_t frame = 0; frame < frames; ++frame) {
            if (voice.cursor >= static_cast<double>(sourceFrames)) {
                if (!voice.loop) { voice.finished = true; break; }
                // Wrap by the length rather than resetting to zero, so a loop
                // does not lose the fractional remainder and drift out of time
                // over a long session.
                voice.cursor = std::fmod(voice.cursor, static_cast<double>(sourceFrames));
            }

            for (uint16_t channel = 0; channel < m_outputChannels; ++channel) {
                const float sample = sampleInterpolated(clip, voice.cursor, channel);
                const float sided = (m_outputChannels >= 2)
                                  ? sample * (channel == 0 ? left : right)
                                  : sample;
                out[frame * m_outputChannels + channel] += sided * voice.volume;
            }

            voice.cursor += voice.step;
        }
    }

    // Clamp after summing, not per voice: two half-volume sounds must be able
    // to add up to full scale.
    //
    // A NaN is made silence first. std::clamp compares, and both of its comparisons
    // are false for NaN, so it hands NaN straight back - and a float WAV can hold
    // one. MixInt16 then took lround() of it, a domain error, and one bad sample
    // poisons everything summed with it.
    for (size_t i = 0; i < samples; ++i) {
        const float sample = out[i];
        out[i] = (sample != sample) ? 0.0f : std::clamp(sample, -1.0f, 1.0f);
    }
}

void AudioMixer::MixInt16(int16_t* out, size_t frames) {
    if (!out || frames == 0) return;

    const size_t samples = frames * m_outputChannels;
    if (m_scratch.size() < samples) m_scratch.resize(samples);
    Mix(m_scratch.data(), frames);

    for (size_t i = 0; i < samples; ++i) {
        // 32767 rather than 32768: +1.0 must not wrap to the most negative
        // sample, which is the loudest possible click.
        const float scaled = std::clamp(m_scratch[i], -1.0f, 1.0f) * 32767.0f;
        out[i] = static_cast<int16_t>(std::lround(scaled));
    }
}

} // namespace Supersonic
