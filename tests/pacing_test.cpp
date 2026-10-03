#include "transport/reassembly.hpp"
#include "transport/send_frame.hpp"
#include <stdexcept>
#include <vector>
#define CHECK(x)                                                                                   \
    do {                                                                                           \
        if (!(x))                                                                                  \
            throw std::runtime_error(#x);                                                          \
    } while (false)
int main() {
    using namespace larp;
    UdpSocket receiver(Endpoint::parse("127.0.0.1", 0));
    UdpSocket sender(Endpoint::parse("127.0.0.1", 0));
    std::vector<std::byte> frame(payload_capacity * 4, std::byte{42});
    const auto before = Clock::now();
    CHECK(send_frame(sender, receiver.local_endpoint(), frame, 1, 123,
                     std::chrono::milliseconds(20)) == 4);
    CHECK(Clock::now() - before >= std::chrono::milliseconds(14));
    Reassembler reassembler;
    std::array<std::byte, datagram_size> wire{};
    for (unsigned i = 0; i < 4; ++i) {
        const auto received = receiver.receive(wire, 100);
        CHECK(received.status == ReceiveStatus::packet);
        const auto bytes = std::span(wire).first(received.size);
        CHECK(decode(bytes)->packet_index == i);
        auto complete = reassembler.accept(bytes, now_us());
        CHECK(complete.has_value() == (i == 3));
        if (complete)
            CHECK(std::equal(complete->begin(), complete->end(), frame.begin(), frame.end()));
    }
    bool rejected = false;
    try {
        send_frame(sender, receiver.local_endpoint(), frame, 2, 456, std::chrono::nanoseconds(-1));
    } catch (const std::invalid_argument &) {
        rejected = true;
    }
    CHECK(rejected);
}
