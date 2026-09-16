#pragma once

#include <string>

#include <vulkan/vulkan.hpp>

#include "renderer/VulkanDevice.hpp"

namespace Supersonic {

// Reads a rendered image back and writes it to a PNG.
//
// The engine could render a frame and had no way to show anyone what it had
// rendered. Every visual change was therefore unverifiable except by a human
// looking at a window - which meant CI could prove the binary links and runs
// clean under validation, and could say nothing at all about whether the
// picture was right.
//
// Deliberately slow and simple. It waits for the device to go idle, copies the
// whole image through a host-visible staging buffer, and writes the file
// synchronously. This runs at the end of a --frames run, and with
// --screenshot-every once every N frames - only ever because a command line
// asked for pictures, so the stall is paid by capture runs, whose wall-clock
// nobody reads (their profiler worst cases are the readback, not the game).
// Doing it properly with a fence and a ring buffer would be machinery in
// service of that.
namespace ScreenCapture {

// Writes `image` to `path` as a PNG. The image must have been created with
// eTransferSrc usage and must currently be in eShaderReadOnlyOptimal, which is
// where the bloom composite leaves its output.
//
// Returns false and explains why rather than throwing: a failed screenshot
// should not take down a run that otherwise succeeded.
bool WritePng(VulkanDevice& device, vk::CommandPool commandPool, vk::Image image,
              uint32_t width, uint32_t height, const std::string& path,
              std::string& outError);

} // namespace ScreenCapture
} // namespace Supersonic
