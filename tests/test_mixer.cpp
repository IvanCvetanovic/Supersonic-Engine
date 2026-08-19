// Regression tests for the software mixer.
//
// The mixer is what makes audio possible off Windows: XAudio2 sums voices
// itself, ALSA and CoreAudio hand you one stream and expect the summing to have
// already happened. It has no platform header in it, so all of it is testable
// without an output device - which is the whole reason it is a separate class
// rather than code inside each backend.

#include "TestHarness.hpp"
#include "core/AudioMixer.hpp"

#include <cmath>
#include <cstring>
#include <string>
#include <vector>

using namespace Supersonic;

namespace {

// A clip of `frames` frames where every sample of every channel is `value`,
// stored as 16-bit signed - by far the most common WAV format.
AudioClip constantClip(size_t frames, float value, uint16_t channels = 1,
                       uint32_t sampleRate = 48000) {
    AudioClip clip;
    clip.channels = channels;
    clip.sampleRate = sampleRate;
    clip.bitsPerSample = 16;
    clip.pcm.resize(frames * channels * sizeof(int16_t));

    const auto sample = static_cast<int16_t>(value * 32767.0f);
    for (size_t i = 0; i < frames * channels; ++i) {
        std::memcpy(clip.pcm.data() + i * sizeof(int16_t), &sample, sizeof(sample));
    }
    return clip;
}

// A ramp from 0 to 1 across the clip, for checking cursor movement.
AudioClip rampClip(size_t frames, uint32_t sampleRate = 48000) {
    AudioClip clip;
    clip.channels = 1;
    clip.sampleRate = sampleRate;
    clip.bitsPerSample = 16;
    clip.pcm.resize(frames * sizeof(int16_t));

    for (size_t i = 0; i < frames; ++i) {
        const float t = static_cast<float>(i) / static_cast<float>(frames);
        const auto sample = static_cast<int16_t>(t * 32767.0f);
        std::memcpy(clip.pcm.data() + i * sizeof(int16_t), &sample, sizeof(sample));
    }
    return clip;
}

float peak(const std::vector<float>& buffer) {
    float highest = 0.0f;
    for (const float v : buffer) highest = std::max(highest, std::abs(v));
    return highest;
}

} // namespace

static void testSilenceWithNoVoices() {
    AudioMixer mixer(48000, 2);
    std::vector<float> out(256 * 2, 12345.0f);
    mixer.Mix(out.data(), 256);

    // Overwritten, not added to: the backend hands the mixer whatever was in
    // the ring buffer last time round, and adding into it would replay the
    // previous period underneath the new one.
    CHECK_NEAR(peak(out), 0.0f);
}

static void testASingleVoiceReachesTheOutput() {
    AudioMixer mixer(48000, 2);
    const AudioClip clip = constantClip(1024, 0.5f);

    const auto voice = mixer.Add(clip, false, 1.0f, 1.0f);
    CHECK(voice != AudioMixer::kInvalidVoice);
    CHECK(mixer.IsPlaying(voice));

    std::vector<float> out(256 * 2, 0.0f);
    mixer.Mix(out.data(), 256);

    // Centre pan is constant-power, so a mono source at full volume arrives at
    // 1/sqrt(2) per side rather than at unity.
    CHECK_MSG(test::nearly(out[0], 0.5f * 0.70710678f, 0.01f),
              "left channel: got " + std::to_string(out[0]));
    CHECK_MSG(test::nearly(out[1], 0.5f * 0.70710678f, 0.01f),
              "right channel: got " + std::to_string(out[1]));
}

static void testVolumeScales() {
    AudioMixer mixer(48000, 2);
    const AudioClip clip = constantClip(1024, 1.0f);

    const auto voice = mixer.Add(clip, false, 0.25f, 1.0f);
    std::vector<float> out(64 * 2, 0.0f);
    mixer.Mix(out.data(), 64);
    const float quiet = peak(out);

    mixer.Remove(voice);
    mixer.Add(clip, false, 1.0f, 1.0f);
    mixer.Mix(out.data(), 64);
    const float loud = peak(out);

    CHECK_MSG(quiet < loud, "a quarter-volume voice must be quieter than a full one");
    CHECK_MSG(test::nearly(quiet / loud, 0.25f, 0.02f),
              "quarter volume must be a quarter as loud: got " +
                  std::to_string(quiet / loud));
}

