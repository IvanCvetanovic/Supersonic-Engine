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
#elif defined(SUPERSONIC_AUDIO_AAUDIO)
#include <aaudio/AAudio.h>

#include <atomic>
#include <mutex>
#include <thread>

#include "core/AudioMixer.hpp"
#include "platform/android/AndroidApp.hpp"
#elif defined(SUPERSONIC_AUDIO_COREAUDIO)
// AssertMacros.h, which Apple's framework headers can reach, otherwise defines
// check(), verify() and require() as macros for every line after it.
#define __ASSERT_MACROS_DEFINE_VERSIONS_WITHOUT_UNDERSCORES 0
#include <AudioToolbox/AudioToolbox.h>

#include <atomic>
#include <cstdint>
#include <mutex>
#include <string>

#include "core/AudioMixer.hpp"
#if defined(SUPERSONIC_PLATFORM_IOS)
#include "platform/ios/IOSApp.hpp"
#endif
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

#elif defined(SUPERSONIC_AUDIO_AAUDIO)

// ---------------------------------------------------------------------------
// AAudio backend, Android's own output API from API 26 (the minimum the
// Android build targets). The ALSA path's shape with the thread turned
// inside out: AudioMixer does the summing, and instead of an engine-owned
// thread writing into the device, AAudio calls back from a thread it owns
// whenever the device wants frames.
//
// Two things a desktop device never does happen here as a matter of course,
// and both are handled on the engine's side of the stream:
//  - THE APP GOES TO THE BACKGROUND. The stream is paused with the activity
//    (AndroidApp's suspend handler) and started again when it resumes;
//    otherwise the game's music would play on over the home screen, with the
//    simulation that drives it stopped.
//  - THE DEVICE GOES AWAY - headphones pulled, a Bluetooth headset connected.
//    AAudio reports a disconnect on its callback thread, where the stream must
//    not be closed, so a thread of the engine's closes it and opens another.
//    The voices live in the mixer, not the stream, and carry on.
// ---------------------------------------------------------------------------
namespace {
constexpr int32_t kAAudioRate = 48000;
constexpr int32_t kAAudioChannels = 2;
} // namespace

struct AudioEngine::Impl {
    std::unique_ptr<AudioMixer> mixer;

    std::mutex streamMutex;   // open, close, pause and start
    AAudioStream* stream{nullptr};
    int32_t rate{kAAudioRate};
    bool suspended{false};    // under streamMutex

    std::atomic<bool> reopening{false};
    std::thread reopenThread;

    static aaudio_data_callback_result_t onData(AAudioStream*, void* user, void* audioData, int32_t frames) {
        auto* impl = static_cast<Impl*>(user);
        impl->mixer->MixInt16(static_cast<int16_t*>(audioData), static_cast<size_t>(frames));
        return AAUDIO_CALLBACK_RESULT_CONTINUE;
    }

    static void onError(AAudioStream*, void* user, aaudio_result_t error) {
        auto* impl = static_cast<Impl*>(user);
        SUPERSONIC_LOG_WARN("AudioEngine") << "AAudio stream error: " << AAudio_convertResultToText(error) << ".";
        if (error != AAUDIO_ERROR_DISCONNECTED) return;
        bool expected = false;
        if (!impl->reopening.compare_exchange_strong(expected, true)) return;
        // The previous reopen, if any, has finished: it clears the flag last.
        if (impl->reopenThread.joinable()) impl->reopenThread.join();
        impl->reopenThread = std::thread([impl] {
            std::lock_guard<std::mutex> lock(impl->streamMutex);
            impl->closeLocked();
            std::string error;
            if (impl->openLocked(error)) {
                SUPERSONIC_LOG_INFO("AudioEngine") << "AAudio stream reopened on the new device.";
            } else {
                SUPERSONIC_LOG_ERROR("AudioEngine") << error;
            }
            impl->reopening.store(false);
        });
    }

