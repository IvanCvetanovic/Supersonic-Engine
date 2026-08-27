#include "sim/AudioTones.hpp"

#include <algorithm>
#include <cmath>

namespace WolfBrigade {
namespace Audio {

using Supersonic::Json::Value;

Tone Tone::FromJson(const Value& spec) {
    Tone tone;
    tone.freq = spec["freq"].AsNumber(440.0);
    tone.ms = static_cast<int>(spec["ms"].AsNumber(120.0));
    tone.wave = spec["wave"].AsString("sine");
    tone.vol = spec["vol"].AsNumber(0.5);

    // Absent means TRUE. Nothing in the shipped file sets it, so every shipped
    // sound reaches this default and the other reading would change all eleven.
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

} // namespace Audio
} // namespace WolfBrigade
