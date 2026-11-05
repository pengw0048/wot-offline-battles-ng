#pragma once
#include "room_channel.hpp"
#include <functional>
#include <memory>

namespace wot::room {
// The room uses the same generated binary codec as visible clients. Injecting
// the codec keeps listener lifecycle tests independent of the game interpreter.
struct Codec {
    std::function<std::size_t(const unsigned char*, std::size_t)> frameSize;
    std::function<bool(const unsigned char*, std::size_t, Value&, std::string&)> decode;
    std::function<bool(const Value&, std::string&, std::string&)> encode;
};
class Network {
public:
    using Peer = std::uint64_t;
    struct Received { Peer peer; std::shared_ptr<const Value> message; std::string address; };
    struct Closed { Peer peer; std::string reason; };
    struct Bound { std::string address; unsigned port = 0; };
    explicit Network(Codec);
    ~Network();
    Network(const Network&) = delete;
    Network& operator=(const Network&) = delete;
    // Binds before returning; a failed bind never publishes readiness.
    Bound start(const std::string& address, unsigned port);
    std::vector<Received> take(std::size_t per_peer_limit);
    std::vector<Closed> takeClosed();
    Channel::Admission offer(Peer, std::shared_ptr<const Value>, std::optional<std::string> key = {});
    void setPlayer(Peer, std::int64_t player_id);
    void resync(Peer);
    // Preserve queued replies before EOF (probe/leave); immediate close on error.
    void disconnect(Peer, bool flush);
    void stop();
    std::string diagnostic() const;
    bool running() const;
private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
}
