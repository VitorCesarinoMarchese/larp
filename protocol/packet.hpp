#pragma once
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
namespace larp {
inline constexpr std::size_t header_size = 34, datagram_size = 1200;
inline constexpr std::uint32_t payload_capacity = datagram_size - header_size;
inline constexpr std::uint32_t max_frame_size = 4 * 1024 * 1024;
inline constexpr std::uint32_t max_packets =
    (max_frame_size + payload_capacity - 1) / payload_capacity;
struct Header {
    std::uint64_t frame_id;
    std::uint32_t packet_index, packet_count;
    std::uint64_t timestamp_us;
    std::uint32_t payload_size;
};
inline void encode(const Header &h, std::span<std::byte, datagram_size> out) {
    std::size_t pos = 0;
    auto put = [&](std::uint64_t value, unsigned width) {
        for (unsigned i = width; i > 0; --i)
            out[pos++] = std::byte((value >> ((i - 1) * 8)) & 255);
    };
    put(0x4c415250, 4);
    put(1, 2);
    put(h.frame_id, 8);
    put(h.packet_index, 4);
    put(h.packet_count, 4);
    put(h.timestamp_us, 8);
    put(h.payload_size, 4);
}
inline std::optional<Header> decode(std::span<const std::byte> in) {
    if (in.size() < header_size || in.size() > datagram_size)
        return {};
    std::size_t pos = 0;
    auto get = [&](unsigned width) {
        std::uint64_t value = 0;
        for (unsigned i = 0; i < width; ++i)
            value = (value << 8) | std::to_integer<unsigned>(in[pos++]);
        return value;
    };
    if (get(4) != 0x4c415250 || get(2) != 1)
        return {};
    Header h{};
    h.frame_id = get(8);
    h.packet_index = static_cast<std::uint32_t>(get(4));
    h.packet_count = static_cast<std::uint32_t>(get(4));
    h.timestamp_us = get(8);
    h.payload_size = static_cast<std::uint32_t>(get(4));
    if (!h.packet_count || h.packet_count > max_packets || h.packet_index >= h.packet_count ||
        !h.payload_size || h.payload_size > payload_capacity ||
        in.size() != header_size + h.payload_size ||
        (h.packet_index + 1 < h.packet_count && h.payload_size != payload_capacity) ||
        std::uint64_t(h.packet_index) * payload_capacity + h.payload_size > max_frame_size)
        return {};
    return h;
}
inline std::byte synthetic_byte(std::uint64_t frame, std::size_t offset) {
    return std::byte((frame + offset) & 255);
}
} // namespace larp
