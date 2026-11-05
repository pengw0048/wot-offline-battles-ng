#pragma once
#include "room_network.hpp"
#include "room_state.hpp"

namespace wot::room {
namespace strategy { struct GraphData; }
// Owned by the hidden worker process. Only the room thread calls Core; all
// public methods below are safe at the engine's frame boundary without a GIL.
class Host {
public:
    struct Status { bool ready = false, stopped = false; std::string error; };
    Host(const std::string& configuration_path, Codec, EffectiveParamsValidator);
    ~Host();
    Host(const Host&) = delete;
    Host& operator=(const Host&) = delete;
    Channel::Admission send(std::shared_ptr<const Value>, std::optional<std::string> key = {});
    std::vector<Channel::Entry> receive(std::size_t limit);
    Status status() const;
    std::pair<Channel::Metrics, Channel::Metrics> metrics() const;
    std::shared_ptr<const Value> resources() const;
    void configureRound(std::int64_t round, std::int64_t epoch, Value resources,
                        std::shared_ptr<const strategy::GraphData>);
    void stop();
private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
}
