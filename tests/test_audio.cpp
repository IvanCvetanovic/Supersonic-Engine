// Tests for the sound decoders behind the audio system.
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
#include "core/Mp3Decode.hpp"
#include "Mp3Fixtures.hpp"

#include <algorithm>
#include <cctype>
#include <chrono>
#include <climits>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <thread>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iterator>
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

// --- Clips that were never on disk ---------------------------------------
//
// A game may GENERATE its sound rather than ship it. Until AddClip there was no
// door into the mixer that did not take a path, so a game whose whole audio is
// synthesised could produce correct samples and play none of them.

void testAClipBuiltInMemoryPlaysLikeAFile() {
    AudioEngine engine;

    AudioClip clip;
    clip.channels = 1;
    clip.sampleRate = 44100;
    clip.bitsPerSample = 16;
    clip.pcm.assign(400, 0);
    clip.pcm[0] = 0xCC;
    clip.pcm[1] = 0x2C;   // 11468, the first sample of Wolf Brigade's train tone

    const AudioClip* added = engine.AddClip("generated:train", std::move(clip));
    CHECK_MSG(added != nullptr, "a valid in-memory clip is accepted");
    if (added == nullptr) return;

    CHECK_MSG(engine.HasClip("generated:train"), "and the name now resolves");

    // THE POINT: LoadClip consults the cache before the filesystem, so a name
    // that was never a path resolves without one - which is what makes Play()
    // work unchanged.
    const AudioClip* found = engine.LoadClip("generated:train");
    CHECK_MSG(found == added, "LoadClip returns it without touching the disk");
    if (found == nullptr) return;

    CHECK_EQ(static_cast<int>(found->pcm.size()), 400);
    CHECK_EQ(static_cast<int>(found->sampleRate), 44100);
    CHECK_EQ(static_cast<int>(found->pcm[0]), 0xCC);
    CHECK_EQ(static_cast<int>(found->pcm[1]), 0x2C);
}

void testAnEmptyClipIsRefusedRatherThanCached() {
    // The opposite of what LoadClip does for a missing file, deliberately. A
    // file that will not decode is worth remembering so the loader stops
    // re-opening it; a caller handing over an empty buffer has a bug NOW.
    AudioEngine engine;

    CHECK_MSG(engine.AddClip("generated:silence", AudioClip{}) == nullptr,
              "an empty clip is refused");
    CHECK_MSG(!engine.HasClip("generated:silence"),
              "and refusing it did not cache a failure under the name");

    // Which means a later, valid registration under the same name still works.
    AudioClip real;
    real.channels = 1;
    real.sampleRate = 44100;
    real.bitsPerSample = 16;
    real.pcm.assign(64, 7);
    CHECK_MSG(engine.AddClip("generated:silence", std::move(real)) != nullptr,
              "the name was not poisoned");
}

void testARegisteredClipCanBeReplacedWhileNothingIsPlayingIt() {
    AudioEngine engine;

    AudioClip first;
    first.channels = 1;
    first.sampleRate = 44100;
    first.bitsPerSample = 16;
    first.pcm.assign(100, 1);
    CHECK_MSG(engine.AddClip("generated:tone", std::move(first)) != nullptr, "registered");

    // Re-synthesised after a data edit: the same name, a different buffer.
    AudioClip second;
    second.channels = 1;
    second.sampleRate = 44100;
    second.bitsPerSample = 16;
    second.pcm.assign(250, 2);
    const AudioClip* replaced = engine.AddClip("generated:tone", std::move(second));

    CHECK_MSG(replaced != nullptr, "replacing a clip nothing is reading is allowed");
    if (replaced == nullptr) return;
    CHECK_EQ(static_cast<int>(replaced->pcm.size()), 250);
    CHECK_EQ(static_cast<int>(engine.LoadClip("generated:tone")->pcm.size()), 250);
}

void testAnUnregisteredNameStillFailsRatherThanInventingSilence() {
    AudioEngine engine;
    CHECK_MSG(!engine.HasClip("generated:never_registered"), "an unknown name resolves to nothing");
    CHECK_MSG(engine.LoadClip("generated:never_registered") == nullptr,
              "and loading it fails rather than returning an empty clip");
}


// --- Reaping finished voices ---------------------------------------------

static void testReapingAnEngineWithNoVoicesIsANoOp() {
    // The base case, and it is not free: an implementation that walked the
    // backend's voice table instead of the path map would trip over an empty
    // one, and an implementation that reaped optimistically would report work
    // it did not do.
    AudioEngine engine;
    CHECK_EQ(engine.ReapFinishedVoices(), size_t{0});
    CHECK_EQ(engine.ReapFinishedVoices(), size_t{0});
}

static void testALoopingVoiceIsNeverFinishedAndIsNeverReaped() {
    // The property that makes reaping safe to run every frame. A looping voice
    // always has a buffer queued, so it is never finished - and music, which is
    // the thing that loops, is exactly the voice a game would be worst served
    // by losing.
    const std::string path = "test_audio_reap_loop_tmp.wav";
    writeWav(path, 22050, false);

    AudioEngine engine;
    const AudioEngine::VoiceId voice = engine.Play(path, true, 1.0f, 1.0f);

    if (voice != AudioEngine::kInvalidVoice) {
        // Needs a real output device, so these are not counted in the floor -
        // without one Play returns kInvalidVoice and there is no voice to keep.
        CHECK_EQ(engine.ReapFinishedVoices(), size_t{0});
        CHECK_MSG(engine.PathOf(voice) == path,
                  "a looping voice keeps its place in the table");
        CHECK_MSG(engine.IsVoicePlaying(voice), "and keeps playing");
        engine.Stop(voice);
    }

    std::remove(path.c_str());
}

static void testAFinishedOneShotIsFreedRatherThanKept() {
    // THE LEAK. Play mints a voice per call and only Stop ever freed one, and
    // the only caller that stops anything is AudioSystem, for voices owned by a
    // component. A game playing fire-and-forget one-shots - an event-driven
    // sound design - therefore accumulated a live backend voice per sound for
    // the whole session.
    //
    // A tiny clip so it finishes on its own within the window below. The wait
    // is bounded and polled rather than slept, so on a machine where the sound
    // finishes instantly this costs nothing.
    AudioClip clip;
    clip.sampleRate = 8000;
    clip.channels = 1;
    clip.bitsPerSample = 16;
    clip.pcm.assign(160 * sizeof(int16_t), 0);   // 20 ms of silence

    AudioEngine engine;
    CHECK_MSG(engine.AddClip("generated:reap", std::move(clip)) != nullptr,
              "a generated clip registers");

    const AudioEngine::VoiceId voice = engine.Play("generated:reap", false, 1.0f, 1.0f);

    if (voice != AudioEngine::kInvalidVoice) {
        // Not counted in the floor: needs a real output device.
        CHECK_MSG(engine.PathOf(voice) == "generated:reap",
                  "the engine is tracking it while it plays");

        bool finished = false;
        for (int i = 0; i < 400 && !finished; ++i) {
            finished = !engine.IsVoicePlaying(voice);
            if (!finished) std::this_thread::sleep_for(std::chrono::milliseconds(5));
        }
        CHECK_MSG(finished, "twenty milliseconds of audio finishes within two seconds");

        if (finished) {
            CHECK_EQ(engine.ReapFinishedVoices(), size_t{1});
            CHECK_MSG(engine.PathOf(voice).empty(),
                      "and the engine has forgotten it, got \"" + engine.PathOf(voice) + "\"");
            CHECK_EQ(engine.ReapFinishedVoices(), size_t{0});
        }
    }
}


static void testAClipWithNoBitDepthIsRefusedRatherThanSilent() {
    // It used to be accepted, cached, and then silent: the backend computes its
    // block alignment as channels * (bitsPerSample / 8), gets zero, and
    // CreateSourceVoice refuses it - so Play returned kInvalidVoice for a clip
    // the engine had said was fine.
    //
    // Only a game that SYNTHESISES its audio can reach it. A decoder that got
    // as far as samples had already read the format chunk.
    AudioEngine engine;

    AudioClip clip;
    clip.channels = 1;
    clip.sampleRate = 44100;
    clip.pcm.assign(400, 0);
    // bitsPerSample deliberately left at its default.

    CHECK_MSG(!clip.valid(), "a clip with no bit depth is not a valid clip");
    CHECK_MSG(engine.AddClip("generated:depthless", std::move(clip)) == nullptr,
              "so the engine refuses it at the door");
    CHECK_MSG(!engine.HasClip("generated:depthless"),
              "rather than caching something that can never sound");
}

// --- Deciding a decoder by the file's name --------------------------------
//
// The engine reads WAV, Ogg Vorbis and MP3. What it must never do is guess:
// a name it does not know is refused by name rather than fed to the WAV
// parser, which would report "not a RIFF/WAVE file" and send whoever reads
// that message looking in the wrong place.
//
// The MP3 decode itself is proved further down, against the synthetic streams
// embedded in Mp3Fixtures.hpp. A game's own recordings are proved in that
// game's suites, which know where the originals are and skip when they are
// absent; this one links the engine alone.
void testAnUnknownSoundExtensionIsRefusedByName() {
    AudioClip clip;
    std::string error;

    CHECK_MSG(!AudioClip::Load("music/theme.flac", clip, error), "a .flac is not read");
    CHECK_MSG(error.find("theme.flac") != std::string::npos,
              "and the message names the file: " + error);
    CHECK(!clip.valid());

    // No extension at all is the same answer.
    CHECK(!AudioClip::Load("music/theme", clip, error));
    CHECK(!error.empty());
}

void testAMissingMp3FailsWithAReasonRatherThanCrashing() {
    // The path is decided by the extension, so this reaches LoadMp3 on every
    // platform: on Windows it is a file Media Foundation cannot open, and
    // elsewhere one the loader cannot open. Either way it must say so and
    // leave the clip empty.
    AudioClip clip;
    std::string error;
    CHECK(!AudioClip::LoadMp3("no_such_sound_file_here.mp3", clip, error));
    CHECK_MSG(!error.empty(), "a failed MP3 load explains itself");
    CHECK(!clip.valid());

    CHECK(!AudioClip::Load("no_such_sound_file_here.mp3", clip, error));
    CHECK(!clip.valid());
}

