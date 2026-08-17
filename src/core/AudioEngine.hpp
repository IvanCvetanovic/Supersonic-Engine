#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

#include "core/AudioClip.hpp"

namespace Supersonic {

// Minimal audio output device.
//
// AudioSystem used to compute a spatial volume and then discard it with
// (void)attenuatedVolume, and CMakeLists linked no audio library at all, so no
// sound was possible under any configuration. This provides a real backend
// (XAudio2 on Windows) behind an interface that degrades to a documented no-op
// elsewhere, so the rest of the engine does not need platform branches.
class AudioEngine {
public:
    // Opaque handle to a playing voice.
    using VoiceId = uint32_t;
    static constexpr VoiceId kInvalidVoice = 0xFFFFFFFFu;

    AudioEngine();
    ~AudioEngine();

    AudioEngine(const AudioEngine&) = delete;
    AudioEngine& operator=(const AudioEngine&) = delete;

    // True when a real output device was opened. When false every call below is
    // a safe no-op and the reason is available from GetStatus().
    bool IsAvailable() const { return m_available; }
    const std::string& GetStatus() const { return m_status; }

    // Loads and caches a clip. Returns nullptr if it could not be decoded.
    const AudioClip* LoadClip(const std::string& path);

    // Starts a voice for a clip. Returns kInvalidVoice on failure.
    VoiceId Play(const std::string& path, bool loop, float volume, float pitch);

    void Stop(VoiceId voice);
    void SetVoiceParameters(VoiceId voice, float volume, float pitch, float pan);
    bool IsVoicePlaying(VoiceId voice) const;

private:
    struct Impl;

    bool m_available{false};
    std::string m_status{"not initialised"};
    std::unordered_map<std::string, AudioClip> m_clips;
    std::unique_ptr<Impl> m_impl;
};

} // namespace Supersonic
