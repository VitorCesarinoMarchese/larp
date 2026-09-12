#pragma once
#include "protocol/packet.hpp"
#include <algorithm>
#include <array>
#include <memory>
namespace larp {
struct Statistics {
    std::uint64_t completed = 0, dropped = 0, missing = 0, expected = 0, skipped = 0;
    std::uint64_t invalid = 0, duplicates = 0, stale = 0;
};
class Reassembler {
    std::unique_ptr<std::byte[]> buffer_ = std::make_unique<std::byte[]>(max_frame_size);
    std::array<bool, max_packets> seen_{};
    Header current_{};
    std::uint64_t started_ = 0;
    std::uint32_t received_ = 0;
    std::size_t size_ = 0;
    bool have_id_ = false, active_ = false;
    void drop() {
        if (!active_)
            return;
        ++stats.dropped;
        stats.missing += current_.packet_count - received_;
        active_ = false;
    }

  public:
    Statistics stats;
    void expire(std::uint64_t now) {
        if (active_ && now - started_ >= 100000)
            drop();
    }
    void finish() {
        drop();
    }
    std::optional<std::span<const std::byte>> accept(std::span<const std::byte> packet,
                                                     std::uint64_t now) {
        expire(now);
        const auto decoded = decode(packet);
        if (!decoded) {
            ++stats.invalid;
            return {};
        }
        const auto &h = *decoded;
        if (have_id_ &&
            (h.frame_id < current_.frame_id || (h.frame_id == current_.frame_id && !active_))) {
            ++stats.stale;
            return {};
        }
        if (!have_id_ || h.frame_id > current_.frame_id) {
            drop();
            if (have_id_)
                stats.skipped += h.frame_id - current_.frame_id - 1;
            current_ = h;
            have_id_ = true;
            active_ = true;
            started_ = now;
            received_ = 0;
            size_ = 0;
            seen_.fill(false);
            stats.expected += h.packet_count;
        }
        if (h.packet_count != current_.packet_count || h.timestamp_us != current_.timestamp_us) {
            ++stats.invalid;
            return {};
        }
        if (seen_[h.packet_index]) {
            ++stats.duplicates;
            return {};
        }
        seen_[h.packet_index] = true;
        const auto offset = std::size_t(h.packet_index) * payload_capacity;
        std::copy(packet.begin() + header_size, packet.end(), buffer_.get() + offset);
        if (h.packet_index + 1 == h.packet_count)
            size_ = offset + h.payload_size;
        if (++received_ != h.packet_count)
            return {};
        active_ = false;
        ++stats.completed;
        return std::span<const std::byte>(buffer_.get(), size_);
    }
};
} // namespace larp
