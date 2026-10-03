#pragma once
#include "common/runtime.hpp"
#include "transport/session.hpp"
#include "transport/rate_control.hpp"
#include <iomanip>
#include <iostream>
#include <random>
#include <thread>
namespace larp {
class AdaptiveSender {
    SessionSocket &socket_;
    Endpoint destination_;
    std::optional<RateControl> control_;
    std::uint64_t generation_;
    void synchronize() {
        if (socket_.generation() != generation_) {
            generation_ = socket_.generation();
            if (control_)
                control_.emplace(control_->bitrate(), session(), now_us());
        }
    }
    static std::uint64_t session() {
        std::random_device random;
        return ((std::uint64_t(random()) << 32) | random()) | 1;
    }
    void report() const {
        const auto &c = *control_;
        std::cout << std::fixed << std::setprecision(2) << "Feedback: " << c.reports
                  << " RTT us: " << c.rtt_us << " Packet loss: " << c.packet_loss
                  << "% Frame loss: " << c.frame_loss << "% Receive Mbps: " << c.throughput_mbps
                  << " Target kbps: " << c.bitrate() / 1000
                  << " Feedback age ms: " << c.feedback_age(now_us()) / 1000
                  << " Rejected feedback: " << c.rejected << '\n'
                  << std::flush;
    }

  public:
    AdaptiveSender(SessionSocket &socket, Endpoint destination, std::uint32_t bitrate)
        : socket_(socket), destination_(destination), generation_(socket.generation()) {
        if (bitrate)
            control_.emplace(bitrate, session(), now_us());
    }
    std::uint32_t bitrate() const {
        return control_ ? control_->bitrate() : 0;
    }
    void wait_until(Clock::time_point deadline) {
        if (!control_ && !socket_.secured()) {
            std::this_thread::sleep_until(deadline);
            return;
        }
        do {
            socket_.service();
            synchronize();
            if (control_) {
                auto now = now_us();
                const auto before = control_->bitrate();
                control_->tick(now);
                if (before != control_->bitrate())
                    report();
                if (auto probe = control_->probe(now))
                    socket_.send(destination_, encode_probe(*probe));
                // A flooded control socket cannot indefinitely delay the next video frame.
                for (unsigned i = 0; i < 32; ++i) {
                    std::array<std::byte, feedback_size> wire{};
                    const auto received = socket_.receive(wire, 0);
                    synchronize();
                    if (received.status == ReceiveStatus::timeout)
                        break;
                    if (received.status != ReceiveStatus::packet || received.peer != destination_) {
                        ++control_->rejected;
                        continue;
                    }
                    const auto feedback = decode_feedback(std::span(wire).first(received.size));
                    if (!feedback) {
                        ++control_->rejected;
                        continue;
                    }
                    if (control_->accept(*feedback, now_us()))
                        report();
                }
            }
            if (Clock::now() >= deadline)
                break;
            std::this_thread::sleep_until(
                std::min(deadline, Clock::now() + std::chrono::milliseconds(5)));
        } while (true);
    }
};
} // namespace larp