    bool openLocked(std::string& error) {
        AAudioStreamBuilder* builder = nullptr;
        aaudio_result_t result = AAudio_createStreamBuilder(&builder);
        if (result != AAUDIO_OK) {
            error = std::string("AAudio_createStreamBuilder failed: ") + AAudio_convertResultToText(result);
            return false;
        }
        AAudioStreamBuilder_setDirection(builder, AAUDIO_DIRECTION_OUTPUT);
        AAudioStreamBuilder_setSharingMode(builder, AAUDIO_SHARING_MODE_SHARED);
        AAudioStreamBuilder_setPerformanceMode(builder, AAUDIO_PERFORMANCE_MODE_LOW_LATENCY);
        AAudioStreamBuilder_setFormat(builder, AAUDIO_FORMAT_PCM_I16);
        AAudioStreamBuilder_setChannelCount(builder, kAAudioChannels);
        AAudioStreamBuilder_setSampleRate(builder, rate);
        AAudioStreamBuilder_setDataCallback(builder, &Impl::onData, this);
        AAudioStreamBuilder_setErrorCallback(builder, &Impl::onError, this);
        result = AAudioStreamBuilder_openStream(builder, &stream);
        AAudioStreamBuilder_delete(builder);
        if (result != AAUDIO_OK) {
            stream = nullptr;
            error = std::string("AAudioStreamBuilder_openStream failed: ") + AAudio_convertResultToText(result);
            return false;
        }

        // The mixer's rate is fixed when it is made; a device that would not
        // take the rate asked for gets a mixer at the rate it gave.
        const int32_t actualRate = AAudioStream_getSampleRate(stream);
        if (!mixer) {
            rate = actualRate;
            mixer = std::make_unique<AudioMixer>(static_cast<uint32_t>(rate),
                                                 static_cast<uint16_t>(kAAudioChannels));
        } else if (actualRate != rate) {
            SUPERSONIC_LOG_WARN("AudioEngine") << "The new AAudio stream runs at " << actualRate
                                               << " Hz, not " << rate << "; sounds play at the wrong pitch.";
        }
        if (AAudioStream_getFormat(stream) != AAUDIO_FORMAT_PCM_I16 ||
            AAudioStream_getChannelCount(stream) != kAAudioChannels) {
            error = "AAudio would not give a 16-bit stereo stream";
            closeLocked();
            return false;
        }

        if (!suspended) {
            result = AAudioStream_requestStart(stream);
            if (result != AAUDIO_OK) {
                error = std::string("AAudioStream_requestStart failed: ") + AAudio_convertResultToText(result);
                closeLocked();
                return false;
            }
        }
        return true;
    }

    void closeLocked() {
        if (!stream) return;
        AAudioStream_requestStop(stream);
        AAudioStream_close(stream);
        stream = nullptr;
    }

    void setSuspended(bool suspend) {
        std::lock_guard<std::mutex> lock(streamMutex);
        suspended = suspend;
        if (!stream) return;
        const aaudio_result_t result = suspend ? AAudioStream_requestPause(stream) : AAudioStream_requestStart(stream);
        if (result != AAUDIO_OK) {
            SUPERSONIC_LOG_WARN("AudioEngine") << "AAudio " << (suspend ? "pause" : "start")
                                               << " failed: " << AAudio_convertResultToText(result) << ".";
        }
    }
};

AudioEngine::AudioEngine() : m_impl(std::make_unique<Impl>()) {
    std::string error;
    {
        std::lock_guard<std::mutex> lock(m_impl->streamMutex);
        if (!m_impl->openLocked(error)) {
            m_status = error;
            SUPERSONIC_LOG_ERROR("AudioEngine") << m_status << std::endl;
            return;
        }
    }

    Impl* impl = m_impl.get();
    Android::SetAudioSuspendHandler([impl](bool suspended) { impl->setSuspended(suspended); });

    m_available = true;
    m_status = "AAudio output stream ready (" + std::to_string(m_impl->rate) + " Hz)";
    SUPERSONIC_LOG_INFO("AudioEngine") << m_status << "." << std::endl;
}

AudioEngine::~AudioEngine() {
    Android::SetAudioSuspendHandler(nullptr);
    // A reopen in flight holds the stream mutex; let it finish, then close.
    while (m_impl->reopening.load()) std::this_thread::yield();
    if (m_impl->reopenThread.joinable()) m_impl->reopenThread.join();
    std::lock_guard<std::mutex> lock(m_impl->streamMutex);
    m_impl->closeLocked();
}

AudioEngine::VoiceId AudioEngine::playImpl(const std::string& path, bool loop, float volume, float pitch) {
    if (!m_available) return kInvalidVoice;

    const AudioClip* clip = LoadClip(path);
    if (!clip) return kInvalidVoice;

    // As on ALSA: m_clips' nodes keep their addresses, so the callback thread
    // can keep reading this clip while more are loaded.
    return m_impl->mixer->Add(*clip, loop, volume, pitch);
}

void AudioEngine::stopImpl(VoiceId voice) {
    if (!m_available) return;
    m_impl->mixer->Remove(voice);
}

void AudioEngine::SetVoiceParameters(VoiceId voice, float volume, float pitch, float pan) {
    if (!m_available) return;
    m_impl->mixer->SetParameters(voice, volume, pitch, pan);
}

bool AudioEngine::IsVoicePlaying(VoiceId voice) const {
    if (!m_available) return false;
    return m_impl->mixer->IsPlaying(voice);
}

#elif defined(SUPERSONIC_AUDIO_COREAUDIO)

