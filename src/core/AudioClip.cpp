#include "core/AudioClip.hpp"

#include <algorithm>
#include <cctype>
#include <cstring>
#include <fstream>

#if defined(_WIN32)
// Media Foundation decodes MP3 and ships with Windows, so this costs no
// third-party dependency - the same bargain the XAudio2 backend strikes.
#include <windows.h>

#include <mfapi.h>
#include <mferror.h>
#include <mfidl.h>
#include <mfreadwrite.h>

#include <wrl/client.h>
#endif

namespace Supersonic {

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

// The file's extension, lowered. Empty when it has none.
std::string extensionOf(const std::string& path) {
    const std::size_t dot = path.find_last_of('.');
    if (dot == std::string::npos) return {};
    const std::size_t slash = path.find_last_of("/\\");
    if (slash != std::string::npos && dot < slash) return {};
    std::string ext = path.substr(dot);
    std::transform(ext.begin(), ext.end(), ext.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return ext;
}

#if defined(_WIN32)

// Media Foundation is started once per process and never shut down.
//
// Deliberate: MFShutdown is per-process too, and a clip decoded on one thread
// while another is shutting the platform down is a crash in somebody else's
// library. The cost of leaving it up is a handful of pages for the lifetime of
// a process that has already loaded a GPU driver.
bool startMediaFoundation(std::string& error) {
    static bool started = false;
    static std::string failure;
    static bool tried = false;
    if (!tried) {
        tried = true;
        // COINIT_MULTITHREADED: the engine's own threads may decode, and MF is
        // happy either way. CoInitializeEx returning S_FALSE means this thread
        // was already initialised, which is not a failure.
        const HRESULT com = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
        if (com != S_OK && com != S_FALSE && com != RPC_E_CHANGED_MODE) {
            failure = "CoInitializeEx failed";
        } else if (FAILED(MFStartup(MF_VERSION, MFSTARTUP_LITE))) {
            failure = "MFStartup failed";
        } else {
            started = true;
        }
    }
    if (!started) error = failure;
    return started;
}

#endif

} // namespace

bool AudioClip::Load(const std::string& path, AudioClip& out, std::string& error) {
    const std::string ext = extensionOf(path);
    if (ext == ".wav") return LoadWav(path, out, error);
    if (ext == ".mp3") return LoadMp3(path, out, error);
    out = AudioClip{};
    error = path + " is not a sound this engine reads (.wav or .mp3)";
    return false;
}

bool AudioClip::LoadMp3(const std::string& path, AudioClip& out, std::string& error) {
    out = AudioClip{};

#if !defined(_WIN32)
    (void)path;
    error = "MP3 is decoded through Media Foundation, which is Windows only; "
            "convert to WAV for this platform";
    return false;
#else
    using Microsoft::WRL::ComPtr;

    if (!startMediaFoundation(error)) return false;

    // MF takes a wide path. The engine's paths are UTF-8.
    const int wide = MultiByteToWideChar(CP_UTF8, 0, path.c_str(), -1, nullptr, 0);
    if (wide <= 0) {
        error = "cannot express " + path + " as a wide path";
        return false;
    }
    std::wstring wpath(static_cast<std::size_t>(wide), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, path.c_str(), -1, wpath.data(), wide);
    wpath.resize(static_cast<std::size_t>(wide - 1)); // drop the terminator

    ComPtr<IMFSourceReader> reader;
    HRESULT hr = MFCreateSourceReaderFromURL(wpath.c_str(), nullptr, reader.GetAddressOf());
    if (FAILED(hr)) {
        // The common causes are a missing file and a file that is not audio at
        // all; MF does not distinguish them in a way worth repeating here.
        error = "cannot open " + path + " as an MP3";
        return false;
    }

    // Ask for interleaved 16-bit PCM. MF inserts its own decoder to satisfy
    // this, which is the whole point: the clip contract is PCM.
    ComPtr<IMFMediaType> wanted;
    if (FAILED(MFCreateMediaType(wanted.GetAddressOf())) ||
        FAILED(wanted->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Audio)) ||
        FAILED(wanted->SetGUID(MF_MT_SUBTYPE, MFAudioFormat_PCM)) ||
        FAILED(wanted->SetUINT32(MF_MT_AUDIO_BITS_PER_SAMPLE, 16))) {
        error = "cannot ask for PCM from " + path;
        return false;
    }
    hr = reader->SetCurrentMediaType(static_cast<DWORD>(MF_SOURCE_READER_FIRST_AUDIO_STREAM),
                                     nullptr, wanted.Get());
    if (FAILED(hr)) {
        error = path + " holds no audio stream this engine can decode";
        return false;
    }

    // What MF settled on, which is where the rate and channel count come from.
    ComPtr<IMFMediaType> actual;
    hr = reader->GetCurrentMediaType(static_cast<DWORD>(MF_SOURCE_READER_FIRST_AUDIO_STREAM),
                                     actual.GetAddressOf());
    UINT32 channels = 0;
    UINT32 rate = 0;
    UINT32 bits = 0;
    if (FAILED(hr) || FAILED(actual->GetUINT32(MF_MT_AUDIO_NUM_CHANNELS, &channels)) ||
        FAILED(actual->GetUINT32(MF_MT_AUDIO_SAMPLES_PER_SECOND, &rate)) ||
        FAILED(actual->GetUINT32(MF_MT_AUDIO_BITS_PER_SAMPLE, &bits))) {
        error = "cannot read the decoded format of " + path;
        return false;
    }
    // The same limits LoadWav states, and for the same reason: the backend
    // computes its block alignment from these.
    if (channels == 0 || channels > 2) {
        error = path + " has unsupported channel count " + std::to_string(channels);
        return false;
    }
    if (bits != 16) {
        error = path + " decoded to " + std::to_string(bits) + "-bit rather than 16";
        return false;
    }

    std::vector<uint8_t> pcm;
    for (;;) {
        DWORD flags = 0;
        ComPtr<IMFSample> sample;
        hr = reader->ReadSample(static_cast<DWORD>(MF_SOURCE_READER_FIRST_AUDIO_STREAM), 0, nullptr,
                                &flags, nullptr, sample.GetAddressOf());
        if (FAILED(hr)) {
            error = "decoding " + path + " failed part way through";
            return false;
        }
        if (flags & MF_SOURCE_READERF_ENDOFSTREAM) break;
        // A format change mid-file would invalidate the rate and channels read
        // above, so it is refused rather than silently mixed.
        if (flags & MF_SOURCE_READERF_CURRENTMEDIATYPECHANGED) {
            error = path + " changes format part way through";
            return false;
        }
        if (!sample) continue; // a gap in the stream, not an error

        ComPtr<IMFMediaBuffer> buffer;
        if (FAILED(sample->ConvertToContiguousBuffer(buffer.GetAddressOf()))) {
            error = "cannot read a decoded buffer of " + path;
            return false;
        }
        BYTE* data = nullptr;
        DWORD length = 0;
        if (FAILED(buffer->Lock(&data, nullptr, &length))) {
            error = "cannot lock a decoded buffer of " + path;
            return false;
        }
        pcm.insert(pcm.end(), data, data + length);
        buffer->Unlock();
    }

    if (pcm.empty()) {
        error = path + " decoded to no audio at all";
        return false;
    }

    out.channels = static_cast<uint16_t>(channels);
    out.sampleRate = rate;
    out.bitsPerSample = static_cast<uint16_t>(bits);
    out.pcm = std::move(pcm);
    return true;
#endif
}

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

} // namespace Supersonic
