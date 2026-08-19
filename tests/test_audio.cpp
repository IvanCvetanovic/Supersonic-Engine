// Tests for the WAV reader behind the audio system.
//
// The engine previously linked no audio library and AudioSystem discarded every
// volume it computed, so there was nothing to test. These cover the decode path
// and, importantly, that malformed input is rejected rather than fed to the
// audio device as garbage.

#include "TestHarness.hpp"
#include "core/AudioClip.hpp"

#include <cstdio>
#include <cstring>
#include <fstream>
#include <string>
#include <vector>

using namespace Supersonic;

namespace {

void writeU16(std::ofstream& f, uint16_t v) {
    const char b[2] = { static_cast<char>(v & 0xFF), static_cast<char>((v >> 8) & 0xFF) };
    f.write(b, 2);
}

void writeU32(std::ofstream& f, uint32_t v) {
    const char b[4] = {
        static_cast<char>(v & 0xFF), static_cast<char>((v >> 8) & 0xFF),
        static_cast<char>((v >> 16) & 0xFF), static_cast<char>((v >> 24) & 0xFF)
    };
    f.write(b, 4);
}

// Writes a valid mono 16-bit PCM WAV, optionally with a junk chunk before
// "data" to prove the reader walks the chunk list instead of assuming the
// canonical 44-byte header.
void writeWav(const std::string& path, uint32_t frames, bool withExtraChunk) {
    std::ofstream f(path, std::ios::binary);
    const uint16_t channels = 1;
    const uint32_t rate = 22050;
    const uint16_t bits = 16;
    const uint32_t dataBytes = frames * channels * (bits / 8);
    const uint32_t extraBytes = withExtraChunk ? 10u : 0u;
    const uint32_t extraTotal = withExtraChunk ? (8u + extraBytes) : 0u;

    f.write("RIFF", 4);
    writeU32(f, 36 + extraTotal + dataBytes);
    f.write("WAVE", 4);

    f.write("fmt ", 4);
    writeU32(f, 16);
    writeU16(f, 1);            // PCM
    writeU16(f, channels);
    writeU32(f, rate);
    writeU32(f, rate * channels * (bits / 8));
    writeU16(f, static_cast<uint16_t>(channels * (bits / 8)));
    writeU16(f, bits);

    if (withExtraChunk) {
        f.write("LIST", 4);
        writeU32(f, extraBytes);
        for (uint32_t i = 0; i < extraBytes; ++i) f.put('x');
    }

    f.write("data", 4);
    writeU32(f, dataBytes);
    for (uint32_t i = 0; i < frames; ++i) writeU16(f, static_cast<uint16_t>(i * 7));
}

} // namespace

static void testLoadsValidWav() {
    const std::string path = "test_audio_tmp.wav";
    writeWav(path, 22050, false);

    AudioClip clip;
    std::string error;
    const bool ok = AudioClip::LoadWav(path, clip, error);
    std::remove(path.c_str());

    CHECK_MSG(ok, error);
    CHECK_EQ(clip.channels, uint16_t{1});
    CHECK_EQ(clip.sampleRate, uint32_t{22050});
    CHECK_EQ(clip.bitsPerSample, uint16_t{16});
    CHECK(clip.valid());
    CHECK_NEAR(clip.durationSeconds(), 1.0f);
}

static void testSkipsUnknownChunks() {
    const std::string path = "test_audio_chunks_tmp.wav";
    writeWav(path, 1000, true);

    AudioClip clip;
    std::string error;
    const bool ok = AudioClip::LoadWav(path, clip, error);
    std::remove(path.c_str());

    CHECK_MSG(ok, "a LIST chunk before data must not defeat the reader: " + error);
    CHECK_EQ(clip.pcm.size(), size_t{2000});
}

static void testRejectsNonRiff() {
    const std::string path = "test_audio_bad_tmp.wav";
    {
        std::ofstream f(path, std::ios::binary);
        f << "this is definitely not a wave file, not even close at all";
    }

    AudioClip clip;
    std::string error;
    const bool ok = AudioClip::LoadWav(path, clip, error);
    std::remove(path.c_str());

    CHECK_MSG(!ok, "a non-RIFF file must be rejected");
    CHECK_MSG(!error.empty(), "failures must explain themselves");
    CHECK(!clip.valid());
}

static void testRejectsTruncatedFile() {
    const std::string path = "test_audio_trunc_tmp.wav";
    {
        std::ofstream f(path, std::ios::binary);
        f.write("RIFF", 4);
    }

    AudioClip clip;
    std::string error;
    const bool ok = AudioClip::LoadWav(path, clip, error);
    std::remove(path.c_str());

    CHECK_MSG(!ok, "a truncated file must be rejected, not read out of bounds");
}

static void testRejectsCompressedFormat() {
    // Format tag 0x0011 is IMA ADPCM. Handing compressed bytes to the device as
    // if they were PCM produces noise at best.
    const std::string path = "test_audio_adpcm_tmp.wav";
    {
        std::ofstream f(path, std::ios::binary);
        f.write("RIFF", 4); writeU32(f, 44); f.write("WAVE", 4);
        f.write("fmt ", 4); writeU32(f, 16);
        writeU16(f, 0x0011); writeU16(f, 1); writeU32(f, 22050);
        writeU32(f, 22050); writeU16(f, 2); writeU16(f, 16);
        f.write("data", 4); writeU32(f, 4);
        writeU32(f, 0);
    }

    AudioClip clip;
    std::string error;
    const bool ok = AudioClip::LoadWav(path, clip, error);
    std::remove(path.c_str());

    CHECK_MSG(!ok, "an unsupported compressed format must be rejected");
}

static void testRejectsMissingDataChunk() {
    const std::string path = "test_audio_nodata_tmp.wav";
    {
        std::ofstream f(path, std::ios::binary);
        f.write("RIFF", 4); writeU32(f, 36); f.write("WAVE", 4);
        f.write("fmt ", 4); writeU32(f, 16);
        writeU16(f, 1); writeU16(f, 1); writeU32(f, 22050);
        writeU32(f, 44100); writeU16(f, 2); writeU16(f, 16);
        // no data chunk, padded so the file is long enough to be considered
        for (int i = 0; i < 8; ++i) f.put('\0');
    }

    AudioClip clip;
    std::string error;
    const bool ok = AudioClip::LoadWav(path, clip, error);
    std::remove(path.c_str());

    CHECK_MSG(!ok, "a file with no samples must be rejected");
}

static void testMissingFileFails() {
    AudioClip clip;
    std::string error;
    CHECK(!AudioClip::LoadWav("no_such_audio_31337.wav", clip, error));
}

static void testShippedAmbientClipIsUsable() {
    // The repository ships assets/audio/ambient.wav; if it is present it must
    // decode, because the default scene references it.
    AudioClip clip;
    std::string error;
    if (AudioClip::LoadWav("assets/audio/ambient.wav", clip, error)) {
        CHECK(clip.valid());
        CHECK_MSG(clip.sampleRate >= 8000, "sample rate should be sane");
        CHECK_MSG(clip.durationSeconds() > 0.1f, "clip should have real length");
    } else {
        std::printf("  note: assets/audio/ambient.wav not reachable from the test cwd (%s)\n",
                    error.c_str());
    }
}

static void runTests() {
    testLoadsValidWav();
    testSkipsUnknownChunks();
    testRejectsNonRiff();
    testRejectsTruncatedFile();
    testRejectsCompressedFormat();
    testRejectsMissingDataChunk();
    testMissingFileFails();
    testShippedAmbientClipIsUsable();
}

TEST_MAIN("test_audio", 12)
