#pragma once

#include <cstdint>
#include <vector>

#include <glm/glm.hpp>

namespace Supersonic {

// The view frustum, cut into a grid, so a fragment only pays for the lights
// that can reach it.
//
// Eight was a hard cap, and `LightSelection` only ever decided WHICH eight - it
// says so itself. Nine lamps in one room still dropped one, because every
// fragment in the frame looped over every light in the scene and the loop had to
// be bounded by something. Cutting the frustum up first is what turns "how many
// lights can the shader afford" into "how many can overlap one small box", which
// is a question about the level rather than about the renderer.
//
// Screen tiles across X and Y, and slices in Z that are EXPONENTIAL rather than
// uniform: a uniform slice puts almost every froxel out where the frustum is
// enormous and nothing is, and crams the near field - which is where the lamps
// actually are - into the first one. The standard exponential distribution keeps
// each slice a similar shape.
//
// Vulkan-free on purpose, like LightSelection beside it. This is arithmetic
// about a frustum; there is nothing to see and everything to get wrong, and the
// suites touch no Vulkan entry point, so keeping it here is what makes an
// off-by-one at a slice boundary a test rather than a bug report.
namespace ClusterGrid {

// Froxels across the screen and through depth.
//
// 16x9 matches the usual aspect closely enough that a tile stays roughly square,
// and 24 slices is the number the clustered-shading literature settles on: deep
// enough that a slice is a thin shell, shallow enough that the table stays a few
// thousand entries rather than a few hundred thousand.
inline constexpr uint32_t kTilesX = 16;
inline constexpr uint32_t kTilesY = 9;
inline constexpr uint32_t kSlices = 24;
inline constexpr uint32_t kClusterCount = kTilesX * kTilesY * kSlices;

// How many light indices the whole frame may record.
//
// A cap on the LIST, not on the lights: a scene may hold any number, and this
// bounds how many (cluster, light) pairs get written. Overflow drops pairs and
// says so, which loses light from a corner of the screen rather than refusing to
// draw - and the count is the number to raise when it ever happens.
inline constexpr uint32_t kMaxLightIndices = 1u << 16;

// Where one cluster's slice of the index list starts, and how long it is.
//
// Two uints rather than a start and an end, because the shader loops
// `for (i = 0; i < count; ++i)` and a count is what it wants; deriving one from
// two offsets would put the subtraction in the hot path for no reason.
struct ClusterRange {
    uint32_t offset{0};
    uint32_t count{0};
};

// What the assignment produced for one frame.
struct Assignment {
    // kClusterCount entries, in x + y*kTilesX + z*kTilesX*kTilesY order - the
    // same order the shader computes its own index in.
    std::vector<ClusterRange> clusters;

    // Indices into the frame's light buffer, grouped by cluster.
    std::vector<uint32_t> indices;

    // Pairs that did not fit in kMaxLightIndices. Nonzero means a corner of the
    // screen is missing light, and is worth a log line rather than a silence.
    uint32_t dropped{0};
};

// One local light, reduced to what the assignment needs.
//
// View space, because that is the space the grid is defined in and converting
// once per light beats converting once per froxel. A spot is treated as its
// bounding sphere: a cone test would cull more, and it would also be a second
// piece of geometry to get wrong for a saving that only shows on spots with a
// narrow cone and a long range.
struct LocalLight {
    glm::vec3 viewPosition{0.0f};
    float radius{0.0f};
};

// Which depth slice a VIEW-SPACE distance falls in.
//
// `viewZ` is positive distance in front of the camera, not the negative z of
// view space - the sign convention is the single easiest thing to get backwards
// here, so the name says which one this wants.
//
// Clamped at both ends rather than left to produce an out-of-range index:
// something exactly on the near plane, or beyond the far one, still has to land
// in a real froxel or the lookup reads someone else's lights.
uint32_t SliceForDepth(float viewZ, float nearPlane, float farPlane);

// The inverse: the view-space depth range a slice covers. For tests, and for
// building the froxel bounds.
void SliceDepthRange(uint32_t slice, float nearPlane, float farPlane,
                     float& outNear, float& outFar);

// Assigns lights to clusters for one frame.
//
// `tanHalfFovY` and `aspect` describe the frustum the same way the projection
// does. Lights are given in VIEW space; the caller has already applied the view
// matrix, because it has the matrix and this file does not want it.
Assignment Assign(const std::vector<LocalLight>& lights,
                  float nearPlane, float farPlane,
                  float tanHalfFovY, float aspect);

// Which cluster a FRAGMENT is in, given where it landed on screen and how far
// in front of the camera it is.
//
// The mirror of clusterIndexFor in scene_ubo.glsl, and it exists so that half
// of the mapping is reachable by a test at all. The screen-space half used to
// live only in GLSL, and the one bug it had - the row index mirrored about the
// horizon, because the projection flips Y for Vulkan while this grid numbers
// its rows in view space - was invisible to every case in the suite and to any
// scene whose lamps happen to reach every froxel anyway.
//
// `fragCoord` is in the render target's pixels with y growing DOWN, which is
// what gl_FragCoord gives. The flip to view-space row order happens here, in
// one place, and the GLSL is a transliteration of it.
uint32_t ClusterForFragment(const glm::vec2& fragCoord, float viewZ,
                            const glm::vec2& targetSize,
                            float nearPlane, float farPlane);

// Whether a sphere touches one froxel. Exposed because it is the predicate the
// whole thing rests on, and a test that can only see the finished lists cannot
// say which half was wrong.
bool SphereTouchesCluster(const LocalLight& light, uint32_t x, uint32_t y, uint32_t z,
                          float nearPlane, float farPlane,
                          float tanHalfFovY, float aspect);

} // namespace ClusterGrid

} // namespace Supersonic
