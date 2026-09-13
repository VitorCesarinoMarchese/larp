#include "common/runtime.hpp"
#include "media/raw_frame.hpp"
#include "platform/udp.hpp"
#include "transport/reassembly.hpp"
#include <array>
#include <fstream>
#include <iomanip>
#include <iostream>
int main(int argc, char **argv) {
    try {
        const bool raw_mode = argc > 1 && std::string_view(argv[1]) == "--raw";
        if ((!raw_mode && argc != 4 && argc != 5) || (raw_mode && argc != 6 && argc != 7)) {
            std::cerr << "Usage: larp-client BIND_IPv4 PORT SECONDS [FRAME_TIMEOUT_MS]\n"
                         "       larp-client --raw BIND_IPv4 PORT SECONDS OUTPUT.ppm "
                         "[FRAME_TIMEOUT_MS]\n";
            return 1;
        }
        using namespace larp;
        const int offset = raw_mode ? 1 : 0;
        const auto local = Endpoint::parse(
            argv[1 + offset], static_cast<std::uint16_t>(number(argv[2 + offset], 0, 65535)));
        const auto seconds = number(argv[3 + offset], 1, 86400);
        const auto timeout_ms =
            argc == (raw_mode ? 7 : 5) ? number(argv[raw_mode ? 6 : 4], 1, 1000) : 100;
        UdpSocket socket(local);
        Reassembler reassembly{std::chrono::milliseconds(timeout_ms)};
        std::optional<Endpoint> peer;
        std::array<std::byte, datagram_size> wire{};
        std::uint64_t packets = 0, bytes = 0, valid = 0, corrupt = 0, foreign = 0;
        std::uint64_t previous_valid = 0, previous_bytes = 0;
        const auto start = now_us();
        auto report_time = start;
        auto report = [&](std::uint64_t now) {
            const double elapsed = static_cast<double>(now - report_time) / 1000000.0;
            const auto &s = reassembly.stats;
            std::cout << std::fixed << std::setprecision(2)
                      << "FPS: " << static_cast<double>(valid - previous_valid) / elapsed
                      << " Packets: " << packets;
            auto percentage = [](const char *label, std::optional<double> value) {
                std::cout << label;
                if (value)
                    std::cout << *value << '%';
                else
                    std::cout << "N/A";
            };
            percentage(" Observed packet loss: ", s.packet_loss_percent());
            percentage(" Frame loss: ", s.frame_loss_percent());
            std::cout << " Dropped: " << s.dropped << " frames Missing: " << s.missing
                      << " Expired: " << s.expired << " Superseded: " << s.superseded
                      << " Shutdown: " << s.shutdown << " Skipped: " << s.skipped
                      << " RTT: N/A Throughput: "
                      << static_cast<double>(bytes - previous_bytes) * 8.0 / elapsed / 1000000.0
                      << " Mbps Validated: " << valid << " Corrupt: " << corrupt
                      << " Invalid: " << s.invalid << " Duplicates: " << s.duplicates
                      << " Stale: " << s.stale << " Foreign: " << foreign << " \n"
                      << std::flush;
            report_time = now;
            previous_valid = valid;
            previous_bytes = bytes;
        };
        std::cout << "Listening: " << socket.local_endpoint().port << std::endl;
        while (now_us() - start < std::uint64_t(seconds) * 1000000) {
            const auto received = socket.receive(wire, static_cast<int>(std::min(timeout_ms, 10U)));
            const auto now = now_us();
            reassembly.expire(now);
            if (received.status != ReceiveStatus::timeout) {
                if (peer && received.peer != *peer)
                    ++foreign;
                else {
                    ++packets;
                    bytes += received.size;
                    if (received.status == ReceiveStatus::truncated)
                        ++reassembly.stats.invalid;
                    else {
                        const auto packet = std::span(wire).first(received.size);
                        const auto header = decode(packet);
                        if (header && !peer)
                            peer = received.peer;
                        const auto frame = reassembly.accept(packet, now);
                        if (frame) {
                            bool correct = true;
                            if (raw_mode) {
                                const auto raw = read_raw_frame(*frame);
                                correct = raw.has_value();
                                if (raw && valid == 0) {
                                    std::ofstream image(argv[5],
                                                        std::ios::binary | std::ios::trunc);
                                    image << "P6\n"
                                          << raw->width << ' ' << raw->height << "\n255\n";
                                    image.write(reinterpret_cast<const char *>(raw->pixels.data()),
                                                static_cast<std::streamsize>(raw->pixels.size()));
                                    image.close();
                                    if (!image)
                                        throw std::runtime_error("cannot write capture snapshot");
                                    std::cout << "Snapshot: " << raw->width << 'x' << raw->height
                                              << '\n';
                                }
                            } else {
                                for (std::size_t i = 0; i < frame->size(); ++i)
                                    if ((*frame)[i] != synthetic_byte(header->frame_id, i)) {
                                        correct = false;
                                        break;
                                    }
                            }
                            if (correct)
                                ++valid;
                            else
                                ++corrupt;
                        }
                    }
                }
            }
            if (now - report_time >= 1000000)
                report(now);
        }
        reassembly.finish();
        report(now_us());
    } catch (const std::exception &error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
