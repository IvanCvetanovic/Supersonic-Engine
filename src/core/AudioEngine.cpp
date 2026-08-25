#include "core/AudioEngine.hpp"
#include "core/Log.hpp"

#include <algorithm>
#include <cmath>
#include <iostream>

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <xaudio2.h>
#elif defined(SUPERSONIC_AUDIO_ALSA)
#include <alsa/asoundlib.h>

#include <atomic>
#include <thread>
#include <vector>

#include "core/AudioMixer.hpp"
#endif

namespace Supersonic {

#if defined(_WIN32)

// ---------------------------------------------------------------------------
// XAudio2 backend. Ships with Windows, so this adds no third-party dependency.
// ---------------------------------------------------------------------------
struct AudioEngine::Impl {
    IXAudio2* xaudio{nullptr};
    IXAudio2MasteringVoice* master{nullptr};
    bool comInitialised{false};

    struct Voice {
        IXAudio2SourceVoice* source{nullptr};
        uint16_t channels{1};
    };
    std::unordered_map<VoiceId, Voice> voices;
    VoiceId nextId{1};

    ~Impl() {
        for (auto& [id, voice] : voices) {
            if (voice.source) {
                voice.source->Stop(0);
                voice.source->DestroyVoice();
            }
        }
        voices.clear();

        if (master) master->DestroyVoice();
        if (xaudio) xaudio->Release();
        if (comInitialised) CoUninitialize();
    }
};

AudioEngine::AudioEngine() : m_impl(std::make_unique<Impl>()) {
    const HRESULT co = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    // S_FALSE means COM was already initialised on this thread by someone else;
    // that is fine, but we must not balance it with CoUninitialize.
    m_impl->comInitialised = SUCCEEDED(co) && co != S_FALSE;

    HRESULT hr = XAudio2Create(&m_impl->xaudio, 0, XAUDIO2_DEFAULT_PROCESSOR);
    if (FAILED(hr)) {
        m_status = "XAudio2Create failed (hr=" + std::to_string(static_cast<long>(hr)) + ")";
        SUPERSONIC_LOG_ERROR("AudioEngine") << m_status << std::endl;
        return;
    }

    hr = m_impl->xaudio->CreateMasteringVoice(&m_impl->master);
    if (FAILED(hr)) {
        m_status = "CreateMasteringVoice failed (hr=" + std::to_string(static_cast<long>(hr)) + ")";
        SUPERSONIC_LOG_ERROR("AudioEngine") << m_status << std::endl;
        return;
    }

    m_available = true;
    m_status = "XAudio2 output device ready";
    SUPERSONIC_LOG_INFO("AudioEngine") << m_status << "." << std::endl;
}

AudioEngine::~AudioEngine() = default;

AudioEngine::VoiceId AudioEngine::playImpl(const std::string& path, bool loop, float volume, float pitch) {
    if (!m_available) return kInvalidVoice;

    const AudioClip* clip = LoadClip(path);
    if (!clip) return kInvalidVoice;

    WAVEFORMATEX format{};
    format.wFormatTag = clip->bitsPerSample == 32 ? WAVE_FORMAT_IEEE_FLOAT : WAVE_FORMAT_PCM;
    format.nChannels = clip->channels;
    format.nSamplesPerSec = clip->sampleRate;
    format.wBitsPerSample = clip->bitsPerSample;
    format.nBlockAlign = static_cast<WORD>(clip->channels * (clip->bitsPerSample / 8));
    format.nAvgBytesPerSec = clip->sampleRate * format.nBlockAlign;
    format.cbSize = 0;

    IXAudio2SourceVoice* source = nullptr;
    if (FAILED(m_impl->xaudio->CreateSourceVoice(&source, &format))) {
        SUPERSONIC_LOG_ERROR("AudioEngine") << "CreateSourceVoice failed for " << path << std::endl;
        return kInvalidVoice;
    }

    XAUDIO2_BUFFER buffer{};
    buffer.AudioBytes = static_cast<UINT32>(clip->pcm.size());
    buffer.pAudioData = clip->pcm.data();
    buffer.Flags = XAUDIO2_END_OF_STREAM;
    buffer.LoopCount = loop ? XAUDIO2_LOOP_INFINITE : 0;

    if (FAILED(source->SubmitSourceBuffer(&buffer))) {
        source->DestroyVoice();
        SUPERSONIC_LOG_ERROR("AudioEngine") << "SubmitSourceBuffer failed for " << path << std::endl;
        return kInvalidVoice;
    }

    source->SetVolume(std::clamp(volume, 0.0f, 1.0f));
    source->SetFrequencyRatio(std::clamp(pitch, 0.5f, 2.0f));

    if (FAILED(source->Start(0))) {
        source->DestroyVoice();
        return kInvalidVoice;
    }

    const VoiceId id = m_impl->nextId++;
    m_impl->voices[id] = Impl::Voice{ source, clip->channels };
    return id;
}

void AudioEngine::stopImpl(VoiceId voice) {
    if (!m_available) return;
    auto it = m_impl->voices.find(voice);
    if (it == m_impl->voices.end()) return;

    if (it->second.source) {
        it->second.source->Stop(0);
        it->second.source->FlushSourceBuffers();
        it->second.source->DestroyVoice();
    }
    m_impl->voices.erase(it);
}

void AudioEngine::SetVoiceParameters(VoiceId voice, float volume, float pitch, float pan) {
    if (!m_available) return;
    auto it = m_impl->voices.find(voice);
    if (it == m_impl->voices.end() || !it->second.source) return;

    it->second.source->SetVolume(std::clamp(volume, 0.0f, 1.0f));
    it->second.source->SetFrequencyRatio(std::clamp(pitch, 0.5f, 2.0f));

    // Constant-power pan across a stereo master. Mono sources feed both output
    // channels; stereo sources are attenuated per side.
    XAUDIO2_VOICE_DETAILS masterDetails{};
    m_impl->master->GetVoiceDetails(&masterDetails);
    if (masterDetails.InputChannels < 2) return;

    const float clamped = std::clamp(pan, -1.0f, 1.0f);
    const float left = std::sqrt(0.5f * (1.0f - clamped));
    const float right = std::sqrt(0.5f * (1.0f + clamped));

    if (it->second.channels == 1) {
        float matrix[2] = { left, right };
        it->second.source->SetOutputMatrix(nullptr, 1, 2, matrix);
    } else {
        // 2 in, 2 out: keep the stereo image but weight each side.
        float matrix[4] = { left, 0.0f, 0.0f, right };
        it->second.source->SetOutputMatrix(nullptr, 2, 2, matrix);
    }
}

bool AudioEngine::IsVoicePlaying(VoiceId voice) const {
    if (!m_available) return false;
    auto it = m_impl->voices.find(voice);
    if (it == m_impl->voices.end() || !it->second.source) return false;

    XAUDIO2_VOICE_STATE state{};
    it->second.source->GetState(&state);
    return state.BuffersQueued > 0;
}

#elif defined(SUPERSONIC_AUDIO_ALSA)

// ---------------------------------------------------------------------------
// ALSA backend. Linux had no audio at all: the non-Windows path was an
// explicit no-op, so every AudioSourceComponent in a scene was silent and
// nothing said why beyond one line at startup.
//
// ALSA gives one output stream rather than XAudio2's per-sound source voices,
// so the summing that XAudio2 does internally happens in AudioMixer here. A
// dedicated thread fills the device; snd_pcm_writei blocks until the buffer
// has room, which is what paces the loop without a timer.
// ---------------------------------------------------------------------------
namespace {
constexpr uint32_t kOutputRate = 48000;
constexpr uint16_t kOutputChannels = 2;

// ~21 ms. Small enough that a sound starts when it is meant to, large enough
// that an ordinary desktop scheduler will not underrun between wakeups.
constexpr snd_pcm_uframes_t kPeriodFrames = 1024;
} // namespace

struct AudioEngine::Impl {
    snd_pcm_t* pcm{nullptr};
    AudioMixer mixer{kOutputRate, kOutputChannels};
    std::thread thread;
    std::atomic<bool> running{false};