// ---------------------------------------------------------------------------
// CoreAudio backend, macOS and iOS. The AAudio path's shape: AudioMixer does
// the summing, pulled from a thread CoreAudio owns - here an output audio
// unit's render callback. The unit is the system's default output on macOS
// (it follows the device the user picks, headphones included) and RemoteIO on
// iOS. Either converts from the stream this hands it - 48 kHz, 16-bit,
// interleaved stereo, what the mixer writes - to whatever the hardware runs
// at, so the mixer's rate never has to follow the device's.
//
// iOS also takes the app off the screen, and a phone call can take the audio
// session away; the unit stops for both and starts again after
// (platform/ios/IOSApp.hpp's suspend handler, as AndroidApp's is for AAudio).
// A Mac game keeps playing unfocused, as it does on Windows and Linux.
// ---------------------------------------------------------------------------
namespace {
constexpr uint32_t kCoreAudioRate = 48000;
constexpr uint32_t kCoreAudioChannels = 2;

std::string coreAudioError(const char* what, OSStatus status) {
    return std::string(what) + " failed (OSStatus " + std::to_string(static_cast<long>(status)) + ")";
}
} // namespace

struct AudioEngine::Impl {
    AudioMixer mixer{kCoreAudioRate, static_cast<uint16_t>(kCoreAudioChannels)};
    AudioComponentInstance unit{nullptr};

    std::mutex unitMutex;     // start and stop
    bool running{false};      // under unitMutex

    // What the render callback has been asked for, for the log at shutdown:
    // proof the device pulled, not merely that it opened.
    std::atomic<uint64_t> framesPulled{0};
    std::atomic<uint64_t> callbacks{0};

    static OSStatus onRender(void* user, AudioUnitRenderActionFlags* /*flags*/, const AudioTimeStamp* /*time*/,
                             UInt32 /*bus*/, UInt32 frames, AudioBufferList* data) {
        auto* impl = static_cast<Impl*>(user);
        if (data == nullptr || data->mNumberBuffers == 0 || data->mBuffers[0].mData == nullptr) return noErr;
        // One interleaved buffer, as the stream format below asks.
        const UInt32 capacity =
            data->mBuffers[0].mDataByteSize / static_cast<UInt32>(sizeof(int16_t) * kCoreAudioChannels);
        const UInt32 count = frames < capacity ? frames : capacity;
        impl->mixer.MixInt16(static_cast<int16_t*>(data->mBuffers[0].mData), static_cast<size_t>(count));
        impl->framesPulled.fetch_add(count, std::memory_order_relaxed);
        impl->callbacks.fetch_add(1, std::memory_order_relaxed);
        return noErr;
    }

    bool open(std::string& error) {
        AudioComponentDescription description{};
        description.componentType = kAudioUnitType_Output;
#if defined(SUPERSONIC_PLATFORM_IOS)
        description.componentSubType = kAudioUnitSubType_RemoteIO;
#else
        description.componentSubType = kAudioUnitSubType_DefaultOutput;
#endif
        description.componentManufacturer = kAudioUnitManufacturer_Apple;
        AudioComponent component = AudioComponentFindNext(nullptr, &description);
        if (component == nullptr) {
            error = "no CoreAudio output unit on this system";
            return false;
        }
        OSStatus status = AudioComponentInstanceNew(component, &unit);
        if (status != noErr) {
            unit = nullptr;
            error = coreAudioError("AudioComponentInstanceNew", status);
            return false;
        }

        AudioStreamBasicDescription format{};
        format.mSampleRate = static_cast<Float64>(kCoreAudioRate);
        format.mFormatID = kAudioFormatLinearPCM;
        format.mFormatFlags = kAudioFormatFlagIsSignedInteger | kAudioFormatFlagIsPacked;
        format.mChannelsPerFrame = kCoreAudioChannels;
        format.mBitsPerChannel = 16;
        format.mBytesPerFrame = static_cast<UInt32>(sizeof(int16_t) * kCoreAudioChannels);
        format.mFramesPerPacket = 1;
        format.mBytesPerPacket = format.mBytesPerFrame;
        status = AudioUnitSetProperty(unit, kAudioUnitProperty_StreamFormat, kAudioUnitScope_Input, 0, &format,
                                      sizeof format);
        if (status != noErr) {
            error = coreAudioError("Setting the output unit's stream format", status);
            close();
            return false;
        }

        AURenderCallbackStruct callback{};
        callback.inputProc = &Impl::onRender;
        callback.inputProcRefCon = this;
        status = AudioUnitSetProperty(unit, kAudioUnitProperty_SetRenderCallback, kAudioUnitScope_Input, 0,
                                      &callback, sizeof callback);
        if (status != noErr) {
            error = coreAudioError("Setting the output unit's render callback", status);
            close();
            return false;
        }

        status = AudioUnitInitialize(unit);
        if (status != noErr) {
            error = coreAudioError("AudioUnitInitialize", status);
            close();
            return false;
        }
        status = AudioOutputUnitStart(unit);
        if (status != noErr) {
            error = coreAudioError("AudioOutputUnitStart", status);
            close();
            return false;
        }
        running = true;
        return true;
    }

