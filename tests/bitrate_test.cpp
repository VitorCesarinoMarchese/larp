#include "codec/h264.hpp"
#include <iostream>
#include <stdexcept>
#include <vector>
#define CHECK(x)                                                                                   \
    do {                                                                                           \
        if (!(x))                                                                                  \
            throw std::runtime_error(#x);                                                          \
    } while (false)
int main() {
    using namespace larp;
    H264Encoder encoder(320, 180, 30, CodecBackend::software, 4000000);
    H264Decoder decoder;
    std::vector<std::byte> rgb(320 * 180 * 3), encoded(h264_capacity), decoded(rgb.size());
    auto measure = [&](std::uint32_t bitrate) {
        encoder.set_bitrate(bitrate);
        std::size_t bytes = 0;
        for (unsigned frame = 0; frame < 90; ++frame) {
            for (std::size_t i = 0; i < rgb.size(); ++i)
                rgb[i] = std::byte((i * 13 + i / 960 * 17 + frame * 7) & 255);
            const auto size = encoder.encode(rgb, encoded);
            CHECK(decoder.decode(std::span(encoded).first(size), decoded));
            if (frame >= 30)
                bytes += size;
        }
        return bytes;
    };
    const auto high = measure(4000000), low = measure(256000), restored = measure(4000000);
    std::cout << "Encoded bytes over 60 frames: high=" << high << " low=" << low
              << " restored=" << restored << '\n';
    CHECK(low * 2 < high);
    CHECK(low * 2 < restored);
    bool rejected = false;
    try {
        encoder.set_bitrate(0);
    } catch (const std::invalid_argument &) {
        rejected = true;
    }
    CHECK(rejected);
    H264Encoder fixed(320, 180, 30);
    rejected = false;
    try {
        fixed.set_bitrate(1000000);
    } catch (const std::invalid_argument &) {
        rejected = true;
    }
    CHECK(rejected);
}
