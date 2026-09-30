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

} // namespace ImagePixels
} // namespace Supersonic
