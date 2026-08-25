// Tests for the WAV reader behind the audio system.
//
// The engine previously linked no audio library and AudioSystem discarded every
// volume it computed, so there was nothing to test. These cover the decode path
// and, importantly, that malformed input is rejected rather than fed to the
// audio device as garbage.

#include "TestHarness.hpp"
#include "core/AudioEngine.hpp"
#include "core/AudioSystem.hpp"
#include "core/Components.hpp"

#include <entt/entt.hpp>
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

// --- Hot reloading a clip ---------------------------------------------------
//
// The dangerous one. A voice reads the clip's sample buffer directly, from an
// audio thread, so "drop the cache entry and let it reload" - which is what
// textures, meshes, materials and rigs all do - is a use-after-free here.

static void testUnloadClipMakesTheNextLoadReadDiskAgain() {
    const std::string path = "test_audio_reload_tmp.wav";
    writeWav(path, 22050, false);

    AudioEngine engine;
    const AudioClip* first = engine.LoadClip(path);
    CHECK_MSG(first != nullptr && first->valid(), "the clip must load");
    const size_t firstBytes = first ? first->pcm.size() : 0;

    // Edited on disk. Without unloading, the cache answers and the new file is
    // never opened - which is the bug, and it has to be visible here or the
    // test below proves nothing.
    writeWav(path, 1000, false);
    const AudioClip* cached = engine.LoadClip(path);
    CHECK_EQ(cached ? cached->pcm.size() : 0, firstBytes);

    CHECK_MSG(engine.UnloadClip(path), "unloading a cached clip must report that it was there");

    const AudioClip* reloaded = engine.LoadClip(path);
    CHECK_MSG(reloaded && reloaded->pcm.size() == 1000 * 2,
              "after unloading, the next load must read the file that is there now");

    std::remove(path.c_str());
}

static void testReloadClipLetsASourceThatGaveUpTryAgain() {
    // A source that could not load its file stops trying, or a typo costs a
    // disk hit every frame for the rest of the session. Somebody fixing the
    // file is exactly the event that has to undo that.
    const std::string path = "test_audio_retry_tmp.wav";

    AudioEngine engine;
    entt::registry registry;
    const auto entity = registry.create();
    auto& source = registry.emplace<AudioSourceComponent>(entity);
    source.soundFile = path;
    source.failedToLoad = true;

    writeWav(path, 4410, false);
    const size_t interrupted = AudioSystem::ReloadClip(registry, engine, path);

    CHECK_EQ(interrupted, size_t{0});
    CHECK_MSG(!registry.get<AudioSourceComponent>(entity).failedToLoad,
              "a fixed file must let the source try again");

    std::remove(path.c_str());
}

static void testReloadClipStopsTheVoiceReadingTheOldSamples() {
    // Needs a real output device, so the three checks inside are not counted in
    // this suite's floor. Without one, Play returns kInvalidVoice and there is
    // no voice to be dangling - which is the honest answer, not a skipped test.
    const std::string path = "test_audio_voice_tmp.wav";
    writeWav(path, 22050, false);

    AudioEngine engine;
    entt::registry registry;
    const auto entity = registry.create();
    auto& source = registry.emplace<AudioSourceComponent>(entity);
    source.soundFile = path;
    source.voice = engine.Play(path, true, 1.0f, 1.0f);

    if (source.voice != AudioEngine::kInvalidVoice) {
        const size_t interrupted = AudioSystem::ReloadClip(registry, engine, path);

        CHECK_EQ(interrupted, size_t{1});
        CHECK_MSG(registry.get<AudioSourceComponent>(entity).voice == AudioEngine::kInvalidVoice,
                  "the handle must be cleared, or a looping source never restarts and goes silent");
        CHECK_MSG(!engine.IsVoicePlaying(AudioEngine::kInvalidVoice),
                  "and the stopped voice must really be gone");
    }

    std::remove(path.c_str());
}

static void testAVoiceIsFoundByWhatItPlaysNotByWhatItsSourceNamesNow() {
    // The trap. Update starts a voice and never looks at soundFile again, so
    // pointing a LOOPING source at another file leaves the old voice running.
    // Asking the components which of them use this path would miss it - and
    // that voice is the one reading the memory about to be freed.
    const std::string path = "test_audio_moved_tmp.wav";
    writeWav(path, 22050, false);

    AudioEngine engine;
    const AudioEngine::VoiceId voice = engine.Play(path, true, 1.0f, 1.0f);

    if (voice != AudioEngine::kInvalidVoice) {
        const auto stopped = engine.StopVoicesUsing(path);
        CHECK_MSG(stopped.size() == 1 && stopped[0] == voice,
                  "the engine must find a voice by the clip it is reading");
        CHECK_MSG(engine.StopVoicesUsing(path).empty(), "and stopping it must be idempotent");
    }

    std::remove(path.c_str());
}

static void testALoopingSourceFollowsAChangedSoundFile() {
    // Update STARTS a voice and then never looks at soundFile again, so
    // changing the file on a looping source did nothing at all - not an error,
    // not silence, just the old sound continuing - until somebody toggled
    // Playing off and on. A non-looping source hid it, because its voice ends
    // and the next one reads the new name.
    const std::string first = "test_audio_swap_a_tmp.wav";
    const std::string second = "test_audio_swap_b_tmp.wav";
    writeWav(first, 22050, false);
    writeWav(second, 4410, false);

    AudioEngine engine;
    entt::registry registry;
    const auto entity = registry.create();
    registry.emplace<TransformComponent>(entity);
    auto& source = registry.emplace<AudioSourceComponent>(entity);
    source.soundFile = first;
    source.loop = true;
    source.isPlaying = true;

    AudioSystem::Update(registry, engine, 0.016f);

    if (registry.get<AudioSourceComponent>(entity).voice != AudioEngine::kInvalidVoice) {
        CHECK_MSG(engine.PathOf(registry.get<AudioSourceComponent>(entity).voice) == first,
                  "the voice must start on the file the source names");

        registry.get<AudioSourceComponent>(entity).soundFile = second;
        AudioSystem::Update(registry, engine, 0.016f);

        CHECK_MSG(engine.PathOf(registry.get<AudioSourceComponent>(entity).voice) == second,
                  "and must follow when that name changes, with no toggle of Playing");
    }

    // Device-free, and load-bearing: if a handle nothing started named some
    // file, the comparison above would differ every frame and restart the
    // voice every frame for the whole run.
    CHECK_MSG(engine.PathOf(AudioEngine::kInvalidVoice).empty(),
              "a handle nothing started must name no file");

    std::remove(first.c_str());
    std::remove(second.c_str());
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
    testUnloadClipMakesTheNextLoadReadDiskAgain();
    testReloadClipLetsASourceThatGaveUpTryAgain();
    testReloadClipStopsTheVoiceReadingTheOldSamples();
    testAVoiceIsFoundByWhatItPlaysNotByWhatItsSourceNamesNow();
    testALoopingSourceFollowsAChangedSoundFile();
}

TEST_MAIN("test_audio", 19)
