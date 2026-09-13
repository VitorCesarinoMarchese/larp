#include "capture/capture.hpp"
#include "common/runtime.hpp"
#include <fstream>
#include <iostream>
#include <vector>
int main(int argc, char **argv) {
    try {
        if (argc != 2 && argc != 3) {
            std::cerr << "Usage: larp-capture-probe SECONDS [OUTPUT.ppm]\n";
            return 1;
        }
        const auto seconds = larp::number(argv[1], 1, 3600);
        std::cout << "Select a screen in the desktop portal.\n" << std::flush;
        auto capture = larp::open_screen_capture();
        std::vector<std::byte> pixels(larp::preview_capacity);
        const auto start = larp::now_us();
        std::uint64_t frames = 0, changes = 0, previous = 0;
        while (larp::now_us() - start < std::uint64_t(seconds) * 1000000) {
            if (auto frame = capture->next(pixels, std::chrono::milliseconds(100))) {
                if (!frames)
                    std::cout << "Preview: " << frame->size.width << 'x' << frame->size.height
                              << '\n'
                              << std::flush;
                const auto size = std::size_t(frame->size.width) * frame->size.height * 3;
                std::uint64_t fingerprint = 14695981039346656037ULL;
                for (const auto byte : std::span(pixels).first(size))
                    fingerprint =
                        (fingerprint ^ std::to_integer<unsigned>(byte)) * 1099511628211ULL;
                if (frames && previous != fingerprint)
                    ++changes;
                previous = fingerprint;
                if (!frames && argc == 3) {
                    std::ofstream image(argv[2], std::ios::binary | std::ios::trunc);
                    image << "P6\n" << frame->size.width << ' ' << frame->size.height << "\n255\n";
                    image.write(reinterpret_cast<const char *>(pixels.data()),
                                static_cast<std::streamsize>(size));
                    image.close();
                    if (!image)
                        throw std::runtime_error("cannot write capture snapshot");
                }
                ++frames;
            }
        }
        const auto stats = capture->statistics();
        std::cout << "Captured: " << frames << " Superseded: " << stats.superseded
                  << " Malformed: " << stats.malformed << " Changed: " << changes << '\n';
        if (!frames)
            return 1;
    } catch (const std::exception &error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
