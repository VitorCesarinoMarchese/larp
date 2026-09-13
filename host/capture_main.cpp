#include "host/capture_main.hpp"
#include "capture/capture.hpp"
#include "common/runtime.hpp"
#include "media/raw_frame.hpp"
#include "platform/udp.hpp"
#include "protocol/packet.hpp"
#include <array>
#include <iostream>
#include <thread>
#include <vector>
int capture_main(int argc, char **argv) {
    using namespace larp;
    if (argc != 6) {
        std::cerr << "Usage: larp-host --capture IPv4 PORT SECONDS FPS\n";
        return 1;
    }
    const auto destination =
        Endpoint::parse(argv[2], static_cast<std::uint16_t>(number(argv[3], 1, 65535)));
    const auto seconds = number(argv[4], 1, 3600), fps = number(argv[5], 1, 30);
    UdpSocket socket(Endpoint::parse("0.0.0.0", 0));
    std::vector<std::byte> frame(raw_header_size + preview_capacity);
    std::array<std::byte, datagram_size> wire{};
    std::cout << "Select a screen in the desktop portal.\n" << std::flush;
    auto capture = open_screen_capture();
    const auto started = Clock::now(), until = started + std::chrono::seconds(seconds);
    auto deadline = started;
    const auto period = std::chrono::nanoseconds(1000000000 / fps);
    std::uint64_t frames = 0, packets = 0, timeouts = 0;
    while (Clock::now() < until) {
        std::this_thread::sleep_until(std::min(deadline, until));
        if (Clock::now() >= until)
            break;
        auto captured = capture->next(std::span(frame).subspan(raw_header_size),
                                      std::chrono::milliseconds(100));
        if (captured) {
            const auto bytes =
                raw_header_size + std::size_t(captured->size.width) * captured->size.height * 3;
            const auto encoded = std::span(frame).first(bytes);
            if (!write_raw_header(encoded, captured->size.width, captured->size.height))
                throw std::runtime_error("capture produced an invalid preview layout");
            if (!frames)
                std::cout << "Preview: " << captured->size.width << 'x' << captured->size.height
                          << '\n'
                          << std::flush;
            ++frames;
            const auto count =
                static_cast<std::uint32_t>((bytes + payload_capacity - 1) / payload_capacity);
            for (std::uint32_t index = 0; index < count; ++index) {
                const auto offset = std::size_t(index) * payload_capacity;
                const auto length = static_cast<std::uint32_t>(
                    std::min<std::size_t>(payload_capacity, bytes - offset));
                encode(Header{frames, index, count, captured->received_at_us, length}, wire);
                std::copy_n(encoded.data() + offset, length, wire.data() + header_size);
                socket.send(destination, std::span(wire).first(header_size + length));
                ++packets;
            }
        } else
            ++timeouts;
        deadline = std::max(deadline + period, Clock::now());
    }
    const auto stats = capture->statistics();
    std::cout << "Captured: " << frames << " Packets: " << packets
              << " Superseded: " << stats.superseded << " Malformed: " << stats.malformed
              << " Timeouts: " << timeouts << '\n';
    return frames ? 0 : 1;
}
