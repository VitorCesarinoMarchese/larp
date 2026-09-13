#include "capture/linux/context.hpp"
#include <stdexcept>
int main() {
    struct Runtime {
        Runtime() {
            pw_init(nullptr, nullptr);
        }
        ~Runtime() {
            pw_deinit();
        }
    } runtime;
    for (unsigned cycle = 0; cycle < 5; ++cycle) {
        const std::unique_ptr<pw_main_loop, decltype(&pw_main_loop_destroy)> loop(
            pw_main_loop_new(nullptr), pw_main_loop_destroy);
        if (!loop)
            throw std::runtime_error("cannot create PipeWire loop");
        const auto context = larp::open_capture_context(pw_main_loop_get_loop(loop.get()));
        if (!context)
            throw std::runtime_error("cannot create capture context");
    }
}
