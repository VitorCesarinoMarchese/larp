#pragma once
#include <charconv>
#include <chrono>
#include <cstdint>
#include <stdexcept>
#include <string_view>
namespace larp {
using Clock = std::chrono::steady_clock;
inline std::uint64_t now_us() {
    return static_cast<std::uint64_t>(
        std::chrono::duration_cast<std::chrono::microseconds>(Clock::now().time_since_epoch())
            .count());
}
inline std::uint32_t number(std::string_view text, std::uint32_t minimum, std::uint32_t maximum) {
    std::uint32_t value = 0;
    const auto [end, error] = std::from_chars(text.data(), text.data() + text.size(), value);
    if (error != std::errc{} || end != text.data() + text.size() || value < minimum ||
        value > maximum)
        throw std::invalid_argument("numeric argument out of range");
    return value;
}
} // namespace larp
