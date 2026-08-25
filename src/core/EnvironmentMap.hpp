#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include <glm/glm.hpp>

namespace Supersonic {

// A cubemap of floating-point colour.
//
// Float, not bytes, and that is the point of the whole feature: the environment
// is currently two colours mixed by height, so a metal surface reflects a
// two-colour gradient and a sky brighter than white cannot exist. An HDRI is
// mostly values above one - a sun is thousands - and clamping those to one
// throws away exactly the range that makes a reflection look like light.
//
// Vulkan-free: this is sampling and integration, and it is where the bugs are.
// A face index or a flipped V is the same class of mistake as the froxel grid's
// Y flip - the scene still looks lit, and everything is reflected in the wrong
// direction.
class Cubemap {
public:
    // The order Vulkan expects a cube image's layers in. Written out rather
    // than left implicit, because the array index IS the face and getting the
    // two out of step reflects the sky off the floor.
    enum Face : uint32_t {
        PositiveX = 0,
        NegativeX = 1,
        PositiveY = 2,
        NegativeY = 3,
        PositiveZ = 4,
        NegativeZ = 5,
        kFaceCount = 6,
    };

    bool Create(uint32_t size, const glm::vec3& fill = glm::vec3(0.0f));
    bool valid() const { return m_size > 0; }
    uint32_t size() const { return m_size; }

    glm::vec3& At(uint32_t face, uint32_t x, uint32_t y);
    const glm::vec3& At(uint32_t face, uint32_t x, uint32_t y) const;

    // Bilinear, and clamped at a face's edge rather than wrapped into its
    // neighbour. The seam that leaves is under a texel wide at the sizes this
    // is used at, and doing it properly means knowing which face adjoins which
    // along which edge in which orientation - twenty-four cases, each of which
    // is wrong in a way you cannot see.
    glm::vec3 Sample(const glm::vec3& direction) const;

    // Which face a direction lands on, and where on it.
    //
    // The cube face convention, written out from the specification rather than
    // guessed: the major axis picks the face, and the other two become s and t
    // with signs that differ per face. This is the piece a test pins by round
    // trip, because every one of the twelve signs is invisible when wrong.
    static void DirectionToFace(const glm::vec3& direction, uint32_t& outFace,
                                float& outU, float& outV);

    // The inverse: the direction a texel centre looks along. Not normalised by
    // the caller's expectation - it is returned unit length.
    static glm::vec3 FaceToDirection(uint32_t face, float u, float v);

    // Row-major, face-major: face 0's rows, then face 1's. The layout a Vulkan
    // cube image wants its layers in.
    const std::vector<glm::vec3>& pixels() const { return m_pixels; }

private:
    uint32_t m_size{0};
    std::vector<glm::vec3> m_pixels;
};

namespace EnvironmentMap {

// A Radiance .hdr, which is what an HDRI actually ships as.
//
// Six PNG faces would sidestep the parser and cannot hold a value above one,
// which is most of the point - so the eighty lines are paid rather than
// avoided. Both the flat and the run-length encodings are read, because
// essentially every file in the wild is the latter.
bool LoadRadiance(const std::string& path, std::vector<glm::vec3>& outPixels,
                  uint32_t& outWidth, uint32_t& outHeight, std::string& outError);

// An equirectangular panorama, which is how an HDRI is stored, onto the six
// faces of a cube, which is how a shader can sample it in one instruction.
bool FromEquirectangular(const std::vector<glm::vec3>& pixels, uint32_t width, uint32_t height,
                         uint32_t faceSize, Cubemap& out);

// The cosine-weighted hemisphere integral: what a diffuse surface facing each
// direction receives from the whole environment.
//
// Its own oracle, which is why it can be checked exactly: over a CONSTANT
// environment the integral of cosine over the hemisphere, divided by pi, is
// one - so the answer is that same constant, to the last bit the sampling
// allows. A botched solid angle or a normalisation off by pi shows up as a
// number that is not the one that went in.
bool Irradiance(const Cubemap& source, uint32_t faceSize, Cubemap& out);

// The specular half: one cubemap per roughness, GGX-importance-sampled.
//
// Two exact endpoints. At roughness zero the distribution collapses onto the
// mirror direction, so the result is the source sampled along it. At roughness
// one over a constant environment it is that constant. The middle is where
// "does it look plausible" lives, and it is bracketed by two things that do
// not.
bool Prefilter(const Cubemap& source, uint32_t baseSize, uint32_t levels,
               std::vector<Cubemap>& out);

// A cubemap of one colour, which is the fixture the tests above need and the
// baseline the renderer is checked against: with the scene's sky and ground
// ambient both set to this colour, an environment map of it has to reproduce
// the analytic hemisphere it replaces exactly.
Cubemap Constant(uint32_t size, const glm::vec3& colour);

} // namespace EnvironmentMap
} // namespace Supersonic
