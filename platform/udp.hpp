#pragma once
#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string_view>
namespace larp {
struct Endpoint {
    std::array<unsigned char, 4> address{};
    std::uint16_t port = 0;
    static Endpoint parse(std::string_view ip, std::uint16_t port);
    bool operator==(const Endpoint &) const = default;
};
enum class ReceiveStatus { packet, timeout, truncated };
struct ReceiveResult {
    ReceiveStatus status;
    std::size_t size;
    Endpoint peer;
};
class UdpSocket {
    int fd_ = -1;

  public:
    explicit UdpSocket(const Endpoint &local);
    ~UdpSocket();
    UdpSocket(const UdpSocket &) = delete;
    UdpSocket &operator=(const UdpSocket &) = delete;
    Endpoint local_endpoint() const;
    void send(const Endpoint &destination, std::span<const std::byte> data) const;
    ReceiveResult receive(std::span<std::byte> buffer, int timeout_ms) const;
};
} // namespace larp
