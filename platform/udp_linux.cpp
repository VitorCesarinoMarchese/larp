#include "platform/udp.hpp"
#include <arpa/inet.h>
#include <cerrno>
#include <cstring>
#include <poll.h>
#include <stdexcept>
#include <string>
#include <sys/socket.h>
#include <system_error>
#include <unistd.h>
namespace larp {
namespace {
[[noreturn]] void fail(const char *operation) {
    throw std::system_error(errno, std::generic_category(), operation);
}
sockaddr_in native(const Endpoint &endpoint) {
    sockaddr_in result{};
    result.sin_family = AF_INET;
    result.sin_port = htons(endpoint.port);
    std::memcpy(&result.sin_addr, endpoint.address.data(), 4);
    return result;
}
Endpoint portable(const sockaddr_in &address) {
    Endpoint result{};
    result.port = ntohs(address.sin_port);
    std::memcpy(result.address.data(), &address.sin_addr, 4);
    return result;
}
} // namespace
Endpoint Endpoint::parse(std::string_view ip, std::uint16_t port) {
    Endpoint result{};
    result.port = port;
    if (inet_pton(AF_INET, std::string(ip).c_str(), result.address.data()) != 1)
        throw std::invalid_argument("expected an IPv4 address");
    return result;
}
UdpSocket::UdpSocket(const Endpoint &local) {
    fd_ = socket(AF_INET, SOCK_DGRAM | SOCK_CLOEXEC | SOCK_NONBLOCK, 0);
    if (fd_ < 0)
        fail("socket");
    try {
        const int bytes = 256 * 1024;
        if (setsockopt(fd_, SOL_SOCKET, SO_RCVBUF, &bytes, sizeof(bytes)) < 0 ||
            setsockopt(fd_, SOL_SOCKET, SO_SNDBUF, &bytes, sizeof(bytes)) < 0)
            fail("socket buffer");
        const int discovery = IP_PMTUDISC_DO;
        if (setsockopt(fd_, IPPROTO_IP, IP_MTU_DISCOVER, &discovery, sizeof(discovery)) < 0)
            fail("disable fragmentation");
        auto address = native(local);
        if (::bind(fd_, reinterpret_cast<const sockaddr *>(&address), sizeof(address)) < 0)
            fail("bind");
    } catch (...) {
        close(fd_);
        throw;
    }
}
UdpSocket::~UdpSocket() {
    if (fd_ >= 0)
        close(fd_);
}
Endpoint UdpSocket::local_endpoint() const {
    sockaddr_in address{};
    socklen_t size = sizeof(address);
    if (getsockname(fd_, reinterpret_cast<sockaddr *>(&address), &size) < 0)
        fail("getsockname");
    return portable(address);
}
void UdpSocket::send(const Endpoint &destination, std::span<const std::byte> data) const {
    auto address = native(destination);
    ssize_t sent;
    do {
        sent = sendto(fd_, data.data(), data.size(), 0,
                      reinterpret_cast<const sockaddr *>(&address), sizeof(address));
    } while (sent < 0 && errno == EINTR);
    if (sent < 0)
        fail("sendto");
    if (static_cast<std::size_t>(sent) != data.size())
        throw std::runtime_error("short UDP send");
}
ReceiveResult UdpSocket::receive(std::span<std::byte> buffer, int timeout_ms) const {
    if (timeout_ms < 0)
        throw std::invalid_argument("negative receive timeout");
    pollfd descriptor{fd_, POLLIN, 0};
    const int ready = poll(&descriptor, 1, timeout_ms);
    if (ready < 0) {
        if (errno == EINTR)
            return {ReceiveStatus::timeout, 0, {}};
        fail("poll");
    }
    if (!ready)
        return {ReceiveStatus::timeout, 0, {}};
    sockaddr_in address{};
    socklen_t size = sizeof(address);
    const auto count = recvfrom(fd_, buffer.data(), buffer.size(), MSG_TRUNC,
                                reinterpret_cast<sockaddr *>(&address), &size);
    if (count < 0) {
        if (errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR)
            return {ReceiveStatus::timeout, 0, {}};
        fail("recvfrom");
    }
    const auto received = static_cast<std::size_t>(count);
    return {received > buffer.size() ? ReceiveStatus::truncated : ReceiveStatus::packet, received,
            portable(address)};
}
} // namespace larp
