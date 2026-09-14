#include "codec/h264.hpp"
#include "common/runtime.hpp"
#include <iostream>
#include <vector>
int main(int argc, char **argv) {
    try {
        using namespace larp;
        if (argc != 2)
            throw std::invalid_argument("Usage: larp-codec-bench FRAMES");
        const auto frames = number(argv[1], 1, 1000000);
        H264Encoder encoder(320, 180, 30);
        H264Decoder decoder;
        std::vector<std::byte> rgb(320 * 180 * 3), encoded(h264_capacity), decoded(rgb.size());
        std::uint64_t encode_us = 0, decode_us = 0, bytes = 0;
        for (std::uint64_t id = 1; id <= frames; ++id) {
            for (std::size_t i = 0; i < rgb.size(); ++i)
                rgb[i] = std::byte((i * 13 + i / 960 * 17 + id * 7) & 255);
            const auto before = now_us();
            const auto size = encoder.encode(rgb, encoded);
            const auto middle = now_us();
            if (!decoder.decode(std::span(encoded).first(size), decoded))
                throw std::runtime_error("benchmark decode failed");
            decode_us += now_us() - middle;
            encode_us += middle - before;
            bytes += size;
        }
        std::cout << "Frames: " << frames << " Bytes: " << bytes
                  << " Encode mean us: " << encode_us / frames
                  << " Decode mean us: " << decode_us / frames << '\n';
    } catch (const std::exception &error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