    void close() {
        if (unit == nullptr) return;
        if (running) AudioOutputUnitStop(unit);
        running = false;
        AudioUnitUninitialize(unit);
        AudioComponentInstanceDispose(unit);
        unit = nullptr;
    }

    void setSuspended(bool suspend) {
        std::lock_guard<std::mutex> lock(unitMutex);
        if (unit == nullptr || running == !suspend) return;
        const OSStatus status = suspend ? AudioOutputUnitStop(unit) : AudioOutputUnitStart(unit);
        if (status == noErr) {
            running = !suspend;
        } else {
            SUPERSONIC_LOG_WARN("AudioEngine") << coreAudioError(suspend ? "AudioOutputUnitStop" : "AudioOutputUnitStart",
                                                                 status) << ".";
        }
    }
};

AudioEngine::AudioEngine() : m_impl(std::make_unique<Impl>()) {
    std::string error;
    {
        std::lock_guard<std::mutex> lock(m_impl->unitMutex);
        if (!m_impl->open(error)) {
            m_status = error;
            SUPERSONIC_LOG_ERROR("AudioEngine") << m_status << std::endl;
            return;
        }
    }

#if defined(SUPERSONIC_PLATFORM_IOS)
    Impl* impl = m_impl.get();
    IOS::SetAudioSuspendHandler([impl](bool suspended) { impl->setSuspended(suspended); });
#endif

    m_available = true;
    m_status = "CoreAudio output unit ready (" + std::to_string(kCoreAudioRate) + " Hz)";
    SUPERSONIC_LOG_INFO("AudioEngine") << m_status << "." << std::endl;
}

AudioEngine::~AudioEngine() {
#if defined(SUPERSONIC_PLATFORM_IOS)
    IOS::SetAudioSuspendHandler(nullptr);
#endif
    std::lock_guard<std::mutex> lock(m_impl->unitMutex);
    if (m_impl->unit != nullptr) {
        // Stopped, and so out of the callback, before the mixer it reads goes.
        m_impl->close();
        SUPERSONIC_LOG_INFO("AudioEngine") << "CoreAudio output closed: "
                                           << m_impl->framesPulled.load(std::memory_order_relaxed)
                                           << " frames pulled in "
                                           << m_impl->callbacks.load(std::memory_order_relaxed) << " callbacks.";
    }
}

AudioEngine::VoiceId AudioEngine::playImpl(const std::string& path, bool loop, float volume, float pitch) {
    if (!m_available) return kInvalidVoice;

    const AudioClip* clip = LoadClip(path);
    if (!clip) return kInvalidVoice;

    // As on ALSA and AAudio: m_clips' nodes keep their addresses, so the render
    // callback can keep reading this clip while more are loaded.
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

std::size_t AudioEngine::ReapFinishedVoices() {
    std::size_t freed = 0;
    for (auto it = m_voicePaths.begin(); it != m_voicePaths.end();) {
        // A looping voice always has a buffer queued, so it is never finished
        // and never reaped. That is the whole of the loop case.
        if (IsVoicePlaying(it->first)) {
            ++it;
            continue;
        }

        // stopImpl rather than Stop: Stop would erase from the map this loop is
        // already walking.
        stopImpl(it->first);
        it = m_voicePaths.erase(it);
        ++freed;
    }
    return freed;
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

const AudioClip* AudioEngine::AddClip(const std::string& name, AudioClip clip) {
    // Refused rather than cached. A cached failure is right for a file that
    // will not decode - it stops the loader re-opening it every frame - and
    // wrong for a caller that has just handed over an empty buffer, which is a
    // bug worth failing on now rather than remembering.
    if (!clip.valid()) return nullptr;

    const auto existing = m_clips.find(name);
    if (existing != m_clips.end()) {
        // A live voice holds a pointer into the buffer this would free.
        for (const auto& [voice, voicePath] : m_voicePaths) {
            if (voicePath == name && IsVoicePlaying(voice)) return nullptr;
        }
        existing->second = std::move(clip);
        return &existing->second;
    }

    auto [it, inserted] = m_clips.emplace(name, std::move(clip));
    return &it->second;
}

bool AudioEngine::HasClip(const std::string& name) const {
    const auto it = m_clips.find(name);
    return it != m_clips.end() && it->second.valid();
}

const AudioClip* AudioEngine::LoadClip(const std::string& path) {
    if (auto it = m_clips.find(path); it != m_clips.end()) {
        return it->second.valid() ? &it->second : nullptr;
    }

    AudioClip clip;
    std::string error;
    // By extension: a WAV and an MP3 are both clips by the time they get here.
    if (!AudioClip::Load(path, clip, error)) {
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
