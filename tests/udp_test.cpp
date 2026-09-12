#include "platform/udp.hpp"
#include <array>
#include <stdexcept>
#define CHECK(x)                                                                                   \
    do {                                                                                           \
        if (!(x))                                                                                  \
            throw std::runtime_error(#x);                                                          \
    } while (false)
int main() {
    using namespace larp;
    UdpSocket receiver(Endpoint::parse("127.0.0.1", 0));
    UdpSocket sender(Endpoint::parse("127.0.0.1", 0));
    std::array<std::byte, 1201> out{};
    std::array<std::byte, 1200> in{};
    CHECK(receiver.receive(in, 1).status == ReceiveStatus::timeout);
    out[0] = std::byte{42};
    sender.send(receiver.local_endpoint(), std::span(out).first(3));
    auto result = receiver.receive(in, 100);
    CHECK(result.status == ReceiveStatus::packet && result.size == 3 && in[0] == std::byte{42});
    CHECK(result.peer == sender.local_endpoint());
    sender.send(receiver.local_endpoint(), out);
    CHECK(receiver.receive(in, 100).status == ReceiveStatus::truncated);
    sender.send(receiver.local_endpoint(), {});
    CHECK(receiver.receive(in, 100).size == 0);
    bool rejected = false;
    try {
        (void)Endpoint::parse("invalid", 1);
    } catch (const std::exception &) {
        rejected = true;
    }
    CHECK(rejected);
}
