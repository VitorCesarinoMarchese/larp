#pragma once
#include "common/runtime.hpp"
#include "platform/udp.hpp"
#include "protocol/packet.hpp"
#include <algorithm>
#include <array>
#include <stdexcept>
#include <thread>
namespace larp {
template <class Socket>
inline std::uint32_t send_frame(Socket &socket, const Endpoint &destination,
                                std::span<const std::byte> frame, std::uint64_t id,
                                std::uint64_t timestamp,
                                std::chrono::nanoseconds pacing_window = {}) {
    if (frame.empty() || frame.size() > max_frame_size || pacing_window.count() < 0)
        throw std::invalid_argument("invalid transport frame size");
    std::array<std::byte, datagram_size> wire{};
    const auto count =
        static_cast<std::uint32_t>((frame.size() + payload_capacity - 1) / payload_capacity);
    const auto spacing = pacing_window / count;
    auto next = Clock::now();
    for (std::uint32_t index = 0; index < count; ++index) {
        if (index && spacing.count())
            std::this_thread::sleep_until(next);
        const auto offset = std::size_t(index) * payload_capacity;
        const auto length = static_cast<std::uint32_t>(
            std::min<std::size_t>(payload_capacity, frame.size() - offset));
        encode(Header{id, index, count, timestamp, length}, wire);
        std::copy_n(frame.data() + offset, length, wire.data() + header_size);
        socket.send(destination, std::span(wire).first(header_size + length));
        if (spacing.count())
            next = Clock::now() + spacing;
    }
    return count;
}
} // namespace larp
