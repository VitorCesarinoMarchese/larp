#include "host/h264_main.hpp"
#include "codec/h264.hpp"
#include "common/runtime.hpp"
#include "transport/send_frame.hpp"
#include <iostream>
#include <thread>
#include <vector>
int h264_synthetic_main(int argc, char **argv) {
    using namespace larp;
    if (argc != 6)
        throw std::invalid_argument("Usage: larp-host --h264-synthetic IPv4 PORT FRAMES FPS");
    const auto destination =
        Endpoint::parse(argv[2], static_cast<std::uint16_t>(number(argv[3], 1, 65535)));
    const auto frames = number(argv[4], 1, 1000000000), fps = number(argv[5], 1, 30);
    UdpSocket socket(Endpoint::parse("0.0.0.0", 0));
    H264Encoder encoder(320, 180, fps);
    std::vector<std::byte> rgb(320 * 180 * 3), encoded(h264_capacity);
    const auto period = std::chrono::nanoseconds(1000000000 / fps);
    auto deadline = Clock::now();
    std::uint64_t packets = 0, encode_us = 0, bytes = 0;
    for (std::uint64_t id = 1; id <= frames; ++id) {
        std::this_thread::sleep_until(deadline);
        std::fill(rgb.begin(), rgb.end(), std::byte(90 + (id - 1) % 80));
        if (id > 1)
            for (std::size_t i = 0; i < rgb.size(); ++i)
                rgb[i] = std::byte((i * 13 + i / 960 * 17 + id * 7) & 255);
        const auto timestamp = now_us();
        const auto size = encoder.encode(rgb, encoded);
        encode_us += now_us() - timestamp;
        bytes += size;
        packets += send_frame(socket, destination, std::span(encoded).first(size), id, timestamp);
        deadline = std::max(deadline + period, Clock::now());
    }
    std::cout << "Encoded: " << frames << " Packets: " << packets << " Bytes: " << bytes
              << " Encode mean us: " << encode_us / frames << '\n';
    return 0;
}
