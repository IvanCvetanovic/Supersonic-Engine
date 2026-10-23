#include "core/Mp3Decode.hpp"

#include <algorithm>
#include <array>
#include <climits>
#include <memory>
#include <new>

// Declarations only. The implementation is compiled once, with its warnings
// silenced, in DrMp3Implementation.cpp; this file stays at /W4. DR_MP3_NO_STDIO
// is repeated there on purpose: the header's declarations depend on it, and two
// translation units that disagree about it disagree about the library.
#define DR_MP3_NO_STDIO
#include <dr_mp3.h>

namespace Supersonic::detail {

namespace {

// How many PCM frames one pull asks dr_mp3 for. That sets how often the format of
// the stream is checked: after each pull, on the frame dr_mp3 has reached, so a
// stretch of the stream in another format is caught only if some pull ends inside
// it.
//
// 576 is the shortest Layer III frame (MPEG-2 and 2.5; MPEG-1 has 1152), so a pull
// of 576 frames ends in every Layer III frame in turn and steps over none. Layer I
// frames are 384 samples, which dr_mp3 also reads, and a pull of 576 can step over
// one of those. In practice a stretch of another format is long, because dr_mp3
// resynchronises past a frame whose neighbours do not match and wants ten that do:
// ten frames of 22.05 kHz mono inside a stereo stream are dropped, eleven are
// decoded (measured on the dr_mp3 vendored here). Eleven Layer III frames are about
// 5760 samples, and a pull no longer than that cannot step over a stretch that
// long; a longer one can, if the stretch lies between two of its ends. So the
// number is held down by testAFormatChangeInTheMiddleIsRefused, which fails for a
// pull of 12288 frames or more; streams that would fail it for the pulls between
// about 5760 and 11500 exist and are not in the test. Nothing in memory safety
// depends on the check; it only keeps a clip from being played at the wrong speed.
constexpr std::size_t kFramesPerPull = 576;

// The sample rates MPEG-1, 2 and 2.5 define run from 8000 to 48000.
// dr_mp3 cannot return anything else, so this is a statement of what the
// backends are handed rather than a filter that ever fires on a real stream.
constexpr uint32_t kMinSampleRate = 8000;
constexpr uint32_t kMaxSampleRate = 48000;

// The first buffer the output gets, so a short sound is not grown a few
// hundred bytes at a time.
constexpr std::size_t kFirstReserveBytes = std::size_t{64} * 1024;

// dr_mp3's decoder is C: it is released by a function, and only when it was
// opened (a failed drmp3_init has already freed what it allocated, and freeing
// it again would be a double free). The state is ~17 KB, so it lives on the
// heap rather than on whatever stack a worker thread was given.
class Mp3Stream {
public:
    Mp3Stream() : m_mp3(std::make_unique<drmp3>()) {}
    ~Mp3Stream() {
        if (m_open) drmp3_uninit(m_mp3.get());
    }
    Mp3Stream(const Mp3Stream&) = delete;
    Mp3Stream& operator=(const Mp3Stream&) = delete;

