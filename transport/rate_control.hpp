#pragma once
#include "common/bitrate.hpp"
#include "protocol/feedback.hpp"
#include <algorithm>
#include <stdexcept>
namespace larp {
class RateControl {
    std::uint64_t session_, sequence_ = 0, last_sequence_ = 0;
    std::array<Probe, 8> probes_{};
    std::optional<Feedback> previous_;
    std::uint64_t next_probe_ = 0, last_feedback_, last_change_, minimum_rtt_ = 0;
    unsigned clean_ = 0;
    std::uint32_t bitrate_;
    void decrease(std::uint64_t now) {
        bitrate_ = std::max(min_bitrate, bitrate_ * 3 / 4);
        last_change_ = now;
        clean_ = 0;
    }

  public:
    std::uint64_t reports = 0, rejected = 0, rtt_us = 0;
    double packet_loss = 0, frame_loss = 0, throughput_mbps = 0;
    RateControl(std::uint32_t bitrate, std::uint64_t session, std::uint64_t now)
        : session_(session), last_feedback_(now), last_change_(now), bitrate_(bitrate) {
        if (!session || bitrate < min_bitrate || bitrate > max_bitrate)
            throw std::invalid_argument("adaptive bitrate must be 128 through 8000 kbps");
    }
    std::uint32_t bitrate() const {
        return bitrate_;
    }
    std::uint64_t feedback_age(std::uint64_t now) const {
        return now - last_feedback_;
    }
    std::optional<Probe> probe(std::uint64_t now) {
        if (now < next_probe_)
            return {};
        next_probe_ = now + 250000;
        Probe result{session_, ++sequence_, now};
        probes_[sequence_ % probes_.size()] = result;
        return result;
    }
    bool accept(const Feedback &report, std::uint64_t now) {
        const auto &p = report.probe;
        if (p.session != session_ || p.sequence <= last_sequence_ ||
            p != probes_[p.sequence % probes_.size()] || now < p.sent_us ||
            now - p.sent_us > 1000000) {
            ++rejected;
            return false;
        }
        if (previous_) {
            const auto before = previous_->counters.values(), after = report.counters.values();
            for (std::size_t i = 0; i < before.size(); ++i)
                if (after[i] < before[i]) {
                    ++rejected;
                    return false;
                }
            if (report.received_us <= previous_->received_us ||
                report.counters.missing - previous_->counters.missing >
                    report.counters.expected - previous_->counters.expected ||
                report.counters.corrupt - previous_->counters.corrupt >
                    report.counters.completed - previous_->counters.completed ||
                report.counters.completed - previous_->counters.completed +
                        report.counters.dropped - previous_->counters.dropped >
                    report.counters.expected - previous_->counters.expected) {
                ++rejected;
                return false;
            }
        }
        rtt_us = now - p.sent_us;
        minimum_rtt_ = reports ? std::min(minimum_rtt_, rtt_us) : rtt_us;
        last_feedback_ = now;
        last_sequence_ = p.sequence;
        ++reports;
        if (previous_) {
            const auto &a = report.counters, &b = previous_->counters;
            const auto expected = a.expected - b.expected, missing = a.missing - b.missing;
            const auto completed = a.completed - b.completed;
            const auto lost = a.dropped - b.dropped + a.skipped - b.skipped;
            packet_loss =
                expected ? 100.0 * static_cast<double>(missing) / static_cast<double>(expected) : 0;
            frame_loss = completed + lost ? 100.0 * static_cast<double>(lost) /
                                                static_cast<double>(completed + lost)
                                          : 0;
            throughput_mbps = static_cast<double>(a.bytes - b.bytes) * 8.0 /
                              static_cast<double>(report.received_us - previous_->received_us);
            const bool congestion = packet_loss >= 2 || frame_loss >= 5 || a.corrupt > b.corrupt ||
                                    rtt_us > minimum_rtt_ + 50000;
            if (congestion) {
                clean_ = 0;
                if (now - last_change_ >= 500000)
                    decrease(now);
            } else if (completed && ++clean_ >= 4 && now - last_change_ >= 1000000) {
                bitrate_ = std::min(max_bitrate, bitrate_ + 64000);
                last_change_ = now;
                clean_ = 0;
            } else if (!completed)
                clean_ = 0;
        }
        previous_ = report;
        return true;
    }
    void tick(std::uint64_t now) {
        if (now - last_feedback_ >= 1000000 && now - last_change_ >= 1000000)
            decrease(now);
    }
};
} // namespace larp