// --- Ogg Vorbis -----------------------------------------------------------
//
// stb_vorbis decodes and cannot encode, and no Vorbis file is shipped here, so
// this suite cannot make a valid one. What it can prove is the edge, which is
// where a decoder handed somebody else's file goes wrong: a name that is
// missing, empty, not Ogg at all, or Ogg that promises more than it holds is
// refused with a reason and an empty clip - never read past its end, never
// handed to the device.
//
// The real decode is proved against Penumbra's own sound effects, in that
// port's suites, which know where the original's files are - the same split as
// the mp3s above.

namespace {

// The header of the first page of an Ogg stream whose one packet is
// packetBytes long. The CRC is left zero: stb_vorbis checks page CRCs only
// when it searches for a page (seeking, or resynchronising pushed data), and
// these files are meant to be refused for what the page holds.
std::string oggFirstPageHeader(uint8_t packetBytes) {
    std::string page("OggS", 4);
    page.push_back('\0');                             // stream structure version
    page.push_back('\x02');                           // beginning of stream
    page.append(8, '\0');                             // granule position
    page.append("\x01\x02\x03\x04", 4);               // serial number
    page.append(4, '\0');                             // page sequence number
    page.append(4, '\0');                             // CRC
    page.push_back('\x01');                           // one segment,
    page.push_back(static_cast<char>(packetBytes));   // holding the whole packet
    return page;
}

void writeBytes(const std::string& path, const std::string& bytes) {
    std::ofstream f(path, std::ios::binary);
    f.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
}

// Valid before the load, so a failing load is seen to EMPTY the clip rather
// than leave the previous sound's samples in it.
AudioClip aClipWithSamples() {
    AudioClip clip;
    clip.channels = 1;
    clip.sampleRate = 44100;
    clip.bitsPerSample = 16;
    clip.pcm.assign(64, 1);
    return clip;
}

} // namespace

void testAMissingOggFailsWithAReasonRatherThanCrashing() {
    AudioClip clip = aClipWithSamples();
    std::string error;
    CHECK(!AudioClip::LoadOgg("no_such_sound_file_here.ogg", clip, error));
    CHECK_MSG(error.find("no_such_sound_file_here.ogg") != std::string::npos,
              "a failed Ogg load names the file: " + error);
    CHECK_MSG(!clip.valid(), "and leaves the clip empty rather than holding the last sound");

    // Through Load, in capitals. The extension decides, case-insensitively, so
    // this must reach LoadOgg and not the refusal for names the engine does
    // not read - which would send whoever reads it off to convert a file the
    // engine can decode.
    clip = aClipWithSamples();
    error.clear();
    CHECK(!AudioClip::Load("NO_SUCH_SOUND_FILE_HERE.OGG", clip, error));
    CHECK_MSG(!error.empty() && error.find("not a sound this engine reads") == std::string::npos,
              "an .ogg is a sound the engine reads: " + error);
    CHECK(!clip.valid());
}

void testAFileThatIsNotOggVorbisIsRefusedCleanly() {
    // Four shapes of wrong, each deeper than the last: nothing at all; bytes
    // that are not Ogg; an Ogg stream that is Opus; and a real Vorbis
    // identification header with the file ending straight after it. The last
    // is the one that takes stb_vorbis past its first page and into reading
    // the next, so it is the one where a read past the end would show.
    std::string opus = oggFirstPageHeader(19);
    opus.append("OpusHead", 8);
    opus.push_back('\x01');                           // version
    opus.push_back('\x02');                           // channels
    opus.append("\x38\x01", 2);                       // pre-skip, 312
    opus.append("\x80\xBB\x00\x00", 4);               // 48000 Hz
    opus.append(2, '\0');                             // output gain
    opus.push_back('\0');                             // channel mapping family

    std::string vorbis = oggFirstPageHeader(30);
    vorbis.append("\x01vorbis", 7);
    vorbis.append(4, '\0');                           // vorbis_version
    vorbis.push_back('\x01');                         // channels
    vorbis.append("\x44\xAC\x00\x00", 4);             // 44100 Hz
    vorbis.append(12, '\0');                          // bitrates: maximum, nominal, minimum
    vorbis.push_back('\xB8');                         // block sizes 256 and 2048
    vorbis.push_back('\x01');                         // framing
    // ...and no comment or setup header: the file ends here.

    struct Case {
        const char* path;
        std::string bytes;
    };
    const Case cases[] = {
        { "test_audio_empty_tmp.ogg", std::string() },
        { "test_audio_text_tmp.ogg", "this is definitely not an ogg vorbis file, not even close" },
        { "test_audio_opus_tmp.ogg", opus },
        { "test_audio_headers_only_tmp.ogg", vorbis },
    };

    for (const Case& c : cases) {
        writeBytes(c.path, c.bytes);
        AudioClip clip = aClipWithSamples();
        std::string error;
        const bool ok = AudioClip::Load(c.path, clip, error);
        std::remove(c.path);

        CHECK_MSG(!ok, std::string(c.path) + " must be refused");
        CHECK_MSG(error.find(c.path) != std::string::npos,
                  "and the reason must name the file: " + error);
        CHECK_MSG(!clip.valid(), std::string(c.path) + " must leave the clip empty");
    }
}

// --- MP3 ------------------------------------------------------------------
//
// Off Windows the engine decodes MP3 itself (dr_mp3, through detail::DecodeMp3);
// on Windows LoadMp3 asks Media Foundation, and the same decoder is still
// compiled so the two can be compared on the same bytes. What can be said of both
// is said by the same code on both: the comparison of LoadMp3 with the decoder
// runs on Linux too, where they are one decoder and must agree exactly, which
// keeps the Windows half of it from being code that only ever ran once.
//
// The streams are synthetic (Mp3Fixtures.hpp). No game's sound is in this
// repository. A game's own are decoded by this executable's manual mode:
//
//     test_audio --mp3-dir <folder of .mp3 files>
//
// Each test names, under "Fails under", the change to the engine that turns it
// red. Those changes were made and run (the commit message lists them).

