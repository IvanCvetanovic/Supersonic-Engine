// The game's synthesised sound effects, against the original's own arithmetic.
//
// Wolf Brigade ships no audio files, for the same reason it ships no art: the
// whole aural language is generated. Every entry in `data/audio.json` is a
// `tone` block, and `scripts/systems/audio.gd::_synth_tone` turns it into a
// short 16-bit mono buffer at load. These eleven buffers ARE the game's sound,
// not a placeholder for it.
//
// This is the strongest oracle left in the original's harness set, and the
// reason is worth naming: these are numbers that are COMPUTATION OUTPUT. A
// shared misreading of audio.json cannot make a square wave and a sawtooth
// produce the same bytes. Every earlier slice of this port could, in principle,
// have agreed with the original because both sides read a field the same wrong
// way; this one cannot.
//
// WHERE THE NUMBERS COME FROM, and how to re-derive them.
//
// `tools/verify_audio.gd` asserts that the train sound synthesises and prints
// its size - "synthesised PCM data (7938 bytes)" - and that is the ONLY sample
// figure the harness reports. It checks format, mix rate and non-silence for
// one sound out of eleven. So the harness anchors the chain but does not span
// it, and the rest was measured directly from Godot:
//
//   cd <scratchpad>/synthprobe
//   GODOT="D:/SteamLibrary/steamapps/common/Godot Engine/godot.windows.opt.tools.64.exe"
//   "$GODOT" --headless --path . --script res://probe.gd     # the eleven shipped
//   GODOT="D:/SteamLibrary/steamapps/common/Godot Engine/godot.windows.opt.tools.64.exe"
//   "$GODOT" --headless --path . --script res://probe2.gd    # the authored branches
//
// probe.gd carries `_synth_tone` VERBATIM and reads the real
// D:/The-Wolf-Brigade/data/audio.json. The game repository is the oracle and is
// never edited in order to be measured, so the probe lives outside it - which
// makes "verbatim" a claim that has to be checked rather than asserted.
//
// It was checked. Taking both bodies from the first `var freq :=` to the line
// that stops synthesising - `var stream := AudioStreamWAV.new()` in the game,
// `return bytes` in the probe - and collapsing runs of whitespace, the two are
// 698 characters and CHARACTER-IDENTICAL. What the probe adds is only what it
// does with the buffer afterwards. Independently, the probe's train row
// reproduces the harness's own printed 7938. Measured 27 August 2026 against
// Godot 4.7.1.
//
// THE TWO TRAPS THAT DISCRIMINATE A CARELESS PORT. Both are truncation, and
// both are invisible in nine of the eleven sounds:
//
//   * `int(MIX_RATE * ms / 1000.0)` truncates toward zero. 55ms is 2425.5
//     samples and 45ms is 1984.5, so `attack` and `shoot` come out one sample
//     SHORTER than a round would give. The other nine divide evenly.
//   * `int(clampf(...) * 32767.0)` truncates too. A sawtooth's first sample at
//     full volume is int(-16383.5) = -16383, NOT -16384. `destroy` and `defeat`
//     are the two that show it.
//
// A port that rounded in either place would agree with the original everywhere
// else and be wrong about the sound of the game.

#include "TestHarness.hpp"
#include "WolfBrigadeFixture.hpp"

#include "WolfBrigadeLayer.hpp"

#include "core/AudioEngine.hpp"
#include "core/AudioSystem.hpp"
#include "core/Components.hpp"
#include "sim/EventBus.hpp"
#include "sim/Building.hpp"
#include "sim/GameState.hpp"
#include "sim/Match.hpp"
#include "sim/Selection.hpp"
#include "sim/Progression.hpp"
#include "sim/AudioTones.hpp"
#include "sim/GameData.hpp"

#include <algorithm>
#include <cstdlib>
#include <string>
#include <vector>

using namespace WolfBrigade;

namespace {

const GameData& shipped() { return wb::Shipped(); }

// The samples for a shipped sfx id, by way of the same lookup the game uses.
//
// Deliberately NOT `Tone::FromJson(data.Audio()["sfx"][id]["tone"])`: routing
// through SoundFor is what makes these cases exercise the file/tone dispatch as
// well as the arithmetic, and a regression that resolved every id to silence
// would otherwise pass eleven times over on a hand-built Tone.
std::vector<int16_t> samplesFor(const std::string& id) {
    const Audio::Sound sound = Audio::SoundFor(shipped(), id);
    if (sound.kind != Audio::Sound::Kind::Tone) return {};
    return Audio::Synthesise(sound.tone);
}

int peakOf(const std::vector<int16_t>& samples) {
    int peak = 0;
    for (const int16_t sample : samples) peak = std::max(peak, std::abs(static_cast<int>(sample)));
    return peak;
}

// A sample that is not there has to FAIL a check, not end the process.
//
// This is not defensive habit; it is a defect this suite actually had. A
// mutation deleting the `max(1, ...)` floor from SampleCount makes Synthesise
// return an EMPTY buffer - and MSVC's debug std::vector answers `front()` on
// one with a blocking assertion dialog. The run did not go red. It HUNG, and
// sat there until the process was killed by hand, which in CI is a timeout
// rather than a failing test and reads as infrastructure trouble rather than
// as the bug it is.
//
// It is the same shape as the rule the port already follows for pointers - a
// CHECK that a pointer is non-null does not stop the next line dereferencing it
// - applied to a container. Out of range returns a value no real sample can
// hold, so whatever was expected, the comparison fails and says what it got.
constexpr int kNoSample = -999999;

int at(const std::vector<int16_t>& samples, size_t index) {
    return index < samples.size() ? static_cast<int>(samples[index]) : kNoSample;
}

int lastOf(const std::vector<int16_t>& samples) {
    return samples.empty() ? kNoSample : static_cast<int>(samples.back());
}

// One row of the probe's table.
struct Row {
    const char* id;
    int samples;
    int bytes;
    int first;    // s[0]
    int second;   // s[1]
    int middle;   // s[n/2], integer division, as the probe indexes it
    int peak;

