#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

namespace Supersonic {

// Operations on 8-bit, four-channel pixels in memory.
//
// Vulkan-free and file-free, so a suite reaches them without a device: the
// renderer hands them what it read back, and the answer is plain bytes.
namespace ImagePixels {

// The byte order of a four-channel image: which byte of each pixel is red.
// Rgba is what stb and PNG use; Bgra is what a Windows swapchain usually is.
enum class ChannelOrder { Rgba, Bgra };

// Tightly packed RGBA with every alpha 255, from `height` rows of `width`
// pixels in `order`, each row starting `rowPitch` bytes after the one before.
//
// For a readback: the rows of a buffer an image was copied into may be padded,
// the swapchain is often BGRA, and its alpha is whatever the last pass left.
// A PNG of it keeps the alpha, and a PNG with alpha below 255 looks empty in
// some viewers and right in others.
//
// Empty when there is nothing to read: a null source, a zero size, or a pitch
// shorter than a row.
std::vector<uint8_t> PackOpaqueRgba(const uint8_t* source, uint32_t width, uint32_t height,
                                    std::size_t rowPitch, ChannelOrder order);

// Godot's process/fix_alpha_border, in place, on tightly packed RGBA.
//
// A sprite painted over transparency stores some colour under alpha 0, and
// the tools that write these PNGs store black. Linear filtering at the
// sprite's edge blends toward that colour, so every edge drawn magnified or
// between pixels gets a dark rim. This gives each nearly transparent texel the
// RGB of its nearest opaque one, so the blend runs toward the sprite's own
// colour. Alpha is untouched, so nothing changes where it is drawn.
//
// Godot's Image::fix_alpha_edges exactly, constants and ties included:
// - a texel with alpha below kFixAlphaThreshold (20, not 1) is filled; the
//   rest are left as they are;
// - from the nearest texel, by squared Euclidean distance, with alpha of at
//   least the threshold, inside the square of kFixAlphaRadius (4) texels each
//   way, cut off at the image's edges; the first such texel in row-major
//   order wins a tie;
// - read from the image as it was, so a filled texel never fills another;
// - a texel with no opaque one in reach keeps its RGB.
//
// Checked texel for texel against the .ctex files Godot 4.7 imported for all
// 38 of Wolf Brigade's runtime PNGs: this function, on each PNG as stb
// decodes it, gives Godot's pixels exactly.
inline constexpr uint8_t kFixAlphaThreshold = 20;
inline constexpr int kFixAlphaRadius = 4;
void FixAlphaBorder(uint8_t* rgba, uint32_t width, uint32_t height);

} // namespace ImagePixels
} // namespace Supersonic
