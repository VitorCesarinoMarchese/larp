#pragma once
#include "capture/pixels.hpp"
#include <chrono>
#include <memory>
#include <optional>
namespace larp {
struct CaptureInfo {
    Dimensions size;
    std::uint64_t received_at_us;
};
struct CaptureStats {
    std::uint64_t received = 0, superseded = 0, malformed = 0;
};
class ScreenCapture {
  public:
    virtual ~ScreenCapture() = default;
    virtual std::optional<CaptureInfo> next(std::span<std::byte> rgb,
                                            std::chrono::milliseconds timeout) = 0;
    virtual CaptureStats statistics() const = 0;
};
std::unique_ptr<ScreenCapture> open_screen_capture();
} // namespace larp