    // And the whole buffer. See the note below - the landmarks above are not
    // enough on their own, and that is a measured fact rather than a worry.
    long long sum;
    long long sumOfSquares;
};

// THE WHOLE BUFFER, AND WHY THE LANDMARKS ABOVE ARE NOT ENOUGH.
//
// The first version of this suite asserted exactly those six numbers per sound
// and nothing else. A mutation that narrowed the phase accumulator to single
// precision - `float` where the GDScript uses a 64-bit float - walked through
// all eleven sounds without failing a single check.
//
// The reason is arithmetic rather than luck. A float phase carries about seven
// significant digits, so early in a buffer its error is orders of magnitude
// below one count, and it only grows into something audible late: by sample
// twenty-two thousand of `victory` the phase is past 457 cycles and the error
// has climbed into the low counts. Every landmark this suite looked at - the
// first sample, the second, the middle - was early enough to be identical
// either way. The peak was too, because a decaying tone peaks in its opening
// cycles.
//
// So both sums run over EVERY sample. A one-count difference anywhere moves the
// sum by one, and the sum of squares catches the case where two differences
// would cancel. Sixty-four bits is comfortable: the largest here is 27,342
// samples peaking at 16,383, which is 7.3e12. With them, the same mutation
// fails sixteen assertions.
//
// The lesson generalises past this suite: SPOT CHECKS MEASURE WHERE YOU LOOKED.
// When the thing under test produces a buffer, assert the buffer.
struct Checksums {
    long long sum{0};
    long long sumOfSquares{0};
};

Checksums checksum(const std::vector<int16_t>& samples) {
    Checksums sums;
    for (const int16_t sample : samples) {
        const auto value = static_cast<long long>(sample);
        sums.sum += value;
        sums.sumOfSquares += value * value;
    }
    return sums;
}

// The eleven shipped sounds, exactly as probe.gd printed them.
//
// Ordered as audio.json orders them so a reader can hold the two side by side.
// `bytes` is twice `samples` in every row and is carried anyway, because the
// harness reports the train sound in BYTES and a port asserting only samples
// would be comparing against half the oracle's number without saying so.
constexpr Row kShipped[] = {
    // id          samples  bytes    s[0]     s[1]    s[n/2]    peak
    {"train",       3969,    7938,   11468,   11465,   -5735,   11468, 236206LL, 174051024140LL},
    {"build",       7497,   14994,       0,     597,    6185,   13044, 286907LL, 214615700091LL},
    {"death",       8820,   17640,  -13106,  -13004,   -6553,   13106, -578249LL, 168438565129LL},
    {"destroy",    14994,   29988,  -16383,  -16315,   -3276,   16383, -1590780LL, 447358930706LL},
    {"wave",       18522,   37044,   14745,   14744,    7372,   14745, 773062LL, 1342319861210LL},
    {"victory",    22932,   45864,       0,    2048,   -7790,   16345, 130543LL, 1025778296219LL},
    {"defeat",     27342,   54684,  -16383,  -16301,   -6553,   16383, -1166072LL, 815514343900LL},
    {"research",    6615,   13230,   13106,   11928,     294,   13106, 6705LL, 126333109311LL},
    {"place",       4851,    9702,       0,     718,    5396,   11409, 182519LL, 106318822013LL},
    {"attack",      2425,    4850,    6553,    6550,   -3278,    6553, 165238LL, 34729063518LL},
    {"shoot",       1984,    3968,       0,     670,    3089,    6500, 63599LL, 14196627301LL},
};

// ---------------------------------------------------------------------------

// The whole table, in one case, because the table IS the assertion.
//
// Every sound, every landmark. Six numbers times eleven sounds is the sound of
// the game pinned to the byte, and any single arithmetic slip in Synthesise
// moves dozens of them at once.
void testTheElevenShippedSoundsSynthesiseWhatGodotSynthesises() {
    for (const Row& row : kShipped) {
        const std::vector<int16_t> samples = samplesFor(row.id);
        const std::string where = std::string(" [") + row.id + "]";

        CHECK_MSG(static_cast<int>(samples.size()) == row.samples,
                  std::string("sample count") + where + " got " +
                      std::to_string(samples.size()) + ", expected " +
                      std::to_string(row.samples));
        if (static_cast<int>(samples.size()) != row.samples) continue;

        CHECK_MSG(static_cast<int>(Audio::ToPcmBytes(samples).size()) == row.bytes,
                  std::string("byte count") + where);
        CHECK_MSG(at(samples, 0) == row.first,
                  std::string("s[0]") + where + " got " + std::to_string(at(samples, 0)));
        CHECK_MSG(at(samples, 1) == row.second,
                  std::string("s[1]") + where + " got " + std::to_string(at(samples, 1)));
        CHECK_MSG(at(samples, samples.size() / 2) == row.middle,
                  std::string("s[n/2]") + where + " got " +
                      std::to_string(at(samples, samples.size() / 2)));
        CHECK_MSG(peakOf(samples) == row.peak,
                  std::string("peak") + where + " got " + std::to_string(peakOf(samples)));

        // And every sample between the landmarks, which is the assertion that
        // actually constrains the arithmetic.
        const Checksums sums = checksum(samples);
        CHECK_MSG(sums.sum == row.sum,
                  std::string("sum over the whole buffer") + where + " got " +
                      std::to_string(sums.sum));
        CHECK_MSG(sums.sumOfSquares == row.sumOfSquares,
                  std::string("sum of squares") + where + " got " +
                      std::to_string(sums.sumOfSquares));
    }
}

// A guard on the guard.
//
// The checksums above are only worth carrying if a change to ONE late sample
// moves them - which is precisely the case a landmark check cannot see. This
// perturbs the last sample of the longest sound by a single count and requires
// both sums to notice.
void testOneCountAnywhereInTheBufferMovesBothChecksums() {
    std::vector<int16_t> samples = samplesFor("defeat");
    CHECK_MSG(samples.size() == 27342, "the longest sound is the one being perturbed");
    if (samples.size() != 27342) return;

    const Checksums before = checksum(samples);
    samples.back() = static_cast<int16_t>(samples.back() + 1);
    const Checksums after = checksum(samples);

    CHECK_MSG(after.sum != before.sum, "one count anywhere moves the sum");
    CHECK_MSG(after.sumOfSquares != before.sumOfSquares, "and the sum of squares");
}

// The first truncation trap, isolated so its failure names itself.
//
// 44100 * 55 / 1000 is 2425.5 and 44100 * 45 / 1000 is 1984.5. Nine of the
// eleven shipped durations divide evenly into the mix rate and say nothing
// about rounding; these two are the entire evidence, and they are why the
// original writes `int(...)` rather than `roundi(...)`.
void testSampleCountsTruncateRatherThanRound() {
    CHECK_EQ(static_cast<int>(samplesFor("attack").size()), 2425);   // not 2426
    CHECK_EQ(static_cast<int>(samplesFor("shoot").size()), 1984);    // not 1985

    // The even case, so a port that truncated the WRONG quantity is still seen.
    CHECK_EQ(static_cast<int>(samplesFor("train").size()), 3969);
}

// The second truncation trap.
//
// A sawtooth starts at -1.0. At vol 0.5 that is -16383.5, and truncation toward
// zero makes it -16383 - one quieter than the -16384 a floor would give. Both
// shipped sawtooths at vol 0.5 land on it exactly.
void testASawtoothsFirstSampleTruncatesTowardZeroNotDownward() {
    CHECK_EQ(at(samplesFor("destroy"), 0), -16383);
    CHECK_EQ(at(samplesFor("defeat"), 0), -16383);

    // And the positive side of the same rule, where floor and truncation agree,
    // so the pair together pins the direction rather than just the value.
    CHECK_EQ(at(samplesFor("train"), 0), 11468);   // int(0.35*32767)
}

// The square wave's boundary sample: `frac < 0.5`, not `<=`.
//
// 210 * 105 / 44100 is 0.5 EXACTLY - the only sample in the shipped data that
// lands on the boundary in binary floating point, which is why this is a real
// assertion rather than a hypothetical about a comparison operator. It flips
// sign: s[104] is +14662 and s[105] is -14661. A port written with `<=` would
// produce +14661 there and be right about all 18521 other samples of `wave`.
void testTheSquareWaveBoundarySampleBelongsToTheLowHalf() {
    const std::vector<int16_t> wave = samplesFor("wave");
    CHECK_EQ(static_cast<int>(wave.size()), 18522);

    CHECK_EQ(at(wave, 104), 14662);
    CHECK_MSG(at(wave, 105) == -14661,
              "phase is exactly 0.5 at i=105; `frac < 0.5` puts it low");
    CHECK_EQ(at(wave, 106), -14660);
}

// Square and triangle are NOT separated by their first sample.
//
// At i=0 the phase is zero, so a square gives +1.0 and a triangle gives
// 4*|0-0.5|-1 = +1.0 as well. Both `train` and `research` therefore open at
// +vol*32767 and a test that pinned only s[0] would not notice the two
// waveforms being swapped. The second sample is where they part: the square is
// still flat while the triangle has already begun ramping down.
void testSquareAndTriangleAreDistinguishedBeyondTheirFirstSample() {
    const std::vector<int16_t> square = samplesFor("train");      // 660Hz, vol 0.35
    const std::vector<int16_t> triangle = samplesFor("research"); // 990Hz, vol 0.40

    // The shape of the trap: both open at the top of their range.
    CHECK_EQ(at(square, 0), 11468);
    CHECK_EQ(at(triangle, 0), 13106);

    // And the samples that actually tell them apart.
    CHECK_EQ(at(square, 1), 11465);      // barely moved: still +1.0
    CHECK_EQ(at(triangle, 1), 11928);    // ramping: 4*|frac-0.5|-1
    CHECK_MSG(at(triangle, 1) < at(triangle, 0) - 1000,
              "a triangle leaves its peak immediately");
    CHECK_MSG(at(square, 1) > at(square, 0) - 10, "a square holds its level");
}

// The decay envelope, from both ends.
//
// It is linear from full to silence across the buffer, so the LAST sample of a
// decaying tone is near zero whatever the waveform - and the peak is the first
// sample for a square or a saw, which start at full deflection. An
// implementation that dropped the envelope would keep every s[0] correct and
// every s[n-1] wrong.
void testTheDecayEnvelopeRunsLinearlyFromFullToSilence() {
    const std::vector<int16_t> train = samplesFor("train");
    CHECK_EQ(peakOf(train), 11468);
    CHECK_MSG(at(train, 0) == 11468, "a square peaks on its first sample");
    CHECK_EQ(lastOf(train), 2);       // silence, but signed

    const std::vector<int16_t> research = samplesFor("research");
    CHECK_EQ(lastOf(research), -1);   // the sign survives the fade

    // Halfway through, a decaying square sits near half its opening amplitude.
    // Carried as measured rather than as reasoning: the exact -5735 depends on
    // which half-cycle sample n/2 lands in as well as on the envelope.
    CHECK_EQ(static_cast<int>(train[train.size() / 2]), -5735);
}

// ---------------------------------------------------------------------------
// Branches the shipped data cannot reach.
//
// Everything above runs on values the game ships, which is the point - but the
// shipped file sets no `decay`, no unknown waveform, no zero duration and no
// volume above one. Those branches exist in `_synth_tone` and would be
// untested on both sides, so they were authored and measured through Godot the
// same way (probe2.gd), and the port is checked harder than the original is.

// `decay` absent means TRUE, and the other reading changes all eleven sounds.
//
// Nothing in audio.json sets `decay`, so every shipped sound reaches this
// default. A port that defaulted it to false would produce eleven sounds that
// never fade and would still match every s[0] in the table above.
void testDecayDefaultsToOnAndTurningItOffHoldsTheAmplitude() {
    Audio::Tone flat;
    flat.freq = 660.0;
    flat.ms = 90;
    flat.wave = "square";
    flat.vol = 0.35;
    flat.decay = false;

    const std::vector<int16_t> held = Audio::Synthesise(flat);
    CHECK_EQ(static_cast<int>(held.size()), 3969);
    CHECK_EQ(at(held, 0), 11468);
    CHECK_EQ(static_cast<int>(held[held.size() / 2]), -11468);   // no fade at all
    CHECK_EQ(lastOf(held), 11468);              // still at full

    // The same spec with the default: the shipped train, which fades to 2.
    Audio::Tone fading = flat;
    fading.decay = true;
    CHECK_EQ(lastOf(Audio::Synthesise(fading)), 2);

    // And the default really is on, read off a spec that omits the key.
    Supersonic::Json::Value spec;
    spec.Set("freq", Supersonic::Json::Value(660.0));
    CHECK_MSG(Audio::Tone::FromJson(spec).decay, "an unset `decay` decays");
}

// An unrecognised waveform is a sine, exactly as the GDScript's `_` arm says.
//
// Measured, not assumed: a spec identical to `build` but naming a waveform that
// does not exist produces `build` byte for byte. A typo in a waveform name is a
// sound that still plays.
void testAnUnrecognisedWaveformFallsBackToASine() {
    Audio::Tone noise;
    noise.freq = 320.0;
    noise.ms = 170;
    noise.wave = "noise";
    noise.vol = 0.4;

    const std::vector<int16_t> fallback = Audio::Synthesise(noise);
    CHECK_MSG(fallback == samplesFor("build"), "an unknown waveform is the sine `build` is");
    CHECK_EQ(static_cast<int>(fallback.size()), 7497);
    CHECK_EQ(at(fallback, 1), 597);
}

// A tone too short to have a sample still has one.
//
// `maxi(1, ...)` is in the original, and it is a real decision rather than
// defensive noise: a zero-length buffer is not a quiet sound, it is a stream
// that cannot be played.
void testAToneShorterThanASingleSampleStillProducesOne() {
    Audio::Tone instant;
    instant.ms = 0;

    const std::vector<int16_t> one = Audio::Synthesise(instant);
    CHECK_EQ(static_cast<int>(one.size()), 1);
    CHECK_EQ(static_cast<int>(Audio::ToPcmBytes(one).size()), 2);
    CHECK_EQ(at(one, 0), 0);      // a sine starts at zero

    CHECK_EQ(instant.SampleCount(), 1);

    // 44100 * 1 / 1000 is 44.1, so one millisecond is 44 samples and not 45.
    Audio::Tone oneMs;
    oneMs.ms = 1;
    CHECK_EQ(oneMs.SampleCount(), 44);
}

// Volume above 1.0 is clamped, not wrapped.
//
// The clamp is the only thing between a mis-authored `vol` and a sample that
// overflows its own type. At vol 2.0 a sawtooth opens at -2.0, and the clamp
// makes that exactly -32767 - not -32768, and not a positive number.
void testAVolumeAboveOneIsClampedRatherThanWrapped() {
    Audio::Tone loud;
    loud.freq = 90.0;
    loud.ms = 340;
    loud.wave = "saw";
    loud.vol = 2.0;

    const std::vector<int16_t> clipped = Audio::Synthesise(loud);
    CHECK_EQ(at(clipped, 0), -32767);
    CHECK_EQ(peakOf(clipped), 32767);
    CHECK_MSG(clipped[clipped.size() / 2] == -13106, "the envelope still applies under the clamp");
}

// An empty spec is a 440Hz sine, and every default is load-bearing.
//
// Four defaults at once - freq 440, ms 120, sine, vol 0.5 - so a port that got
// any one of them wrong produces a different buffer here while every shipped
// sound, which sets all four, stays correct.
void testAnEmptySpecIsTheGDScriptsFourDefaults() {
    const Audio::Tone fallback = Audio::Tone::FromJson(Supersonic::Json::Value{});

    CHECK_NEAR(static_cast<float>(fallback.freq), 440.0f);
    CHECK_EQ(fallback.ms, 120);
    CHECK_MSG(fallback.wave == "sine", "an unset waveform is a sine");
    CHECK_NEAR(static_cast<float>(fallback.vol), 0.5f);

    const std::vector<int16_t> samples = Audio::Synthesise(fallback);
    CHECK_EQ(static_cast<int>(samples.size()), 5292);
    CHECK_EQ(at(samples, 1), 1026);
    CHECK_EQ(peakOf(samples), 16305);
}

// ---------------------------------------------------------------------------
// The lookup, which is where the port's SHAPE differs from the original's.

// Every shipped sfx id resolves to a tone, and there are eleven of them.
//
// The count matters as much as the kinds: audio.json is the one data file whose
// contents are an inventory of sounds rather than a schema, and a file that
// arrived here truncated would show up as a missing id rather than a bad value.
void testEveryShippedSoundIsASynthesisedToneAndThereAreEleven() {
    int tones = 0;
    for (const Row& row : kShipped) {
        const Audio::Sound sound = Audio::SoundFor(shipped(), row.id);
        CHECK_MSG(sound.kind == Audio::Sound::Kind::Tone, std::string("tone for ") + row.id);
        if (sound.kind == Audio::Sound::Kind::Tone) ++tones;
    }
    CHECK_EQ(tones, 11);
    CHECK_EQ(static_cast<int>(shipped().Audio()["sfx"].AsObject().size()), 11);
}

// A FILE WINS OVER A TONE, and this assertion is the only thing holding it.
//
// The original is an `if file / elif tone` chain, so a spec carrying both plays
// the file - or, if the file is missing, plays SILENCE. It never falls back to
// the tone. That is deliberate: audio.json's own comment says "Swap tone->file
// to use a real sound, no code", and a fallback would let a placeholder outlive
// the asset that replaced it.
//
// The port reports Kind::File and leaves existence to the caller that owns a
// resource system, which means nothing in the sim can enforce the rule - so if
// this case were dropped, a later reader could reorder the two checks and every
// other test here would still pass.
void testAFileWinsOverAToneEvenWhenBothAreAuthored() {
    const wb::ScratchData authored("audio", "audio.json",
        "{\n"
        "  \"sfx\": {\n"
        "    \"both\":     { \"file\": \"res://sfx/real.wav\",\n"
        "                    \"tone\": { \"freq\": 660, \"ms\": 90, \"wave\": \"square\" } },\n"
        "    \"fileonly\": { \"file\": \"res://sfx/only.wav\" },\n"
        "    \"toneonly\": { \"tone\": { \"freq\": 220, \"ms\": 100 } },\n"
        "    \"neither\":  { }\n"
        "  },\n"
        "  \"music\": { \"menu\": {}, \"game\": {} },\n"
        "  \"volumes\": { \"master_db\": 0.0, \"sfx_db\": 0.0, \"music_db\": -6.0 }\n"
        "}\n");

    GameData data;
    data.LoadAll(authored.Path());

    const Audio::Sound both = Audio::SoundFor(data, "both");
    CHECK_MSG(both.kind == Audio::Sound::Kind::File, "a file beats a tone in the same spec");
    CHECK_MSG(both.file == "res://sfx/real.wav", "and it is the authored path");

    CHECK_MSG(Audio::SoundFor(data, "fileonly").kind == Audio::Sound::Kind::File, "file only");
    CHECK_MSG(Audio::SoundFor(data, "toneonly").kind == Audio::Sound::Kind::Tone, "tone only");

    // A spec with neither key is silence, not a default beep.
    CHECK_MSG(Audio::SoundFor(data, "neither").kind == Audio::Sound::Kind::None,
              "an sfx with neither file nor tone is silent");
}

// An id nobody authored is silence, and asking for one does not crash.
//
// The original caches a null stream against the id and plays nothing. A port
// that answered with a default 440Hz sine would make a typo audible instead of
// visible, which is the harder bug of the two to find.
void testAnUnknownIdResolvesToSilenceRatherThanADefaultTone() {
    const Audio::Sound missing = Audio::SoundFor(shipped(), "trumpet");
    CHECK_MSG(missing.kind == Audio::Sound::Kind::None, "an unauthored id is silent");
    CHECK_MSG(missing.file.empty(), "and carries no path");

    // The empty string, which is what a caller building an id by concatenation
    // hands over when one half of it is missing.
    CHECK_MSG(Audio::SoundFor(shipped(), "").kind == Audio::Sound::Kind::None, "empty id");
}

// The bytes are little-endian pairs, low byte first.
//
// Here because the harness reports sizes in bytes and the endianness is the one
// thing a sample-level test cannot see. A port that wrote the high byte first
// would match every assertion above and produce noise on a real device.
void testPcmBytesArePairedLowByteFirst() {
    const std::vector<int16_t> train = samplesFor("train");
    const std::vector<uint8_t> bytes = Audio::ToPcmBytes(train);

    CHECK_EQ(static_cast<int>(bytes.size()), 7938);   // the harness's own number
    CHECK_EQ(static_cast<int>(bytes.size()), static_cast<int>(train.size()) * 2);

    // 11468 is 0x2CCC: low byte 0xCC, high byte 0x2C.
    CHECK_EQ(static_cast<int>(bytes[0]), 0xCC);
    CHECK_EQ(static_cast<int>(bytes[1]), 0x2C);

    // And a negative sample, where a sign-extension slip would show.
    // -16383 is 0xC001 in two's complement.
    const std::vector<uint8_t> saw = Audio::ToPcmBytes(samplesFor("destroy"));
    CHECK_EQ(static_cast<int>(saw[0]), 0x01);
    CHECK_EQ(static_cast<int>(saw[1]), 0xC0);
}

// --- 8. And it can actually be heard --------------------------------------

void testEverySynthesisedSoundCanReachTheMixer() {
    // The gap this closes was real and embarrassing: the tone slice was the
    // port's most strongly verified work - eleven buffers matched sample for
    // sample on the first run - and every door into the engine's mixer took a
    // PATH. Correct samples that nothing could play.
    //
    // AudioEngine::AddClip is that door. This walks the whole loop for all
    // eleven shipped sounds: a `tone` block in audio.json, through the
    // synthesiser, into a clip, into the engine, and back out by name without
    // the filesystem being consulted once.
    Supersonic::AudioEngine engine;

    for (const Row& row : kShipped) {
        const Audio::Sound sound = Audio::SoundFor(shipped(), row.id);
        if (sound.kind != Audio::Sound::Kind::Tone) continue;

        const std::string name = std::string("wolfbrigade:sfx:") + row.id;
        const Supersonic::AudioClip clip = Audio::ToClip(sound.tone);

        // The shape the mixer needs, and the shape the synthesiser produces -
        // asserted rather than assumed, because a mismatch here is silence at
        // the wrong pitch rather than a compile error.
        CHECK_MSG(clip.channels == 1, std::string(row.id) + " is mono");
        CHECK_MSG(static_cast<int>(clip.sampleRate) == Audio::kMixRate,
                  std::string(row.id) + " runs at the synthesiser's rate");
        CHECK_MSG(clip.bitsPerSample == 16, std::string(row.id) + " is 16-bit");
        CHECK_MSG(static_cast<int>(clip.pcm.size()) == row.bytes,
                  std::string(row.id) + " carries the oracle's byte count");

        const Supersonic::AudioClip* registered = engine.AddClip(name, clip);
        CHECK_MSG(registered != nullptr, std::string(row.id) + " is accepted by the engine");
        if (registered == nullptr) continue;

        CHECK_MSG(engine.HasClip(name), std::string(row.id) + " resolves by name");
        CHECK_MSG(engine.LoadClip(name) == registered,
                  std::string(row.id) + " loads from cache, not from disk");
    }
}


// --- The layer's half: registration, the pool, mute and the nine edges -----
//
// Everything above is arithmetic and needs no device. Everything here needs the
// engine, and some of it needs a real output device - those checks are guarded
// and are deliberately not counted in this suite's floor, exactly as
// test_audio's voice cases are. Without a device Play returns kInvalidVoice and
// there is no voice to assert about, which is the honest answer rather than a
// skipped test.

// The name a sound is registered under. Prefixed, because the engine's clip
// cache is shared with every path a file could arrive on and "train" is a
// plausible filename.
std::string clipName(const std::string& id) { return "wolfbrigade:sfx:" + id; }

// A layer with an engine behind it. AudioSystem::Attach is what puts the handle
// in the registry context, which is the only way a layer can reach one.
struct SoundedLayer {
    Supersonic::AudioEngine engine;
    entt::registry registry;
    WolfBrigade::WolfBrigadeLayer layer;

