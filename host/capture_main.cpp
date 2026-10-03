#include "host/capture_main.hpp"
#include "capture/capture.hpp"
#include "codec/h264.hpp"
#include "codec/options.hpp"
#include "common/runtime.hpp"
#include "media/raw_frame.hpp"
#include "platform/udp.hpp"
#include "protocol/packet.hpp"
#include "transport/send_frame.hpp"
#include <array>
#include <iostream>
#include <thread>
#include <vector>
int capture_main(int argc, char **argv) {
    using namespace larp;
    const bool h264 = argc > 1 && std::string_view(argv[1]) == "--h264";
    const auto backend = h264 ? take_codec_option(argc, argv) : CodecBackend::software;
    if (argc != 6) {
        std::cerr << "Usage: larp-host --capture|--h264 IPv4 PORT SECONDS FPS [--codec "
                     "software|nvidia]\n";
        return 1;
    }
    const auto destination =
        Endpoint::parse(argv[2], static_cast<std::uint16_t>(number(argv[3], 1, 65535)));
    const auto seconds = number(argv[4], 1, 3600), fps = number(argv[5], 1, 30);
    UdpSocket socket(Endpoint::parse("0.0.0.0", 0));
    std::vector<std::byte> frame(raw_header_size + preview_capacity);
    std::vector<std::byte> compressed(h264 ? h264_capacity : 0);
    std::unique_ptr<H264Encoder> encoder;
    Dimensions encoder_size{};
    std::uint64_t encode_us = 0, encoded_bytes = 0;
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
            std::span<const std::byte> encoded;
            if (h264) {
                if (!encoder || encoder_size != captured->size) {
                    encoder = std::make_unique<H264Encoder>(captured->size.width,
                                                            captured->size.height, fps, backend);
                    encoder_size = captured->size;
                    std::cout << "Encoder: " << encoder->name() << '\n' << std::flush;
                }
                const auto before = now_us();
                const auto size = encoder->encode(
                    std::span(frame).subspan(raw_header_size, bytes - raw_header_size), compressed);
                encode_us += now_us() - before;
                encoded = std::span(compressed).first(size);
            } else {
                if (!write_raw_header(std::span(frame).first(bytes), captured->size.width,
                                      captured->size.height))
                    throw std::runtime_error("capture produced an invalid preview layout");
                encoded = std::span(frame).first(bytes);
            }
            if (!frames)
                std::cout << "Preview: " << captured->size.width << 'x' << captured->size.height
                          << '\n'
                          << std::flush;
            ++frames;
            packets += send_frame(socket, destination, encoded, frames, captured->received_at_us);
            encoded_bytes += encoded.size();
        } else
            ++timeouts;
        deadline = std::max(deadline + period, Clock::now());
    }
    const auto stats = capture->statistics();
    std::cout << "Captured: " << frames << " Packets: " << packets
              << " Superseded: " << stats.superseded << " Malformed: " << stats.malformed
              << " Timeouts: " << timeouts << " Bytes: " << encoded_bytes
              << " Encode mean us: " << (frames ? encode_us / frames : 0) << '\n';
    return frames ? 0 : 1;
}
