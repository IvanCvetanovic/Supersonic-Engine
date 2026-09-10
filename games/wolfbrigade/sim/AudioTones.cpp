#include "sim/AudioTones.hpp"

#include <algorithm>
#include <cmath>

namespace WolfBrigade {
namespace Audio {

using Supersonic::Json::Value;

namespace {

// Godot's `fposmod`: fmod, and then the divisor added back when the result's
// sign disagrees with it.
//
// A helper rather than a bare std::fmod because the correction IS the branch.
// The noise hash below takes fmod of a sine times 43758.5453, which is
// negative about half the time; std::fmod keeps the sign of the dividend, and
// without the `+= y` half the noise samples would sit in [-1, 0) instead of
// [0, 1) and the waveform would be a different sound. The trailing `+ 0.0` is
// Godot's too - it turns a negative zero into a positive one.
double fposmod(double x, double y) {
    double value = std::fmod(x, y);
    if ((value < 0.0 && y > 0.0) || (value > 0.0 && y < 0.0)) value += y;
    value += 0.0;
    return value;
}

} // namespace

Tone Tone::FromJson(const Value& spec) {
    Tone tone;
    tone.freq = spec["freq"].AsNumber(440.0);
    tone.ms = static_cast<int>(spec["ms"].AsNumber(120.0));
    tone.wave = spec["wave"].AsString("sine");
    tone.vol = spec["vol"].AsNumber(0.5);

    // Absent means TRUE. Nothing in the shipped file sets it, so every shipped
    // sound reaches this default and the other reading would change all of them.
    tone.decay = spec.Has("decay") ? spec["decay"].AsBool(true) : true;
    return tone;
}

int Tone::SampleCount() const {
    // The multiplication in integers and the division in doubles, exactly as
    // the GDScript writes it - `int(MIX_RATE * ms / 1000.0)`. Truncated toward
    // zero rather than rounded, which is what makes 55ms 2425 samples and not
    // 2426.
    const double exact = static_cast<double>(kMixRate * ms) / 1000.0;
    return std::max(1, static_cast<int>(exact));
}

std::vector<int16_t> Synthesise(const Tone& tone) {
    const int n = tone.SampleCount();

    std::vector<int16_t> samples;
    samples.reserve(static_cast<size_t>(n));

    for (int i = 0; i < n; ++i) {
        // Phase in CYCLES rather than radians, so the non-sine waveforms can
        // read the fractional part directly. Only the sine converts.
        const double phase = tone.freq * static_cast<double>(i) / static_cast<double>(kMixRate);
        const double frac = std::fmod(phase, 1.0);

        double s = 0.0;
        if (tone.wave == "square") {
            s = frac < 0.5 ? 1.0 : -1.0;
        } else if (tone.wave == "saw") {
            s = 2.0 * frac - 1.0;
        } else if (tone.wave == "triangle") {
            s = 4.0 * std::fabs(frac - 0.5) - 1.0;
        } else if (tone.wave == "noise") {
            // Percussive: a hash of the sample index rather than an RNG, so the
            // same spec is the same buffer on every run, mixed with a sine at
            // `freq` so the frequency still shapes the character - low is a
            // wooden thud, high is a clink. The constants are the classic
            // shader hash, and they are the GDScript's to the digit.
            const double h =
                fposmod(std::sin(static_cast<double>(i) * 12.9898) * 43758.5453, 1.0) * 2.0 - 1.0;
            s = 0.65 * h + 0.35 * std::sin(phase * kTau);
        } else {
            // Anything unrecognised is a sine, matching the GDScript's `_`
            // branch. A typo in a waveform name is a sound that still plays.
            s = std::sin(phase * kTau);
        }

        const double env =
            tone.decay ? (1.0 - static_cast<double>(i) / static_cast<double>(n)) : 1.0;

        const double clamped = std::min(std::max(s * tone.vol * env, -1.0), 1.0);

        // TRUNCATED, not rounded, and it shows: a sawtooth's first sample at
        // full volume is int(-16383.5), which is -16383 and not -16384.
        samples.push_back(static_cast<int16_t>(static_cast<int>(clamped * 32767.0)));
    }

    return samples;
}

std::vector<uint8_t> ToPcmBytes(const std::vector<int16_t>& samples) {
    std::vector<uint8_t> bytes;
    bytes.reserve(samples.size() * 2);
    for (const int16_t sample : samples) {
        const auto raw = static_cast<uint16_t>(sample);
        bytes.push_back(static_cast<uint8_t>(raw & 0xFFu));
        bytes.push_back(static_cast<uint8_t>((raw >> 8) & 0xFFu));
    }
    return bytes;
}

Supersonic::AudioClip ToClip(const Tone& tone) {
    Supersonic::AudioClip clip;
    clip.channels = 1;
    clip.sampleRate = static_cast<uint32_t>(kMixRate);
    clip.bitsPerSample = 16;
    clip.pcm = ToPcmBytes(Synthesise(tone));
    return clip;
}

Sound SoundFor(const GameData& data, const std::string& id) {
    const Value& entry = data.Audio()["sfx"][id];

    Sound sound;
    if (entry.Has("file")) {
        sound.kind = Sound::Kind::File;
        sound.file = entry["file"].AsString();
        return sound;
    }
    if (entry.Has("tone")) {
        sound.kind = Sound::Kind::Tone;
        sound.tone = Tone::FromJson(entry["tone"]);
        return sound;
    }

    // An id nobody authored. Silence rather than a default beep: the original
    // caches a null stream for it and plays nothing, and a port that invented
    // a 440Hz sine would make a typo audible instead of visible.
    return sound;
}

double FalloffLinear(const GameData& data, double distance) {
    const Value& falloff = data.Audio()["falloff"];
    const double full = falloff["full_px"].AsNumber(480.0);

    // At least a pixel past `full`, so an authored file with the two equal (or
    // crossed) divides by one rather than by zero or a negative.
    const double silent = std::max(falloff["silent_px"].AsNumber(1500.0), full + 1.0);
    return std::clamp(1.0 - (distance - full) / (silent - full), 0.0, 1.0);
}

} // namespace Audio
} // namespace WolfBrigade
