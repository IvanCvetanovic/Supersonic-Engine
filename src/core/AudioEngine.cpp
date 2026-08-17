#include "core/AudioEngine.hpp"

#include <algorithm>
#include <iostream>

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <xaudio2.h>
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
        std::cerr << "[AudioEngine] " << m_status << std::endl;
        return;
    }

    hr = m_impl->xaudio->CreateMasteringVoice(&m_impl->master);
    if (FAILED(hr)) {
        m_status = "CreateMasteringVoice failed (hr=" + std::to_string(static_cast<long>(hr)) + ")";
        std::cerr << "[AudioEngine] " << m_status << std::endl;
        return;
    }

    m_available = true;
    m_status = "XAudio2 output device ready";
    std::cout << "[AudioEngine] " << m_status << "." << std::endl;
}

AudioEngine::~AudioEngine() = default;

AudioEngine::VoiceId AudioEngine::Play(const std::string& path, bool loop, float volume, float pitch) {
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
        std::cerr << "[AudioEngine] CreateSourceVoice failed for " << path << std::endl;
        return kInvalidVoice;
    }

    XAUDIO2_BUFFER buffer{};
    buffer.AudioBytes = static_cast<UINT32>(clip->pcm.size());
    buffer.pAudioData = clip->pcm.data();
    buffer.Flags = XAUDIO2_END_OF_STREAM;
    buffer.LoopCount = loop ? XAUDIO2_LOOP_INFINITE : 0;

    if (FAILED(source->SubmitSourceBuffer(&buffer))) {
        source->DestroyVoice();
        std::cerr << "[AudioEngine] SubmitSourceBuffer failed for " << path << std::endl;
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

void AudioEngine::Stop(VoiceId voice) {
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

#else

// ---------------------------------------------------------------------------
// No backend on this platform yet. Explicitly a no-op rather than pretending.
// ---------------------------------------------------------------------------
struct AudioEngine::Impl {};

AudioEngine::AudioEngine() : m_impl(std::make_unique<Impl>()) {
    m_status = "no audio backend compiled for this platform";
    std::cout << "[AudioEngine] " << m_status << "." << std::endl;
}

AudioEngine::~AudioEngine() = default;

AudioEngine::VoiceId AudioEngine::Play(const std::string&, bool, float, float) { return kInvalidVoice; }
void AudioEngine::Stop(VoiceId) {}
void AudioEngine::SetVoiceParameters(VoiceId, float, float, float) {}
bool AudioEngine::IsVoicePlaying(VoiceId) const { return false; }

#endif

const AudioClip* AudioEngine::LoadClip(const std::string& path) {
    if (auto it = m_clips.find(path); it != m_clips.end()) {
        return it->second.valid() ? &it->second : nullptr;
    }

    AudioClip clip;
    std::string error;
    if (!AudioClip::LoadWav(path, clip, error)) {
        std::cerr << "[AudioEngine] " << error << std::endl;
        // Cache the failure so a missing file is not re-opened every frame.
        m_clips.emplace(path, AudioClip{});
        return nullptr;
    }

    std::cout << "[AudioEngine] Loaded " << path << " ("
              << clip.channels << "ch, " << clip.sampleRate << " Hz, "
              << clip.bitsPerSample << "-bit, "
              << clip.durationSeconds() << "s)." << std::endl;

    auto [it, inserted] = m_clips.emplace(path, std::move(clip));
    return &it->second;
}

} // namespace Supersonic