    void run() {
        std::vector<int16_t> buffer(kPeriodFrames * kOutputChannels);

        while (running.load(std::memory_order_relaxed)) {
            mixer.MixInt16(buffer.data(), kPeriodFrames);

            snd_pcm_sframes_t written = snd_pcm_writei(pcm, buffer.data(), kPeriodFrames);
            if (written < 0) {
                // An underrun is normal under load and is recoverable; anything
                // it cannot recover from means the device is gone, and spinning
                // on a dead device would peg a core forever.
                written = snd_pcm_recover(pcm, static_cast<int>(written), 1);
                if (written < 0) {
                    SUPERSONIC_LOG_ERROR("AudioEngine") << "ALSA write failed: "
                              << snd_strerror(static_cast<int>(written)) << std::endl;
                    break;
                }
            }
        }
    }
};

AudioEngine::AudioEngine() : m_impl(std::make_unique<Impl>()) {
    int err = snd_pcm_open(&m_impl->pcm, "default", SND_PCM_STREAM_PLAYBACK, 0);
    if (err < 0) {
        m_status = std::string("snd_pcm_open failed: ") + snd_strerror(err);
        SUPERSONIC_LOG_INFO("AudioEngine") << m_status << "." << std::endl;
        return;
    }

    // The high-level parameter call rather than the hw_params dance: it picks a
    // sane buffer size and will resample if the device cannot do 48 kHz, which
    // is the difference between "works on this machine" and "works".
    err = snd_pcm_set_params(m_impl->pcm,
                             SND_PCM_FORMAT_S16_LE,
                             SND_PCM_ACCESS_RW_INTERLEAVED,
                             kOutputChannels,
                             kOutputRate,
                             1,        /* allow the driver to resample */
                             100000);  /* 100 ms of latency to play with */
    if (err < 0) {
        m_status = std::string("snd_pcm_set_params failed: ") + snd_strerror(err);
        SUPERSONIC_LOG_INFO("AudioEngine") << m_status << "." << std::endl;
        snd_pcm_close(m_impl->pcm);
        m_impl->pcm = nullptr;
        return;
    }

    m_impl->running.store(true, std::memory_order_relaxed);
    m_impl->thread = std::thread([this] { m_impl->run(); });

    m_available = true;
    m_status = "ALSA output device ready";
    SUPERSONIC_LOG_INFO("AudioEngine") << m_status << "." << std::endl;
}

AudioEngine::~AudioEngine() {
    // Stop the thread before closing the device: snd_pcm_close while a write is
    // in flight is a use-after-free in the driver, not a tidy shutdown.
    m_impl->running.store(false, std::memory_order_relaxed);
    if (m_impl->thread.joinable()) m_impl->thread.join();

    if (m_impl->pcm) {
        snd_pcm_drain(m_impl->pcm);
        snd_pcm_close(m_impl->pcm);
    }
}

AudioEngine::VoiceId AudioEngine::playImpl(const std::string& path, bool loop, float volume, float pitch) {
    if (!m_available) return kInvalidVoice;

    const AudioClip* clip = LoadClip(path);
    if (!clip) return kInvalidVoice;

    // m_clips is an unordered_map, whose nodes keep their addresses when other
    // entries are inserted, so this pointer stays valid for the audio thread
    // even as more clips are loaded.
    return m_impl->mixer.Add(*clip, loop, volume, pitch);
}

void AudioEngine::stopImpl(VoiceId voice) {
    if (!m_available) return;
    m_impl->mixer.Remove(voice);
}

void AudioEngine::SetVoiceParameters(VoiceId voice, float volume, float pitch, float pan) {
    if (!m_available) return;
    m_impl->mixer.SetParameters(voice, volume, pitch, pan);
}

bool AudioEngine::IsVoicePlaying(VoiceId voice) const {
    if (!m_available) return false;
    return m_impl->mixer.IsPlaying(voice);
}

#else

// ---------------------------------------------------------------------------
// No backend on this platform yet. Explicitly a no-op rather than pretending.
// ---------------------------------------------------------------------------
struct AudioEngine::Impl {};

AudioEngine::AudioEngine() : m_impl(std::make_unique<Impl>()) {
    m_status = "no audio backend compiled for this platform";
    SUPERSONIC_LOG_INFO("AudioEngine") << m_status << "." << std::endl;
}

AudioEngine::~AudioEngine() = default;

AudioEngine::VoiceId AudioEngine::playImpl(const std::string&, bool, float, float) { return kInvalidVoice; }
void AudioEngine::stopImpl(VoiceId) {}
void AudioEngine::SetVoiceParameters(VoiceId, float, float, float) {}
bool AudioEngine::IsVoicePlaying(VoiceId) const { return false; }

#endif

// --- Shared across every backend ---------------------------------------------

AudioEngine::VoiceId AudioEngine::Play(const std::string& path, bool loop, float volume, float pitch) {
    const VoiceId id = playImpl(path, loop, volume, pitch);
    if (id != kInvalidVoice) m_voicePaths.emplace(id, path);
    return id;
}

void AudioEngine::Stop(VoiceId voice) {
    stopImpl(voice);
    m_voicePaths.erase(voice);
}

const std::string& AudioEngine::PathOf(VoiceId voice) const {
    const auto it = m_voicePaths.find(voice);
    return it == m_voicePaths.end() ? m_noPath : it->second;
}

std::vector<AudioEngine::VoiceId> AudioEngine::StopVoicesUsing(const std::string& path) {
    std::vector<VoiceId> stopped;
    for (const auto& [voice, playing] : m_voicePaths) {
        if (playing == path) stopped.push_back(voice);
    }
    // Collected first: Stop erases from the map being read.
    for (const VoiceId voice : stopped) Stop(voice);
    return stopped;
}

bool AudioEngine::UnloadClip(const std::string& path) {
    return m_clips.erase(path) > 0;
}

const AudioClip* AudioEngine::LoadClip(const std::string& path) {
    if (auto it = m_clips.find(path); it != m_clips.end()) {
        return it->second.valid() ? &it->second : nullptr;
    }

    AudioClip clip;
    std::string error;
    if (!AudioClip::LoadWav(path, clip, error)) {
        SUPERSONIC_LOG_ERROR("AudioEngine") << error << std::endl;
        // Cache the failure so a missing file is not re-opened every frame.
        m_clips.emplace(path, AudioClip{});
        return nullptr;
    }

    SUPERSONIC_LOG_INFO("AudioEngine") << "Loaded " << path << " ("
              << clip.channels << "ch, " << clip.sampleRate << " Hz, "
              << clip.bitsPerSample << "-bit, "
              << clip.durationSeconds() << "s)." << std::endl;

    auto [it, inserted] = m_clips.emplace(path, std::move(clip));
    return &it->second;
}

} // namespace Supersonic
