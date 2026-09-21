#include "render/window.hpp"
#include <SDL3/SDL.h>
#include <algorithm>
#include <cstring>
#include <stdexcept>
#include <string>
namespace larp {
namespace {
void check(bool success) {
    if (!success)
        throw std::runtime_error(std::string("SDL: ") + SDL_GetError());
}
struct Video {
    Video() {
        if (!SDL_GetHint(SDL_HINT_VIDEO_DRIVER) && !SDL_getenv("SDL_VIDEODRIVER"))
            check(SDL_SetHint(SDL_HINT_VIDEO_DRIVER, "x11"));
        check(SDL_SetHintWithPriority(SDL_HINT_FRAMEBUFFER_ACCELERATION, "0", SDL_HINT_OVERRIDE));
        check(SDL_InitSubSystem(SDL_INIT_VIDEO));
    }
    ~Video() {
        SDL_QuitSubSystem(SDL_INIT_VIDEO);
    }
};
struct WindowDelete {
    void operator()(SDL_Window *window) const {
        SDL_DestroyWindow(window);
    }
};
struct SurfaceDelete {
    void operator()(SDL_Surface *surface) const {
        SDL_DestroySurface(surface);
    }
};
} // namespace
struct VideoWindow::Impl {
    Video video;
    std::unique_ptr<SDL_Window, WindowDelete> window;
    std::unique_ptr<SDL_Surface, SurfaceDelete> frame;
    bool open = true;

    Impl()
        : window(SDL_CreateWindow("L.A.R.P. live preview", 960, 540,
                                  SDL_WINDOW_RESIZABLE | SDL_WINDOW_HIGH_PIXEL_DENSITY)) {
        check(window != nullptr);
        redraw();
    }

    void redraw() {
        auto *surface = SDL_GetWindowSurface(window.get());
        check(surface != nullptr);
        check(SDL_FillSurfaceRect(surface, nullptr, SDL_MapSurfaceRGB(surface, 0, 0, 0)));
        if (frame) {
            const auto scale =
                std::min(double(surface->w) / frame->w, double(surface->h) / frame->h);
            const int width = std::max(1, static_cast<int>(frame->w * scale));
            const int height = std::max(1, static_cast<int>(frame->h * scale));
            const SDL_Rect destination{(surface->w - width) / 2, (surface->h - height) / 2, width,
                                       height};
            check(SDL_BlitSurfaceScaled(frame.get(), nullptr, surface, &destination,
                                        SDL_SCALEMODE_NEAREST));
        }
        check(SDL_UpdateWindowSurface(window.get()));
    }
};
VideoWindow::VideoWindow() : impl_(std::make_unique<Impl>()) {}
VideoWindow::~VideoWindow() = default;
bool VideoWindow::poll() {
    SDL_Event event;
    bool redraw = false;
    while (SDL_PollEvent(&event)) {
        if (event.type == SDL_EVENT_QUIT ||
            (event.type == SDL_EVENT_WINDOW_CLOSE_REQUESTED &&
             event.window.windowID == SDL_GetWindowID(impl_->window.get())) ||
            (event.type == SDL_EVENT_KEY_DOWN && event.key.key == SDLK_ESCAPE &&
             event.key.windowID == SDL_GetWindowID(impl_->window.get())))
            impl_->open = false;
        if (event.type == SDL_EVENT_WINDOW_EXPOSED ||
            event.type == SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED)
            redraw = true;
    }
    if (redraw && impl_->open)
        impl_->redraw();
    return impl_->open;
}
void VideoWindow::present(RawFrameView frame) {
    if (!frame.width || !frame.height || frame.width > 4096 || frame.height > 2160 ||
        frame.pixels.size() != std::uint64_t(frame.width) * frame.height * 3)
        throw std::invalid_argument("invalid RGB frame for display");
    const int width = static_cast<int>(frame.width), height = static_cast<int>(frame.height);
    if (!impl_->frame || impl_->frame->w != width || impl_->frame->h != height) {
        impl_->frame.reset(SDL_CreateSurface(width, height, SDL_PIXELFORMAT_RGB24));
        check(impl_->frame != nullptr);
    }
    auto *surface = impl_->frame.get();
    for (int row = 0; row < height; ++row)
        std::memcpy(static_cast<std::byte *>(surface->pixels) +
                        std::size_t(row) * static_cast<std::size_t>(surface->pitch),
                    frame.pixels.data() + std::size_t(row) * frame.width * 3,
                    std::size_t(frame.width) * 3);
    impl_->redraw();
}
} // namespace larp
