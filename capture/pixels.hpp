#pragma once
#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <span>
namespace larp {
struct Dimensions {
    std::uint32_t width, height;
    bool operator==(const Dimensions &) const = default;
};
inline constexpr Dimensions preview_limit{320, 180};
inline constexpr std::size_t preview_capacity = 320 * 180 * 3;
enum class PixelOrder { rgbx, bgrx };
struct PackedPixels {
    std::span<const std::byte> bytes;
    std::uint32_t width, height, stride;
    PixelOrder order;
};
inline Dimensions preview_size(Dimensions source) {
    if (!source.width || !source.height)
        return {0, 0};
    if (source.width <= preview_limit.width && source.height <= preview_limit.height)
        return source;
    if (std::uint64_t(source.width) * preview_limit.height >=
        std::uint64_t(source.height) * preview_limit.width)
        return {preview_limit.width,
                std::max(1U, static_cast<std::uint32_t>(std::uint64_t(source.height) *
                                                        preview_limit.width / source.width))};
    return {std::max(1U, static_cast<std::uint32_t>(std::uint64_t(source.width) *
                                                    preview_limit.height / source.height)),
            preview_limit.height};
}
inline bool copy_rgb(PackedPixels source, std::span<std::byte> target, Dimensions size) {
    if (!source.width || !source.height || !size.width || !size.height ||
        size.width > source.width || size.height > source.height ||
        size.width > preview_limit.width || size.height > preview_limit.height ||
        std::uint64_t(source.width) * 4 > source.stride ||
        std::uint64_t(source.stride) * (source.height - 1) + std::uint64_t(source.width) * 4 >
            source.bytes.size() ||
        std::uint64_t(size.width) * size.height * 3 > target.size())
        return false;
    for (std::uint32_t y = 0; y < size.height; ++y) {
        const auto row = (std::uint64_t(y) * source.height / size.height) * source.stride;
        for (std::uint32_t x = 0; x < size.width; ++x) {
            const auto from = row + (std::uint64_t(x) * source.width / size.width) * 4;
            const auto to = (std::size_t(y) * size.width + x) * 3;
            target[to] = source.bytes[from + (source.order == PixelOrder::bgrx ? 2 : 0)];
            target[to + 1] = source.bytes[from + 1];
            target[to + 2] = source.bytes[from + (source.order == PixelOrder::bgrx ? 0 : 2)];
        }
    }
    return true;
}
} // namespace larp