static void testVoicesSumRatherThanReplace() {
    AudioMixer mixer(48000, 2);
    const AudioClip clip = constantClip(1024, 0.4f);

    mixer.Add(clip, false, 1.0f, 1.0f);
    std::vector<float> one(64 * 2, 0.0f);
    mixer.Mix(one.data(), 64);

    mixer.Add(clip, false, 1.0f, 1.0f);
    std::vector<float> two(64 * 2, 0.0f);
    mixer.Mix(two.data(), 64);

    CHECK_EQ(mixer.VoiceCount(), size_t{2});
    CHECK_MSG(peak(two) > peak(one) * 1.5f,
              "two voices must be louder than one - a mixer that overwrites "
              "instead of summing plays only the last sound added");
}

static void testOutputIsClampedNotWrapped() {
    AudioMixer mixer(48000, 2);
    const AudioClip clip = constantClip(1024, 1.0f);

    // Six voices at full scale is far past what the output can represent.
    for (int i = 0; i < 6; ++i) mixer.Add(clip, false, 1.0f, 1.0f);

    std::vector<float> out(64 * 2, 0.0f);
    mixer.Mix(out.data(), 64);

    for (const float v : out) {
        CHECK_MSG(v <= 1.0001f && v >= -1.0001f,
                  "overload must clamp; wrapping turns a loud moment into noise");
    }
    // And it must actually be loud, not silenced by an over-eager guard.
    CHECK(peak(out) > 0.9f);
}

static void testIntegerOutputDoesNotWrapAtFullScale() {
    AudioMixer mixer(48000, 2);
    const AudioClip clip = constantClip(256, 1.0f);
    for (int i = 0; i < 4; ++i) mixer.Add(clip, false, 1.0f, 1.0f);

    std::vector<int16_t> out(64 * 2, 0);
    mixer.MixInt16(out.data(), 64);

    for (const int16_t v : out) {
        // Scaling by 32768 rather than 32767 makes +1.0 land on -32768, which
        // is the loudest possible click on every frame of a sustained sound.
        CHECK_MSG(v > 0, "positive full scale must stay positive");
    }
}

static void testNonLoopingVoiceFinishes() {
    AudioMixer mixer(48000, 2);
    const AudioClip clip = constantClip(100, 0.5f);

    const auto voice = mixer.Add(clip, false, 1.0f, 1.0f);
    CHECK(mixer.IsPlaying(voice));

    std::vector<float> out(256 * 2, 0.0f);
    mixer.Mix(out.data(), 256);   // longer than the clip

    CHECK_MSG(!mixer.IsPlaying(voice),
              "a one-shot must report finished, or AudioSystem never reclaims it");
}

static void testLoopingVoiceDoesNotFinish() {
    AudioMixer mixer(48000, 2);
    const AudioClip clip = constantClip(100, 0.5f);

    const auto voice = mixer.Add(clip, true, 1.0f, 1.0f);
    std::vector<float> out(1024 * 2, 0.0f);
    mixer.Mix(out.data(), 1024);   // ten times round

    CHECK_MSG(mixer.IsPlaying(voice), "a looping voice must keep playing");
    // Still audible all the way to the end of the buffer, not just the first
    // pass: a loop that wraps to a stale cursor goes silent after one lap.
    CHECK(std::abs(out[2000]) > 0.1f);
}

static void testPitchChangesPlaybackRate() {
    // A ramp read at double speed reaches its end in half the frames.
    AudioMixer mixer(48000, 1);
    const AudioClip clip = rampClip(400);

    const auto fast = mixer.Add(clip, false, 1.0f, 2.0f);
    std::vector<float> out(100, 0.0f);
    mixer.Mix(out.data(), 100);
    const float fastValue = out[99];
    mixer.Remove(fast);

    mixer.Add(clip, false, 1.0f, 1.0f);
    mixer.Mix(out.data(), 100);
    const float normalValue = out[99];

    CHECK_MSG(fastValue > normalValue * 1.8f,
              "at double pitch the cursor must be twice as far through the clip");
}

