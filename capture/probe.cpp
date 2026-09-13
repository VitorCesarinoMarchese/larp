#include "capture/capture.hpp"
#include "common/runtime.hpp"
#include <iostream>
#include <vector>
int main(int argc, char **argv) {
    try {
        if (argc != 2) {
            std::cerr << "Usage: larp-capture-probe SECONDS\n";
            return 1;
        }
        const auto seconds = larp::number(argv[1], 1, 3600);
        std::cout << "Select a screen in the desktop portal.\n" << std::flush;
        auto capture = larp::open_screen_capture();
        std::vector<std::byte> pixels(larp::preview_capacity);
        const auto start = larp::now_us();
        std::uint64_t frames = 0;
        while (larp::now_us() - start < std::uint64_t(seconds) * 1000000) {
            if (auto frame = capture->next(pixels, std::chrono::milliseconds(100))) {
                if (!frames)
                    std::cout << "Preview: " << frame->size.width << 'x' << frame->size.height
                              << '\n'
                              << std::flush;
                ++frames;
            }
        }
        const auto stats = capture->statistics();
        std::cout << "Captured: " << frames << " Superseded: " << stats.superseded
                  << " Malformed: " << stats.malformed << '\n';
        if (!frames)
            return 1;
    } catch (const std::exception &error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
