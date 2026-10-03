#include "transport/session.hpp"
#include "common/runtime.hpp"
#include "protocol/feedback.hpp"
#include "protocol/packet.hpp"
#include <sodium.h>
#include <algorithm>
#include <array>
#include <bitset>
#include <cerrno>
#include <cstring>
#include <fcntl.h>
#include <limits>
#include <stdexcept>
#include <sys/stat.h>
#include <system_error>
#include <thread>
#include <unistd.h>
namespace larp {
namespace {
constexpr std::size_t prefix_size = 8, nonce_size = 24, overhead = 48;
constexpr std::size_t session_datagram_size = datagram_size + overhead;
static_assert(crypto_aead_xchacha20poly1305_ietf_NPUBBYTES == nonce_size);
static_assert(crypto_aead_xchacha20poly1305_ietf_ABYTES == overhead - prefix_size - nonce_size);
using Nonce = std::array<unsigned char, 16>;
enum class Kind : unsigned char { hello = 1, challenge, confirm, ready, media, ping, pong };
struct Key {
    std::array<unsigned char, 32> bytes{};
    Key() = default;
    Key(const Key &) = delete;
    Key &operator=(const Key &) = delete;
    ~Key() { sodium_memzero(bytes.data(), bytes.size()); }
};
void initialize() {
    static const int result = sodium_init();
    if (result < 0)
        throw std::runtime_error("libsodium initialization failed");
}
[[noreturn]] void system_failure(const char *operation) {
    throw std::system_error(errno, std::generic_category(), operation);
}
struct File {
    int fd;
    ~File() { if (fd >= 0) close(fd); }
};
void load_key(const std::string &path, Key &key) {
    File file{open(path.c_str(), O_RDONLY | O_CLOEXEC | O_NOFOLLOW | O_NONBLOCK)};
    if (file.fd < 0)
        system_failure("open session key");
    struct stat info{};
    if (fstat(file.fd, &info) < 0)
        system_failure("stat session key");
    if (!S_ISREG(info.st_mode) || info.st_uid != geteuid() || (info.st_mode & 077) ||
        info.st_size != static_cast<off_t>(key.bytes.size()))
        throw std::invalid_argument("session key must be a private, owned, 32-byte regular file");
    std::size_t offset = 0;
    while (offset < key.bytes.size()) {
        const auto count = read(file.fd, key.bytes.data() + offset, key.bytes.size() - offset);
        if (count < 0 && errno == EINTR)
            continue;
        if (count < 0)
            system_failure("read session key");
        if (!count)
            throw std::runtime_error("short session key");
        offset += static_cast<std::size_t>(count);
    }
}
Nonce random_nonce() {
    Nonce result{};
    randombytes_buf(result.data(), result.size());
    return result;
}
std::array<std::byte, 32> pair(const Nonce &client, const Nonce &server) {
    std::array<std::byte, 32> result{};
    std::memcpy(result.data(), client.data(), 16);
    std::memcpy(result.data() + 16, server.data(), 16);
    return result;
}
bool matches(std::span<const std::byte> bytes, std::size_t offset, const Nonce &nonce) {
    return bytes.size() >= offset + nonce.size() &&
           sodium_memcmp(bytes.data() + offset, nonce.data(), nonce.size()) == 0;
}
Nonce nonce_at(std::span<const std::byte> bytes, std::size_t offset) {
    Nonce result{};
    std::memcpy(result.data(), bytes.data() + offset, result.size());
    return result;
}
struct ReplayWindow {
    std::uint64_t highest = 0;
    std::bitset<1024> seen;
    bool accept(std::uint64_t sequence) {
        if (!sequence)
            return false;
        if (sequence > highest) {
            const auto distance = sequence - highest;
            if (distance >= seen.size())
                seen.reset();
            else
                seen <<= static_cast<std::size_t>(distance);
            highest = sequence;
            seen.set(0);
            return true;
        }
        const auto distance = highest - sequence;
        if (distance >= seen.size() || seen.test(static_cast<std::size_t>(distance)))
            return false;
        seen.set(static_cast<std::size_t>(distance));
        return true;
    }
};
struct Connection {
    Endpoint peer;
    Nonce client, server;
    Key tx, rx;
    ReplayWindow replay;
    std::uint64_t sequence = 0, activity;
    Connection(Endpoint endpoint, Nonce c, Nonce s, const Key &master, SessionRole role)
        : peer(endpoint), client(c), server(s), activity(now_us()) {
        const auto nonces = pair(client, server);
        Key base;
        if (crypto_generichash(base.bytes.data(), base.bytes.size(),
                reinterpret_cast<const unsigned char *>(nonces.data()), nonces.size(),
                master.bytes.data(), master.bytes.size()) != 0)
            throw std::runtime_error("session key derivation failed");
        constexpr char context[8] = {'L', 'A', 'R', 'P', 'v', '0', '0', '1'};
        const std::uint64_t outbound = role == SessionRole::sender ? 1 : 2;
        if (crypto_kdf_derive_from_key(tx.bytes.data(), tx.bytes.size(), outbound,
                                      context, base.bytes.data()) != 0 ||
            crypto_kdf_derive_from_key(rx.bytes.data(), rx.bytes.size(), 3 - outbound,
                                      context, base.bytes.data()) != 0)
            throw std::runtime_error("directional key derivation failed");
    }
};
struct Candidate {
    Endpoint peer;
    Nonce client, server;
    std::uint64_t expires;
};
struct Handshake {
    Endpoint peer;
    Nonce client;
    std::optional<Nonce> server;
    std::uint64_t started, next_send = 0;
};
struct Pending {
    std::array<std::byte, datagram_size> bytes{};
    std::size_t size;
    Endpoint peer;
};
} // namespace

NetworkOptions take_network_options(int &argc, char **argv, SessionRole role) {
    NetworkOptions result;
    bool bind_seen = false;
    int write = 1;
    for (int i = 1; i < argc; ++i) {
        const std::string_view option = argv[i];
        if (option != "--key-file" && option != "--peer" && option != "--bind") {
            argv[write++] = argv[i];
            continue;
        }
        if (++i == argc)
            throw std::invalid_argument("network option needs a value");
        if (option == "--key-file" && result.key_file.empty()) {
            result.key_file = argv[i];
            if (result.key_file.empty())
                throw std::invalid_argument("empty session key path");
        } else if (option == "--peer" && role == SessionRole::receiver && !result.peer)
            result.peer = Endpoint::parse(argv[i], 0);
        else if (option == "--bind" && role == SessionRole::sender && !bind_seen) {
            result.bind = Endpoint::parse(argv[i], 0);
            bind_seen = true;
        } else
            throw std::invalid_argument("unknown or duplicate network option");
    }
    argc = write;
    argv[argc] = nullptr;
    if (role == SessionRole::receiver && result.peer.has_value() != !result.key_file.empty())
        throw std::invalid_argument("secure receiver requires both --key-file and --peer");
    return result;
}

void generate_session_key(const std::string &path) {
    initialize();
    Key key;
    randombytes_buf(key.bytes.data(), key.bytes.size());
    File file{open(path.c_str(), O_WRONLY | O_CREAT | O_EXCL | O_CLOEXEC | O_NOFOLLOW, 0600)};
    if (file.fd < 0)
        system_failure("create session key");
    std::size_t offset = 0;
    while (offset < key.bytes.size()) {
        const auto count = write(file.fd, key.bytes.data() + offset, key.bytes.size() - offset);
        if (count < 0 && errno == EINTR)
            continue;
        if (count < 0)
            system_failure("write session key");
        if (!count)
            throw std::runtime_error("short session key write");
        offset += static_cast<std::size_t>(count);
    }
}

struct SessionSocket::State {
    SessionRole role;
    Key master;
    std::optional<Endpoint> allowed;
    std::unique_ptr<Connection> active;
    std::optional<Candidate> candidate;
    std::optional<Handshake> handshake;
    std::optional<Pending> pending;
    std::optional<Nonce> ping;
    std::uint64_t ping_sent = 0, next_ping = 0, generation = 0, rejected = 0, unsent = 0;
    State(SessionRole r, const NetworkOptions &options) : role(r), allowed(options.peer) {
        initialize();
        load_key(options.key_file, master);
        if (role == SessionRole::receiver && !allowed)
            throw std::invalid_argument("secure receiver requires an allowed peer");
    }
    unsigned direction() const { return role == SessionRole::sender ? 0 : 1; }
    void transmit(UdpSocket &socket, Endpoint destination, Kind kind,
                  std::span<const std::byte> body) {
        std::array<std::byte, session_datagram_size> wire{};
        feedback_detail::put(wire, 0, 0x4c415345, 4);
        feedback_detail::put(wire, 4, 1, 2);
        wire[6] = std::byte(kind);
        wire[7] = std::byte(direction());
        auto *nonce = reinterpret_cast<unsigned char *>(wire.data() + prefix_size);
        const Key *key = &master;
        if (kind == Kind::media) {
            key = &active->tx;
            const auto &prefix = role == SessionRole::sender ? active->client : active->server;
            std::memcpy(nonce, prefix.data(), prefix.size());
            feedback_detail::put(wire, prefix_size + 16, ++active->sequence, 8);
        } else
            randombytes_buf(nonce, nonce_size);
        unsigned long long length = 0;
        if (crypto_aead_xchacha20poly1305_ietf_encrypt(
                reinterpret_cast<unsigned char *>(wire.data() + prefix_size + nonce_size),
                &length, reinterpret_cast<const unsigned char *>(body.data()), body.size(),
                reinterpret_cast<const unsigned char *>(wire.data()), prefix_size,
                nullptr, nonce, key->bytes.data()) != 0)
            throw std::runtime_error("session encryption failed");
        socket.send(destination, std::span(wire).first(prefix_size + nonce_size +
                                                     static_cast<std::size_t>(length)));
    }
    void begin(Endpoint destination) {
        handshake = Handshake{destination, random_nonce(), {}, now_us(), 0};
        active.reset();
        pending.reset();
        ping.reset();
    }
    void drive(UdpSocket &socket) {
        const auto now = now_us();
        if (handshake) {
            if (now - handshake->started >= 3000000)
                begin(handshake->peer);
            if (now >= handshake->next_send) {
                if (handshake->server)
                    transmit(socket, handshake->peer, Kind::confirm,
                             pair(handshake->client, *handshake->server));
                else
                    transmit(socket, handshake->peer, Kind::hello,
                             std::as_bytes(std::span(handshake->client)));
                handshake->next_send = now + 250000;
            }
            return;
        }
        if (!active)
            return;
        if ((ping && now - ping_sent >= 1000000) ||
            active->sequence == std::numeric_limits<std::uint64_t>::max()) {
            begin(active->peer);
            drive(socket);
        } else if (!ping && now >= next_ping) {
            ping = random_nonce();
            ping_sent = now;
            std::array<std::byte, 48> request{};
            const auto nonces = pair(active->client, active->server);
            std::copy(nonces.begin(), nonces.end(), request.begin());
            std::memcpy(request.data() + 32, ping->data(), ping->size());
            transmit(socket, active->peer, Kind::ping, request);
        }
    }
    std::optional<Pending> process(UdpSocket &socket, const ReceiveResult &received,
                                  std::span<const std::byte> wire) {
        if (received.status != ReceiveStatus::packet || wire.size() < overhead ||
            wire.size() > session_datagram_size ||
            (allowed && received.peer.address != allowed->address) ||
            feedback_detail::get(wire, 0, 4) != 0x4c415345 ||
            feedback_detail::get(wire, 4, 2) != 1 ||
            std::to_integer<unsigned>(wire[7]) != 1 - direction()) {
            ++rejected;
            return {};
        }
        const auto kind = static_cast<Kind>(std::to_integer<unsigned char>(wire[6]));
        const Key *key = &master;
        if (kind == Kind::media) {
            if (!active || received.peer != active->peer ||
                !matches(wire, prefix_size,
                         role == SessionRole::sender ? active->server : active->client)) {
                ++rejected;
                return {};
            }
            key = &active->rx;
        }
        std::array<std::byte, datagram_size> plain{};
        unsigned long long length = 0;
        if (crypto_aead_xchacha20poly1305_ietf_decrypt(
                reinterpret_cast<unsigned char *>(plain.data()), &length, nullptr,
                reinterpret_cast<const unsigned char *>(wire.data() + prefix_size + nonce_size),
                wire.size() - prefix_size - nonce_size,
                reinterpret_cast<const unsigned char *>(wire.data()), prefix_size,
                reinterpret_cast<const unsigned char *>(wire.data() + prefix_size),
                key->bytes.data()) != 0) {
            ++rejected;
            return {};
        }
        const auto body = std::span(plain).first(static_cast<std::size_t>(length));
        const auto now = now_us();
        if (kind == Kind::media) {
            if (!active->replay.accept(feedback_detail::get(wire, prefix_size + 16, 8))) {
                ++rejected;
                return {};
            }
            active->activity = now;
            Pending packet{};
            std::copy(body.begin(), body.end(), packet.bytes.begin());
            packet.size = body.size();
            packet.peer = received.peer;
            return packet;
        }
        if (role == SessionRole::receiver) {
            if (kind == Kind::hello && body.size() == 16) {
                const auto client = nonce_at(body, 0);
                if (active && now - active->activity < 1000000) {
                    ++rejected;
                    return {};
                }
                if (!candidate || candidate->peer != received.peer ||
                    candidate->client != client || now >= candidate->expires)
                    candidate = Candidate{received.peer, client, random_nonce(), now + 3000000};
                transmit(socket, received.peer, Kind::challenge,
                         pair(candidate->client, candidate->server));
                return {};
            }
            if (kind == Kind::confirm && body.size() == 32) {
                if (candidate && now < candidate->expires && candidate->peer == received.peer &&
                    matches(body, 0, candidate->client) && matches(body, 16, candidate->server) &&
                    (!active || now - active->activity >= 1000000)) {
                    active = std::make_unique<Connection>(candidate->peer, candidate->client,
                                                         candidate->server, master, role);
                    ++generation;
                    candidate.reset();
                }
                if (active && received.peer == active->peer && matches(body, 0, active->client) &&
                    matches(body, 16, active->server)) {
                    transmit(socket, received.peer, Kind::ready, pair(active->client, active->server));
                    return {};
                }
            }
            if (kind == Kind::ping && body.size() == 48) {
                std::array<std::byte, 49> reply{};
                std::copy(body.begin(), body.end(), reply.begin());
                reply[48] = std::byte(active && received.peer == active->peer &&
                    matches(body, 0, active->client) && matches(body, 16, active->server));
                transmit(socket, received.peer, Kind::pong, reply);
                return {};
            }
        } else {
            if (handshake && received.peer == handshake->peer && body.size() == 32 &&
                matches(body, 0, handshake->client)) {
                if (kind == Kind::challenge) {
                    handshake->server = nonce_at(body, 16);
                    handshake->next_send = 0;
                    drive(socket);
                    return {};
                }
                if (kind == Kind::ready && handshake->server &&
                    matches(body, 16, *handshake->server)) {
                    active = std::make_unique<Connection>(handshake->peer, handshake->client,
                                                         *handshake->server, master, role);
                    handshake.reset();
                    ++generation;
                    ping.reset();
                    next_ping = now + 250000;
                    return {};
                }
            }
            if (kind == Kind::pong && body.size() == 49 && active && ping &&
                received.peer == active->peer && matches(body, 0, active->client) &&
                matches(body, 16, active->server) && matches(body, 32, *ping) &&
                (body[48] == std::byte{} || body[48] == std::byte{1})) {
                ping.reset();
                next_ping = now + 250000;
                if (body[48] == std::byte{})
                    begin(active->peer);
                return {};
            }
        }
        ++rejected;
        return {};
    }
};

SessionSocket::SessionSocket(Endpoint local, SessionRole role, const NetworkOptions &options)
    : socket_(local) {
    if (!options.key_file.empty())
        state_ = std::make_unique<State>(role, options);
}
SessionSocket::~SessionSocket() = default;
Endpoint SessionSocket::local_endpoint() const { return socket_.local_endpoint(); }
bool SessionSocket::secured() const { return state_ != nullptr; }
std::uint64_t SessionSocket::generation() const { return state_ ? state_->generation : 0; }
std::uint64_t SessionSocket::rejected() const { return state_ ? state_->rejected : 0; }
std::uint64_t SessionSocket::unsent() const { return state_ ? state_->unsent : 0; }
void SessionSocket::connect(Endpoint destination) {
    if (!state_)
        return;
    if (state_->role != SessionRole::sender)
        throw std::logic_error("receiver cannot initiate a session");
    state_->allowed = destination;
    state_->begin(destination);
    const auto deadline = Clock::now() + std::chrono::seconds(5);
    while (!state_->active && Clock::now() < deadline) {
        service();
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    if (!state_->active)
        throw std::runtime_error("authenticated session handshake timed out");
}
void SessionSocket::service() {
    if (!state_ || state_->role != SessionRole::sender)
        return;
    for (unsigned i = 0; i < 32; ++i) {
        std::array<std::byte, session_datagram_size + 1> wire{};
        const auto received = socket_.receive(wire, 0);
        if (received.status == ReceiveStatus::timeout)
            break;
        if (received.status == ReceiveStatus::truncated) {
            ++state_->rejected;
            continue;
        }
        if (auto packet = state_->process(socket_, received, std::span(wire).first(received.size)))
            state_->pending = *packet;
    }
    state_->drive(socket_);
}
void SessionSocket::send(Endpoint destination, std::span<const std::byte> data) {
    if (!state_) {
        socket_.send(destination, data);
        return;
    }
    if (data.size() > datagram_size)
        throw std::invalid_argument("secure payload exceeds 1200 bytes");
    if (state_->role == SessionRole::sender)
        service();
    if (!state_->active) {
        ++state_->unsent;
        return;
    }
    if (destination != state_->active->peer)
        throw std::invalid_argument("secure destination differs from session peer");
    if (state_->active->sequence == std::numeric_limits<std::uint64_t>::max())
        throw std::runtime_error("session sequence exhausted");
    state_->transmit(socket_, destination, Kind::media, data);
}
ReceiveResult SessionSocket::receive(std::span<std::byte> buffer, int timeout_ms) {
    if (!state_)
        return socket_.receive(buffer, timeout_ms);
    if (timeout_ms < 0)
        throw std::invalid_argument("negative receive timeout");
    auto deliver = [&](const Pending &packet) {
        std::copy_n(packet.bytes.begin(), std::min(packet.size, buffer.size()), buffer.begin());
        return ReceiveResult{packet.size > buffer.size() ? ReceiveStatus::truncated : ReceiveStatus::packet,
                             packet.size, packet.peer};
    };
    if (state_->pending) {
        const auto result = deliver(*state_->pending);
        state_->pending.reset();
        return result;
    }
    const auto deadline = Clock::now() + std::chrono::milliseconds(timeout_ms);
    for (unsigned i = 0; i < 32; ++i) {
        std::array<std::byte, session_datagram_size + 1> wire{};
        const auto remaining = std::chrono::duration_cast<std::chrono::milliseconds>(deadline - Clock::now());
        const auto received = socket_.receive(wire, static_cast<int>(std::max<std::int64_t>(0, remaining.count())));
        if (received.status == ReceiveStatus::timeout)
            break;
        if (received.status == ReceiveStatus::truncated) {
            ++state_->rejected;
            continue;
        }
        auto packet = state_->process(socket_, received, std::span(wire).first(received.size));
        if (packet)
            return deliver(*packet);
        if (Clock::now() >= deadline && timeout_ms)
            break;
    }
    if (state_->role == SessionRole::sender)
        state_->drive(socket_);
    return {ReceiveStatus::timeout, 0, {}};
}
} // namespace larp
