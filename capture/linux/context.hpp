#pragma once
#include <memory>
#include <pipewire/pipewire.h>
namespace larp {
struct ContextDelete {
    void operator()(pw_context *context) const {
        pw_context_destroy(context);
    }
};
inline std::unique_ptr<pw_context, ContextDelete> open_capture_context(pw_loop *loop) {
    return std::unique_ptr<pw_context, ContextDelete>(
        pw_context_new(loop, pw_properties_new("module.rt", "false", nullptr), 0));
}
} // namespace larp
