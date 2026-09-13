#pragma once
#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
namespace larp {
inline constexpr std::size_t raw_header_size = 20;
struct RawFrameView {
    std::uint32_t width, height;
    std::span<const std::byte> pixels;
};
namespace raw_detail {
inline constexpr auto crc_table = [] {
    std::array<std::uint32_t, 256> table{};
    for (std::uint32_t i = 0; i < 256; ++i) {
        auto value = i;
        for (unsigned bit = 0; bit < 8; ++bit)
            value = (value >> 1) ^ ((value & 1) ? 0xedb88320U : 0U);
        table[i] = value;
    }
    return table;
}();
inline std::uint32_t checksum(std::span<const std::byte> data) {
    std::uint32_t crc = 0xffffffffU;
    for (std::size_t i = 0; i < data.size(); ++i) {
        if (i >= 16 && i < raw_header_size)
            continue;
        crc = crc_table[(crc ^ std::to_integer<unsigned>(data[i])) & 255] ^ (crc >> 8);
    }
    return crc ^ 0xffffffffU;
}
inline bool layout(std::size_t bytes, std::uint32_t width, std::uint32_t height) {
    return width && height && width <= 4096 && height <= 2160 &&
           bytes == raw_header_size + std::uint64_t(width) * height * 3;
}
inline void put(std::span<std::byte> data, std::size_t offset, std::uint32_t value) {
    for (unsigned i = 0; i < 4; ++i)
        data[offset + i] = std::byte((value >> ((3 - i) * 8)) & 255);
}
inline std::uint32_t get(std::span<const std::byte> data, std::size_t offset) {
    std::uint32_t value = 0;
    for (unsigned i = 0; i < 4; ++i)
        value = (value << 8) | std::to_integer<unsigned>(data[offset + i]);
    return value;
}
} // namespace raw_detail
inline bool write_raw_header(std::span<std::byte> frame, std::uint32_t width,
                             std::uint32_t height) {
    if (!raw_detail::layout(frame.size(), width, height))
        return false;
    raw_detail::put(frame, 0, 0x4c524742);
    raw_detail::put(frame, 4, 0x00010001);
    raw_detail::put(frame, 8, width);
    raw_detail::put(frame, 12, height);
    raw_detail::put(frame, 16, raw_detail::checksum(frame));
    return true;
}
inline std::optional<RawFrameView> read_raw_frame(std::span<const std::byte> frame) {
    if (frame.size() < raw_header_size || raw_detail::get(frame, 0) != 0x4c524742 ||
        raw_detail::get(frame, 4) != 0x00010001)
        return {};
    const auto width = raw_detail::get(frame, 8), height = raw_detail::get(frame, 12);
    if (!raw_detail::layout(frame.size(), width, height) ||
        raw_detail::checksum(frame) != raw_detail::get(frame, 16))
        return {};
    return RawFrameView{width, height, frame.subspan(raw_header_size)};
}
} // namespace larp
