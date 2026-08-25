#include "core/EnvironmentMap.hpp"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <fstream>
#include <sstream>

namespace Supersonic {

namespace {

constexpr float kPi = 3.14159265358979323846f;

float clamp01(float value) { return std::min(std::max(value, 0.0f), 1.0f); }

} // namespace

bool Cubemap::Create(uint32_t size, const glm::vec3& fill) {
    if (size == 0) {
        m_size = 0;
        m_pixels.clear();
        return false;
    }
    m_size = size;
    m_pixels.assign(static_cast<size_t>(size) * size * kFaceCount, fill);
    return true;
}

glm::vec3& Cubemap::At(uint32_t face, uint32_t x, uint32_t y) {
    static glm::vec3 dummy(0.0f);
    if (!valid() || face >= kFaceCount || x >= m_size || y >= m_size) return dummy;
    return m_pixels[(static_cast<size_t>(face) * m_size + y) * m_size + x];
}

const glm::vec3& Cubemap::At(uint32_t face, uint32_t x, uint32_t y) const {
    static const glm::vec3 dummy(0.0f);
    if (!valid() || face >= kFaceCount || x >= m_size || y >= m_size) return dummy;
    return m_pixels[(static_cast<size_t>(face) * m_size + y) * m_size + x];
}

void Cubemap::DirectionToFace(const glm::vec3& direction, uint32_t& outFace,
                              float& outU, float& outV) {
    const glm::vec3 magnitude = glm::abs(direction);

    float sc = 0.0f;
    float tc = 0.0f;
    float ma = 1.0f;

    // The major axis picks the face; the other two become s and t with signs
    // that differ per face. Straight out of the specification - every one of
    // these signs is invisible when it is wrong, and the round-trip test is
    // what pins them.
    if (magnitude.x >= magnitude.y && magnitude.x >= magnitude.z) {
        ma = magnitude.x;
        if (direction.x > 0.0f) {
            outFace = PositiveX;
            sc = -direction.z;
            tc = -direction.y;
        } else {
            outFace = NegativeX;
            sc = direction.z;
            tc = -direction.y;
        }
    } else if (magnitude.y >= magnitude.z) {
        ma = magnitude.y;
        if (direction.y > 0.0f) {
            outFace = PositiveY;
            sc = direction.x;
            tc = direction.z;
        } else {
            outFace = NegativeY;
            sc = direction.x;
            tc = -direction.z;
        }
    } else {
        ma = magnitude.z;
        if (direction.z > 0.0f) {
            outFace = PositiveZ;
            sc = direction.x;
            tc = -direction.y;
        } else {
            outFace = NegativeZ;
            sc = -direction.x;
            tc = -direction.y;
        }
    }

    if (ma < 1.0e-20f) {
        outFace = PositiveX;
        outU = 0.5f;
        outV = 0.5f;
        return;
    }

    outU = 0.5f * (sc / ma + 1.0f);
    outV = 0.5f * (tc / ma + 1.0f);
}

glm::vec3 Cubemap::FaceToDirection(uint32_t face, float u, float v) {
    const float sc = u * 2.0f - 1.0f;
    const float tc = v * 2.0f - 1.0f;

    glm::vec3 direction(0.0f);
    switch (face) {
    case PositiveX: direction = glm::vec3(1.0f, -tc, -sc); break;
    case NegativeX: direction = glm::vec3(-1.0f, -tc, sc); break;
    case PositiveY: direction = glm::vec3(sc, 1.0f, tc); break;
    case NegativeY: direction = glm::vec3(sc, -1.0f, -tc); break;
    case PositiveZ: direction = glm::vec3(sc, -tc, 1.0f); break;
    default:        direction = glm::vec3(-sc, -tc, -1.0f); break;
    }
    return glm::normalize(direction);
}

glm::vec3 Cubemap::Sample(const glm::vec3& direction) const {
    if (!valid()) return glm::vec3(0.0f);

    uint32_t face = 0;
    float u = 0.0f;
    float v = 0.0f;
    DirectionToFace(direction, face, u, v);

    // Half a texel in, so u = 0 lands on the centre of the first texel rather
    // than half way between it and a texel that is not there.
    const float scale = static_cast<float>(m_size);
    const float x = clamp01(u) * scale - 0.5f;
    const float y = clamp01(v) * scale - 0.5f;

    const int x0 = static_cast<int>(std::floor(x));
    const int y0 = static_cast<int>(std::floor(y));
    const float fx = x - static_cast<float>(x0);
    const float fy = y - static_cast<float>(y0);

    const auto clampIndex = [this](int value) {
        return static_cast<uint32_t>(std::min(std::max(value, 0), static_cast<int>(m_size) - 1));
    };

    const glm::vec3 c00 = At(face, clampIndex(x0), clampIndex(y0));
    const glm::vec3 c10 = At(face, clampIndex(x0 + 1), clampIndex(y0));
    const glm::vec3 c01 = At(face, clampIndex(x0), clampIndex(y0 + 1));
    const glm::vec3 c11 = At(face, clampIndex(x0 + 1), clampIndex(y0 + 1));

    return glm::mix(glm::mix(c00, c10, fx), glm::mix(c01, c11, fx), fy);
}

namespace EnvironmentMap {

namespace {

// RGBE to three floats. A zero exponent is black rather than a very small
// number, which is what the format says and what stops a denormal storm.
glm::vec3 decodeRgbe(const unsigned char rgbe[4]) {
    if (rgbe[3] == 0) return glm::vec3(0.0f);
    const float scale = std::ldexp(1.0f, static_cast<int>(rgbe[3]) - (128 + 8));
    return glm::vec3(static_cast<float>(rgbe[0]) * scale,
                     static_cast<float>(rgbe[1]) * scale,
                     static_cast<float>(rgbe[2]) * scale);
}

bool readScanline(std::ifstream& file, uint32_t width, std::vector<unsigned char>& row) {
    row.assign(static_cast<size_t>(width) * 4, 0);

    unsigned char header[4];
    if (!file.read(reinterpret_cast<char*>(header), 4)) return false;

    // The RUN-LENGTH form, which is what essentially every file in the wild
    // uses: a 2,2 marker, the width in the next two bytes, then four separate
    // component planes each run-length encoded.
    const bool encoded = header[0] == 2 && header[1] == 2 &&
                         ((static_cast<uint32_t>(header[2]) << 8) | header[3]) == width;
    if (!encoded) {
        // Flat. The four bytes already read are the first pixel.
        row[0] = header[0];
        row[1] = header[1];
        row[2] = header[2];
        row[3] = header[3];
        if (width > 1) {
            if (!file.read(reinterpret_cast<char*>(row.data() + 4),
                           static_cast<std::streamsize>(width - 1) * 4)) {
                return false;
            }
        }
        return true;
    }

    for (int component = 0; component < 4; ++component) {
        uint32_t x = 0;
        while (x < width) {
            unsigned char count = 0;
            if (!file.read(reinterpret_cast<char*>(&count), 1)) return false;

            if (count > 128) {
                // A run: one value repeated.
                unsigned char value = 0;
                if (!file.read(reinterpret_cast<char*>(&value), 1)) return false;
                const uint32_t run = count - 128u;
                if (x + run > width) return false;
                for (uint32_t i = 0; i < run; ++i) {
                    row[static_cast<size_t>(x + i) * 4 + component] = value;
                }
                x += run;
            } else {
                if (count == 0) return false;
                if (x + count > width) return false;
                for (uint32_t i = 0; i < count; ++i) {
                    unsigned char value = 0;
                    if (!file.read(reinterpret_cast<char*>(&value), 1)) return false;
                    row[static_cast<size_t>(x + i) * 4 + component] = value;
                }
                x += count;
            }
        }
    }
    return true;
}

// An orthonormal frame about a normal, for integrating over a hemisphere.
void frameAbout(const glm::vec3& normal, glm::vec3& outRight, glm::vec3& outUp) {
    // World up unless the normal IS world up, which is the case that makes the
    // cross product zero - and a zero tangent turns every sample direction into
    // a NaN, so the whole face comes out black.
    const glm::vec3 reference =
        std::fabs(normal.y) < 0.999f ? glm::vec3(0.0f, 1.0f, 0.0f) : glm::vec3(1.0f, 0.0f, 0.0f);
    outRight = glm::normalize(glm::cross(reference, normal));
    outUp = glm::cross(normal, outRight);
}

// The GGX half vector for a sample of the low-discrepancy sequence.
glm::vec3 importanceSampleGgx(float u1, float u2, const glm::vec3& normal, float roughness) {
    const float a = roughness * roughness;

    const float phi = 2.0f * kPi * u1;
    const float cosTheta = std::sqrt((1.0f - u2) / (1.0f + (a * a - 1.0f) * u2));
    const float sinTheta = std::sqrt(std::max(0.0f, 1.0f - cosTheta * cosTheta));

    const glm::vec3 local(std::cos(phi) * sinTheta, std::sin(phi) * sinTheta, cosTheta);

    glm::vec3 right(0.0f);
    glm::vec3 up(0.0f);
    frameAbout(normal, right, up);
    return glm::normalize(right * local.x + up * local.y + normal * local.z);
}

// Van der Corput, which is the cheap half of a Hammersley sequence.
float radicalInverse(uint32_t bits) {
    bits = (bits << 16u) | (bits >> 16u);
    bits = ((bits & 0x55555555u) << 1u) | ((bits & 0xAAAAAAAAu) >> 1u);
    bits = ((bits & 0x33333333u) << 2u) | ((bits & 0xCCCCCCCCu) >> 2u);
    bits = ((bits & 0x0F0F0F0Fu) << 4u) | ((bits & 0xF0F0F0F0u) >> 4u);
    bits = ((bits & 0x00FF00FFu) << 8u) | ((bits & 0xFF00FF00u) >> 8u);
    return static_cast<float>(bits) * 2.3283064365386963e-10f;
}

} // namespace

bool LoadRadiance(const std::string& path, std::vector<glm::vec3>& outPixels,
                  uint32_t& outWidth, uint32_t& outHeight, std::string& outError) {
    outPixels.clear();
    outWidth = 0;
    outHeight = 0;

    std::ifstream file(path, std::ios::binary);
    if (!file.is_open()) {
        outError = "cannot open " + path;
        return false;
    }

    std::string line;
    if (!std::getline(file, line) || line.rfind("#?", 0) != 0) {
        outError = path + " is not a Radiance file (no #? on the first line)";
        return false;
    }

    // The header runs until a blank line. FORMAT is the only field that matters
    // here, and anything but the 32-bit RGBE encoding is a file this cannot
    // read rather than one it reads wrongly.
    bool rgbe = false;
    while (std::getline(file, line)) {
        if (line.empty() || line == "\r") break;
        if (line.rfind("FORMAT=", 0) == 0) {
            rgbe = line.find("32-bit_rle_rgbe") != std::string::npos ||
                   line.find("32-bit_rgbe") != std::string::npos;
        }
    }
    if (!rgbe) {
        outError = path + " is not 32-bit RGBE; XYZE and half formats are not read";
        return false;
    }

    if (!std::getline(file, line)) {
        outError = path + " ends before its resolution line";
        return false;
    }

    int height = 0;
    int width = 0;
    // Only the standard orientation. A file written -X +Y or with the axes
    // swapped would load transposed or upside down, which is worse than
    // refusing it.
    std::istringstream resolution(line);
    std::string down;
    std::string across;
    if (!(resolution >> down >> height >> across >> width) || down != "-Y" || across != "+X" ||
        width <= 0 || height <= 0) {
        outError = path + " has a resolution line this does not read: '" + line + "'";
        return false;
    }

    outWidth = static_cast<uint32_t>(width);
    outHeight = static_cast<uint32_t>(height);
    outPixels.resize(static_cast<size_t>(width) * height);

    std::vector<unsigned char> row;
    for (uint32_t y = 0; y < outHeight; ++y) {
        if (!readScanline(file, outWidth, row)) {
            outError = path + " ends part way through scanline " + std::to_string(y);
            outPixels.clear();
            return false;
        }
        for (uint32_t x = 0; x < outWidth; ++x) {
            unsigned char rgbeTexel[4] = {row[static_cast<size_t>(x) * 4 + 0],
                                          row[static_cast<size_t>(x) * 4 + 1],
                                          row[static_cast<size_t>(x) * 4 + 2],
                                          row[static_cast<size_t>(x) * 4 + 3]};
            outPixels[static_cast<size_t>(y) * outWidth + x] = decodeRgbe(rgbeTexel);
        }
    }
    return true;
}

bool FromEquirectangular(const std::vector<glm::vec3>& pixels, uint32_t width, uint32_t height,
                         uint32_t faceSize, Cubemap& out) {
    if (width == 0 || height == 0 || faceSize == 0) return false;
    if (pixels.size() != static_cast<size_t>(width) * height) return false;
    if (!out.Create(faceSize)) return false;

    for (uint32_t face = 0; face < Cubemap::kFaceCount; ++face) {
        for (uint32_t y = 0; y < faceSize; ++y) {
            for (uint32_t x = 0; x < faceSize; ++x) {
                const float u = (static_cast<float>(x) + 0.5f) / static_cast<float>(faceSize);
                const float v = (static_cast<float>(y) + 0.5f) / static_cast<float>(faceSize);
                const glm::vec3 direction = Cubemap::FaceToDirection(face, u, v);

                // The panorama's own mapping: longitude across, latitude down.
                const float longitude = std::atan2(direction.z, direction.x);
                const float latitude = std::asin(std::min(std::max(direction.y, -1.0f), 1.0f));

                const float sourceU = (longitude / (2.0f * kPi)) + 0.5f;
                const float sourceV = 0.5f - (latitude / kPi);

                const auto sx = static_cast<uint32_t>(std::min(
                    std::max(sourceU * static_cast<float>(width), 0.0f),
                    static_cast<float>(width) - 1.0f));
                const auto sy = static_cast<uint32_t>(std::min(
                    std::max(sourceV * static_cast<float>(height), 0.0f),
                    static_cast<float>(height) - 1.0f));

                out.At(face, x, y) = pixels[static_cast<size_t>(sy) * width + sx];
            }
        }
    }
    return true;
}

bool Irradiance(const Cubemap& source, uint32_t faceSize, Cubemap& out) {
    if (!source.valid() || faceSize == 0) return false;
    if (!out.Create(faceSize)) return false;

    // Coarse on purpose. The output is a handful of texels a side - a diffuse
    // response has no detail in it - and the integral below is the expensive
    // part of the whole feature.
    constexpr float kStep = 0.025f;

    for (uint32_t face = 0; face < Cubemap::kFaceCount; ++face) {
        for (uint32_t y = 0; y < faceSize; ++y) {
            for (uint32_t x = 0; x < faceSize; ++x) {
                const float u = (static_cast<float>(x) + 0.5f) / static_cast<float>(faceSize);
                const float v = (static_cast<float>(y) + 0.5f) / static_cast<float>(faceSize);
                const glm::vec3 normal = Cubemap::FaceToDirection(face, u, v);

                glm::vec3 right(0.0f);
                glm::vec3 up(0.0f);
                frameAbout(normal, right, up);

                glm::vec3 total(0.0f);
                uint32_t samples = 0;

                for (float phi = 0.0f; phi < 2.0f * kPi; phi += kStep) {
                    for (float theta = 0.0f; theta < 0.5f * kPi; theta += kStep) {
                        const float sinTheta = std::sin(theta);
                        const float cosTheta = std::cos(theta);

                        const glm::vec3 tangent(sinTheta * std::cos(phi),
                                                sinTheta * std::sin(phi), cosTheta);
                        const glm::vec3 direction =
                            right * tangent.x + up * tangent.y + normal * tangent.z;

                        // cos for the projected area, sin for the solid angle of
                        // the ring. Drop either and a constant environment does
                        // not come back as itself, which is what the test looks
                        // for.
                        total += source.Sample(direction) * cosTheta * sinTheta;
                        ++samples;
                    }
                }

                out.At(face, x, y) =
                    samples > 0 ? total * (kPi / static_cast<float>(samples)) : glm::vec3(0.0f);
            }
        }
    }
    return true;
}

bool Prefilter(const Cubemap& source, uint32_t baseSize, uint32_t levels,
               std::vector<Cubemap>& out) {
    out.clear();
    if (!source.valid() || baseSize == 0 || levels == 0) return false;

    constexpr uint32_t kSamples = 128;

    for (uint32_t level = 0; level < levels; ++level) {
        const uint32_t size = std::max(baseSize >> level, 1u);
        const float roughness =
            levels > 1 ? static_cast<float>(level) / static_cast<float>(levels - 1) : 0.0f;

        Cubemap mip;
        if (!mip.Create(size)) return false;

        for (uint32_t face = 0; face < Cubemap::kFaceCount; ++face) {
            for (uint32_t y = 0; y < size; ++y) {
                for (uint32_t x = 0; x < size; ++x) {
                    const float u = (static_cast<float>(x) + 0.5f) / static_cast<float>(size);
                    const float v = (static_cast<float>(y) + 0.5f) / static_cast<float>(size);
                    const glm::vec3 normal = Cubemap::FaceToDirection(face, u, v);

                    if (roughness == 0.0f) {
                        // Not an approximation of the loop below - it is what
                        // the loop converges to, exactly. At zero roughness the
                        // GGX lobe is a delta: importanceSampleGgx returns the
                        // normal for every sample, so the reflected direction is
                        // the normal for every sample, and a weighted average of
                        // one value is that value. The loop was spending 128
                        // samples to arrive back where it started.
                        //
                        // This is the level the SKY reads, so it is also the
                        // level whose cost decides how large the chain can be.
                        mip.At(face, x, y) = source.Sample(normal);
                        continue;
                    }

                    // The usual approximation: the view direction is the normal,
                    // so the reflection is too. It is what makes a single
                    // prefiltered map serve every viewing angle, and the cost is
                    // that grazing reflections lose their stretch.
                    const glm::vec3 view = normal;

                    glm::vec3 total(0.0f);
                    float weight = 0.0f;

                    for (uint32_t i = 0; i < kSamples; ++i) {
                        const float u1 =
                            static_cast<float>(i) / static_cast<float>(kSamples);
                        const float u2 = radicalInverse(i);

                        const glm::vec3 half = importanceSampleGgx(u1, u2, normal, roughness);
                        const glm::vec3 light =
                            glm::normalize(half * (2.0f * glm::dot(view, half)) - view);

                        const float cosLight = glm::dot(normal, light);
                        if (cosLight <= 0.0f) continue;

                        total += source.Sample(light) * cosLight;
                        weight += cosLight;
                    }

                    mip.At(face, x, y) =
                        weight > 0.0f ? total / weight : source.Sample(normal);
                }
            }
        }
        out.push_back(std::move(mip));
    }
    return true;
}

Cubemap Constant(uint32_t size, const glm::vec3& colour) {
    Cubemap map;
    map.Create(size, colour);
    return map;
}

} // namespace EnvironmentMap
} // namespace Supersonic
