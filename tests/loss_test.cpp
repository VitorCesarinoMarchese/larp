#include "transport/reassembly.hpp"
#include <array>
#include <stdexcept>
#define CHECK(x)                                                                                   \
    do {                                                                                           \
        if (!(x))                                                                                  \
            throw std::runtime_error(#x);                                                          \
    } while (false)
int main() {
    using namespace larp;
    Reassembler receiver;
    std::array<std::byte, datagram_size> wire{};
    auto send = [&](std::uint64_t id, std::uint32_t index, std::uint64_t now) {
        encode(Header{id, index, 2, 0, index == 0 ? payload_capacity : 1}, wire);
        return receiver.accept(std::span(wire).first(index == 0 ? datagram_size : header_size + 1),
                               now);
    };
    CHECK(!receiver.stats.packet_loss_percent());
    CHECK(!receiver.stats.frame_loss_percent());
    CHECK(!send(10, 0, 0));
    CHECK(receiver.stats.expected == 0);
    receiver.expire(100000);
    CHECK(receiver.stats.expected == 2 && receiver.stats.missing == 1);
    CHECK(!send(12, 1, 100001));
    CHECK(receiver.stats.expected == 2);
    CHECK(send(12, 0, 100002));
    CHECK(receiver.stats.expected == 4 && receiver.stats.skipped == 1);
    receiver.finish();
    receiver.finish();
    CHECK(receiver.stats.expected == 4 && receiver.stats.dropped == 1);
    CHECK(receiver.stats.expired == 1);
    CHECK(receiver.stats.packet_loss_percent() == 25.0);
    CHECK(*receiver.stats.frame_loss_percent() > 66.6 &&
          *receiver.stats.frame_loss_percent() < 66.7);
    CHECK(!send(13, 0, 200000));
    CHECK(!send(14, 0, 200001));
    CHECK(receiver.stats.superseded == 1);
    receiver.finish();
    receiver.finish();
    CHECK(receiver.stats.shutdown == 1);
    CHECK(receiver.stats.dropped ==
          receiver.stats.expired + receiver.stats.superseded + receiver.stats.shutdown);
    Reassembler short_deadline(std::chrono::microseconds(2000));
    encode(Header{1, 0, 2, 0, payload_capacity}, wire);
    CHECK(!short_deadline.accept(wire, 0));
    CHECK(!short_deadline.accept(wire, 1999));
    short_deadline.expire(2000);
    CHECK(short_deadline.stats.expired == 1);
    CHECK(!short_deadline.accept(wire, 2001));
    CHECK(short_deadline.stats.stale == 1);
    bool rejected = false;
    try {
        Reassembler invalid(std::chrono::microseconds(0));
    } catch (const std::invalid_argument &) {
        rejected = true;
    }
    CHECK(rejected);
}