namespace {

using Bytes = std::vector<uint8_t>;

#if defined(_WIN32)
constexpr bool kDecodedByMediaFoundation = true;
#else
constexpr bool kDecodedByMediaFoundation = false;
#endif

// 1152 samples in an MPEG-1 frame (32, 44.1 and 48 kHz), 576 in MPEG-2 and 2.5.
size_t mpegFrameSamples(uint32_t sampleRate) {
    return sampleRate >= 32000 ? 1152 : 576;
}

// What dr_mp3 takes off the FRONT of a stream whose LAME header names an encoder
// delay of 576: the delay and the 529 samples its own filterbank adds.
constexpr size_t kLameFrontTrim = 576 + 529;

// HOW MUCH TWO CORRECT DECODERS MAY DIFFER IN LENGTH. Decided before the first
// Windows run, from the worst case below, and not widened since.
//   * One MPEG frame. Two decoders can disagree about the last frame, which one
//     finishes and the other drops, and about whether a frame the stream's first
//     bytes only half-describe is there at all.
//   * Where the stream carries a LAME header, dr_mp3 drops its Info frame, trims
//     the encoder delay and decoder delay from the front and the padding from the
//     back, so a gapless loop is gapless. A decoder that knows none of that plays
//     the Info frame as one more (silent) frame, keeps all of the delay and keeps
//     the padding: one frame, kLameFrontTrim samples and under one frame more, which
//     is two frames and kLameFrontTrim. That absorbs the one-frame disagreement above
//     as well; they are not added.
// (What the first Windows run then measured, for the next reader: Media Foundation
// cuts the first 528 samples of any stream, plays a LAME header's Info frame as a
// frame of silence, and keeps the encoder delay and the padding. Stereo with a
// header: 15600 frames against dr_mp3's 13230, a difference of 2370 in the 3409
// allowed. Without a header: 528 fewer than dr_mp3, in the 1152 allowed.)
[[maybe_unused]] size_t allowedLengthDifference(uint32_t sampleRate, bool lameHeader) {
    const size_t frame = mpegFrameSamples(sampleRate);
    return lameHeader ? 2 * frame + kLameFrontTrim : frame;
}

// HOW CLOSE THE WAVEFORMS MUST BE once the two are lined up. Two decoders that
// round their last bit differently agree to a fraction of a percent; 2% of the
// signal's own RMS is a wrong filterbank, a wrong gain or the wrong channel, not
// rounding.
constexpr double kMaxNormalizedRmsError = 0.02;
constexpr double kMinCorrelation = 0.999;

Bytes bytesOf(const uint8_t* first, size_t count) {
    return Bytes(first, first + count);
}

Bytes stereoTagged() {
    return bytesOf(Mp3Fixtures::kStereo44k, sizeof(Mp3Fixtures::kStereo44k));
}

Bytes monoTagged() {
    return bytesOf(Mp3Fixtures::kMono22k, sizeof(Mp3Fixtures::kMono22k));
}

// The first frame of kStereo44k is the Info frame: 144 * 64000 / 44100 = 208
// bytes. Without it the stream is what `lame -t` writes for the same audio
// (checked when the fixture was made), and its first frame is audio.
constexpr size_t kStereoInfoFrameBytes = 208;

// And kMono22k's: 72 * 64000 / 22050 = 208 bytes with the padding bit clear.
constexpr size_t kMonoInfoFrameBytes = 208;

Bytes stereoUntagged() {
    Bytes b = stereoTagged();
    b.erase(b.begin(), b.begin() + static_cast<std::ptrdiff_t>(kStereoInfoFrameBytes));
    return b;
}

Bytes monoUntagged() {
    Bytes b = monoTagged();
    b.erase(b.begin(), b.begin() + static_cast<std::ptrdiff_t>(kMonoInfoFrameBytes));
    return b;
}

// kMono24k has no tag: every frame of it is audio.
Bytes mono24Untagged() {
    return bytesOf(Mp3Fixtures::kMono24k, sizeof(Mp3Fixtures::kMono24k));
}

Bytes concat(const Bytes& first, const Bytes& second) {
    Bytes b = first;
    b.insert(b.end(), second.begin(), second.end());
    return b;
}

// An ID3v2.3 tag of `payload` zero bytes: "ID3", version 3.0, no flags, and the
// size as four 7-bit bytes.
Bytes withId3v2(const Bytes& mp3, size_t payload) {
    Bytes b;
    for (const char c : {'I', 'D', '3'}) b.push_back(static_cast<uint8_t>(c));
    b.push_back(3);
    b.push_back(0);
    b.push_back(0);
    b.push_back(static_cast<uint8_t>((payload >> 21) & 0x7F));
    b.push_back(static_cast<uint8_t>((payload >> 14) & 0x7F));
    b.push_back(static_cast<uint8_t>((payload >> 7) & 0x7F));
    b.push_back(static_cast<uint8_t>(payload & 0x7F));
    b.insert(b.end(), payload, uint8_t{0});
    return concat(b, mp3);
}

// An ID3v1 tag: "TAG" and 125 more bytes, behind the stream.
Bytes withId3v1(const Bytes& mp3) {
    Bytes b = mp3;
    for (const char c : {'T', 'A', 'G'}) b.push_back(static_cast<uint8_t>(c));
    b.insert(b.end(), 125, uint8_t{0});
    return b;
}

// Where the Info frame keeps its count of frames: the four bytes after "Info"
// and its four flag bytes. Searched for in the first 64 bytes, which is where
// MPEG-1 stereo (offset 36) and MPEG-2 mono (offset 13) put the tag.
size_t infoFrameCountOffset(const Bytes& mp3) {
    const char tag[] = {'I', 'n', 'f', 'o'};
    const auto end = mp3.begin() + static_cast<std::ptrdiff_t>(std::min<size_t>(64, mp3.size()));
    const auto at = std::search(mp3.begin(), end, std::begin(tag), std::end(tag));
    CHECK_MSG(at != end, "the fixture has an Info frame");
    return static_cast<size_t>(at - mp3.begin()) + 8;
}

Bytes withClaimedFrameCount(Bytes mp3, uint32_t claimed) {
    const size_t at = infoFrameCountOffset(mp3);
    if (at + 4 > mp3.size()) return mp3;
    for (int i = 0; i < 4; ++i) {
        mp3[at + static_cast<size_t>(i)] = static_cast<uint8_t>(claimed >> (24 - 8 * i));
    }
    return mp3;
}

// 4 KB with no 0xFF in it: an MP3 frame header begins with eleven set bits, so no
// decoder can find a frame in this however forgiving it is.
Bytes garbageWithoutSync() {
    Bytes b(4096);
    for (size_t i = 0; i < b.size(); ++i) b[i] = static_cast<uint8_t>(0x20 + (i * 7) % 0x60);
    return b;
}

// An absolute path in the working directory. Media Foundation is given what the
// engine is given as a URL to resolve, and a bare relative name is not something
// to rely on it opening the way a file open does; an absolute one is.
std::string tempPath(const char* name) {
    return (std::filesystem::current_path() / name).string();
}

// A path no earlier case used. Media Foundation can still hold a file for a moment
// after the load that read it has returned, and a later case reusing the name would
// then write over a file that is open, or read the previous case's bytes.
std::string freshTempPath(const char* stem, const char* extension) {
    static int counter = 0;
    return tempPath((std::string(stem) + std::to_string(counter++) + "_tmp" + extension).c_str());
}

void writeBinary(const std::string& path, const Bytes& bytes) {
    std::ofstream f(path, std::ios::binary);
    f.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
}

Bytes readBinary(const std::string& path) {
    std::ifstream f(path, std::ios::binary);
    return Bytes((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
}

// DecodeMp3 over a whole vector, with the engine's default cap unless one is given.
bool decode(const Bytes& mp3, detail::Mp3Pcm& out, std::string& reason,
            size_t cap = detail::kMp3MaxPcmBytes) {
    return detail::DecodeMp3(mp3.data(), mp3.size(), out, reason, cap);
}

int16_t sampleAt(const Bytes& pcm, size_t frame, unsigned channels, unsigned channel) {
    const size_t i = 2 * (frame * channels + channel);
    return static_cast<int16_t>(static_cast<uint16_t>(pcm[i] | (pcm[i + 1] << 8)));
}

std::vector<int16_t> samplesOf(const Bytes& pcm) {
    std::vector<int16_t> s(pcm.size() / 2);
    for (size_t i = 0; i < s.size(); ++i) {
        s[i] = static_cast<int16_t>(static_cast<uint16_t>(pcm[2 * i] | (pcm[2 * i + 1] << 8)));
    }
    return s;
}

struct ToneStats {
    int peak{0};
    double hz{0.0};
};

// Peak amplitude, and the frequency from the spacing of the upward zero
// crossings: crossings - 1 periods between the first and the last.
//
// Counted only where the tone is at least half its peak. A fixed margin at each
// end was not enough: Media Foundation plays a LAME header's Info frame as a frame
// of silence and keeps the encoder's delay, so its clip of the same stream opens
// with about 1700 frames in which the codec has not yet reached the tone, and the
// stray crossings in them read as 450 Hz where the tone is 440. The first Windows
// run said so; the test below reproduces it.
ToneStats measureTone(const Bytes& pcm, unsigned channels, unsigned channel, uint32_t sampleRate) {
    ToneStats t;
    const size_t frames = pcm.size() / (2u * channels);
    for (size_t f = 0; f < frames; ++f) t.peak = std::max(t.peak, std::abs(sampleAt(pcm, f, channels, channel)));

    size_t strongFirst = frames;
    size_t strongLast = 0;
    for (size_t f = 0; f < frames; ++f) {
        if (std::abs(sampleAt(pcm, f, channels, channel)) * 2 >= t.peak) {
            if (strongFirst == frames) strongFirst = f;
            strongLast = f;
        }
    }

    size_t crossings = 0;
    size_t first = 0;
    size_t last = 0;
    int previous = 0;
    for (size_t f = 0; f < frames; ++f) {
        const int s = sampleAt(pcm, f, channels, channel);
        if (f > strongFirst && f <= strongLast && previous < 0 && s >= 0) {
            if (crossings == 0) first = f;
            last = f;
            ++crossings;
        }
        previous = s;
    }
    if (crossings > 1) {
        t.hz = static_cast<double>(crossings - 1) * sampleRate / static_cast<double>(last - first);
    }
    return t;
}

bool within(double value, double expected, double relativeTolerance) {
    return std::fabs(value - expected) <= expected * relativeTolerance;
}

// --- Lining two decodes up and measuring how far apart they are -------------

struct Alignment {
    int lag{0};               // a[i] is b[i + lag]: how many frames b is LATE
    double correlation{0.0};  // over the whole overlap, all channels
    double rmsError{0.0};
    double referenceRms{0.0};
    size_t overlapFrames{0};
    double normalizedError() const { return referenceRms > 0.0 ? rmsError / referenceRms : rmsError; }
};

// The lag is found on the loudest 8192 frames of `a` (channel 0), by normalised
// cross-correlation over every lag within maxLag, rather than over the whole
// track: brute force over a minute of audio is 4801 lags times 2.7 million frames.
// The error and the correlation are then taken over the whole overlap at that lag.
Alignment alignAndCompare(const std::vector<int16_t>& a, const std::vector<int16_t>& b, unsigned channels,
                          int maxLag) {
    Alignment r;
    const auto ch = static_cast<std::ptrdiff_t>(channels);
    const auto aFrames = static_cast<std::ptrdiff_t>(a.size()) / ch;
    const auto bFrames = static_cast<std::ptrdiff_t>(b.size()) / ch;
    if (aFrames < 16 || bFrames < 16) return r;

    const std::ptrdiff_t window = std::min<std::ptrdiff_t>(8192, aFrames / 2);
    std::ptrdiff_t start = 0;
    double loudest = -1.0;
    for (std::ptrdiff_t s = 0; s + window <= aFrames; s += 1024) {
        double e = 0.0;
        for (std::ptrdiff_t i = 0; i < window; ++i) {
            const double v = a[static_cast<size_t>((s + i) * ch)];
            e += v * v;
        }
        if (e > loudest) {
            loudest = e;
            start = s;
        }
    }
    double ea = 0.0;
    for (std::ptrdiff_t i = 0; i < window; ++i) {
        const double v = a[static_cast<size_t>((start + i) * ch)];
        ea += v * v;
    }

    double bestCorrelation = -2.0;
    std::ptrdiff_t bestLag = 0;
    for (std::ptrdiff_t lag = -maxLag; lag <= maxLag; ++lag) {
        const std::ptrdiff_t from = start + lag;
        if (from < 0 || from + window > bFrames) continue;
        double dot = 0.0;
        double eb = 0.0;
        for (std::ptrdiff_t i = 0; i < window; ++i) {
            const double x = a[static_cast<size_t>((start + i) * ch)];
            const double y = b[static_cast<size_t>((from + i) * ch)];
            dot += x * y;
            eb += y * y;
        }
        const double c = (ea > 0.0 && eb > 0.0) ? dot / std::sqrt(ea * eb) : 0.0;
        if (c > bestCorrelation) {
            bestCorrelation = c;
            bestLag = lag;
        }
    }
    r.lag = static_cast<int>(bestLag);

    const std::ptrdiff_t lo = std::max<std::ptrdiff_t>(0, -bestLag);
    const std::ptrdiff_t hi = std::min<std::ptrdiff_t>(aFrames, bFrames - bestLag);
    if (hi <= lo) return r;
    double diff = 0.0;
    double aa = 0.0;
    double bb = 0.0;
    double ab = 0.0;
    for (std::ptrdiff_t i = lo; i < hi; ++i) {
        for (std::ptrdiff_t c = 0; c < ch; ++c) {
            const double x = a[static_cast<size_t>(i * ch + c)];
            const double y = b[static_cast<size_t>((i + bestLag) * ch + c)];
            diff += (x - y) * (x - y);
            aa += x * x;
            bb += y * y;
            ab += x * y;
        }
    }
    const double n = static_cast<double>((hi - lo) * ch);
    r.overlapFrames = static_cast<size_t>(hi - lo);
    r.rmsError = std::sqrt(diff / n);
    r.referenceRms = std::sqrt(aa / n);
    r.correlation = (aa > 0.0 && bb > 0.0) ? ab / std::sqrt(aa * bb) : 0.0;
    return r;
}

// --- The engine's own load of an MP3 against its own decoder ---------------

struct Mp3Comparison {
    bool loadOk{false};
    bool decodeOk{false};
    std::string loadError;
    std::string decodeReason;
    AudioClip clip;           // what AudioClip::LoadMp3 returned
    detail::Mp3Pcm decoded;   // what detail::DecodeMp3 returned for the same bytes
    double loadMs{0.0};
    double decodeMs{0.0};
    Alignment alignment;
};

// AudioClip::LoadMp3 (Media Foundation on Windows, dr_mp3 elsewhere) against
// detail::DecodeMp3 (dr_mp3 everywhere), on one file.
Mp3Comparison compareLoadWithDecoder(const std::string& path) {
    using Clock = std::chrono::steady_clock;
    Mp3Comparison c;

    auto t0 = Clock::now();
    c.loadOk = AudioClip::LoadMp3(path, c.clip, c.loadError);
    c.loadMs = std::chrono::duration<double, std::milli>(Clock::now() - t0).count();

    const Bytes bytes = readBinary(path);
    t0 = Clock::now();
    c.decodeOk = detail::DecodeMp3(bytes.data(), bytes.size(), c.decoded, c.decodeReason);
    c.decodeMs = std::chrono::duration<double, std::milli>(Clock::now() - t0).count();

    if (c.loadOk && c.decodeOk && c.clip.channels == c.decoded.channels &&
        c.clip.sampleRate == c.decoded.sampleRate && c.clip.bitsPerSample == 16) {
        // Media Foundation's start can be a whole frame plus the encoder's delay
        // from dr_mp3's trimmed one, and a lag outside that is no alignment at all.
        const int maxLag = static_cast<int>(2 * 1152 + kLameFrontTrim + 64);
        c.alignment = alignAndCompare(samplesOf(c.decoded.pcm), samplesOf(c.clip.pcm), c.clip.channels, maxLag);
    }
    return c;
}

// A clip of a different sound than any test below loads, valid before a load, so
// a failing load is seen to EMPTY it.
AudioClip someOtherClip() {
    AudioClip clip;
    clip.channels = 1;
    clip.sampleRate = 8000;
    clip.bitsPerSample = 16;
    clip.pcm.assign(32, 9);
    return clip;
}

struct Rng {
    uint32_t state;
    uint32_t next() {
        state = state * 1664525u + 1013904223u;
        return state >> 8;
    }
};

// One damaged copy of `base`: cut short, bits flipped, a span overwritten,
// zeroed, filled with 0xFF (every byte a frame header's first), or doubled.
Bytes damaged(const Bytes& base, Rng& rng) {
    Bytes b = base;
    if (b.empty()) return b;
    // Half the damage lands in the first 64 bytes, where the headers are.
    auto where = [&]() -> size_t {
        const size_t limit = (rng.next() & 1u) ? std::min<size_t>(64, b.size()) : b.size();
        return rng.next() % limit;
    };
    switch (rng.next() % 6) {
    case 0:
        b.resize(rng.next() % (b.size() + 1));
        break;
    case 1:
        for (uint32_t n = 1 + rng.next() % 8; n > 0; --n) {
            b[where()] ^= static_cast<uint8_t>(1u << (rng.next() % 8));
        }
        break;
    case 2:
    case 3:
    case 4: {
        const size_t at = where();
        const size_t len = std::min<size_t>(1 + rng.next() % 64, b.size() - at);
        const uint32_t kind = rng.next() % 3;
        for (size_t i = 0; i < len; ++i) {
            b[at + i] = kind == 0 ? static_cast<uint8_t>(rng.next()) : (kind == 1 ? uint8_t{0} : uint8_t{0xFF});
        }
        break;
    }
    default: {
        const size_t at = where();
        const size_t len = std::min<size_t>(1 + rng.next() % 256, b.size() - at);
        const Bytes span(b.begin() + static_cast<std::ptrdiff_t>(at),
                         b.begin() + static_cast<std::ptrdiff_t>(at + len));
        b.insert(b.begin() + static_cast<std::ptrdiff_t>(at), span.begin(), span.end());
        break;
    }
    }
    return b;
}

// What must hold of ANY answer the decoder gives, however the input was damaged.
// Returns false (and says why) when it does not.
bool decoderAnswerIsSane(bool ok, const detail::Mp3Pcm& out, const std::string& reason, size_t cap,
                         std::string& why) {
    if (!ok) {
        if (reason.empty()) why = "refused without a reason";
        else if (out.channels != 0 || !out.pcm.empty()) why = "refused but left audio in the output";
        return why.empty();
    }
    if (out.channels < 1 || out.channels > 2) why = "channel count " + std::to_string(out.channels);
    else if (out.sampleRate < 8000 || out.sampleRate > 48000) why = "sample rate " + std::to_string(out.sampleRate);
    else if (out.pcm.empty()) why = "accepted with no audio";
    else if (out.pcm.size() % (2u * out.channels) != 0) why = "a partial frame of PCM";
    else if (out.pcm.size() > cap) why = "more PCM than the cap";
    else if (out.pcm.capacity() > cap) why = "more capacity than the cap";
    return why.empty();
}

} // namespace

static void testTheDecoderReadsAStereoToneFaithfully() {
    // Fails under: the sample bytes written high byte first (peak and both
    // frequencies are nonsense); the channel count recorded as 1 (the channel
    // check and the length); the encoder delay not trimmed (the length); the last
    // chunk of the output dropped (the length).
    const Bytes mp3 = stereoTagged();
    detail::Mp3Pcm out;
    std::string reason;
    const bool ok = detail::DecodeMp3(mp3.data(), mp3.size(), out, reason);
    CHECK_MSG(ok, reason);
    if (!ok) return;

    CHECK_EQ(out.channels, uint16_t{2});
    CHECK_EQ(out.sampleRate, uint32_t{44100});
    // EXACT, not within a frame: dr_mp3 trims what the LAME header names, which
    // leaves the 13230 frames that were encoded. This is the engine's claim about
    // gapless loops, and the one place the decoder's trimming is pinned.
    CHECK_EQ(out.pcm.size(), size_t{13230} * 4);

    const ToneStats left = measureTone(out.pcm, 2, 0, 44100);
    const ToneStats right = measureTone(out.pcm, 2, 1, 44100);
    CHECK_MSG(within(left.hz, 440.0, 0.01), "left is 440 Hz, got " + std::to_string(left.hz));
    CHECK_MSG(within(right.hz, 880.0, 0.01), "right is 880 Hz, got " + std::to_string(right.hz));
    // Encoded at 16384. A lossy codec lands within a few percent (15835 and
    // 15980 here); 10% is the allowance, and a byte-swapped or mis-scaled sample
    // is nowhere near it.
    CHECK_MSG(left.peak >= 14745 && left.peak <= 18022, "left peak " + std::to_string(left.peak));
    CHECK_MSG(right.peak >= 14745 && right.peak <= 18022, "right peak " + std::to_string(right.peak));
}

static void testTheDecoderReadsAMonoMpeg2Stream() {
    // The other MPEG version: 576-sample frames, half the sample rate, one channel.
    // Fails under: the channel count recorded as 2 (the channel check, and the
    // damaged-stream and load tests that read this stream); a sample rate fixed at
    // 44100; the encoder delay not trimmed (the length).
    const Bytes mp3 = monoTagged();
    detail::Mp3Pcm out;
    std::string reason;
    const bool ok = detail::DecodeMp3(mp3.data(), mp3.size(), out, reason);
    CHECK_MSG(ok, reason);
    if (!ok) return;

    CHECK_EQ(out.channels, uint16_t{1});
    CHECK_EQ(out.sampleRate, uint32_t{22050});
    CHECK_EQ(out.pcm.size(), size_t{6615} * 2);
    const ToneStats tone = measureTone(out.pcm, 1, 0, 22050);
    CHECK_MSG(within(tone.hz, 1000.0, 0.01), "1000 Hz, got " + std::to_string(tone.hz));
    CHECK_MSG(tone.peak >= 14745 && tone.peak <= 18022, "peak " + std::to_string(tone.peak));
}

static void testAStreamWithoutALameHeaderIsNotTrimmed() {
    // No header, so nothing to trim by: all 13 frames, 13 * 1152 samples, and the
    // same audio the tagged stream has.
    // Fails under: a trim applied where the stream names none (a fixed skip of
    // LAME's usual 1105 samples).
    const Bytes mp3 = stereoUntagged();
    detail::Mp3Pcm out;
    std::string reason;
    const bool ok = detail::DecodeMp3(mp3.data(), mp3.size(), out, reason);
    CHECK_MSG(ok, reason);
    if (!ok) return;
    CHECK_EQ(out.channels, uint16_t{2});
    CHECK_EQ(out.sampleRate, uint32_t{44100});
    CHECK_EQ(out.pcm.size(), size_t{13} * 1152 * 4);
    const ToneStats right = measureTone(out.pcm, 2, 1, 44100);
    CHECK_MSG(within(right.hz, 880.0, 0.01), "right is 880 Hz, got " + std::to_string(right.hz));
}

static void testTagsAroundAStreamAreSkipped() {
    // An ID3v2 tag in front and an ID3v1 tag behind change nothing about the
    // audio: byte-identical PCM. Both are common on files from a tagger.
    // Fails under: dr_mp3 not skipping the ID3v2 tag (mutated in a scratch copy of
    // the header: the audio then differs, whatever the loader does). There is no
    // engine code of ours to mutate here, so this is the guard for an upgrade of
    // the vendored decoder.
    const Bytes plain = stereoTagged();
    detail::Mp3Pcm reference;
    std::string reason;
    const bool plainOk = decode(plain, reference, reason);
    CHECK_MSG(plainOk, reason);

    detail::Mp3Pcm withFront;
    const bool frontOk = decode(withId3v2(plain, 300), withFront, reason);
    CHECK_MSG(frontOk, reason);
    CHECK_MSG(withFront.pcm == reference.pcm, "an ID3v2 tag in front changes the audio");

    detail::Mp3Pcm withBack;
    const bool backOk = decode(withId3v1(plain), withBack, reason);
    CHECK_MSG(backOk, reason);
    CHECK_MSG(withBack.pcm == reference.pcm, "an ID3v1 tag behind changes the audio");

    detail::Mp3Pcm withBoth;
    const bool bothOk = decode(withId3v1(withId3v2(plain, 40)), withBoth, reason);
    CHECK_MSG(bothOk, reason);
    CHECK_MSG(withBoth.pcm == reference.pcm, "both tags together change the audio");
}

static void testAnAbsurdLengthInTheHeaderIsNotBelieved() {
    // The Info frame says how many frames the stream has. A hostile file says
    // billions; what the decoder produces and allocates comes from the frames that
    // are actually there, never from the claim.
    // Fails under: sizing the output from the header's count (a reserve of the
    // claimed length throws, and the suite stops).
    const size_t cap = detail::kMp3MaxPcmBytes;
    const uint32_t claims[] = {0u, 1u, 13u, 0x7FFFFFFEu, 0xFFFFFFFFu};
    for (const uint32_t claim : claims) {
        const Bytes mp3 = withClaimedFrameCount(stereoTagged(), claim);
        detail::Mp3Pcm out;
        std::string reason;
        const bool ok = detail::DecodeMp3(mp3.data(), mp3.size(), out, reason);
        std::string why;
        CHECK_MSG(decoderAnswerIsSane(ok, out, reason, cap, why),
                  "a frame count of " + std::to_string(claim) + ": " + why);
        if (ok) {
            // There are 13 audio frames in the stream. Whatever is claimed, no
            // more than that can come out.
            CHECK_MSG(out.pcm.size() <= size_t{13} * 1152 * 4,
                      "a frame count of " + std::to_string(claim) + " produced " +
                          std::to_string(out.pcm.size()) + " bytes");
            CHECK_MSG(out.pcm.capacity() <= size_t{4} * 1024 * 1024,
                      "a frame count of " + std::to_string(claim) + " allocated " +
                          std::to_string(out.pcm.capacity()) + " bytes");
        }
    }
}

static void testTheDecoderRefusesWhatIsNotAnMp3WithAReason() {
    // Fails under: no check for an empty input (dr_mp3 refuses it too, but with
    // the "not an MP3" wording, and the first assertion pins the plainer one); an
    // `out` that is not emptied on failure; the INT_MAX check removed (the last
    // case, which would hand dr_mp3 a length it cannot count, and the suite
    // crashes); an empty result accepted (the Info-only stream).
    detail::Mp3Pcm out;
    std::string reason;

    out.channels = 2;
    out.pcm.assign(8, 1);
    CHECK(!detail::DecodeMp3(nullptr, 0, out, reason));
    CHECK_MSG(reason == "is empty", "nothing at all: " + reason);
    CHECK_MSG(out.channels == 0 && out.pcm.empty(), "a refusal leaves the output empty");

    const Bytes junk = garbageWithoutSync();
    out.pcm.assign(8, 1);
    CHECK(!detail::DecodeMp3(junk.data(), junk.size(), out, reason));
    CHECK_MSG(reason.find("not an MP3") != std::string::npos, "4 KB of text: " + reason);
    CHECK_MSG(out.pcm.empty(), "and the output is empty");

    // Fewer bytes than a frame header and an ID3 header together.
    const Bytes tiny = bytesOf(Mp3Fixtures::kStereo44k, 6);
    CHECK(!detail::DecodeMp3(tiny.data(), tiny.size(), out, reason));
    CHECK_MSG(!reason.empty(), "six bytes of a stream");

    // Only the Info frame: a valid frame, and no audio behind it.
    const Bytes infoOnly = bytesOf(Mp3Fixtures::kStereo44k, kStereoInfoFrameBytes);
    CHECK(!detail::DecodeMp3(infoOnly.data(), infoOnly.size(), out, reason));
    CHECK_MSG(!reason.empty() && out.pcm.empty(), "a stream that is only its header: " + reason);

    if constexpr (sizeof(std::size_t) > 4) {
        // A length past INT_MAX over a few bytes: the check has to come before
        // anything reads, since the bytes are not there to read.
        const size_t claimed = static_cast<size_t>(INT_MAX) + 1;
        const Bytes small = stereoTagged();
        CHECK(!detail::DecodeMp3(small.data(), claimed, out, reason));
        CHECK_MSG(reason.find("too large") != std::string::npos, "past INT_MAX: " + reason);
    }
}

static void testATruncatedStreamDecodesAsFarAsItGoes() {
    // Cut anywhere, the decoder must not crash and must give one of two answers:
    // a refusal with a reason, or audio no longer than the whole stream's. Cut in
    // the middle it gives the frames that are there (LoadOgg does the same with
    // Vorbis).
    // Fails under: any read past the end of the input (run under ASan/UBSan, where
    // this loop is the test); a decoder that treats a short pull as an error and so
    // refuses a stream that was merely cut; an empty result accepted.
    for (const Bytes& whole : {stereoTagged(), monoTagged()}) {
        detail::Mp3Pcm full;
        std::string reason;
        const bool wholeOk = decode(whole, full, reason);
        CHECK_MSG(wholeOk, reason);

        size_t refused = 0;
        size_t shorter = 0;
        std::string firstProblem;
        for (size_t len = 0; len <= whole.size(); ++len) {
            detail::Mp3Pcm out;
            const bool ok = detail::DecodeMp3(whole.data(), len, out, reason);
            std::string why;
            if (!decoderAnswerIsSane(ok, out, reason, detail::kMp3MaxPcmBytes, why)) {
                if (firstProblem.empty()) firstProblem = "length " + std::to_string(len) + ": " + why;
            } else if (ok && out.pcm.size() > full.pcm.size()) {
                if (firstProblem.empty()) firstProblem = "length " + std::to_string(len) + " decodes to MORE than the whole";
            }
            if (!ok) ++refused;
            else if (out.pcm.size() < full.pcm.size()) ++shorter;
        }
        CHECK_MSG(firstProblem.empty(), firstProblem);
        // Both kinds of answer happen: the stream cut inside its first frames is
        // refused, and cut later it is shorter. A loop that never saw either would
        // be testing nothing.
        CHECK_MSG(refused > 0 && shorter > 0, "refused " + std::to_string(refused) + ", shorter " +
                                                std::to_string(shorter));
    }

    // And the midpoint, concretely: audio, shorter than the whole.
    const Bytes whole = stereoTagged();
    detail::Mp3Pcm full;
    detail::Mp3Pcm half;
    std::string reason;
    const bool fullOk = decode(whole, full, reason);
    CHECK_MSG(fullOk, reason);
    const bool ok = detail::DecodeMp3(whole.data(), whole.size() / 2, half, reason);
    CHECK_MSG(ok, "half of a stream: " + reason);
    CHECK_MSG(ok && !half.pcm.empty() && half.pcm.size() < full.pcm.size(), "and it is shorter");
}

static void testTheDecodedSizeIsCapped() {
    // The cap is exact: a stream that decodes to S bytes is refused at S - 1 and
    // accepted at S. 14976 frames of 16-bit stereo here.
    // Fails under: the cap check removed (the S - 1 case decodes); the cap made
    // exclusive (the S case is refused); the growth not held to the cap (the
    // capacity check).
    const Bytes mp3 = stereoUntagged();
    detail::Mp3Pcm all;
    std::string reason;
    const bool allOk = decode(mp3, all, reason);
    CHECK_MSG(allOk, reason);
    const size_t s = all.pcm.size();
    CHECK_EQ(s, size_t{14976} * 4);

    detail::Mp3Pcm out;
    CHECK(!detail::DecodeMp3(mp3.data(), mp3.size(), out, reason, s - 1));
    CHECK_MSG(reason.find("more than " + std::to_string(s - 1)) != std::string::npos,
              "the reason says what the cap was: " + reason);
    CHECK_MSG(out.pcm.empty(), "and nothing is kept");

    const bool exactOk = decode(mp3, out, reason, s);
    CHECK_MSG(exactOk, reason);
    CHECK_EQ(out.pcm.size(), s);
    // The allocation is held to the cap too, not just the size: a clamped
    // reserve, since a vector would grow 64 KiB past it by doubling.
    CHECK_MSG(out.pcm.capacity() <= s, "capacity " + std::to_string(out.pcm.capacity()) + " exceeds the cap " +
                                          std::to_string(s));

    // The default cap is a bound a game can live with and a hostile file cannot
    // abuse: under a gigabyte, over any clip anyone ships.
    CHECK(detail::kMp3MaxPcmBytes >= size_t{64} * 1024 * 1024);
    CHECK(detail::kMp3MaxPcmBytes <= size_t{1024} * 1024 * 1024);
}

static void testAllocationNeverExceedsTheCap() {
    // A longer decode, so the buffer grows several times: eleven copies of the
    // untagged stream. The cap is set 1000 bytes above what it decodes to, where
    // plain doubling from the 64 KiB first buffer would end at 1 MiB.
    // Fails under: the clamp in the growth removed (capacity is then 1 MiB).
    Bytes mp3;
    for (int i = 0; i < 11; ++i) mp3 = concat(mp3, stereoUntagged());

    detail::Mp3Pcm all;
    std::string reason;
    const bool allOk = decode(mp3, all, reason);
    CHECK_MSG(allOk, reason);
    const size_t cap = all.pcm.size() + 1000;
    CHECK_MSG(all.pcm.size() > 500 * 1024, "long enough to grow the buffer past 512 KiB");

    detail::Mp3Pcm out;
    const bool cappedOk = decode(mp3, out, reason, cap);
    CHECK_MSG(cappedOk, reason);
    CHECK_EQ(out.pcm.size(), all.pcm.size());
    CHECK_MSG(out.pcm.capacity() <= cap,
              "capacity " + std::to_string(out.pcm.capacity()) + " exceeds the cap " + std::to_string(cap));
}

static void testAFormatChangeIsRefused() {
    // A stereo 44.1 kHz stream with a mono 22.05 kHz one behind it. The rate and
    // channel count were read from the first frame, so playing the rest would be
    // wrong; the Media Foundation path refuses this by name and so does this one.
    // The other format is at the END, where the last pull sees it however long the
    // pulls are: how often the check runs is pinned by the test after this one.
    // Fails under: the format check after each pull removed (the stream decodes,
    // its second half at the wrong speed).
    const Bytes mp3 = concat(stereoUntagged(), monoTagged());
    detail::Mp3Pcm out;
    std::string reason;
    const bool ok = detail::DecodeMp3(mp3.data(), mp3.size(), out, reason);
    CHECK_MSG(!ok, "a stream that changes format is not played as one format");
    CHECK_MSG(reason.find("format") != std::string::npos, "and the reason says why: " + reason);
    CHECK_MSG(out.pcm.empty(), "with nothing kept");
}

static void testAFormatChangeInTheMiddleIsRefused() {
    // The format is checked once per pull of the decoder's output, so a stretch of
    // another format with the first format on both sides of it is caught only if
    // some pull ENDS inside the stretch. These are such streams: 13 or 14 frames of
    // one format, 14 or 15 of another, 13 or 14 of the first again.
    //
    // dr_mp3 decodes a stretch of another format only when it is long. It
    // resynchronises past a frame whose neighbours do not match and wants ten that
    // do, so ten frames of 22.05 kHz mono inside a stereo stream are dropped (the
    // clip lacks them, and nothing is wrong with what is left) and eleven are
    // decoded (measured, pulls of 384 to 65536 frames). Eleven frames of the
    // shortest kind this test has (576 samples; Layer I's 384 are in no fixture) are
    // about 5760 samples. A pull no longer than that cannot step over one; a longer
    // pull can, if the stretch happens to lie between two of its ends, and where it
    // does depends on where they fall. These three streams are placed so that it
    // does from 12288 frames up: mono inside stereo is accepted at 12288, the 24 kHz
    // one at 16384 and all three at 24576 and above (measured). Pulls of 384 to 8192
    // frames pass all three. Those under about 5760 could not fail any stream; for
    // the ones above it, up to about 11500, a stream with the stretch placed between
    // two of the pull's ends (the length of the lead-in swept) would, and this test
    // does not have one.
    // Fails under: the pull made 12288 frames or more (the stream decodes, its
    // middle at the wrong speed).
    const Bytes stereo = stereoUntagged();
    const Bytes mono22 = monoUntagged();
    const Bytes mono24 = mono24Untagged();

    struct Case {
        const char* what;
        Bytes stream;
    };
    const Case cases[] = {
        {"24 kHz mono inside 22.05 kHz mono (the rate alone)", concat(concat(mono22, mono24), mono22)},
        {"mono inside stereo", concat(concat(stereo, mono22), stereo)},
        {"stereo inside mono", concat(concat(mono22, stereo), mono22)},
    };
    for (const Case& c : cases) {
        const std::string label = std::string(c.what) + ": ";
        detail::Mp3Pcm out;
        std::string reason;
        const bool ok = decode(c.stream, out, reason);
        CHECK_MSG(!ok, label + "a stretch of another format is not played as the first");
        CHECK_MSG(reason.find("format") != std::string::npos, label + "and the reason says why: " + reason);
        CHECK_MSG(out.pcm.empty(), label + "with nothing kept");
    }

    // The same joins with nothing different in the middle decode: it is the format,
    // not the seams between the copies, that the refusals above are about.
    detail::Mp3Pcm out;
    std::string reason;
    const bool monoOk = decode(concat(concat(mono22, mono22), mono22), out, reason);
    CHECK_MSG(monoOk && out.channels == 1 && out.sampleRate == 22050, "three copies of the mono stream: " + reason);
    const bool stereoOk = decode(concat(concat(stereo, stereo), stereo), out, reason);
    CHECK_MSG(stereoOk && out.channels == 2 && out.sampleRate == 44100, "three copies of the stereo stream: " + reason);
}

static void testDamagedStreamsNeverBreakTheDecoder() {
    // Fixed seed, so a failure repeats. Four streams, damaged six ways. The
    // invariants are those of decoderAnswerIsSane; the streams are small, so a
    // thousand and more decodes take well under a second, and under ASan and UBSan
    // (CI's sanitizer job) this is the test that would show a read past the end.
    // Fails under: any change that lets damage through to an unchecked read, a
    // partial frame of PCM or an allocation past the cap (under ASan and UBSan); an
    // empty result accepted; the channel count recorded wrongly for the mono stream.
    const Bytes streams[] = {stereoTagged(), stereoUntagged(), monoTagged(), withId3v2(stereoTagged(), 64)};
    Rng rng{0x5EEDu};
    size_t accepted = 0;
    size_t refused = 0;
    std::string firstProblem;
    for (int i = 0; i < 1600; ++i) {
        const Bytes mp3 = damaged(streams[static_cast<size_t>(i) % 4], rng);
        detail::Mp3Pcm out;
        std::string reason;
        const bool ok = detail::DecodeMp3(mp3.data(), mp3.size(), out, reason);
        std::string why;
        if (!decoderAnswerIsSane(ok, out, reason, detail::kMp3MaxPcmBytes, why) && firstProblem.empty()) {
            firstProblem = "case " + std::to_string(i) + ": " + why;
        }
        (ok ? accepted : refused) += 1;
    }
    CHECK_MSG(firstProblem.empty(), firstProblem);
    // Both answers have to happen, or the damage is testing nothing. Most of it is
    // survived - dr_mp3 resynchronises past a bad frame - and what is refused is
    // the streams cut or struck down to nothing a decoder could start from (1550
    // and 50 at this seed).
    CHECK_MSG(accepted > 100 && refused > 10,
              "accepted " + std::to_string(accepted) + ", refused " + std::to_string(refused));
}

static void testTheAlignmentHelperFindsAKnownShift() {
    // The Windows comparison is only as good as this: a delayed copy of a noisy
    // signal must be found at the delay, in either direction, and its error must
    // be the noise that was added and nothing else.
    // Fails under: the lag taken with the opposite sign, or the error measured
    // over the unaligned samples.
    Rng rng{12345u};
    std::vector<int16_t> a(30000);
    for (auto& v : a) v = static_cast<int16_t>(static_cast<int>(rng.next() % 16000) - 8000);

    std::vector<int16_t> late(a.size(), 0);   // late[i + 300] = a[i], plus +-3 of noise
    std::vector<int16_t> early(a.size(), 0);  // early[i] = a[i + 250]
    for (size_t i = 0; i < a.size(); ++i) {
        if (i >= 300) late[i] = static_cast<int16_t>(a[i - 300] + static_cast<int>(rng.next() % 7) - 3);
        if (i + 250 < a.size()) early[i] = a[i + 250];
    }

    const Alignment l = alignAndCompare(a, late, 1, 2400);
    CHECK_EQ(l.lag, 300);
    CHECK_MSG(l.normalizedError() > 0.0 && l.normalizedError() < 0.002,
              "the noise is 0.03% of the signal, got " + std::to_string(l.normalizedError()));
    CHECK_MSG(l.correlation > 0.9999, "correlation " + std::to_string(l.correlation));

    const Alignment e = alignAndCompare(a, early, 1, 2400);
    CHECK_EQ(e.lag, -250);
    CHECK_NEAR(static_cast<float>(e.rmsError), 0.0f);

    const Alignment same = alignAndCompare(a, a, 1, 2400);
    CHECK_EQ(same.lag, 0);
    CHECK_NEAR(static_cast<float>(same.rmsError), 0.0f);
    CHECK_EQ(same.overlapFrames, a.size());

    // Two channels: the channel with the signal in it decides the lag, and both
    // are measured.
    std::vector<int16_t> stereo(a.size() * 2);
    for (size_t i = 0; i < a.size(); ++i) {
        stereo[2 * i] = a[i];
        stereo[2 * i + 1] = static_cast<int16_t>(-a[i]);
    }
    const Alignment s = alignAndCompare(stereo, stereo, 2, 2400);
    CHECK_EQ(s.lag, 0);
    CHECK_NEAR(static_cast<float>(s.correlation), 1.0f);

    // Nothing like each other: a poor correlation, reported as such rather than
    // forced to look good.
    std::vector<int16_t> other(a.size());
    for (auto& v : other) v = static_cast<int16_t>(static_cast<int>(rng.next() % 16000) - 8000);
    const Alignment unrelated = alignAndCompare(a, other, 1, 2400);
    CHECK_MSG(unrelated.correlation < 0.5, "unrelated noise correlates " + std::to_string(unrelated.correlation));
}

static void testTheToneMeasurementSurvivesALeadInOfSilence() {
    // What Media Foundation returned for the stream with a LAME header: a frame of
    // silence where dr_mp3 drops the Info frame, the codec's whole encoder delay,
    // and the first 528 samples of it cut. Built here from dr_mp3's own decode of the
    // stream without the header, so the tone has the lead-in the Windows clip had
    // (about 1700 frames before it) and the measurement can be checked on any
    // platform. With the estimator that counted crossings from a fixed margin this
    // read 450.6 Hz for the 440 Hz tone.
    // Fails under: the crossings counted outside the part of the clip where the
    // tone is at its strength (a fixed margin from each end).
    Bytes untagged = stereoTagged();
    untagged.erase(untagged.begin(), untagged.begin() + static_cast<std::ptrdiff_t>(kStereoInfoFrameBytes));
    detail::Mp3Pcm whole;
    std::string reason;
    const bool ok = decode(untagged, whole, reason);
    CHECK_MSG(ok, reason);
    if (!ok) return;

    Bytes leadIn(size_t{1152 - 528} * 4, 0);   // the Info frame, less the 528 samples cut
    leadIn.insert(leadIn.end(), whole.pcm.begin(), whole.pcm.end());
    CHECK_EQ(leadIn.size() / 4, size_t{15600});   // the length the Windows clip had

    const ToneStats left = measureTone(leadIn, 2, 0, 44100);
    const ToneStats right = measureTone(leadIn, 2, 1, 44100);
    CHECK_MSG(within(left.hz, 440.0, 0.01), "left is 440 Hz, got " + std::to_string(left.hz));
    CHECK_MSG(within(right.hz, 880.0, 0.01), "right is 880 Hz, got " + std::to_string(right.hz));

    // And without any tone at all there is nothing to report, rather than a
    // frequency made of noise.
    const Bytes silence(4000, 0);
    CHECK_MSG(measureTone(silence, 2, 0, 44100).hz == 0.0, "silence has no frequency");
}

static void testTheComparisonFindsATrimmedStartOnRealCodecOutput() {
    // The Windows half of the comparison, rehearsed where both sides are dr_mp3.
    // The same audio is decoded with its LAME header (trimmed by 1105 samples at
    // the front) and without (the whole of it), which is the shape of the answer
    // when a decoder keeps the delay that another trims: the second is the first
    // delayed by kLameFrontTrim, sample for sample. This runs the window choice,
    // the lag search and the overlap arithmetic on real codec output of two pure
    // tones, where a periodic signal could make a wrong lag win.
    // Fails under: the lag taken with the opposite sign, or the error measured over
    // the unaligned samples.
    struct Stream {
        const char* name;
        Bytes tagged;
        unsigned channels;
        size_t infoFrameBytes;
    };
    const Stream streams[] = {
        {"stereo 44.1 kHz", stereoTagged(), 2, kStereoInfoFrameBytes},
        {"mono 22.05 kHz", monoTagged(), 1, kMonoInfoFrameBytes},
    };
    const int trim = static_cast<int>(kLameFrontTrim);
    for (const Stream& s : streams) {
        const std::string label = std::string(s.name) + ": ";
        Bytes untagged = s.tagged;
        untagged.erase(untagged.begin(), untagged.begin() + static_cast<std::ptrdiff_t>(s.infoFrameBytes));
        detail::Mp3Pcm trimmed;
        detail::Mp3Pcm whole;
        std::string reason;
        const bool trimmedOk = decode(s.tagged, trimmed, reason);
        CHECK_MSG(trimmedOk, label + reason);
        const bool wholeOk = decode(untagged, whole, reason);
        CHECK_MSG(wholeOk, label + reason);
        if (!trimmedOk || !wholeOk) continue;

        const std::vector<int16_t> a = samplesOf(trimmed.pcm);
        const std::vector<int16_t> b = samplesOf(whole.pcm);
        const Alignment forward = alignAndCompare(a, b, s.channels, 3473);
        CHECK_MSG(forward.lag == trim, label + "the untrimmed decode is late by " + std::to_string(forward.lag));
        CHECK_MSG(forward.rmsError == 0.0, label + "and is the same samples: error " + std::to_string(forward.rmsError));
        CHECK_MSG(forward.correlation > 0.999999, label + "correlation " + std::to_string(forward.correlation));
        CHECK_MSG(forward.overlapFrames == a.size() / s.channels,
                  label + "the whole trimmed decode lies inside the other: " + std::to_string(forward.overlapFrames));

        const Alignment backward = alignAndCompare(b, a, s.channels, 3473);
        CHECK_MSG(backward.lag == -trim, label + "and the other way round it is early by " + std::to_string(backward.lag));
        CHECK_MSG(backward.rmsError == 0.0, label + "reversed: error " + std::to_string(backward.rmsError));
    }
}

static void testLoadMp3AgreesWithTheDecoder() {
    // The engine's own load against its own decoder, on each fixture. On Linux and
    // the rest they are the same decoder and agree to the byte. On Windows one is
    // Media Foundation: same rate and channels, lengths within the allowance above,
    // waveforms within kMaxNormalizedRmsError once lined up.
    // Fails under: LoadMp3 changing what the decoder returned (the byte comparison
    // off Windows); the decoder's channel count or rate recorded wrongly; on
    // Windows, a Media Foundation load that disagrees with dr_mp3 about what the
    // stream says.
    struct Case {
        const char* name;
        Bytes bytes;
        bool lameHeader;
        unsigned channels;
        uint32_t rate;
        double leftHz;
        double rightHz;
    };
    const Case cases[] = {
        {"stereo with a LAME header", stereoTagged(), true, 2, 44100, 440.0, 880.0},
        {"stereo without one", stereoUntagged(), false, 2, 44100, 440.0, 880.0},
        {"mono, MPEG-2", monoTagged(), true, 1, 22050, 1000.0, 0.0},
        {"stereo with ID3 tags", withId3v1(withId3v2(stereoTagged(), 200)), true, 2, 44100, 440.0, 880.0},
    };

    for (const Case& c : cases) {
        const std::string label = std::string(c.name) + ": ";
        const std::string path = freshTempPath("test_audio_mp3_", ".mp3");
        writeBinary(path, c.bytes);
        const Mp3Comparison r = compareLoadWithDecoder(path);
        std::remove(path.c_str());

        CHECK_MSG(r.loadOk, label + "AudioClip::LoadMp3 failed: " + r.loadError);
        CHECK_MSG(r.decodeOk, label + "the decoder failed: " + r.decodeReason);
        if (!r.loadOk || !r.decodeOk) continue;

        CHECK_EQ(r.clip.bitsPerSample, uint16_t{16});
        CHECK_MSG(r.clip.valid(), label + "the clip is valid");
        CHECK_EQ(r.clip.channels, static_cast<uint16_t>(c.channels));
        CHECK_EQ(r.clip.sampleRate, c.rate);
        CHECK_EQ(r.decoded.channels, r.clip.channels);
        CHECK_EQ(r.decoded.sampleRate, r.clip.sampleRate);

        const size_t loadFrames = r.clip.pcm.size() / (2u * c.channels);
        const size_t decodeFrames = r.decoded.pcm.size() / (2u * c.channels);
        const size_t difference = loadFrames > decodeFrames ? loadFrames - decodeFrames : decodeFrames - loadFrames;
        if constexpr (!kDecodedByMediaFoundation) {
            CHECK_MSG(r.clip.pcm == r.decoded.pcm, label + "LoadMp3 returns the decoder's bytes unchanged");
        } else {
            const size_t allowed = allowedLengthDifference(c.rate, c.lameHeader);
            CHECK_MSG(difference <= allowed, label + "Media Foundation gave " + std::to_string(loadFrames) +
                                                 " frames, dr_mp3 " + std::to_string(decodeFrames) +
                                                 " (allowed difference " + std::to_string(allowed) + ")");
            CHECK_MSG(r.alignment.correlation >= kMinCorrelation,
                      label + "correlation " + std::to_string(r.alignment.correlation));
            CHECK_MSG(r.alignment.normalizedError() <= kMaxNormalizedRmsError,
                      label + "normalised RMS error " + std::to_string(r.alignment.normalizedError()));
            std::printf("  note: %s: Media Foundation %zu frames, dr_mp3 %zu, lag %d, error %.4f, correlation %.5f\n",
                        c.name, loadFrames, decodeFrames, r.alignment.lag, r.alignment.normalizedError(),
                        r.alignment.correlation);
        }

        // What either decoder plays is the tone that was encoded.
        const ToneStats left = measureTone(r.clip.pcm, c.channels, 0, c.rate);
        CHECK_MSG(within(left.hz, c.leftHz, 0.01), label + "left " + std::to_string(left.hz) + " Hz");
        CHECK_MSG(left.peak >= 14745 && left.peak <= 18022, label + "left peak " + std::to_string(left.peak));
        if (c.channels == 2) {
            const ToneStats right = measureTone(r.clip.pcm, 2, 1, c.rate);
            CHECK_MSG(within(right.hz, c.rightHz, 0.01), label + "right " + std::to_string(right.hz) + " Hz");
        }
    }
}

static void testBrokenMp3FilesAreRefusedThroughLoad() {
    // The end-to-end path: AudioClip::Load on a file, by its name. Each refusal
    // names the file, and the clip handed in comes back empty. What a platform's
    // decoder makes of a damaged stream is its own (Media Foundation may play what
    // dr_mp3 refuses), so those cases hold either answer to the same rules.
    // Fails under: an error that does not name the path (the prefix in LoadMp3
    // removed), a clip left holding the previous sound.
    struct Case {
        const char* what;
        Bytes bytes;
    };
    const Case mustRefuse[] = {
        {"an empty file", Bytes()},
        {"4 KB of text", garbageWithoutSync()},
        {"six bytes", bytesOf(Mp3Fixtures::kStereo44k, 6)},
    };
    for (const Case& c : mustRefuse) {
        const std::string path = freshTempPath("test_audio_mp3_refused_", ".mp3");
        writeBinary(path, c.bytes);
        AudioClip clip = someOtherClip();
        std::string error;
        const bool ok = AudioClip::Load(path, clip, error);
        std::remove(path.c_str());
        CHECK_MSG(!ok, std::string(c.what) + " is not a sound");
        CHECK_MSG(error.find(path) != std::string::npos, std::string(c.what) + ": the reason names the file: " + error);
        CHECK_MSG(!clip.valid(), std::string(c.what) + " leaves the clip empty");
    }

    // Damaged but partly there: a refusal or a clip, never a crash, and a clip is
    // never longer than the whole.
    const Bytes whole = stereoTagged();
    const size_t wholeFrames = 13 * 1152 + 1152;   // the most any decoder could find
    const Case mayDecode[] = {
        {"cut in half", bytesOf(Mp3Fixtures::kStereo44k, whole.size() / 2)},
        {"cut mid-frame", bytesOf(Mp3Fixtures::kStereo44k, whole.size() - 100)},
        {"claiming 2 billion frames", withClaimedFrameCount(whole, 0x7FFFFFFEu)},
        {"claiming 0xFFFFFFFF frames", withClaimedFrameCount(whole, 0xFFFFFFFFu)},
        {"claiming none", withClaimedFrameCount(whole, 0u)},
    };
    for (const Case& c : mayDecode) {
        const std::string path = freshTempPath("test_audio_mp3_damaged_", ".mp3");
        writeBinary(path, c.bytes);
        AudioClip clip = someOtherClip();
        std::string error;
        const bool ok = AudioClip::Load(path, clip, error);
        std::remove(path.c_str());
        if (ok) {
            CHECK_MSG(clip.valid() && clip.channels == 2 && clip.sampleRate == 44100,
                      std::string(c.what) + ": a clip, in the stream's format");
            CHECK_MSG(clip.pcm.size() / 4 <= wholeFrames, std::string(c.what) + ": no longer than the stream");
        } else {
            CHECK_MSG(error.find(path) != std::string::npos, std::string(c.what) + ": the reason names the file: " + error);
            CHECK_MSG(!clip.valid(), std::string(c.what) + ": and the clip is empty");
        }
        if constexpr (!kDecodedByMediaFoundation) {
            if (std::strstr(c.what, "cut") != nullptr) {
                CHECK_MSG(ok, std::string(c.what) + ": dr_mp3 plays a cut stream as far as it goes");
            }
        }
    }

    // Missing, and a directory that is called something.mp3.
    AudioClip clip = someOtherClip();
    std::string error;
    CHECK(!AudioClip::Load("no_such_sound_file_here.mp3", clip, error));
    CHECK_MSG(error.find("no_such_sound_file_here.mp3") != std::string::npos, "a missing file is named: " + error);
    CHECK(!clip.valid());

    const std::string directory = tempPath("test_audio_mp3_dir_tmp.mp3");
    std::error_code ec;
    std::filesystem::create_directory(directory, ec);
    clip = someOtherClip();
    error.clear();
    CHECK_MSG(!AudioClip::Load(directory, clip, error), "a directory is not a sound");
    CHECK_MSG(error.find(directory) != std::string::npos, "and is named: " + error);
    CHECK(!clip.valid());
    std::filesystem::remove(directory, ec);
}

static void testAnMp3IsRecognisedWhateverTheCaseOfItsName() {
    // The extension decides the decoder, case-insensitively, as it does for .ogg.
    // Fails under: the extension compared case-sensitively for .mp3.
    const std::string path = tempPath("test_audio_mp3_case_tmp.MP3");
    writeBinary(path, stereoTagged());
    AudioClip clip;
    std::string error;
    const bool ok = AudioClip::Load(path, clip, error);
    std::remove(path.c_str());
    CHECK_MSG(ok, error);
    CHECK(clip.valid());
}

// --- The manual mode: decode a folder of the owner's own MP3s ---------------
//
//     test_audio --mp3-dir <folder>
//
// Decodes every .mp3 in the folder through the engine's own path, prints a line
// for each and a summary, and fails on any file that does not decode, on a folder
// that cannot be read and on one with no .mp3 in it. On Windows it also decodes
// each file with dr_mp3 and compares the two: Media Foundation against the decoder
// every other platform uses, which is what tells whether a game's recordings sound
// the same on both. No file is copied or kept: the folder is the person's own.

namespace {

std::string utf8Of(const std::filesystem::path& p) {
    const auto u = p.u8string();
    return std::string(reinterpret_cast<const char*>(u.data()), u.size());
}

// A std::string of UTF-8 as a path, the way the engine reads its paths. u8path
// is deprecated in C++20, and the narrow constructor would read the bytes in the
// Windows code page.
std::filesystem::path pathOf(const std::string& utf8) {
    return std::filesystem::path(std::u8string(reinterpret_cast<const char8_t*>(utf8.data()), utf8.size()));
}

std::string lowered(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return s;
}

} // namespace

static int runMp3Directory(const std::string& directory) {
    namespace fs = std::filesystem;
    std::error_code ec;
    if (!fs::is_directory(pathOf(directory), ec)) {
        std::printf("FAIL: %s is not a folder that can be read\n", directory.c_str());
        return 1;
    }
    std::vector<fs::path> files;
    for (const fs::directory_entry& entry : fs::directory_iterator(pathOf(directory), ec)) {
        if (entry.is_regular_file() && lowered(utf8Of(entry.path().extension())) == ".mp3") {
            files.push_back(entry.path());
        }
    }
    if (ec) {
        std::printf("FAIL: cannot list %s: %s\n", directory.c_str(), ec.message().c_str());
        return 1;
    }
    std::sort(files.begin(), files.end());
    if (files.empty()) {
        std::printf("FAIL: no .mp3 files in %s\n", directory.c_str());
        return 1;
    }

    std::printf("decoding %zu .mp3 files from %s with %s\n", files.size(), directory.c_str(),
                kDecodedByMediaFoundation ? "Media Foundation (AudioClip::Load), compared with dr_mp3" : "dr_mp3 (AudioClip::Load)");
    size_t failures = 0;
    size_t decoded = 0;
    size_t findings = 0;
    [[maybe_unused]] size_t refusedOnlyByMediaFoundation = 0;
    size_t largestBytes = 0;
    std::string largestName;
    double totalMs = 0.0;
    double totalSeconds = 0.0;
    size_t worstFrames = 0;
    double worstError = 0.0;
    double worstCorrelation = 1.0;

    for (const fs::path& file : files) {
        const std::string path = utf8Of(file);
        const std::string name = utf8Of(file.filename());
        const Mp3Comparison r = compareLoadWithDecoder(path);
        // The engine's path is AudioClip::Load; compareLoadWithDecoder times
        // LoadMp3, which is what Load calls for this extension.
        if (!r.loadOk) {
            std::printf("FAIL %-44s %s", name.c_str(), r.loadError.c_str());
            if constexpr (kDecodedByMediaFoundation) {
                // What the other platforms' decoder makes of it, since a file that
                // Media Foundation refuses is exactly where the two need comparing.
                if (r.decodeOk) {
                    ++refusedOnlyByMediaFoundation;
                    std::printf("  | dr_mp3 DECODES it: %u Hz %u ch %zu frames", r.decoded.sampleRate,
                                static_cast<unsigned>(r.decoded.channels),
                                r.decoded.pcm.size() / (2u * std::max<unsigned>(r.decoded.channels, 1)));
                } else {
                    std::printf("  | dr_mp3 also fails: %s", r.decodeReason.c_str());
                }
            }
            std::printf("\n");
            ++failures;
            continue;
        }
        ++decoded;
        const size_t frames = r.clip.pcm.size() / (2u * r.clip.channels);
        totalMs += r.loadMs;
        totalSeconds += static_cast<double>(frames) / r.clip.sampleRate;
        if (r.clip.pcm.size() > largestBytes) {
            largestBytes = r.clip.pcm.size();
            largestName = name;
        }
        std::printf("ok   %-44s %5u Hz %u ch %9zu frames %8.2f s %8.1f ms", name.c_str(), r.clip.sampleRate,
                    static_cast<unsigned>(r.clip.channels), frames, static_cast<double>(frames) / r.clip.sampleRate, r.loadMs);
        if constexpr (kDecodedByMediaFoundation) {
            if (!r.decodeOk) {
                std::printf("  | dr_mp3 FAILED: %s\n", r.decodeReason.c_str());
                ++failures;
                continue;
            }
            const size_t other = r.decoded.pcm.size() / (2u * std::max<unsigned>(r.decoded.channels, 1));
            const size_t diff = other > frames ? other - frames : frames - other;
            const bool formatSame = r.decoded.channels == r.clip.channels && r.decoded.sampleRate == r.clip.sampleRate;
            const bool lengthFinding = diff > mpegFrameSamples(r.clip.sampleRate);
            const bool waveFinding = !formatSame || r.alignment.normalizedError() > kMaxNormalizedRmsError ||
                                     r.alignment.correlation < kMinCorrelation;
            std::printf("  | dr_mp3 %u Hz %u ch %9zu frames %7.1f ms  d=%zu lag=%d err=%.4f corr=%.5f%s%s", r.decoded.sampleRate,
                        static_cast<unsigned>(r.decoded.channels), other, r.decodeMs, diff, r.alignment.lag,
                        r.alignment.normalizedError(), r.alignment.correlation, lengthFinding ? "  FINDING:length" : "",
                        waveFinding ? "  FINDING:waveform" : "");
            findings += (lengthFinding || waveFinding) ? 1 : 0;
            worstFrames = std::max(worstFrames, diff);
            worstError = std::max(worstError, r.alignment.normalizedError());
            worstCorrelation = std::min(worstCorrelation, r.alignment.correlation);
        }
        std::printf("\n");
    }

    std::printf("\n%zu files: %zu decoded, %zu failed; %.1f s of audio in %.0f ms; largest %s decodes to %.2f MB\n",
                files.size(), decoded, failures, totalSeconds, totalMs, largestName.c_str(),
                static_cast<double>(largestBytes) / (1024.0 * 1024.0));
    if constexpr (kDecodedByMediaFoundation) {
        std::printf("against dr_mp3: %zu with a finding; worst length difference %zu frames, worst normalised RMS error %.4f, "
                    "lowest correlation %.5f; %zu refused by Media Foundation that dr_mp3 decodes\n",
                    findings, worstFrames, worstError, worstCorrelation, refusedOnlyByMediaFoundation);
    }
    return failures == 0 ? 0 : 1;
}

static void runTests() {
    testAnUnknownSoundExtensionIsRefusedByName();
    testAMissingMp3FailsWithAReasonRatherThanCrashing();
    testAMissingOggFailsWithAReasonRatherThanCrashing();
    testAFileThatIsNotOggVorbisIsRefusedCleanly();
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

    testAClipWithNoBitDepthIsRefusedRatherThanSilent();
    testReapingAnEngineWithNoVoicesIsANoOp();
    testALoopingVoiceIsNeverFinishedAndIsNeverReaped();
    testAFinishedOneShotIsFreedRatherThanKept();
    testAVoiceIsFoundByWhatItPlaysNotByWhatItsSourceNamesNow();
    testALoopingSourceFollowsAChangedSoundFile();

    testAClipBuiltInMemoryPlaysLikeAFile();
    testAnEmptyClipIsRefusedRatherThanCached();
    testARegisteredClipCanBeReplacedWhileNothingIsPlayingIt();
    testAnUnregisteredNameStillFailsRatherThanInventingSilence();
}

// The MP3 group, apart from the rest so main can say how many checks it added: the
// checks before it are the ones this suite had when MP3 gained a decoder off
// Windows, and they must not change.
static void runMp3Tests() {
    testTheDecoderReadsAStereoToneFaithfully();
    testTheDecoderReadsAMonoMpeg2Stream();
    testAStreamWithoutALameHeaderIsNotTrimmed();
    testTagsAroundAStreamAreSkipped();
    testAnAbsurdLengthInTheHeaderIsNotBelieved();
    testTheDecoderRefusesWhatIsNotAnMp3WithAReason();
    testATruncatedStreamDecodesAsFarAsItGoes();
    testTheDecodedSizeIsCapped();
    testAllocationNeverExceedsTheCap();
    testAFormatChangeIsRefused();
    testAFormatChangeInTheMiddleIsRefused();
    testDamagedStreamsNeverBreakTheDecoder();
    testTheAlignmentHelperFindsAKnownShift();
    testTheComparisonFindsATrimmedStartOnRealCodecOutput();
    testTheToneMeasurementSurvivesALeadInOfSilence();
    testLoadMp3AgreesWithTheDecoder();
    testBrokenMp3FilesAreRefusedThroughLoad();
    testAnMp3IsRecognisedWhateverTheCaseOfItsName();
}

// The floor is what runs WITHOUT an output device: 54 checks, where a machine
// with one runs more. The voice cases above say they are not counted in it, and
// until this number agreed with them a machine with no sound card - a Linux CI
// runner, WSL - failed the suite for skipping exactly what it was told it may.
//
// Raised from 47 with the two decoder-choice cases, which need no device: they
// only ask AudioClip which loader a name resolves to. A floor left behind by
// the tests it guards stops guarding, which is the one thing it is for.
//
// Raised from 54 to 72 with the two Ogg Vorbis cases, eighteen checks and none
// of them near a device: six for a missing file, three for each of four files
// that are not Ogg Vorbis.
//
// Raised from 72 to 250 with the MP3 decoder: the MP3 group adds 207 checks off
// Windows (279 in all from a working directory without the shipped ambient clip),
// none of them near a device. The floor sits a little under that, so a handful of
// checks more or less does not trip it; Media Foundation's comparison adds a few
// more on Windows, which the floor does not count on. (The executable prints both
// figures each run, "N checks before the MP3 group, M in it": read them there, not
// from this comment, which is only right until the next test is added.)
//
// Written by hand rather than with TEST_MAIN so the executable takes an argument:
// `--mp3-dir <folder>` is the manual mode above, and with no argument (how ctest
// runs it) nothing extra happens.
int main(int argc, char** argv) {
    if (argc >= 2) {
        if (argc == 3 && std::strcmp(argv[1], "--mp3-dir") == 0) return runMp3Directory(argv[2]);
        std::printf("usage: test_audio                  run the suite\n"
                    "       test_audio --mp3-dir <dir>  decode every .mp3 in <dir> and report\n");
        return 2;
    }
    runTests();
    const int before = ::test::g_checks;
    runMp3Tests();
    std::printf("  %d checks before the MP3 group, %d in it\n", before, ::test::g_checks - before);
    return ::test::summary("test_audio", 250);
}
