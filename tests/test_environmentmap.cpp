// Image-based lighting, on the CPU.
//
// This one was deferred three times on the grounds that its natural check is
// "does it look plausible", which this repository rejects as evidence. That was
// the wrong question. IBL has four pieces and three of them have exact oracles:
//
//   1. Sampling a cubemap. A face index or a flipped V is the froxel grid's Y
//      flip all over again - the scene still looks lit, and everything is
//      reflected in the wrong direction. Pinned by a round trip.
//   2. The irradiance integral. Over a CONSTANT environment the cosine integral
//      over the hemisphere, divided by pi, is one - so the answer is that same
//      constant. A botched solid angle or a normalisation off by pi produces a
//      number that is not the one that went in.
//   3. The prefiltered specular chain. At roughness zero it is the source
//      sampled along the mirror direction; at roughness one over a constant
//      environment it is that constant. Two exact endpoints bracketing the part
//      that has no oracle.
//
// The fourth piece - the shader combining them - is checked by a rendered
// image, against a baseline where a constant environment must reproduce the
// analytic hemisphere it replaces.

#include "core/EnvironmentMap.hpp"
#include "TestHarness.hpp"

#include <cmath>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

namespace fs = std::filesystem;
using namespace Supersonic;

namespace {

bool nearlyVec(const glm::vec3& a, const glm::vec3& b, float eps = 1e-4f) {
    return test::nearly(a.x, b.x, eps) && test::nearly(a.y, b.y, eps) &&
           test::nearly(a.z, b.z, eps);
}

const glm::vec3 kAxes[6] = {
    {1, 0, 0}, {-1, 0, 0}, {0, 1, 0}, {0, -1, 0}, {0, 0, 1}, {0, 0, -1},
};

// --- the mapping, which is the piece that fails invisibly ------------------

void testEachAxisLandsOnItsOwnFace() {
    // The order a Vulkan cube image wants its layers in. Getting the array
    // index out of step with the direction reflects the sky off the floor, and
    // the picture still looks like a picture.
    for (uint32_t expected = 0; expected < Cubemap::kFaceCount; ++expected) {
        uint32_t face = 0;
        float u = 0.0f;
        float v = 0.0f;
        Cubemap::DirectionToFace(kAxes[expected], face, u, v);

        CHECK_MSG(face == expected, "each axis has to land on the face named after it");
        CHECK_MSG(test::nearly(u, 0.5f, 1e-5f) && test::nearly(v, 0.5f, 1e-5f),
                  "and straight down an axis is the middle of that face");
    }
}

void testADirectionSurvivesTheRoundTrip() {
    // THE check for the twelve signs in the face mapping. Every one of them is
    // invisible when wrong: the reflection simply comes from somewhere else.
    int mismatches = 0;
    int checked = 0;

    for (int i = 0; i < 500; ++i) {
        const float t = static_cast<float>(i);
        const glm::vec3 direction = glm::normalize(glm::vec3(
            std::sin(t * 1.1f) + 0.01f, std::cos(t * 0.7f) - 0.02f, std::sin(t * 0.3f) + 0.03f));

        uint32_t face = 0;
        float u = 0.0f;
        float v = 0.0f;
        Cubemap::DirectionToFace(direction, face, u, v);

        const glm::vec3 back = Cubemap::FaceToDirection(face, u, v);
        ++checked;
        if (!nearlyVec(direction, back, 1e-4f)) ++mismatches;
    }

    CHECK_EQ(checked, 500);
    CHECK_MSG(mismatches == 0,
              "a direction turned into a face and back has to be the direction it was");
}

void testEveryTexelLooksSomewhereDifferent() {
    // A face whose u and v were swapped, or whose v ran the other way, still
    // round trips - the two errors cancel. What it cannot do is cover the whole
    // sphere: two faces would look at the same place.
    Cubemap map;
    CHECK(map.Create(4));

    std::vector<glm::vec3> seen;
    for (uint32_t face = 0; face < Cubemap::kFaceCount; ++face) {
        for (uint32_t y = 0; y < 4; ++y) {
            for (uint32_t x = 0; x < 4; ++x) {
                const float u = (static_cast<float>(x) + 0.5f) / 4.0f;
                const float v = (static_cast<float>(y) + 0.5f) / 4.0f;
                seen.push_back(Cubemap::FaceToDirection(face, u, v));
            }
        }
    }
    CHECK_EQ(seen.size(), size_t{96});

    int duplicates = 0;
    for (size_t i = 0; i < seen.size(); ++i) {
        for (size_t j = i + 1; j < seen.size(); ++j) {
            if (glm::dot(seen[i], seen[j]) > 0.9999f) ++duplicates;
        }
    }
    CHECK_MSG(duplicates == 0, "ninety-six texels have to look ninety-six different ways");
}

void testSamplingFindsTheFaceItWasWrittenTo() {
    Cubemap map;
    CHECK(map.Create(8));

    // A different colour per face, so a mix-up is a colour that is not there.
    for (uint32_t face = 0; face < Cubemap::kFaceCount; ++face) {
        const glm::vec3 colour(static_cast<float>(face) + 1.0f, 0.0f, 0.0f);
        for (uint32_t y = 0; y < 8; ++y) {
            for (uint32_t x = 0; x < 8; ++x) map.At(face, x, y) = colour;
        }
    }

    for (uint32_t face = 0; face < Cubemap::kFaceCount; ++face) {
        const glm::vec3 sampled = map.Sample(kAxes[face]);
        CHECK_MSG(test::nearly(sampled.x, static_cast<float>(face) + 1.0f, 1e-4f),
                  "sampling down an axis reads the face that axis names");
    }
}

// --- the integral, which has its own oracle -------------------------------

void testAConstantEnvironmentIrradiatesToItself() {
    // The cosine integral over a hemisphere, divided by pi, is exactly one. So
    // a constant environment has to come back as that constant - and a botched
    // solid angle, a missing sine, or a normalisation off by pi each produce a
    // number that is not the one that went in.
    const glm::vec3 colour(0.3f, 0.7f, 1.4f);   // one component above 1, which is the point
    const Cubemap source = EnvironmentMap::Constant(8, colour);

    Cubemap irradiance;
    CHECK(EnvironmentMap::Irradiance(source, 4, irradiance));

    int wrong = 0;
    for (uint32_t face = 0; face < Cubemap::kFaceCount; ++face) {
        for (uint32_t y = 0; y < 4; ++y) {
            for (uint32_t x = 0; x < 4; ++x) {
                if (!nearlyVec(irradiance.At(face, x, y), colour, 5e-3f)) ++wrong;
            }
        }
    }
    CHECK_MSG(wrong == 0, "every texel of the irradiance of a constant sky is that constant");
}

void testIrradianceIsBrighterFacingTheBrightSide() {
    // A sky bright above and dark below. A surface facing up must receive more
    // than one facing down, and the two must bracket what a sideways-facing one
    // gets. That is the whole shape of a hemisphere light, checked as an
    // ordering rather than as a number.
    Cubemap source;
    CHECK(source.Create(16));
    for (uint32_t face = 0; face < Cubemap::kFaceCount; ++face) {
        for (uint32_t y = 0; y < 16; ++y) {
            for (uint32_t x = 0; x < 16; ++x) {
                const float u = (static_cast<float>(x) + 0.5f) / 16.0f;
                const float v = (static_cast<float>(y) + 0.5f) / 16.0f;
                const glm::vec3 direction = Cubemap::FaceToDirection(face, u, v);
                source.At(face, x, y) = direction.y > 0.0f ? glm::vec3(1.0f) : glm::vec3(0.05f);
            }
        }
    }

    Cubemap irradiance;
    CHECK(EnvironmentMap::Irradiance(source, 4, irradiance));

    const float up = irradiance.Sample(glm::vec3(0.0f, 1.0f, 0.0f)).r;
    const float down = irradiance.Sample(glm::vec3(0.0f, -1.0f, 0.0f)).r;
    const float side = irradiance.Sample(glm::vec3(1.0f, 0.0f, 0.0f)).r;

    CHECK_MSG(up > side && side > down, "up is brightest, down is darkest, sideways is between");
    CHECK_MSG(up > 0.8f, "and a surface facing a white sky receives nearly all of it");
    CHECK_MSG(down < 0.2f, "while one facing a dark floor receives nearly none");
}

// --- the specular chain, bracketed at both ends ---------------------------

void testTheSharpestLevelIsTheEnvironmentItself() {
    // At roughness zero the GGX lobe collapses onto the mirror direction, so
    // the answer is the source sampled along it. Anything else means the
    // importance sampling is spreading energy it should not.
    Cubemap source;
    CHECK(source.Create(16));
    for (uint32_t face = 0; face < Cubemap::kFaceCount; ++face) {
        for (uint32_t y = 0; y < 16; ++y) {
            for (uint32_t x = 0; x < 16; ++x) {
                source.At(face, x, y) = glm::vec3(static_cast<float>(face) * 0.2f, 0.5f, 1.0f);
            }
        }
    }

    std::vector<Cubemap> levels;
    CHECK(EnvironmentMap::Prefilter(source, 16, 5, levels));
    CHECK_EQ(levels.size(), size_t{5});

    int wrong = 0;
    for (uint32_t face = 0; face < Cubemap::kFaceCount; ++face) {
        const glm::vec3 direction = kAxes[face];
        if (!nearlyVec(levels.front().Sample(direction), source.Sample(direction), 1e-2f)) ++wrong;
    }
    CHECK_MSG(wrong == 0, "the sharpest level is the environment, unchanged");
}

void testTheSharpestLevelIsExactlyAResample() {
    // Stronger than the case above, and it is what makes the sky affordable.
    //
    // At roughness zero the GGX lobe is a delta: importanceSampleGgx returns
    // the normal for every sample, the reflected direction is the normal for
    // every sample, and a weighted average of one value is that value. So the
    // level the sky reads is not an approximation of the environment - it IS
    // the environment, and computing it with 128 samples per texel was paying
    // to arrive back where it started.
    //
    // A checker rather than a constant, because a constant survives any weights
    // at all and would pass against a blur.
    Cubemap source;
    CHECK(source.Create(16));
    for (uint32_t face = 0; face < Cubemap::kFaceCount; ++face) {
        for (uint32_t y = 0; y < 16; ++y) {
            for (uint32_t x = 0; x < 16; ++x) {
                const bool light = ((x / 2) + (y / 2)) % 2 == 0;
                source.At(face, x, y) =
                    light ? glm::vec3(1.0f, 0.75f, 0.5f) : glm::vec3(0.0f, 0.125f, 0.25f);
            }
        }
    }

    std::vector<Cubemap> levels;
    CHECK(EnvironmentMap::Prefilter(source, 16, 5, levels));

    int wrong = 0;
    for (uint32_t face = 0; face < Cubemap::kFaceCount; ++face) {
        for (uint32_t y = 0; y < 16; ++y) {
            for (uint32_t x = 0; x < 16; ++x) {
                if (!nearlyVec(levels.front().At(face, x, y), source.At(face, x, y), 1e-5f)) {
                    ++wrong;
                }
            }
        }
    }
    CHECK_MSG(wrong == 0, "level zero must equal the source texel for texel, not merely resemble it");

    // And the level after it must NOT, or the assertion above is satisfied by a
    // chain that never blurs anything.
    int same = 0;
    for (uint32_t face = 0; face < Cubemap::kFaceCount; ++face) {
        if (nearlyVec(levels[1].Sample(kAxes[face]), levels.front().Sample(kAxes[face]), 1e-5f)) {
            ++same;
        }
    }
    CHECK_MSG(same < static_cast<int>(Cubemap::kFaceCount),
              "roughness above zero must still integrate");
}

void testEveryLevelOfAConstantEnvironmentIsThatConstant() {
    // The other endpoint, and it holds at every roughness rather than only at
    // one: however the lobe is spread, an average of one colour is that colour.
    // A weight that does not sum to what it should shows here and nowhere else.
    const glm::vec3 colour(0.25f, 0.5f, 2.0f);
    const Cubemap source = EnvironmentMap::Constant(16, colour);

    std::vector<Cubemap> levels;
    CHECK(EnvironmentMap::Prefilter(source, 16, 5, levels));

    int wrong = 0;
    for (const Cubemap& level : levels) {
        for (uint32_t face = 0; face < Cubemap::kFaceCount; ++face) {
            if (!nearlyVec(level.Sample(kAxes[face]), colour, 1e-3f)) ++wrong;
        }
    }
    CHECK_MSG(wrong == 0, "every roughness of a constant sky is that constant");
}

void testTheChainGetsSmallerAndBlurrier() {
    Cubemap source;
    CHECK(source.Create(32));
    for (uint32_t face = 0; face < Cubemap::kFaceCount; ++face) {
        for (uint32_t y = 0; y < 32; ++y) {
            for (uint32_t x = 0; x < 32; ++x) {
                // A checker, which has something to blur away.
                const bool light = ((x / 4) + (y / 4)) % 2 == 0;
                source.At(face, x, y) = light ? glm::vec3(1.0f) : glm::vec3(0.0f);
            }
        }
    }

    std::vector<Cubemap> levels;
    CHECK(EnvironmentMap::Prefilter(source, 32, 5, levels));

    CHECK_EQ(levels[0].size(), uint32_t{32});
    CHECK_EQ(levels[4].size(), uint32_t{2});

    // Contrast across one face, which a blur can only reduce.
    const auto contrast = [](const Cubemap& map) {
        float low = 1e9f;
        float high = -1e9f;
        for (uint32_t y = 0; y < map.size(); ++y) {
            for (uint32_t x = 0; x < map.size(); ++x) {
                low = std::min(low, map.At(Cubemap::PositiveX, x, y).r);
                high = std::max(high, map.At(Cubemap::PositiveX, x, y).r);
            }
        }
        return high - low;
    };

    CHECK_MSG(contrast(levels.back()) < contrast(levels.front()),
              "a rougher level is a blurrier one");
}

// --- the file format ------------------------------------------------------

fs::path scratchRoot() {
    return fs::temp_directory_path() / "supersonic_env_test";
}

// A Radiance file written by hand, so the reader is checked against bytes
// rather than against whatever a library happened to produce.
std::string writeRadiance(const std::string& name, uint32_t width, uint32_t height,
                          const unsigned char rgbe[4], bool runLengthEncoded) {
    std::error_code ec;
    fs::create_directories(scratchRoot(), ec);
    const fs::path path = scratchRoot() / name;

    std::ofstream file(path, std::ios::binary | std::ios::trunc);
    file << "#?RADIANCE\n";
    file << "FORMAT=32-bit_rle_rgbe\n";
    file << "\n";
    file << "-Y " << height << " +X " << width << "\n";

    for (uint32_t y = 0; y < height; ++y) {
        if (runLengthEncoded) {
            const unsigned char header[4] = {
                2, 2, static_cast<unsigned char>((width >> 8) & 0xFF),
                static_cast<unsigned char>(width & 0xFF)};
            file.write(reinterpret_cast<const char*>(header), 4);
            for (int component = 0; component < 4; ++component) {
                // One run covering the whole scanline.
                const unsigned char run = static_cast<unsigned char>(128 + width);
                file.write(reinterpret_cast<const char*>(&run), 1);
                file.write(reinterpret_cast<const char*>(&rgbe[component]), 1);
            }
        } else {
            for (uint32_t x = 0; x < width; ++x) {
                file.write(reinterpret_cast<const char*>(rgbe), 4);
            }
        }
    }
    file.close();
    return path.generic_string();
}

void testARadianceFileIsReadBothWaysItIsWritten() {
    // 128 with an exponent of 128 is 0.5 exactly: ldexp(1, 128 - 136) is 1/256,
    // and 128/256 is a half. A value chosen so the arithmetic can be checked
    // rather than eyeballed.
    const unsigned char texel[4] = {128, 64, 32, 128};
    const glm::vec3 expected(0.5f, 0.25f, 0.125f);

    for (const bool encoded : {false, true}) {
        const std::string path =
            writeRadiance(encoded ? "rle.hdr" : "flat.hdr", 8, 4, texel, encoded);

        std::vector<glm::vec3> pixels;
        uint32_t width = 0;
        uint32_t height = 0;
        std::string error;
        CHECK_MSG(EnvironmentMap::LoadRadiance(path, pixels, width, height, error), error);
        CHECK_EQ(width, uint32_t{8});
        CHECK_EQ(height, uint32_t{4});
        CHECK_EQ(pixels.size(), size_t{32});

        int wrong = 0;
        for (const glm::vec3& pixel : pixels) {
            if (!nearlyVec(pixel, expected, 1e-5f)) ++wrong;
        }
        CHECK_MSG(wrong == 0,
                  encoded ? "the run-length form decodes to what was written"
                          : "and so does the flat one");
    }
}

void testRefusingAFileIsBetterThanReadingItWrongly() {
    std::vector<glm::vec3> pixels;
    uint32_t width = 0;
    uint32_t height = 0;
    std::string error;

    CHECK(!EnvironmentMap::LoadRadiance("no_such_file_98765.hdr", pixels, width, height, error));
    CHECK_MSG(!error.empty(), "and it says why");

    // A PNG renamed, which is what somebody will do.
    std::error_code ec;
    fs::create_directories(scratchRoot(), ec);
    const fs::path fake = scratchRoot() / "notreally.hdr";
    {
        std::ofstream file(fake, std::ios::binary | std::ios::trunc);
        file << "\x89PNG\r\n\x1a\n";
    }
    CHECK_MSG(!EnvironmentMap::LoadRadiance(fake.generic_string(), pixels, width, height, error),
              "a file that is not Radiance is refused rather than read as noise");
}

void testAnHdrBecomesACubemap() {
    // A constant panorama through the whole path: file, decode, project onto
    // six faces. Every face has to come back as the colour that went in, which
    // catches a projection that samples off the end of the source.
    const unsigned char texel[4] = {128, 128, 128, 128};
    const std::string path = writeRadiance("sky.hdr", 32, 16, texel, true);

    std::vector<glm::vec3> pixels;
    uint32_t width = 0;
    uint32_t height = 0;
    std::string error;
    CHECK_MSG(EnvironmentMap::LoadRadiance(path, pixels, width, height, error), error);

    Cubemap cube;
    CHECK(EnvironmentMap::FromEquirectangular(pixels, width, height, 8, cube));
    CHECK_EQ(cube.size(), uint32_t{8});

    int wrong = 0;
    for (uint32_t face = 0; face < Cubemap::kFaceCount; ++face) {
        for (uint32_t y = 0; y < 8; ++y) {
            for (uint32_t x = 0; x < 8; ++x) {
                if (!nearlyVec(cube.At(face, x, y), glm::vec3(0.5f), 1e-5f)) ++wrong;
            }
        }
    }
    CHECK_MSG(wrong == 0, "a constant panorama is a constant cube");
}

void testNonsenseSizesAreRefused() {
    Cubemap map;
    CHECK(!map.Create(0));
    CHECK(!map.valid());
    CHECK(nearlyVec(map.Sample(glm::vec3(0, 1, 0)), glm::vec3(0.0f)));

    Cubemap out;
    CHECK(!EnvironmentMap::Irradiance(map, 4, out));
    CHECK(!EnvironmentMap::Irradiance(EnvironmentMap::Constant(4, glm::vec3(1.0f)), 0, out));

    std::vector<Cubemap> levels;
    CHECK(!EnvironmentMap::Prefilter(map, 8, 4, levels));
    CHECK(!EnvironmentMap::Prefilter(EnvironmentMap::Constant(4, glm::vec3(1.0f)), 8, 0, levels));

    CHECK(!EnvironmentMap::FromEquirectangular({}, 0, 0, 8, out));
    // A pixel count that disagrees with the dimensions would read past the end.
    CHECK(!EnvironmentMap::FromEquirectangular(std::vector<glm::vec3>(10), 8, 4, 8, out));
}

void runTests() {
    testEachAxisLandsOnItsOwnFace();
    testADirectionSurvivesTheRoundTrip();
    testEveryTexelLooksSomewhereDifferent();
    testSamplingFindsTheFaceItWasWrittenTo();

    testAConstantEnvironmentIrradiatesToItself();
    testIrradianceIsBrighterFacingTheBrightSide();

    testTheSharpestLevelIsTheEnvironmentItself();

    testTheSharpestLevelIsExactlyAResample();
    testEveryLevelOfAConstantEnvironmentIsThatConstant();
    testTheChainGetsSmallerAndBlurrier();

    testARadianceFileIsReadBothWaysItIsWritten();
    testRefusingAFileIsBetterThanReadingItWrongly();
    testAnHdrBecomesACubemap();
    testNonsenseSizesAreRefused();

    std::error_code ec;
    fs::remove_all(scratchRoot(), ec);
}

} // namespace

TEST_MAIN("test_environmentmap", 54)
