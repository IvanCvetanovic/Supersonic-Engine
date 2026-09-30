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

} // namespace Supersonic::ImagePixels
