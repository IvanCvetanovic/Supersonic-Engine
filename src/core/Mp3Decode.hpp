#pragma once

// MP3 held in memory -> interleaved 16-bit PCM, through dr_mp3.
//
// INTERNAL. It is not part of the engine's API: AudioClip::LoadMp3 is, and this
// is how it decodes everywhere but Windows (where it asks Media Foundation, and
// never reaches this file). It is declared in a header, and compiled on every
// platform including Windows, for one reason: the engine's own test can then
// decode the same bytes twice, once through Media Foundation and once through
// this, and say whether the two agree. Nothing else on Windows calls it.

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace Supersonic::detail {

// The most PCM one MP3 may decode to, in bytes: 256 MiB, about 25 minutes of
// 44.1 kHz stereo.
//
// A BOUND, NOT A BUDGET. LoadWav and LoadOgg cap only the size of the file they
// read, which is enough for a format whose output is no larger than its input
// (WAV) or a small multiple of it. MP3's is many times larger: a 32 kbit/s,
// 48 kHz stereo stream is 96-byte frames that each decode to 4608 bytes, 48
// times the file, so a 2 GiB file (the largest the decoder accepts) would ask
// for about 100 GB. The cap is what makes that a refusal rather than an
// allocation. A game that really has sounds longer than this can stream them.
inline constexpr std::size_t kMp3MaxPcmBytes = std::size_t{256} * 1024 * 1024;

struct Mp3Pcm {
    uint16_t channels{0};
    uint32_t sampleRate{0};
    std::vector<uint8_t> pcm;   // interleaved, 16-bit, little-endian whatever the host's order
};

// Decodes `size` bytes at `data`. Returns false and leaves `out` empty on
// anything it will not play, and sets `reason` to a fragment that finishes a
// sentence about the file ("is empty", "has unsupported channel count 6"): the
// caller puts the path in front, as LoadOgg's refusals do, so the message names
// the file without this function knowing its name.
//
// What it accepts: a stream of MPEG-1/2/2.5 audio frames (Layer III, which is what
// an .mp3 file holds; dr_mp3 reads Layers I and II too, and this does not turn
// them away), one or two channels, 8 to 48 kHz, with or without an ID3v2 tag in
// front, an ID3v1 tag behind, and a Xing/Info/LAME header (whose encoder delay and
// padding it trims, so a gapless loop is gapless). A stream cut short, or damaged part way
// through, decodes as far as dr_mp3 can follow it and succeeds, which is what
// LoadOgg does with Vorbis and for the same reason: the decoder reports the end
// of the data and not why it ended.
//
// What it refuses, each with its own reason: no data, more than INT_MAX bytes
// (dr_mp3 counts them in an int), nothing that starts an MPEG audio frame, no
// audio at all after the first frame, a channel count or sample rate outside
// what the backends handle, a format that changes part way through (the same
// refusal the Media Foundation path makes), and PCM beyond `maxPcmBytes`.
//
// Nothing the stream says about its own length is believed. The Xing header's
// frame count and the LAME tag's delay and padding steer dr_mp3's trimming, but
// the output is grown a chunk at a time from what the decoder actually produced,
// and no allocation is made for more than `maxPcmBytes`.
//
// `maxPcmBytes` is a parameter, not just the constant above, so a test can prove
// the refusal with a few kilobytes of input instead of a quarter gigabyte.
bool DecodeMp3(const uint8_t* data, std::size_t size, Mp3Pcm& out, std::string& reason,
               std::size_t maxPcmBytes = kMp3MaxPcmBytes);

} // namespace Supersonic::detail
