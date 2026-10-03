#pragma once
#include "platform/udp.hpp"
#include "protocol/packet.hpp"
#include <algorithm>
#include <array>
#include <stdexcept>
namespace larp {
inline std::uint32_t send_frame(UdpSocket &socket, const Endpoint &destination,
                                std::span<const std::byte> frame, std::uint64_t id,
                                std::uint64_t timestamp) {
    if (frame.empty() || frame.size() > max_frame_size)
        throw std::invalid_argument("invalid transport frame size");
    std::array<std::byte, datagram_size> wire{};
    const auto count =
        static_cast<std::uint32_t>((frame.size() + payload_capacity - 1) / payload_capacity);
    for (std::uint32_t index = 0; index < count; ++index) {
        const auto offset = std::size_t(index) * payload_capacity;
        const auto length = static_cast<std::uint32_t>(
            std::min<std::size_t>(payload_capacity, frame.size() - offset));
        encode(Header{id, index, count, timestamp, length}, wire);
        std::copy_n(frame.data() + offset, length, wire.data() + header_size);
        socket.send(destination, std::span(wire).first(header_size + length));
    }
    return count;
}
} // namespace larp
