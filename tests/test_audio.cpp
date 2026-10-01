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

#include <chrono>
#include <cstdio>
#include <thread>
#include <cstring>
#include <fstream>
#include <initializer_list>
#include <filesystem>
#include <exception>
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
// The engine reads WAV, Ogg Vorbis and, on Windows, MP3. What it must never
// do is guess:
// a name it does not know is refused by name rather than fed to the WAV
// parser, which would report "not a RIFF/WAVE file" and send whoever reads
// that message looking in the wrong place.
//
// The real decode is proved against one of Magic Portals' own mp3s, in
// test_mp_sprites - that suite knows where the original's assets are and skips
// when they are absent, and this one links the engine alone.
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
    // elsewhere it is the documented refusal. Either way it must say so and
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

// --- what a WAV's format tag and width are allowed to mean ------------------
//
// The clip does not remember the format TAG, only the bit depth, and the mixer (and
// the Windows output, which asks for IEEE float exactly when bitsPerSample == 32)
// read a 32-bit clip as float. LoadWav accepted integer PCM at 32 bits too, so a
// 32-bit integer WAV was reinterpreted as floats: noise, and NaNs, from a legal file.

namespace {

std::string wavFile(uint16_t tag, uint16_t channels, uint32_t rate, uint16_t bits,
                    const std::string& data) {
    const auto u16 = [](uint16_t v) {
        return std::string{static_cast<char>(v & 0xFF), static_cast<char>((v >> 8) & 0xFF)};
    };
    const auto u32 = [](uint32_t v) {
        return std::string{static_cast<char>(v & 0xFF), static_cast<char>((v >> 8) & 0xFF),
                           static_cast<char>((v >> 16) & 0xFF), static_cast<char>((v >> 24) & 0xFF)};
    };
    std::string fmt = u16(tag) + u16(channels) + u32(rate) +
                      u32(rate * channels * (bits / 8)) + u16(static_cast<uint16_t>(channels * (bits / 8))) +
                      u16(bits);
    std::string body = "WAVE" + std::string("fmt ") + u32(static_cast<uint32_t>(fmt.size())) + fmt +
                       "data" + u32(static_cast<uint32_t>(data.size())) + data;
    return "RIFF" + u32(static_cast<uint32_t>(body.size())) + body;
}

template <typename T>
std::string bytesOf(std::initializer_list<T> values) {
    std::string out;
    for (const T v : values) out.append(reinterpret_cast<const char*>(&v), sizeof(T));
    return out;
}

float floatAt(const AudioClip& clip, size_t index) {
    float v = 0.0f;
    std::memcpy(&v, clip.pcm.data() + index * sizeof(float), sizeof(float));
    return v;
}

} // namespace

void testA32BitIntegerWavIsConvertedToTheFloatsTheMixerReads() {
    const std::string path = "test_audio_int32_tmp.wav";
    writeBytes(path, wavFile(1, 1, 44100, 32,
                             bytesOf<int32_t>({0x40000000, -0x40000000, 0x7FFFFFFF, 0})));

    AudioClip clip;
    std::string error;
    CHECK_MSG(AudioClip::LoadWav(path, clip, error), error);
    std::remove(path.c_str());
    if (!clip.valid() || clip.pcm.size() != 4 * sizeof(float)) {
        CHECK_MSG(false, "four samples in, four floats out");
        return;
    }
    CHECK_EQ(static_cast<int>(clip.bitsPerSample), 32);
    CHECK_NEAR(floatAt(clip, 0), 0.5f);
    CHECK_NEAR(floatAt(clip, 1), -0.5f);
    CHECK_NEAR(floatAt(clip, 2), 1.0f);
    CHECK_NEAR(floatAt(clip, 3), 0.0f);
}

void testAnIeeeFloatWavIsLeftExactlyAsItWas() {
    // The control: a real float WAV must not be touched by the conversion above.
    const std::string path = "test_audio_float32_tmp.wav";
    writeBytes(path, wavFile(3, 1, 44100, 32, bytesOf<float>({0.25f, -0.75f, 1.0f})));

    AudioClip clip;
    std::string error;
    CHECK_MSG(AudioClip::LoadWav(path, clip, error), error);
    std::remove(path.c_str());
    if (clip.pcm.size() != 3 * sizeof(float)) { CHECK_MSG(false, "three floats"); return; }
    CHECK(floatAt(clip, 0) == 0.25f && floatAt(clip, 1) == -0.75f && floatAt(clip, 2) == 1.0f);
}

void testAFloatTaggedWavOfTheWrongWidthIsRefused() {
    // IEEE float is 32 or 64 bits. Tag 3 with 16 was accepted and read as int16.
    const std::string path = "test_audio_float16_tmp.wav";
    writeBytes(path, wavFile(3, 1, 44100, 16, bytesOf<int16_t>({100, 200, 300, 400})));

    AudioClip clip;
    std::string error;
    CHECK_MSG(!AudioClip::LoadWav(path, clip, error), "a float tag with a 16-bit width is refused");
    CHECK_MSG(!error.empty(), "and says why");
    CHECK(!clip.valid());
    std::remove(path.c_str());
}

void testADirectoryIsNotAWavFile() {
    // ifstream opens a directory without complaint on some platforms, and tellg()
    // then returns -1 - which was cast straight to size_t, an eighteen-exabyte
    // vector. A path somebody mistyped, or a folder named like a file.
    const std::string directory = "test_audio_dir_tmp.wav";
    std::filesystem::create_directories(directory);

    AudioClip clip;
    std::string error;
    bool threw = false;
    bool loaded = true;
    try {
        loaded = AudioClip::LoadWav(directory, clip, error);
    } catch (const std::exception&) {
        threw = true;
    }
    std::filesystem::remove_all(directory);
    CHECK_MSG(!threw, "a directory is refused, not thrown");
    CHECK_MSG(!loaded, "and not loaded");
}

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

static void runTests() {
    testAnUnknownSoundExtensionIsRefusedByName();
    testAMissingMp3FailsWithAReasonRatherThanCrashing();
    testAMissingOggFailsWithAReasonRatherThanCrashing();
    testAFileThatIsNotOggVorbisIsRefusedCleanly();
    testLoadsValidWav();
    testA32BitIntegerWavIsConvertedToTheFloatsTheMixerReads();
    testAnIeeeFloatWavIsLeftExactlyAsItWas();
    testAFloatTaggedWavOfTheWrongWidthIsRefused();
    testADirectoryIsNotAWavFile();
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
TEST_MAIN("test_audio", 72)
