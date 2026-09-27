#include "core/AudioClip.hpp"

#include <algorithm>
#include <cctype>
#include <climits>
#include <cstring>
#include <fstream>
#include <memory>

// Declarations only. The implementation is compiled once, with its warnings
// silenced, in StbVorbisImplementation.cpp; this file stays at /W4.
#define STB_VORBIS_HEADER_ONLY
#include <stb_vorbis.c>

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

// How many frames LoadOgg asks stb_vorbis for at a time. A buffer size, not a
// limit - the loop pulls until the stream ends - and the step
// stb_vorbis_decode_memory itself grows by.
constexpr int kVorbisFramesPerPull = 4096;

// stb_vorbis is C: its decoder is released by a function, not a destructor,
// and every early return in LoadOgg has to release it.
struct VorbisCloser {
    void operator()(stb_vorbis* decoder) const { stb_vorbis_close(decoder); }
};

// stb_vorbis says why it refused a file with a number. The ones a person can
// act on are said in words; the rest keep the number, which is what to look up
// in stb_vorbis.c's STBVorbisError.
std::string vorbisRefusal(const std::string& path, int code, const std::vector<uint8_t>& bytes) {
    switch (code) {
    case VORBIS_missing_capture_pattern:
        // stb_vorbis says this of ANY page it expected and did not find, the
        // first included - so a Vorbis file cut short after its first page
        // would be reported as "not Ogg", and whoever reads that goes looking
        // at the format instead of the copy. The first four bytes tell them apart.
        if (bytes.size() >= 4 && std::memcmp(bytes.data(), "OggS", 4) == 0) {
            return path + " is cut short or damaged after its first Ogg page";
        }
        return path + " is not an Ogg file";
    case VORBIS_invalid_first_page:
    case VORBIS_ogg_skeleton_not_supported:
        // Also what a truncated identification header produces, which is why
        // this does not claim to know what the stream is instead.
        return path + " does not begin with a Vorbis stream (Opus and FLAC in Ogg are not read)";
    case VORBIS_feature_not_supported:
        return path + " uses Vorbis floor 0, which predates 2004 and is not decoded";
    case VORBIS_unexpected_eof:
        return path + " ends inside its Vorbis headers";
    case VORBIS_outofmem:
        return "out of memory decoding " + path;
    default:
        return "cannot decode " + path + " as Ogg Vorbis (stb_vorbis error " +
               std::to_string(code) + ")";
    }
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
    if (ext == ".ogg") return LoadOgg(path, out, error);
    out = AudioClip{};
    error = path + " is not a sound this engine reads (.wav, .mp3 or .ogg)";
    return false;
}

bool AudioClip::LoadOgg(const std::string& path, AudioClip& out, std::string& error) {
    out = AudioClip{};

    std::ifstream file(path, std::ios::binary | std::ios::ate);
    if (!file.is_open()) {
        error = "cannot open " + path;
        return false;
    }

    // tellg is -1 on a stream that cannot say where it is, and stb_vorbis
    // takes the length as an int. Both are refused rather than truncated into
    // a length that is not the file's.
    const std::streamoff size = file.tellg();
    if (size < 0) {
        error = "cannot read the size of " + path;
        return false;
    }
    if (size == 0) {
        error = path + " is empty";
        return false;
    }
    if (size > INT_MAX) {
        error = path + " is too large to decode in one piece";
        return false;
    }

    // Declared before the decoder, so destroyed after it: stb_vorbis reads
    // straight out of this buffer for as long as it is open.
    std::vector<uint8_t> bytes(static_cast<size_t>(size));
    file.seekg(0);
    file.read(reinterpret_cast<char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
    if (!file) {
        error = "read error on " + path;
        return false;
    }

    // From memory rather than stb_vorbis_decode_filename, which opens the path
    // with fopen: this way a missing file and a file that is not Vorbis give
    // different answers, and the path is opened the way LoadWav opens it.
    int code = 0;
    std::unique_ptr<stb_vorbis, VorbisCloser> decoder(
        stb_vorbis_open_memory(bytes.data(), static_cast<int>(bytes.size()), &code, nullptr));
    if (!decoder) {
        error = vorbisRefusal(path, code, bytes);
        return false;
    }

    // The same channel limit LoadWav and LoadMp3 state, and for the same
    // reason: the backend computes its block alignment from it. Checked before
    // decoding rather than after, because the headers already say, and a file
    // that will be refused should not cost its whole decode first.
    const stb_vorbis_info info = stb_vorbis_get_info(decoder.get());
    if (info.channels <= 0 || info.channels > 2) {
        error = path + " has unsupported channel count " + std::to_string(info.channels);
        return false;
    }
    const int channels = info.channels;

    std::vector<short> chunk(static_cast<size_t>(kVorbisFramesPerPull) * static_cast<size_t>(channels));
    std::vector<uint8_t> pcm;
    for (;;) {
        const int frames = stb_vorbis_get_samples_short_interleaved(
            decoder.get(), channels, chunk.data(), static_cast<int>(chunk.size()));
        if (frames <= 0) break;

        // Byte by byte, because the clip's contract is little-endian, not the
        // host's order. LoadWav and LoadMp3 get that for free by copying bytes
        // that are already little-endian; these are shorts.
        const size_t count = static_cast<size_t>(frames) * static_cast<size_t>(channels);
        const size_t at = pcm.size();
        pcm.resize(at + count * 2);
        for (size_t i = 0; i < count; ++i) {
            const auto sample = static_cast<uint16_t>(chunk[i]);
            pcm[at + 2 * i] = static_cast<uint8_t>(sample & 0xFFu);
            pcm[at + 2 * i + 1] = static_cast<uint8_t>(sample >> 8);
        }
    }

    if (pcm.empty()) {
        error = path + " decoded to no audio at all";
        return false;
    }

    out.channels = static_cast<uint16_t>(channels);
    out.sampleRate = info.sample_rate;
    out.bitsPerSample = 16;
    out.pcm = std::move(pcm);
    return true;
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
