#include "core/ImagePixels.hpp"

namespace Supersonic::ImagePixels {

std::vector<uint8_t> PackOpaqueRgba(const uint8_t* source, uint32_t width, uint32_t height,
                                    std::size_t rowPitch, ChannelOrder order) {
    const std::size_t rowBytes = static_cast<std::size_t>(width) * 4;
    if (!source || width == 0 || height == 0 || rowPitch < rowBytes) return {};

    // Which source byte lands in the output's red and blue. Green stays where
    // it is in both orders.
    const std::size_t red = order == ChannelOrder::Bgra ? 2 : 0;
    const std::size_t blue = order == ChannelOrder::Bgra ? 0 : 2;

    std::vector<uint8_t> out(rowBytes * height);
    for (uint32_t y = 0; y < height; ++y) {
        const uint8_t* in = source + static_cast<std::size_t>(y) * rowPitch;
        uint8_t* row = out.data() + static_cast<std::size_t>(y) * rowBytes;
        for (uint32_t x = 0; x < width; ++x) {
            const uint8_t* pixel = in + static_cast<std::size_t>(x) * 4;
            uint8_t* packed = row + static_cast<std::size_t>(x) * 4;
            packed[0] = pixel[red];
            packed[1] = pixel[1];
            packed[2] = pixel[blue];
            packed[3] = 255;
        }
    }
    return out;
}

void FixAlphaBorder(uint8_t* rgba, uint32_t width, uint32_t height) {
    if (!rgba || width == 0 || height == 0) return;

    // In place, with no copy, and the answer is the one Godot's copy gives:
    // only texels below the threshold are written, only their RGB, and only
    // the RGB of texels at or above it is read. Alpha decides who is who and
    // is never changed, so a filled texel is never read as a neighbour.
    const uint8_t* source = rgba;

    const int w = static_cast<int>(width);
    const int h = static_cast<int>(height);
    const auto at = [w](int x, int y) {
        return (static_cast<std::size_t>(y) * static_cast<std::size_t>(w) +
                static_cast<std::size_t>(x)) * 4;
    };

    for (int y = 0; y < h; ++y) {
        for (int x = 0; x < w; ++x) {
            if (source[at(x, y) + 3] >= kFixAlphaThreshold) continue;

            bool found = false;
            std::size_t closest = 0;
            int closestDistance = 0;
            const int fromY = y - kFixAlphaRadius > 0 ? y - kFixAlphaRadius : 0;
            const int toY = y + kFixAlphaRadius < h - 1 ? y + kFixAlphaRadius : h - 1;
            const int fromX = x - kFixAlphaRadius > 0 ? x - kFixAlphaRadius : 0;
            const int toX = x + kFixAlphaRadius < w - 1 ? x + kFixAlphaRadius : w - 1;
            for (int ny = fromY; ny <= toY; ++ny) {
                for (int nx = fromX; nx <= toX; ++nx) {
                    const int dx = x - nx;
                    const int dy = y - ny;
                    const int distance = dx * dx + dy * dy;
                    // Strictly nearer, so the first in row-major order keeps
                    // a tie, as Godot's loop does.
                    if (found && distance >= closestDistance) continue;
                    if (source[at(nx, ny) + 3] < kFixAlphaThreshold) continue;
                    found = true;
                    closest = at(nx, ny);
                    closestDistance = distance;
                }
            }
            if (!found) continue;

            uint8_t* texel = rgba + at(x, y);
            texel[0] = source[closest + 0];
            texel[1] = source[closest + 1];
            texel[2] = source[closest + 2];
        }
    }
}

} // namespace Supersonic::ImagePixels
