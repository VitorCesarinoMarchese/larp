#pragma once
#include "protocol/packet.hpp"
#include <algorithm>
#include <array>
#include <chrono>
#include <memory>
#include <stdexcept>
namespace larp {
struct Statistics {
    std::uint64_t completed = 0, dropped = 0, missing = 0, expected = 0, skipped = 0;
    std::uint64_t invalid = 0, duplicates = 0, stale = 0;
    std::uint64_t expired = 0, superseded = 0, shutdown = 0;
    std::optional<double> packet_loss_percent() const {
        if (!expected)
            return {};
        return 100.0 * static_cast<double>(missing) / static_cast<double>(expected);
    }
    std::optional<double> frame_loss_percent() const {
        const double lost = static_cast<double>(dropped) + static_cast<double>(skipped);
        const double finalized = static_cast<double>(completed) + lost;
        if (finalized == 0)
            return {};
        return 100.0 * lost / finalized;
    }
};
class Reassembler {
    std::unique_ptr<std::byte[]> buffer_;
    std::uint64_t timeout_us_;
    std::array<bool, max_packets> seen_{};
    Header current_{};
    std::uint64_t started_ = 0;
    std::uint32_t received_ = 0;
    std::size_t size_ = 0;
    bool have_id_ = false, active_ = false;
    enum class DropReason { expired, superseded, shutdown };
    void drop(DropReason reason) {
        if (!active_)
            return;
        switch (reason) {
        case DropReason::expired:
            ++stats.expired;
            break;
        case DropReason::superseded:
            ++stats.superseded;
            break;
        case DropReason::shutdown:
            ++stats.shutdown;
            break;
        }
        ++stats.dropped;
        stats.expected += current_.packet_count;
        stats.missing += current_.packet_count - received_;
        active_ = false;
    }

  public:
    Statistics stats;
    explicit Reassembler(std::chrono::microseconds timeout = std::chrono::milliseconds(100)) {
        if (timeout.count() < 1 || timeout > std::chrono::seconds(1))
            throw std::invalid_argument("frame timeout must be 1 through 1000000 microseconds");
        timeout_us_ = static_cast<std::uint64_t>(timeout.count());
        buffer_ = std::make_unique<std::byte[]>(max_frame_size);
    }
    void expire(std::uint64_t now) {
        if (active_ && now - started_ >= timeout_us_)
            drop(DropReason::expired);
    }
    void finish() {
        drop(DropReason::shutdown);
    }
    void reset_session() {
        drop(DropReason::superseded);
        have_id_ = false;
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
            drop(DropReason::superseded);
            if (have_id_)
                stats.skipped += h.frame_id - current_.frame_id - 1;
            current_ = h;
            have_id_ = true;
            active_ = true;
            started_ = now;
            received_ = 0;
            size_ = 0;
            seen_.fill(false);
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
        stats.expected += current_.packet_count;
        return std::span<const std::byte>(buffer_.get(), size_);
    }
};
} // namespace larp