    SoundedLayer() {
        Supersonic::AudioSystem::Attach(registry, engine);
        layer.OnAttach(registry);
    }

    ~SoundedLayer() {
        layer.OnDetach(registry);
        Supersonic::AudioSystem::Detach(registry);
    }

    SoundedLayer(const SoundedLayer&) = delete;
    SoundedLayer& operator=(const SoundedLayer&) = delete;

    WolfBrigade::Match& Match() { return *layer.CurrentMatch(); }
};

void testEveryShippedSoundIsRegisteredWithTheEngineOnce() {
    // Needs no device: AddClip is a cache write. This is the check that the
    // synthesised samples above actually reach the thing that would play them,
    // which is the seam the suite could not see until the layer had one.
    SoundedLayer sounded;

    for (const char* id : { "train", "build", "death", "destroy", "wave", "victory",
                            "defeat", "research", "place", "attack", "shoot" }) {
        CHECK_MSG(sounded.engine.HasClip(clipName(id)),
                  std::string(id) + " is registered as " + clipName(id));
    }

    // PREFIXED, not bare. The cache is keyed by string and shared with the
    // filesystem path, so a game registering "train" would shadow a file of
    // that name for everything downstream.
    CHECK_MSG(!sounded.engine.HasClip("train"),
              "and not under a name a file could also have");
}

void testEachGameplaySignalPlaysItsOwnSound() {
    // The nine edges `connect_events` wires. Driven by emitting on the Match's
    // own bus, which is what the simulation does - so this tests the wiring
    // rather than a function the test called itself.
    SoundedLayer sounded;
    WolfBrigade::EventBus& bus = sounded.Match().Bus();

    struct Edge {
        const char* sound;
        void (*emit)(WolfBrigade::EventBus&);
    };
    const Edge edges[] = {
        { "train",    [](WolfBrigade::EventBus& b) { b.unitTrained.Emit("soldier", glm::vec2(0.0f)); } },
        { "place",    [](WolfBrigade::EventBus& b) { b.buildingPlaced.Emit(nullptr); } },
        { "build",    [](WolfBrigade::EventBus& b) { b.buildingCompleted.Emit(nullptr); } },
        { "death",    [](WolfBrigade::EventBus& b) { b.unitDied.Emit(nullptr); } },
        { "destroy",  [](WolfBrigade::EventBus& b) { b.buildingDestroyed.Emit(nullptr); } },
        { "wave",     [](WolfBrigade::EventBus& b) { b.waveStarted.Emit(1); } },
        { "victory",  [](WolfBrigade::EventBus& b) { b.gameWon.Emit(); } },
        { "defeat",   [](WolfBrigade::EventBus& b) { b.gameLost.Emit(); } },
        { "research", [](WolfBrigade::EventBus& b) { b.upgradeResearched.Emit("iron_swords"); } },
    };

    for (const Edge& edge : edges) {
        edge.emit(bus);

        // Not counted in the floor: needs a real output device.
        const auto playing = sounded.engine.StopVoicesUsing(clipName(edge.sound));
        if (!playing.empty()) {
            CHECK_MSG(playing.size() == 1,
                      std::string(edge.sound) + " started exactly one voice");
        }
    }
}

void testTheVoicePoolBoundsHowManySoundsOverlap() {
    // SIX, from `audio.gd`'s own constant, and the pool is ported rather than
    // dropped even though the engine mints a voice per call. It is a decision
    // about how the game SOUNDS - a wave of forty deaths is forty simultaneous
    // sounds without it - not an artefact of Godot needing one player each.
    SoundedLayer sounded;
    WolfBrigade::EventBus& bus = sounded.Match().Bus();

    for (int i = 0; i < 20; ++i) bus.unitDied.Emit(nullptr);

    // Not counted in the floor: needs a real output device.
    const auto playing = sounded.engine.StopVoicesUsing(clipName("death"));
    if (!playing.empty()) {
        CHECK_MSG(playing.size() <= 6,
                  "twenty deaths hold at most six voices, got " +
                      std::to_string(playing.size()));
    }
}

void testAMutedGameStartsNoVoicesAtAll() {
    // Mute is a SKIPPED CALL, not a bus level, which is what `audio.gd:86`
    // does. A muted game that still started voices at zero gain would burn the
    // pool and the mixer on sounds nobody can hear.
    SoundedLayer sounded;
    sounded.Match().PlayerProfile().SetMuted(true);

    WolfBrigade::EventBus& bus = sounded.Match().Bus();
    for (int i = 0; i < 5; ++i) bus.unitDied.Emit(nullptr);

    CHECK_MSG(sounded.engine.StopVoicesUsing(clipName("death")).empty(),
              "a muted game starts nothing");

    // And un-muting brings it back, or this would pass on an engine that had
    // simply stopped working.
    sounded.Match().PlayerProfile().SetMuted(false);
    bus.unitDied.Emit(nullptr);

    // Not counted in the floor: needs a real output device.
    const auto playing = sounded.engine.StopVoicesUsing(clipName("death"));
    if (sounded.engine.IsAvailable()) {
        CHECK_MSG(!playing.empty(), "and un-muting starts sounds again");
    }
}

void testARestartHangsTheHandlersOnTheNewBus() {
    // A Match owns its EventBus, so a new one is a new bus and the old
    // subscriptions are simply gone. Nothing would say so - the game would go
    // quiet from the first Restart onward and no test above would notice,
    // because they all use the first match's bus.
    SoundedLayer sounded;

    // Restart through the pause menu, the way a player does.
    for (auto [entity, tag] : sounded.registry.view<const Supersonic::TagComponent>().each()) {
        if (tag.tag == "Pause Restart") {
            sounded.registry.get<Supersonic::UIButtonComponent>(entity).clickedThisTick = true;
        }
    }
    sounded.layer.OnFixedUpdate(sounded.registry, 1.0f / 30.0f);

    WolfBrigade::Match* restarted = sounded.layer.CurrentMatch();
    CHECK_MSG(restarted != nullptr, "a new match is running");
    if (restarted == nullptr) return;

    restarted->Bus().unitDied.Emit(nullptr);

    // Not counted in the floor: needs a real output device.
    if (sounded.engine.IsAvailable()) {
        CHECK_MSG(!sounded.engine.StopVoicesUsing(clipName("death")).empty(),
                  "and its signals still reach the mixer");
    }
}

// The bar's Research button, found by what it says - every bar button carries
// the same tag. Re-found after every tick that could have rebuilt the strip,
// because a rebuild DESTROYS the old entities: holding one across a research
// that succeeds is a handle to something that no longer exists, and EnTT
// answers that with an assertion rather than a wrong value.
entt::entity findResearchButton(entt::registry& registry) {
    for (auto [entity, tag, button] :
         registry.view<const Supersonic::TagComponent,
                       Supersonic::UIButtonComponent>().each()) {
        if (tag.tag == "WB Bar Button" && button.label.find("Research") != std::string::npos) {
            return entity;
        }
    }
    return entt::null;
}

void testResearchingAnUpgradeAnnouncesIt() {
    // `EventBus::upgradeResearched` was DECLARED AND NEVER EMITTED. EventBus.hpp
    // says outright that a signal nothing emits is declared anyway, which is
    // what let this sit unnoticed - and it is one of the nine edges `audio.gd`
    // wires, so the research sound could never have played.
    //
    // Upgrades::Research takes no bus, so the emit belongs to whoever called
    // it. There is exactly one caller.
    SoundedLayer sounded;
    Match& match = sounded.Match();

    // A probe rather than the sound, because the claim is about the SIGNAL. The
    // sound it drives is covered by the edge table above.
    std::vector<std::string> announced;
    match.Bus().upgradeResearched.Connect(
        [&announced](const std::string& id) { announced.push_back(id); });

    // Select a completed building that researches something, and make it
    // affordable. Arranging the WORLD is testing; what would make this
    // worthless is arranging the answer.
    Building* researcher = nullptr;
    for (const auto& building : match.Buildings()) {
        if (building->IsAlive() && building->IsComplete() &&
            !building->Stats().researches.empty()) {
            researcher = building.get();
            break;
        }
    }
    CHECK_MSG(researcher != nullptr, "the board has a building that researches");
    if (researcher == nullptr) return;

    match.Picked().SelectBuilding(researcher);
    match.Run().Add("wood", 5000);
    match.Run().Add("food", 5000);
    sounded.layer.OnFixedUpdate(sounded.registry, 1.0f / 30.0f);

    const entt::entity button = findResearchButton(sounded.registry);
    CHECK_MSG(button != entt::null, "the bar offers a Research button");
    if (button == entt::null) return;

    CHECK_MSG(sounded.registry.get<Supersonic::UIButtonComponent>(button).enabled,
              "and it is affordable");

    sounded.registry.get<Supersonic::UIButtonComponent>(button).clickedThisTick = true;
    sounded.layer.OnFixedUpdate(sounded.registry, 1.0f / 30.0f);

    CHECK_EQ(announced.size(), size_t{1});
    if (!announced.empty()) {
        CHECK_MSG(match.Run().IsResearched(announced[0]),
                  "and it announced the upgrade that was actually bought, got \"" +
                      announced[0] + "\"");
    }

    // ONLY ON SUCCESS, and this is the case the layer's own second lock
    // describes: a button that went unaffordable between the press and the tick
    // that consumes it. UIInput would not mark a disabled button clicked, so
    // the state is arranged directly - enabled in the registry, unaffordable in
    // the run - which is exactly the race.
    const entt::entity next = findResearchButton(sounded.registry);
    if (next != entt::null) {
        const size_t announcedSoFar = announced.size();

        match.Run().Add("wood", -match.Run().Amount("wood"));
        match.Run().Add("food", -match.Run().Amount("food"));

        auto& widget = sounded.registry.get<Supersonic::UIButtonComponent>(next);
        widget.enabled = true;
        widget.clickedThisTick = true;
        sounded.layer.OnFixedUpdate(sounded.registry, 1.0f / 30.0f);

        CHECK_EQ(announced.size(), announcedSoFar);
    }
}



// --- The two combat sounds, and their throttle ---------------------------

void testABlowAndAnArrowEachMakeTheirOwnSound() {
    // The two the original plays directly from unit.gd and building.gd rather
    // than from a bus. Here they arrive as signals, because the simulation
    // stays free of a device, a clock and a mixer.
    SoundedLayer sounded;
    EventBus& bus = sounded.Match().Bus();

    bus.unitAttacked.Emit(glm::vec2(0.0f));
    if (sounded.engine.IsAvailable()) {
        // Not counted in the floor: needs a real output device.
        CHECK_MSG(!sounded.engine.StopVoicesUsing(clipName("attack")).empty(),
                  "a blow landing is heard");
    }

    bus.projectileFired.Emit(glm::vec2(0.0f));
    if (sounded.engine.IsAvailable()) {
        CHECK_MSG(!sounded.engine.StopVoicesUsing(clipName("shoot")).empty(),
                  "and an arrow leaving");
    }
}

void testTheCombatThrottleStopsALaneTurningToMush() {
    // Many units attack in the same tick - the original names the problem and
    // picks seventy milliseconds. Without a throttle a lane of twenty soldiers
    // starts twenty sounds at once, which the six-voice pool then eats.
    SoundedLayer sounded;
    EventBus& bus = sounded.Match().Bus();

    for (int i = 0; i < 20; ++i) bus.unitAttacked.Emit(glm::vec2(0.0f));

    if (sounded.engine.IsAvailable()) {
        // Not counted in the floor: needs a real output device.
        const auto playing = sounded.engine.StopVoicesUsing(clipName("attack"));
        CHECK_MSG(playing.size() == 1,
                  "twenty blows in one tick are one sound, got " +
                      std::to_string(playing.size()));
    }
}

void testTheThrottleOpensAgainAfterEnoughSimulatedTime() {
    // MEASURED IN TICKS, not off a wall clock, and this is the case that says
    // so: no real time passes inside this loop worth speaking of, and the
    // throttle opens anyway because the simulation moved.
    //
    // The other direction is what matters in a game: a wall clock would let a
    // paused game accumulate its whole pause as elapsed, so the first tick
    // after a resume would sound every throttled event at once.
    SoundedLayer sounded;
    EventBus& bus = sounded.Match().Bus();

    bus.unitAttacked.Emit(glm::vec2(0.0f));
    if (sounded.engine.IsAvailable()) {
        CHECK_MSG(!sounded.engine.StopVoicesUsing(clipName("attack")).empty(),
                  "the first one plays");
    }

    // Immediately again: refused.
    bus.unitAttacked.Emit(glm::vec2(0.0f));
    if (sounded.engine.IsAvailable()) {
        CHECK_MSG(sounded.engine.StopVoicesUsing(clipName("attack")).empty(),
                  "the second, in the same tick, is refused");
    }

    // Four ticks at thirty hertz is 133 ms, past the seventy the throttle asks
    // for. Nothing else advances it.
    for (int i = 0; i < 4; ++i) sounded.layer.OnFixedUpdate(sounded.registry, 1.0f / 30.0f);

    bus.unitAttacked.Emit(glm::vec2(0.0f));
    if (sounded.engine.IsAvailable()) {
        CHECK_MSG(!sounded.engine.StopVoicesUsing(clipName("attack")).empty(),
                  "and after enough simulated time it opens again");
    }
}

void testEachThrottledSoundHasItsOwnWindow() {
    // Per id, as `play_sfx_throttled` keys its map. An arrow leaving must not
    // be silenced because a sword landed a millisecond earlier - they are
    // different sounds and a shared window would drop half the combat.
    SoundedLayer sounded;
    EventBus& bus = sounded.Match().Bus();

    bus.unitAttacked.Emit(glm::vec2(0.0f));
    bus.projectileFired.Emit(glm::vec2(0.0f));

    if (sounded.engine.IsAvailable()) {
        CHECK_MSG(!sounded.engine.StopVoicesUsing(clipName("attack")).empty(),
                  "the blow is heard");
        CHECK_MSG(!sounded.engine.StopVoicesUsing(clipName("shoot")).empty(),
                  "and so is the arrow, in the same tick");
    }
}

void testAnUnthrottledSoundIsNotRateLimitedByAccident() {
    // The other nine edges are things that happen once - a wave, a purchase, a
    // building finishing - and must not inherit the combat window. A wave
    // starting twice in quick succession is two waves.
    SoundedLayer sounded;
    EventBus& bus = sounded.Match().Bus();

    bus.waveStarted.Emit(1);
    if (sounded.engine.IsAvailable()) {
        CHECK_MSG(!sounded.engine.StopVoicesUsing(clipName("wave")).empty(), "the first");
    }
    bus.waveStarted.Emit(2);
    if (sounded.engine.IsAvailable()) {
        CHECK_MSG(!sounded.engine.StopVoicesUsing(clipName("wave")).empty(),
                  "and the second, immediately after");
    }
}

void runTests() {
    testTheElevenShippedSoundsSynthesiseWhatGodotSynthesises();
    testOneCountAnywhereInTheBufferMovesBothChecksums();
    testSampleCountsTruncateRatherThanRound();
    testASawtoothsFirstSampleTruncatesTowardZeroNotDownward();
    testTheSquareWaveBoundarySampleBelongsToTheLowHalf();
    testSquareAndTriangleAreDistinguishedBeyondTheirFirstSample();
    testTheDecayEnvelopeRunsLinearlyFromFullToSilence();
    testDecayDefaultsToOnAndTurningItOffHoldsTheAmplitude();
    testAnUnrecognisedWaveformFallsBackToASine();
    testAToneShorterThanASingleSampleStillProducesOne();
    testAVolumeAboveOneIsClampedRatherThanWrapped();
    testAnEmptySpecIsTheGDScriptsFourDefaults();
    testEveryShippedSoundIsASynthesisedToneAndThereAreEleven();
    testAFileWinsOverAToneEvenWhenBothAreAuthored();
    testAnUnknownIdResolvesToSilenceRatherThanADefaultTone();
    testPcmBytesArePairedLowByteFirst();
    testEverySynthesisedSoundCanReachTheMixer();

    testEveryShippedSoundIsRegisteredWithTheEngineOnce();
    testEachGameplaySignalPlaysItsOwnSound();
    testTheVoicePoolBoundsHowManySoundsOverlap();
    testAMutedGameStartsNoVoicesAtAll();
    testARestartHangsTheHandlersOnTheNewBus();
    testResearchingAnUpgradeAnnouncesIt();

    testABlowAndAnArrowEachMakeTheirOwnSound();
    testTheCombatThrottleStopsALaneTurningToMush();
    testTheThrottleOpensAgainAfterEnoughSimulatedTime();
    testEachThrottledSoundHasItsOwnWindow();
    testAnUnthrottledSoundIsNotRateLimitedByAccident();
}

} // namespace

// The floor is what runs WITHOUT an output device: 260 checks, where a machine
// with one runs 282. The layer's half says its guarded checks are not counted
// in it, and until this number agreed a machine with no sound card failed the
// suite for skipping exactly what it was told it may.
TEST_MAIN("test_wb_audio", 260)
