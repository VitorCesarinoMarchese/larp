#include "render/window.hpp"
#include <SDL3/SDL.h>
#include <algorithm>
#include <array>
#include <iostream>
#include <stdexcept>
#include <vector>

namespace {
void require(bool condition, const char *message) {
    if (!condition)
        throw std::runtime_error(message);
}
SDL_Window *window() {
    int count = 0;
    auto **windows = SDL_GetWindows(&count);
    auto *result = count == 1 ? windows[0] : nullptr;
    SDL_free(windows);
    require(result != nullptr, "expected exactly one preview window");
    return result;
}
void pixel(SDL_Window *window, int x, int y, std::array<Uint8, 3> expected) {
    auto *surface = SDL_GetWindowSurface(window);
    require(surface != nullptr, "window has no surface");
    Uint8 red = 0, green = 0, blue = 0, alpha = 0;
    require(SDL_ReadSurfacePixel(surface, x, y, &red, &green, &blue, &alpha),
            "cannot read rendered pixel");
    require(std::array{red, green, blue} == expected, "rendered pixel differs");
}
void event(SDL_Window *window, Uint32 type) {
    SDL_Event event{};
    event.type = type;
    event.window.windowID = SDL_GetWindowID(window);
    require(SDL_PushEvent(&event), "cannot push window event");
}
} // namespace

int main() {
    try {
        require(SDL_SetHintWithPriority(SDL_HINT_VIDEO_DRIVER, "dummy", SDL_HINT_OVERRIDE),
                "cannot select dummy video");
        {
            larp::VideoWindow preview;
            auto *native = window();
            require(SDL_SetWindowSize(native, 12, 12), "cannot resize window");
            require(preview.poll(), "window closed before first frame");
            // Nine-byte source rows exercise padding in SDL's RGB24 surface.
            std::vector<std::byte> pixels;
            for (auto channel :
                 {255, 0, 0, 0, 255, 0, 0, 0, 255, 255, 255, 0, 0, 255, 255, 255, 0, 255})
                pixels.push_back(std::byte(channel));
            preview.present({3, 2, pixels});
            pixel(native, 2, 3, {255, 0, 0});
            pixel(native, 6, 3, {0, 255, 0});
            pixel(native, 10, 3, {0, 0, 255});
            pixel(native, 2, 7, {255, 255, 0});
            pixel(native, 6, 7, {0, 255, 255});
            pixel(native, 10, 7, {255, 0, 255});
            pixel(native, 6, 0, {0, 0, 0});
            pixel(native, 6, 11, {0, 0, 0});

            std::fill(pixels.begin(), pixels.end(), std::byte{0});
            auto *surface = SDL_GetWindowSurface(native);
            require(SDL_FillSurfaceRect(surface, nullptr, SDL_MapSurfaceRGB(surface, 17, 18, 19)),
                    "cannot erase displayed surface");
            event(native, SDL_EVENT_WINDOW_EXPOSED);
            require(preview.poll(), "expose closed window");
            pixel(native, 2, 3, {255, 0, 0});
            pixel(native, 10, 7, {255, 0, 255});

            require(SDL_SetWindowSize(native, 20, 10), "cannot resize window");
            event(native, SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED);
            require(preview.poll(), "resize closed window");
            pixel(native, 0, 5, {0, 0, 0});
            pixel(native, 19, 5, {0, 0, 0});
            pixel(native, 3, 2, {255, 0, 0});
            pixel(native, 14, 7, {255, 0, 255});

            const std::array replacement{std::byte{20}, std::byte{80}, std::byte{190}};
            preview.present({1, 1, replacement});
            pixel(native, 10, 5, {20, 80, 190});
            pixel(native, 1, 5, {0, 0, 0});
            for (const auto invalid :
                 {larp::RawFrameView{0, 1, replacement}, larp::RawFrameView{1, 0, replacement},
                  larp::RawFrameView{4097, 1, replacement},
                  larp::RawFrameView{1, 2161, replacement},
                  larp::RawFrameView{2, 1, replacement}}) {
                bool rejected = false;
                try {
                    preview.present(invalid);
                } catch (const std::invalid_argument &) {
                    rejected = true;
                }
                require(rejected, "invalid RGB frame accepted");
            }
            pixel(native, 10, 5, {20, 80, 190});
            event(native, SDL_EVENT_WINDOW_CLOSE_REQUESTED);
            require(!preview.poll() && !preview.poll(), "close must stay closed");
        }
        for (int iteration = 0; iteration < 8; ++iteration) {
            larp::VideoWindow preview;
            auto *native = window();
            const std::array pixels{std::byte{41}, std::byte{102}, std::byte{203}};
            preview.present({1, 1, pixels});
            pixel(native, 480, 270, {41, 102, 203});
            require(preview.poll(), "new lifecycle starts closed");
            SDL_Event escape{};
            escape.type = SDL_EVENT_KEY_DOWN;
            escape.key.windowID = SDL_GetWindowID(native);
            escape.key.key = SDLK_ESCAPE;
            require(SDL_PushEvent(&escape), "cannot push Escape");
            require(!preview.poll() && !preview.poll(), "Escape must stay closed");
        }
        std::cout << "render pixels, ownership, resize, validation and lifecycle passed\n";
    } catch (const std::exception &error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
