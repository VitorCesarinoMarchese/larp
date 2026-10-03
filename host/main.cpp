#include "common/runtime.hpp"
#include "host/h264_main.hpp"
#ifdef LARP_CAPTURE
#include "host/capture_main.hpp"
#endif
#include "platform/udp.hpp"
#include "protocol/packet.hpp"
#include "transport/session.hpp"
#include "transport/adaptive_sender.hpp"
#include <algorithm>
#include <array>
#include <iostream>
#include <thread>
#include <vector>
int main(int argc, char **argv) {
    try {
        const auto network = larp::take_network_options(argc, argv, larp::SessionRole::sender);
        if (argc > 1 && std::string_view(argv[1]) == "--h264-synthetic")
            return h264_synthetic_main(argc, argv, network);
        if (argc > 1 &&
            (std::string_view(argv[1]) == "--capture" || std::string_view(argv[1]) == "--h264")) {
#ifdef LARP_CAPTURE
            return capture_main(argc, argv, network);
#else
            throw std::runtime_error("PipeWire capture was disabled at build time");
#endif
        }
        if (argc != 6) {
            std::cerr << "Usage: larp-host IPv4 PORT FRAMES BYTES FPS [--key-file PATH] [--bind IPv4]\n";
            return 1;
        }
        using namespace larp;
        const auto destination =
            Endpoint::parse(argv[1], static_cast<std::uint16_t>(number(argv[2], 1, 65535)));
        const auto frames = number(argv[3], 1, 1000000000),
                   bytes = number(argv[4], 1, max_frame_size), fps = number(argv[5], 1, 1000);
        SessionSocket socket(network.bind, SessionRole::sender, network);
        socket.connect(destination);
        AdaptiveSender feedback(socket, destination, 0);
        std::vector<std::byte> frame(bytes);
        std::array<std::byte, datagram_size> wire{};
        const auto count = (bytes + payload_capacity - 1) / payload_capacity;
        const auto period = std::chrono::nanoseconds(1000000000 / fps);
        auto deadline = Clock::now();
        std::uint64_t packets = 0;
        const auto start = now_us();
        for (std::uint64_t id = 1; id <= frames; ++id) {
            feedback.wait_until(deadline);
            const auto timestamp = now_us();
            for (std::size_t i = 0; i < frame.size(); ++i)
                frame[i] = synthetic_byte(id, i);
            for (std::uint32_t index = 0; index < count; ++index) {
                const auto offset = index * payload_capacity;
                const auto length = std::min(payload_capacity, bytes - offset);
                encode(Header{id, index, count, timestamp, length}, wire);
                std::copy_n(frame.data() + offset, length, wire.data() + header_size);
                socket.send(destination, std::span(wire).first(header_size + length));
                ++packets;
            }
            deadline = std::max(deadline + period, Clock::now());
        }
        std::cout << "Sent frames: " << frames << " Packets: " << packets
                  << " Elapsed us: " << now_us() - start << '\n';
        if (socket.secured())
            std::cout << "Sessions: " << socket.generation() << " Security rejected: " << socket.rejected()
                      << " Unsent: " << socket.unsent() << '\n';
    } catch (const std::exception &error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
