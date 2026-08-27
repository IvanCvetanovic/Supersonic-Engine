#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "core/AudioClip.hpp"
#include "core/Json.hpp"
#include "sim/GameData.hpp"

namespace WolfBrigade {

// The game's placeholder sound effects, from `scripts/systems/audio.gd`.
//
// Wolf Brigade ships no audio files, for the same reason it ships no art: the
// whole visual and aural language is generated. Every sound in `audio.json` is
// a `tone` block - a frequency, a duration, a waveform and a volume - and the
// game synthesises a short 16-bit mono buffer from it at load. So this is not
// a stand-in for the sound; it IS the sound, and a port that skipped it would
// be a silent game rather than a game whose audio arrives later.
//
// It is also the best remaining oracle in the original's harness set, and the
// reason is worth naming: these are numbers that are COMPUTATION OUTPUT. A
// shared misreading of audio.json cannot make a square wave and a sawtooth
// produce the same bytes, which is exactly the property the first three slices
// of this port did not have.
//
// Deliberately free of any playback. There is no voice pool here, no bus, no
// mixer and no device - `audio.gd`'s player pool, its round-robin, its mute
// flag and its dB volumes are all engine-side plumbing and belong with whoever
// owns an audio device. What is here is the part with arithmetic in it.
namespace Audio {

// 44100, from `audio.gd`'s own constant. Not a preference: it is baked into
// every sample count below, and changing it changes every one of them.
inline constexpr int kMixRate = 44100;

// TAU, at double precision. GDScript's float is 64-bit and `sin` is a double
// sine, so the port's is too - a float here would move samples by ones and
// twos across the buffer and no amount of tolerance would hide it.
inline constexpr double kTau = 6.283185307179586476925286766559;

// One `tone` block out of audio.json.
//
// The defaults are the GDScript's, and they matter: a spec that omits `wave`
// is a sine, and one that omits `decay` DECAYS. Nothing in the shipped file
// sets `decay` at all, so every shipped sound fades - and a port that defaulted
// it the other way would produce eleven sounds that end abruptly and eleven
// sample values that all still match at index zero.
struct Tone {
    double freq{440.0};
    int ms{120};
    std::string wave{"sine"};
    double vol{0.5};
    bool decay{true};

    static Tone FromJson(const Supersonic::Json::Value& spec);

    // How many samples this spec produces.
    //
    // TRUNCATED TOWARD ZERO, not rounded, and that is observable in the shipped
    // data twice: 55ms is 2425.5 samples and 45ms is 1984.5, and both come out
    // one sample SHORTER than a `round` would give. A port that rounded would
    // agree with the original on nine of the eleven sounds.
    //
    // At least one sample, however short the spec. A zero-length buffer is not
    // a quiet sound, it is a sound that cannot be played.
    //
    // Truncating and FLOORING are indistinguishable here, and the reason is
    // worth writing down rather than leaving as an untested branch: the two
    // differ only for negative values, and every negative value is clamped to
    // one by the line above. A mutation swapping them survives the suite
    // because it cannot change an answer, not because nothing is looking. The
    // truncation is written the way the GDScript writes it so the two files
    // read the same, and that is the whole of why it is a truncation.
    int SampleCount() const;
};

// The samples, mono, signed 16-bit.
//
// Deterministic: the same spec gives the same buffer on every platform and
// every run, which is what lets a test assert individual sample values rather
// than a property of the whole.
//
// The envelope is linear from full to silence across the buffer, so the LAST
// sample of a decaying tone is always near zero regardless of the waveform.
// That is why the interesting values to pin are at the start and the middle.
std::vector<int16_t> Synthesise(const Tone& tone);

// The bytes a stream would carry: two per sample, little-endian.
//
// Here because the original's harness reports its buffer in BYTES - "7938
// bytes" for the train sound - and a port that asserted samples would be
// comparing against half of the oracle's number without saying so.
std::vector<uint8_t> ToPcmBytes(const std::vector<int16_t>& samples);

// A synthesised tone as something the engine's mixer will take.
//
// The buffer is mono 16-bit at kMixRate, which is exactly what AudioClip
// describes - so this is a field copy rather than a conversion, and it is here
// rather than in the caller so the sample rate and the bit depth cannot drift
// apart from the synthesiser that produced them.
//
// Register it with AudioEngine::AddClip under a name, then Play that name. The
// engine's cache is keyed by string and consults itself before the filesystem,
// so a generated sound and a shipped file are the same thing to everything
// downstream.
Supersonic::AudioClip ToClip(const Tone& tone);

// What a sound id resolves to, from `audio.gd::stream_for`.
//
// A FILE WINS OVER A TONE, and it is the whole point of the design: the file's
// own comment says an sfx is "EITHER a real asset {file} OR a synthesised
// placeholder {tone}. Swap tone->file to use a real sound, no code." Checking
// them in the other order would make a placeholder outlive the asset that
// replaced it.
//
// Whether the file actually EXISTS is not asked here. The original asks Godot's
// ResourceLoader and falls back to silence; this port has no resource system
// and the caller that does have one is the only thing that can answer. Reported
// rather than resolved.
struct Sound {
    enum class Kind { None, File, Tone };

    Kind kind{Kind::None};
    std::string file;   // Kind::File only
    Tone tone;          // Kind::Tone only
};

Sound SoundFor(const GameData& data, const std::string& id);

} // namespace Audio

} // namespace WolfBrigade
