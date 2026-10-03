#include "transport/rate_control.hpp"
#include <stdexcept>
#define CHECK(x)                                                                                   \
    do {                                                                                           \
        if (!(x))                                                                                  \
            throw std::runtime_error(#x);                                                          \
    } while (false)
int main() {
    using namespace larp;
    const Probe probe{123, 1, 1000};
    const auto wire = encode_probe(probe);
    CHECK(decode_probe(wire) == probe);
    CHECK(!decode_feedback(wire));
    CHECK(!decode_probe(std::span(wire).first(31)));
    auto bad_probe = wire;
    bad_probe[5] = std::byte{2};
    CHECK(!decode_probe(bad_probe));
    bad_probe = wire;
    bad_probe[8] = std::byte{};
    feedback_detail::put(bad_probe, 8, 0, 8);
    CHECK(!decode_probe(bad_probe));
    Feedback valid{probe, 5000, {10, 1, 2, 1, 1, 0, 8000}};
    auto encoded = encode_feedback(valid);
    auto parsed = decode_feedback(encoded);
    CHECK(parsed && parsed->probe == probe && parsed->counters.values() == valid.counters.values());
    CHECK(!decode_feedback(std::span(encoded).first(95)));
    feedback_detail::put(encoded, 48, 11, 8);
    CHECK(!decode_feedback(encoded));
    encoded = encode_feedback(valid);
    feedback_detail::put(encoded, 88, UINT64_MAX, 8);
    CHECK(!decode_feedback(encoded));
    encoded = encode_feedback(valid);
    feedback_detail::put(encoded, 80, 3, 8);
    CHECK(!decode_feedback(encoded));

    RateControl control(2000000, 123, 0);
    FeedbackCounters counters{};
    std::uint64_t clock = 0;
    auto reply = [&](bool loss) {
        clock += 250000;
        auto request = control.probe(clock);
        CHECK(request);
        CHECK(!control.probe(clock + 1));
        counters.expected += 100;
        counters.completed += loss ? 9 : 10;
        counters.missing += loss ? 10 : 0;
        counters.dropped += loss ? 1 : 0;
        counters.bytes += 100000;
        Feedback report{*request, clock + 10000000, counters};
        CHECK(control.accept(report, clock + 10000));
        CHECK(!control.accept(report, clock + 10001));
        return report;
    };
    reply(false);
    CHECK(control.bitrate() == 2000000);
    auto report = reply(true);
    CHECK(control.bitrate() == 1500000 && control.packet_loss == 10 && control.frame_loss == 10);
    CHECK(control.rtt_us == 10000);
    for (unsigned i = 0; i < 4; ++i)
        reply(false);
    CHECK(control.bitrate() == 1564000);
    auto request = control.probe(clock + 250000);
    CHECK(request);
    report.probe = *request;
    report.probe.session = 321;
    CHECK(!control.accept(report, clock + 260000));
    report.probe = *request;
    report.received_us = clock + 10250000;
    report.counters = {}; // Counter reset from a restarted receiver is not valid feedback.
    CHECK(!control.accept(report, clock + 260000));
    report.counters = counters;
    ++report.counters.completed;
    CHECK(!control.accept(report, clock + 260000));
    report.counters = counters;
    report.probe.sent_us += 1;
    CHECK(!control.accept(report, clock + 260000));
    report.probe = *request;
    CHECK(!control.accept(report, clock + 1500001));
    control.tick(clock + 1100000);
    CHECK(control.bitrate() == 1173000);
    control.tick(clock + 1100001);
    CHECK(control.bitrate() == 1173000);
    for (unsigned i = 2; i < 30; ++i)
        control.tick(clock + i * 1000000ULL);
    CHECK(control.bitrate() == min_bitrate);

    RateControl maximum(max_bitrate, 44, 0);
    for (std::uint64_t i = 1; i < 12; ++i) {
        auto p = maximum.probe(i * 250000);
        CHECK(p);
        CHECK(
            maximum.accept({*p, i * 250000, {i * 10, 0, i, 0, 0, 0, i * 1000}}, i * 250000 + 1000));
    }
    CHECK(maximum.bitrate() == max_bitrate);
    auto p = maximum.probe(3000000);
    CHECK(p);
    CHECK(maximum.accept({*p, 3000000, {120, 0, 12, 0, 0, 0, 12000}}, 3100000));
    CHECK(maximum.bitrate() < max_bitrate); // RTT growth alone can signal congestion.
}