static void testSampleRateConversion() {
    // A 24 kHz clip played into a 48 kHz device must last twice as long, not
    // play at double speed. Getting this backwards is the classic chipmunk bug.
    AudioMixer mixer(48000, 1);
    const AudioClip clip = constantClip(100, 0.5f, 1, 24000);

    const auto voice = mixer.Add(clip, false, 1.0f, 1.0f);
    std::vector<float> out(150, 0.0f);
    mixer.Mix(out.data(), 150);

    CHECK_MSG(std::abs(out[150 - 1]) > 0.1f,
              "100 frames at 24 kHz must fill 200 frames at 48 kHz");
    CHECK(mixer.IsPlaying(voice));
}

static void testPanMovesEnergyBetweenChannels() {
    AudioMixer mixer(48000, 2);
    const AudioClip clip = constantClip(1024, 0.8f);

    const auto voice = mixer.Add(clip, false, 1.0f, 1.0f);
    mixer.SetParameters(voice, 1.0f, 1.0f, -1.0f);   // hard left

    std::vector<float> out(64 * 2, 0.0f);
    mixer.Mix(out.data(), 64);

    CHECK_MSG(std::abs(out[0]) > 0.5f, "hard left must be loud on the left");
    CHECK_MSG(std::abs(out[1]) < 0.01f, "hard left must be silent on the right");

    mixer.SetParameters(voice, 1.0f, 1.0f, 1.0f);    // hard right
    mixer.Mix(out.data(), 64);
    CHECK_MSG(std::abs(out[0]) < 0.01f, "hard right must be silent on the left");
    CHECK_MSG(std::abs(out[1]) > 0.5f, "hard right must be loud on the right");
}

static void testMonoClipFeedsBothChannels() {
    AudioMixer mixer(48000, 2);
    const AudioClip clip = constantClip(512, 0.6f, 1);
    mixer.Add(clip, false, 1.0f, 1.0f);

    std::vector<float> out(32 * 2, 0.0f);
    mixer.Mix(out.data(), 32);

    CHECK_MSG(std::abs(out[0]) > 0.1f && std::abs(out[1]) > 0.1f,
              "a mono source must be heard on both speakers, not only the left");
}

static void testEightBitClipsAreUnsigned() {
    // 8-bit WAV stores silence as 128, not 0. Treating it as signed makes every
    // quiet passage a square wave at half scale.
    AudioClip clip;
    clip.channels = 1;
    clip.sampleRate = 48000;
    clip.bitsPerSample = 8;
    clip.pcm.assign(256, uint8_t{128});   // 256 frames of silence

    AudioMixer mixer(48000, 1);
    mixer.Add(clip, false, 1.0f, 1.0f);

    std::vector<float> out(64, 0.0f);
    mixer.Mix(out.data(), 64);
    CHECK_MSG(peak(out) < 0.01f, "8-bit 128 is silence, not half scale");
}

static void testEmptyClipIsRejected() {
    AudioMixer mixer(48000, 2);
    AudioClip empty;
    CHECK(mixer.Add(empty, false, 1.0f, 1.0f) == AudioMixer::kInvalidVoice);
    CHECK_EQ(mixer.VoiceCount(), size_t{0});
}

static void runTests() {
    testSilenceWithNoVoices();
    testASingleVoiceReachesTheOutput();
    testVolumeScales();
    testVoicesSumRatherThanReplace();
    testOutputIsClampedNotWrapped();
    testIntegerOutputDoesNotWrapAtFullScale();
    testNonLoopingVoiceFinishes();
    testLoopingVoiceDoesNotFinish();
    testPitchChangesPlaybackRate();
    testSampleRateConversion();
    testPanMovesEnergyBetweenChannels();
    testMonoClipFeedsBothChannels();
    testEightBitClipsAreUnsigned();
    testEmptyClipIsRejected();
}

TEST_MAIN("test_mixer", 190)
