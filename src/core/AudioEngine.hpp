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

    // Registers a clip built IN MEMORY, under a name rather than a path.
    //
    // The whole audio path is keyed by string and LoadClip consults that cache
    // before it touches the filesystem, so a clip registered here plays through
    // Play() exactly as a file does - the name simply never reaches a loader.
    // Give it a shape a real path cannot collide with; the engine does not
    // reserve a prefix and will happily let a game shadow a file.
    //
    // THIS EXISTS BECAUSE A GAME MAY GENERATE ITS SOUND. Wolf Brigade ships no
    // audio files at all: every effect is a `tone` block in a data file that the
    // game synthesises at load. The port reproduces those buffers sample for
    // sample and, until this door, had no way to make a single one audible -
    // every entrance to the mixer took a path.
    //
    // REFUSES TO REPLACE A CLIP THAT HAS A LIVE VOICE, returning nullptr and
    // changing nothing. A voice reads the sample buffer directly, so assigning
    // over it frees memory an audio thread is mid-read of - the same
    // use-after-free UnloadClip's comment describes, and re-registering a
    // re-synthesised tone is exactly when somebody would hit it. Call
    // StopVoicesUsing(name) first, as you would before unloading.
    //
    // An invalid clip is refused rather than cached, which is the opposite of
    // what LoadClip does for a missing file. A file that will not decode is
    // worth remembering so it is not re-opened every frame; a caller handing
    // over an empty buffer has a bug this frame and should be told now.
    const AudioClip* AddClip(const std::string& name, AudioClip clip);

    // Whether a name resolves without touching the filesystem.
    bool HasClip(const std::string& name) const;

    // Starts a voice for a clip. Returns kInvalidVoice on failure.
    VoiceId Play(const std::string& path, bool loop, float volume, float pitch);

    void Stop(VoiceId voice);
    void SetVoiceParameters(VoiceId voice, float volume, float pitch, float pan);
    bool IsVoicePlaying(VoiceId voice) const;

    // What a voice is actually reading, or empty for one that is not playing.
    //
    // Not the same question as "what does its source name", and the difference
    // is the whole reason this map exists: a component's soundFile can be
    // changed while its voice goes on reading the file it was started from.
    const std::string& PathOf(VoiceId voice) const;

    // Stops every voice reading this clip and returns their ids, so whoever is
    // holding those handles can forget them.
    //
    // This is the ONLY safe way to make a clip droppable. A voice reads the
    // clip's sample buffer directly - XAudio2 was handed clip->pcm.data() and
    // reads it from its own thread until the buffer ends, and the software
    // mixer keeps a const AudioClip* whose contract is written down as "the
    // clip must outlive the voice". Both mean an erase while something is
    // playing is a read of freed memory on an audio thread, which is a crash
    // with no stack anyone can read.
    std::vector<VoiceId> StopVoicesUsing(const std::string& path);

    // Drops a cached clip so the next Play re-reads it from disk.
    //
    // ONLY safe once StopVoicesUsing has returned for the same path. Erase is
    // deliberate rather than assigning a freshly-loaded clip over the old one:
    // assignment frees the sample buffer just the same, so it would be the same
    // use-after-free with the danger hidden one level further down.
    //
    // It also clears a cached FAILURE, which is the point of doing this at all
    // for a file somebody has just fixed.
    bool UnloadClip(const std::string& path);

private:
    struct Impl;

    // Per-backend. Play and Stop above wrap these so the voice-to-path map is
    // maintained in ONE place - a backend that has to remember to do it itself
    // is a backend that will forget, and the symptom is a use-after-free on an
    // audio thread rather than anything that points at the omission.
    VoiceId playImpl(const std::string& path, bool loop, float volume, float pitch);
    void stopImpl(VoiceId voice);

    // What each live voice is reading. Keyed by voice rather than by path
    // because one clip can have many voices, and because the component that
    // started a voice may since have been pointed at a different file - its
    // soundFile is not evidence of what the voice is actually reading.
    std::unordered_map<VoiceId, std::string> m_voicePaths;
    std::string m_noPath;

    bool m_available{false};
    std::string m_status{"not initialised"};
    std::unordered_map<std::string, AudioClip> m_clips;
    std::unique_ptr<Impl> m_impl;
};

} // namespace Supersonic
