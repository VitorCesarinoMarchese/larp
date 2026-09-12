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
    Reassembler r;
    std::array<std::byte, datagram_size> wire{};
    auto packet = [&](std::uint64_t id, std::uint32_t index, std::uint64_t now) {
        Header h{id, index, 2, 77, index == 0 ? payload_capacity : 3};
        encode(h, wire);
        for (std::size_t i = 0; i < h.payload_size; ++i)
            wire[header_size + i] = synthetic_byte(id, index * payload_capacity + i);
        return r.accept(std::span(wire).first(header_size + h.payload_size), now);
    };
    CHECK(!packet(1, 1, 0));
    CHECK(!packet(1, 1, 1));
    CHECK(r.stats.duplicates == 1);
    auto frame = packet(1, 0, 2);
    CHECK(frame && frame->size() == payload_capacity + 3);
    for (std::size_t i = 0; i < frame->size(); ++i)
        CHECK((*frame)[i] == synthetic_byte(1, i));
    CHECK(!packet(1, 0, 3));
    CHECK(!packet(2, 0, 10));
    CHECK(!packet(3, 1, 11));
    CHECK(r.stats.dropped == 1 && r.stats.missing == 1);
    r.expire(100011);
    CHECK(r.stats.dropped == 2 && r.stats.missing == 2);
    CHECK(!packet(3, 0, 100012));
    CHECK(!packet(5, 0, 100013));
    CHECK(r.stats.skipped == 1);
    r.expire(200013);
    CHECK(r.stats.dropped == 3);
    CHECK(!r.accept(std::span(wire).first(2), 200014));
    CHECK(r.stats.invalid == 1);
    CHECK(!packet(6, 0, 300000));
    Header conflict{6, 1, 2, 88, 3};
    encode(conflict, wire);
    CHECK(!r.accept(std::span(wire).first(header_size + 3), 300001));
    CHECK(r.stats.invalid == 2);
    conflict.timestamp_us = 77;
    conflict.packet_count = 3;
    encode(conflict, wire);
    CHECK(!r.accept(wire, 300002));
    CHECK(r.stats.invalid == 3);
    CHECK(packet(6, 1, 300003));
    CHECK(!packet(7, 0, 400000));
    r.expire(499999);
    CHECK(r.stats.dropped == 3);
    CHECK(!packet(7, 0, 499999));
    r.expire(500000);
    CHECK(r.stats.dropped == 4);
    CHECK(!packet(7, 1, 500001));
    for (std::uint64_t id = 8; id < 10008; ++id) {
        CHECK(!packet(id, 1, id * 100000));
        CHECK(packet(id, 0, id * 100000 + 1));
    }
}