    bool Open(const uint8_t* data, std::size_t size) {
        m_open = drmp3_init_memory(m_mp3.get(), data, size, nullptr) == DRMP3_TRUE;
        return m_open;
    }
    drmp3& Get() { return *m_mp3; }

private:
    std::unique_ptr<drmp3> m_mp3;
    bool m_open{false};
};

// Room for `needed` bytes, by an allocation that never exceeds `cap`.
//
// std::vector's own growth doubles (1.5 times on MSVC) and so can ask for a
// capacity well past the size it was grown to; for a decode that is bounded by a
// cap, the cap has to bound the ALLOCATION and not just the size, or the limit
// is a statement about bytes the vector never needed to hold. needed <= cap is
// the caller's to have checked.
void reserveWithin(std::vector<uint8_t>& pcm, std::size_t needed, std::size_t cap) {
    if (needed <= pcm.capacity()) return;
    const std::size_t grown = std::max({pcm.capacity() * 2, kFirstReserveBytes, needed});
    pcm.reserve(std::min(grown, cap));
}

} // namespace

bool DecodeMp3(const uint8_t* data, std::size_t size, Mp3Pcm& out, std::string& reason,
               std::size_t maxPcmBytes) {
    out = Mp3Pcm{};

    if (data == nullptr || size == 0) {
        reason = "is empty";
        return false;
    }
    // dr_mp3 hands minimp3 the bytes that remain as an int; past INT_MAX that
    // count wraps. LoadOgg refuses the same size for stb_vorbis, which also
    // takes an int.
    if (size > static_cast<std::size_t>(INT_MAX)) {
        reason = "is too large to decode in one piece";
        return false;
    }

    Mp3Stream stream;
    if (!stream.Open(data, size)) {
        // dr_mp3 reports one failure for all of: fewer than ten bytes, no frame
        // header anywhere, and a first frame that does not decode. They are not
        // told apart because the person reading this does the same thing in
        // each case: look at the file.
        reason = "is not an MP3 this engine can decode (no valid MPEG audio frame found)";
        return false;
    }
    drmp3& mp3 = stream.Get();

    const uint32_t channels = mp3.channels;
    const uint32_t rate = mp3.sampleRate;
    // The same channel limit LoadWav, LoadOgg and the Media Foundation path
    // state, and for the same reason: the backend computes its block alignment
    // from it. dr_mp3 reports one or two, so this too guards a decoder upgrade
    // more than a file.
    if (channels == 0 || channels > 2) {
        reason = "has unsupported channel count " + std::to_string(channels);
        return false;
    }
    if (rate < kMinSampleRate || rate > kMaxSampleRate) {
        reason = "has unsupported sample rate " + std::to_string(rate);
        return false;
    }

    std::array<drmp3_int16, kFramesPerPull * 2> chunk{};
    std::vector<uint8_t> pcm;
    try {
        for (;;) {
            const drmp3_uint64 frames = drmp3_read_pcm_frames_s16(&mp3, kFramesPerPull, chunk.data());
            if (frames == 0) break;

            // A format change would invalidate the rate and channels read above.
            // The Media Foundation path refuses it by name, so this does too,
            // rather than quietly playing the second half at the wrong speed.
            if (mp3.mp3FrameChannels != channels || mp3.mp3FrameSampleRate != rate) {
                reason = "changes format part way through";
                return false;
            }

            const std::size_t count = static_cast<std::size_t>(frames) * channels;
            const std::size_t bytes = count * 2;
            // Written to not overflow: pcm.size() never exceeds the cap.
            if (bytes > maxPcmBytes - pcm.size()) {
                reason = "decodes to more than " + std::to_string(maxPcmBytes) +
                         " bytes of audio, which is more than this engine will hold in one clip";
                return false;
            }

            const std::size_t at = pcm.size();
            reserveWithin(pcm, at + bytes, maxPcmBytes);
            pcm.resize(at + bytes);
            // Byte by byte, because the clip's contract is little-endian, not the
            // host's order (LoadOgg does the same, for the same reason).
            for (std::size_t i = 0; i < count; ++i) {
                const auto sample = static_cast<uint16_t>(chunk[i]);
                pcm[at + 2 * i] = static_cast<uint8_t>(sample & 0xFFu);
                pcm[at + 2 * i + 1] = static_cast<uint8_t>(sample >> 8);
            }
        }
    } catch (const std::bad_alloc&) {
        reason = "ran out of memory while decoding";
        return false;
    }

    if (pcm.empty()) {
        reason = "decoded to no audio at all";
        return false;
    }

    out.channels = static_cast<uint16_t>(channels);
    out.sampleRate = rate;
    out.pcm = std::move(pcm);
    return true;
}

} // namespace Supersonic::detail
