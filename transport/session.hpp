#pragma once
#include "platform/udp.hpp"
#include <memory>
#include <optional>
#include <string>
namespace larp {
enum class SessionRole { sender, receiver };
struct NetworkOptions {
    std::string key_file;
    std::optional<Endpoint> peer;
    Endpoint bind{};
};
NetworkOptions take_network_options(int &argc, char **argv, SessionRole role);
void generate_session_key(const std::string &path);
class SessionSocket {
    UdpSocket socket_;
    struct State;
    std::unique_ptr<State> state_;
  public:
    SessionSocket(Endpoint local, SessionRole role, const NetworkOptions &options = {});
    ~SessionSocket();
    SessionSocket(const SessionSocket &) = delete;
    SessionSocket &operator=(const SessionSocket &) = delete;
    Endpoint local_endpoint() const;
    bool secured() const;
    std::uint64_t generation() const;
    std::uint64_t rejected() const;
    std::uint64_t unsent() const;
    void connect(Endpoint destination);
    void service();
    void send(Endpoint destination, std::span<const std::byte> data);
    ReceiveResult receive(std::span<std::byte> buffer, int timeout_ms);
};
} // namespace larp
