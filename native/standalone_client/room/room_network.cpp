#include "room_network.hpp"
#include "room_replica.hpp"
#if defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <winsock2.h>
#include <ws2tcpip.h>
#else
#include <arpa/inet.h>
#include <cerrno>
#include <fcntl.h>
#include <netdb.h>
#include <netinet/tcp.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <unistd.h>
#endif
#include <atomic>
#include <cstring>
#include <map>
#include <thread>
#include "room_perf.hpp"

namespace wot::room {
namespace {
#if defined(_WIN32)
using Socket = SOCKET;
constexpr Socket invalid = INVALID_SOCKET;
void closeSocket(Socket s) { closesocket(s); }
bool wouldBlock() { const int n = WSAGetLastError(); return n == WSAEWOULDBLOCK || n == WSAEINTR; }
void nonblocking(Socket s) { u_long enabled = 1; if (ioctlsocket(s, FIONBIO, &enabled)) throw std::runtime_error("Room socket nonblocking mode failed"); }
#else
using Socket = int;
constexpr Socket invalid = -1;
void closeSocket(Socket s) { ::close(s); }
bool wouldBlock() { return errno == EWOULDBLOCK || errno == EAGAIN || errno == EINTR; }
void nonblocking(Socket s) {
    const auto flags = fcntl(s, F_GETFL, 0);
    if (flags < 0 || fcntl(s, F_SETFL, flags | O_NONBLOCK) < 0) throw std::runtime_error("Room socket nonblocking mode failed");
}
#endif
constexpr std::size_t max_frame = 4 * 1024 * 1024;
constexpr std::size_t max_connections = 40;
// These bounds cover owned native trees, rather than their smaller wire image.
constexpr std::size_t peer_messages = 256, peer_bytes = 32 * 1024 * 1024;
struct Connection {
    explicit Connection(Socket value) : socket(value), input(peer_messages, peer_bytes), output(peer_messages, peer_bytes) {}
    Socket socket;
    std::string address, error;
    Channel input, output;
    std::string received, sending;
    std::size_t sent = 0;
    std::shared_ptr<const Value> blocked;
    ReplicaEncoder replica;
    std::optional<ReplicaEncoder::Prepared> pending_replica;
    std::atomic<std::int64_t> player_id{0};
    std::atomic<bool> full_requested{false};
    std::atomic<bool> read_eof{false}, eof_announced{false};
    std::atomic<bool> close_requested{false}, flush{false};
    std::optional<Channel::Clock::time_point> close_deadline;
};
}
struct Network::Impl {
    explicit Impl(Codec value) : codec(std::move(value)) {}
    Codec codec;
    Socket listener = invalid;
    std::atomic<bool> running{false};
    std::thread thread;
    std::mutex mutex;
    std::map<Peer, std::shared_ptr<Connection>> peers;
    std::vector<Closed> closed;
    Peer next_id = 1;
    Timing decode_time, encode_time, output_age;
    std::uint64_t sent_bytes = 0, received_bytes = 0;
    std::string diagnostic;
#if defined(_WIN32)
    bool winsock = false;
#endif
    void retire(Peer id, const std::shared_ptr<Connection>& connection, std::string why) {
        closeSocket(connection->socket);
        connection->socket = invalid;
        connection->input.close(); connection->output.close();
        std::lock_guard<std::mutex> lock(mutex);
        peers.erase(id); closed.push_back({id, std::move(why)});
    }
    bool decode(Connection& c) {
        if (c.blocked) {
            Channel::Refusal reason;
            if (c.input.offer(c.blocked, {}, &reason) == Channel::Admission::refused)
                return reason == Channel::Refusal::capacity;
            c.blocked.reset();
        }
        while (c.received.size() >= 8) {
            std::size_t size = 0;
            try { size = codec.frameSize(reinterpret_cast<const unsigned char*>(c.received.data()), c.received.size()); }
            catch (const std::exception&) { return false; }
            if (size < 8 || size > max_frame) return false;
            if (size > c.received.size()) return true;
            Value value; std::string error;
            try {
                const auto started = Timing::Clock::now();
                if (!codec.decode(reinterpret_cast<const unsigned char*>(c.received.data()), size, value, error)) { c.error = "decode: " + error; return false; }
                decode_time.elapsed(started);
            } catch (const std::exception& error) { c.error = "decode: " + std::string(error.what()); return false; }
            auto frozen = freeze(std::move(value));
            c.received.erase(0, size);
            Channel::Refusal reason;
            if (c.input.offer(frozen, {}, &reason) == Channel::Admission::refused) {
                if (reason != Channel::Refusal::capacity) return false;
                c.blocked = std::move(frozen); return true;
            }
        }
        return true;
    }
    bool write(Connection& c) {
        if (c.sending.empty()) {
            auto entries = c.output.take(1);
            if (entries.empty()) return true;
            output_age.add(std::chrono::duration<double, std::milli>(Timing::Clock::now() - entries[0].queued).count());
            const Value* message = entries[0].message.get();
            const auto started = Timing::Clock::now();
            std::string error;
            try {
                if (message->get("type").text == "snapshot" && !has(*message, "client_build")) {
                    c.pending_replica = c.replica.prepare(*message, c.player_id, c.full_requested.exchange(false));
                    message = &c.pending_replica->wire;
                }
                Value envelope;
                if (has(*message, "_room_sections")) {
                    envelope = message->resolved(); envelope.object.erase("_room_sections"); message = &envelope;
                }
                if (!codec.encode(*message, c.sending, error)) {
                    c.error = "encode " + message->get("type").resolved().text + ": " + error; return false;
                }
                if (c.sending.size() > max_frame) { c.error = "encoded frame exceeds native room wire limit"; return false; }
            } catch (const std::exception& error) { c.error = "encode: " + std::string(error.what()); return false; }
            encode_time.elapsed(started);
            c.sent = 0;
        }
#if defined(MSG_NOSIGNAL)
        constexpr int flags = MSG_NOSIGNAL;
#else
        constexpr int flags = 0;
#endif
        const auto count = ::send(c.socket, c.sending.data() + c.sent, static_cast<int>(c.sending.size() - c.sent), flags);
        if (count < 0) return wouldBlock();
        if (!count) return false;
        c.sent += static_cast<std::size_t>(count); sent_bytes += static_cast<std::uint64_t>(count);
        if (c.sent == c.sending.size()) {
            if (c.pending_replica) { c.replica.commit(std::move(*c.pending_replica)); c.pending_replica.reset(); }
            c.sending.clear(); c.sent = 0;
        }
        return true;
    }
    void loop() {
        auto last_report = Timing::Clock::now(); auto last_cpu = threadCpuMilliseconds();
        while (running) {
            std::vector<std::pair<Peer, std::shared_ptr<Connection>>> current;
            std::size_t tracked = 0;
            { std::lock_guard<std::mutex> lock(mutex); for (const auto& peer : peers) current.push_back(peer); tracked = peers.size() + closed.size(); }
            fd_set reads, writes; FD_ZERO(&reads); FD_ZERO(&writes);
            FD_SET(listener, &reads); Socket largest = listener;
            for (const auto& row : current) {
                auto& c = *row.second;
                if (!c.blocked && !c.close_requested && !c.read_eof) FD_SET(c.socket, &reads);
                if (!c.sending.empty() || c.output.metrics().pending) FD_SET(c.socket, &writes);
                if (c.socket > largest) largest = c.socket;
            }
            timeval timeout{0, 5000};
            const auto ready = select(static_cast<int>(largest + 1), &reads, &writes, nullptr, &timeout);
            if (ready < 0) { if (wouldBlock()) continue; throw std::runtime_error("Native room socket poll failed"); }
            if (FD_ISSET(listener, &reads)) {
                Socket s = ::accept(listener, nullptr, nullptr);
                if (s != invalid) {
                    // Unconsumed close notifications occupy the same finite peer
                    // budget, including while the engine applies backpressure.
                    bool admitted = tracked < max_connections;
#if !defined(_WIN32)
                    admitted = admitted && s < FD_SETSIZE;
#endif
                    if (admitted) {
                        try {
                            nonblocking(s); int enabled = 1;
                            setsockopt(s, IPPROTO_TCP, TCP_NODELAY, reinterpret_cast<const char*>(&enabled), sizeof(enabled));
#if defined(SO_NOSIGPIPE)
                            setsockopt(s, SOL_SOCKET, SO_NOSIGPIPE, &enabled, sizeof(enabled));
#endif
                            auto connection = std::make_shared<Connection>(s);
                            sockaddr_in address{};
#if defined(_WIN32)
                            int length = sizeof(address);
#else
                            socklen_t length = sizeof(address);
#endif
                            char shown[64]{};
                            if (!getpeername(s, reinterpret_cast<sockaddr*>(&address), &length) &&
                                !getnameinfo(reinterpret_cast<sockaddr*>(&address), length, shown, sizeof(shown),
                                             nullptr, 0, NI_NUMERICHOST)) connection->address = shown;
                            std::lock_guard<std::mutex> lock(mutex); peers.emplace(next_id++, std::move(connection));
                        } catch (...) { closeSocket(s); }
                    } else closeSocket(s);
                }
            }
            for (const auto& row : current) {
                auto& c = *row.second; bool healthy = true;
                if (c.close_requested) {
                    if (!c.close_deadline) c.close_deadline = Channel::Clock::now() + std::chrono::seconds(2);
                    if (!c.flush || (c.output.metrics().pending == 0 && c.sending.empty()) || Channel::Clock::now() >= *c.close_deadline) {
                        retire(row.first, row.second, "closed"); continue;
                    }
                }
                if (c.blocked) healthy = decode(c);
                if (healthy && FD_ISSET(c.socket, &reads)) {
                    char data[65536]; const auto n = ::recv(c.socket, data, sizeof(data), 0);
                    if (n > 0) { received_bytes += static_cast<std::uint64_t>(n); c.received.append(data, static_cast<std::size_t>(n)); healthy = c.received.size() <= max_frame + sizeof(data) && decode(c); }
                    else if (n == 0) {
                        // A probe half-closes after leave. Preserve its decoded
                        // commands until the room consumes them, and its reply
                        // until the room requests a flushed close.
                        c.read_eof = true;
                    } else healthy = wouldBlock();
                }
                if (healthy && FD_ISSET(c.socket, &writes)) healthy = write(c);
                if (!healthy) retire(row.first, row.second, c.error.empty() ? "transport ended or invalid frame" : c.error.substr(0, 2048));
            }
            const auto now = Timing::Clock::now();
            if (now - last_report >= std::chrono::seconds(5)) {
                const auto cpu = threadCpuMilliseconds();
                const auto elapsed = std::chrono::duration<double, std::milli>(now - last_report).count();
                std::size_t rx_pending = 0, tx_pending = 0, rx_bytes = 0, tx_bytes = 0;
                double oldest = 0.;
                for (const auto& row : current) {
                    const auto rx = row.second->input.metrics(), tx = row.second->output.metrics();
                    rx_pending += rx.pending; tx_pending += tx.pending; rx_bytes += rx.bytes; tx_bytes += tx.bytes;
                    oldest = std::max({oldest, rx.oldest_seconds, tx.oldest_seconds});
                }
                std::ostringstream out; out << "PERF room_net tid=" << threadIdentity() << " cpu_core_pct=" << 100. * (cpu - last_cpu) / elapsed
                    << " rx_pending=" << rx_pending << " tx_pending=" << tx_pending << " rx_tree_bytes=" << rx_bytes
                    << " tx_tree_bytes=" << tx_bytes << " oldest_ms=" << oldest * 1000.
                    << " sent_bytes=" << sent_bytes << " received_bytes=" << received_bytes << " "
                    << decode_time.report("decode") << " " << encode_time.report("encode") << " " << output_age.report("tx_age");
                { std::lock_guard<std::mutex> lock(mutex); diagnostic = out.str(); }
                decode_time = {}; encode_time = {}; output_age = {}; sent_bytes = 0; received_bytes = 0;
                last_cpu = cpu; last_report = now;
            }
        }
        running = false;
        std::vector<std::pair<Peer, std::shared_ptr<Connection>>> remaining;
        { std::lock_guard<std::mutex> lock(mutex); for (const auto& row : peers) remaining.push_back(row); }
        for (const auto& row : remaining) retire(row.first, row.second, "room stopped");
    }
};
Network::Network(Codec codec) : impl_(new Impl(std::move(codec))) {
    if (!impl_->codec.frameSize || !impl_->codec.decode || !impl_->codec.encode) throw std::invalid_argument("Room codec is incomplete");
}
Network::~Network() { stop(); }
bool Network::running() const { return impl_->running; }
std::string Network::diagnostic() const { std::lock_guard<std::mutex> lock(impl_->mutex); return impl_->diagnostic; }
Network::Bound Network::start(const std::string& address, unsigned port) {
    auto& i = *impl_;
    if (i.running || i.listener != invalid) throw std::logic_error("Room listener already started");
    if (port > 65535) throw std::invalid_argument("Room port is out of range");
#if defined(_WIN32)
    WSADATA data;
    if (WSAStartup(MAKEWORD(2, 2), &data)) throw std::runtime_error("Room Winsock startup failed");
    i.winsock = true;
#endif
    sockaddr_in endpoint{}; endpoint.sin_family = AF_INET; endpoint.sin_port = htons(static_cast<unsigned short>(port));
    addrinfo hints{}; hints.ai_family = AF_INET; hints.ai_socktype = SOCK_STREAM; hints.ai_flags = AI_NUMERICHOST;
    addrinfo* resolved = nullptr;
    if (getaddrinfo(address.c_str(), nullptr, &hints, &resolved) || !resolved) {
        stop(); throw std::invalid_argument("Room bind address must be IPv4");
    }
    endpoint.sin_addr = reinterpret_cast<const sockaddr_in*>(resolved->ai_addr)->sin_addr;
    freeaddrinfo(resolved);
    i.listener = ::socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (i.listener == invalid) { stop(); throw std::runtime_error("Room listener creation failed"); }
    try {
        nonblocking(i.listener);
        if (::bind(i.listener, reinterpret_cast<const sockaddr*>(&endpoint), sizeof(endpoint)) || ::listen(i.listener, 32))
            throw std::runtime_error("Room listener bind failed");
#if defined(_WIN32)
        int length = sizeof(endpoint);
#else
        socklen_t length = sizeof(endpoint);
#endif
        if (getsockname(i.listener, reinterpret_cast<sockaddr*>(&endpoint), &length)) throw std::runtime_error("Room listener address failed");
        i.running = true; i.thread = std::thread([&i] {
            try { i.loop(); }
            catch (const std::exception& error) {
                { std::lock_guard<std::mutex> lock(i.mutex); i.diagnostic = "NATIVE ROOM network failed: " + std::string(error.what()); }
                std::vector<std::pair<Peer, std::shared_ptr<Connection>>> remaining;
                { std::lock_guard<std::mutex> lock(i.mutex); for (const auto& row : i.peers) remaining.push_back(row); }
                for (const auto& row : remaining) i.retire(row.first, row.second, "network failed");
            }
            i.running = false;
        });
        return {address, ntohs(endpoint.sin_port)};
    } catch (...) { stop(); throw; }
}
std::vector<Network::Received> Network::take(std::size_t limit) {
    std::vector<std::pair<Peer, std::shared_ptr<Connection>>> peers;
    { std::lock_guard<std::mutex> lock(impl_->mutex); for (const auto& row : impl_->peers) peers.push_back(row); }
    std::vector<Received> out;
    for (const auto& peer : peers) for (auto& entry : peer.second->input.take(limit)) out.push_back({peer.first, std::move(entry.message), peer.second->address});
    return out;
}
std::vector<Network::Closed> Network::takeClosed() {
    std::lock_guard<std::mutex> lock(impl_->mutex);
    std::vector<Closed> out; out.swap(impl_->closed);
    for (const auto& row : impl_->peers) {
        auto& c = *row.second;
        if (c.read_eof && c.input.metrics().pending == 0 && !c.eof_announced.exchange(true))
            out.push_back({row.first, "peer input ended"});
    }
    return out;
}
Channel::Admission Network::offer(Peer id, std::shared_ptr<const Value> message, std::optional<std::string> key) {
    std::shared_ptr<Connection> peer;
    { std::lock_guard<std::mutex> lock(impl_->mutex); const auto found = impl_->peers.find(id); if (found != impl_->peers.end()) peer = found->second; }
    return peer ? peer->output.offer(std::move(message), std::move(key)) : Channel::Admission::refused;
}
void Network::disconnect(Peer id, bool flush) {
    std::lock_guard<std::mutex> lock(impl_->mutex); const auto found = impl_->peers.find(id);
    if (found != impl_->peers.end()) { found->second->flush = flush; found->second->close_requested = true; }
}
void Network::setPlayer(Peer id, std::int64_t player_id) {
    std::lock_guard<std::mutex> lock(impl_->mutex); const auto found = impl_->peers.find(id);
    if (found != impl_->peers.end()) found->second->player_id = player_id;
}
void Network::resync(Peer id) {
    std::lock_guard<std::mutex> lock(impl_->mutex); const auto found = impl_->peers.find(id);
    if (found != impl_->peers.end()) found->second->full_requested = true;
}
void Network::stop() {
    auto& i = *impl_; i.running = false;
    if (i.thread.joinable()) i.thread.join();
    if (i.listener != invalid) { closeSocket(i.listener); i.listener = invalid; }
#if defined(_WIN32)
    if (i.winsock) { WSACleanup(); i.winsock = false; }
#endif
}
}
