#include "protocol/packet.hpp"
#include <array>
#include <iostream>
#include <stdexcept>
#define CHECK(x)                                                                                   \
    do {                                                                                           \
        if (!(x))                                                                                  \
            throw std::runtime_error(#x);                                                          \
    } while (false)
int main() {
    using namespace larp;
    std::array<std::byte, datagram_size> wire{};
    Header h{0x0102030405060708ULL, 0, 2, 42, payload_capacity};
    encode(h, wire);
    CHECK(wire[0] == std::byte{'L'} && wire[3] == std::byte{'P'});
    CHECK(wire[6] == std::byte{1} && wire[13] == std::byte{8});
    auto p = decode(wire);
    CHECK(p && p->frame_id == h.frame_id && p->timestamp_us == 42);
    CHECK(!decode(std::span(wire).first(33)));
    wire[0] = std::byte{0};
    CHECK(!decode(wire));
    encode(h, wire);
    wire[5] = std::byte{2};
    CHECK(!decode(wire));
    encode(h, wire);
    CHECK(!decode(std::span(wire).first(1199)));
    h.packet_index = 2;
    encode(h, wire);
    CHECK(!decode(wire));
    h.packet_index = 0;
    h.packet_count = 0;
    encode(h, wire);
    CHECK(!decode(wire));
    h.packet_count = max_packets + 1;
    encode(h, wire);
    CHECK(!decode(wire));
    h.packet_count = 2;
    h.payload_size = 1;
    encode(h, wire);
    CHECK(!decode(std::span(wire).first(header_size + 1)));
    h.packet_index = 1;
    encode(h, wire);
    CHECK(decode(std::span(wire).first(header_size + 1)));
    h.packet_count = max_packets;
    h.packet_index = max_packets - 1;
    h.payload_size = max_frame_size - (max_packets - 1) * payload_capacity;
    encode(h, wire);
    CHECK(decode(std::span(wire).first(header_size + h.payload_size)));
    ++h.payload_size;
    encode(h, wire);
    CHECK(!decode(std::span(wire).first(header_size + h.payload_size)));
    std::uint32_t random = 17;
    for (int iteration = 0; iteration < 10000; ++iteration) {
        for (auto &byte : wire) {
            random = random * 1664525U + 1013904223U;
            byte = std::byte(random >> 24);
        }
        (void)decode(std::span(wire).first(random % (datagram_size + 1)));
    }
    std::cout << "protocol checks passed\n";
}
