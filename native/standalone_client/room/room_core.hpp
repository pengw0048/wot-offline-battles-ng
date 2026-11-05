#pragma once
#include "room_rules.hpp"
#include "room_strategy.hpp"
#include "room_publication_clock.hpp"
#include <deque>
#include <functional>
#include <random>
#include <set>

namespace wot::room {
namespace strategy { struct GraphData; }
class Inputs;
// Single-threaded canonical room owner. Networking and the engine communicate
// with it only through immutable messages; neither may mutate State directly.
class Core {
public:
    using Peer = std::uint64_t;
    static constexpr Peer worker_peer = 0;
    using Publish = std::function<bool(Peer, std::shared_ptr<const Value>, std::optional<std::string>)>;
    using Disconnect = std::function<void(Peer, bool)>;
    using Resync = std::function<void(Peer, std::int64_t)>;
    Core(Value configuration, Value resources, Publish, Disconnect, Resync, EffectiveParamsValidator);
    ~Core();
    void receive(Peer, const Value&, const std::string& address = {});
    void disconnected(Peer);
    void advance(double elapsed, std::int64_t clock_us);
    void clock(std::int64_t clock_us);
    bool flushPending();
    void stop();
    void configureRound(std::int64_t round, std::int64_t epoch, Value resources,
                        std::shared_ptr<const strategy::GraphData> graph);
    State& state() { return state_; }
    bool workerReady() const { return worker_ready_; }
    Rules& rules() { return rules_; }
    void logger(std::function<void(const std::string&)> callback) { log_ = std::move(callback); }
private:
    friend struct LobbyProbe;
    struct Delivery { std::shared_ptr<const Value> message; std::optional<std::string> key; };
    struct Connection {
        Sender sender;
        bool probe = false, ready = false;
        std::set<std::string> capabilities;
        std::deque<Delivery> pending;
        std::int64_t ready_round = 0, manifest_sent = -1, order_sent = -1, destructible_sent = -1;
    };
    State state_;
    Rules rules_;
    Planner planner_;
    std::unique_ptr<Inputs> inputs_;
    std::map<Peer, Connection> peers_;
    Publish publish_;
    Disconnect disconnect_;
    Resync resync_;
    std::mt19937 random_;
    std::int64_t next_player_ = 1, host_player_ = 0;
    std::int64_t prebattle_us_ = 0, battle_started_us_ = 0;
    bool worker_ready_ = false, live_published_ = false, stopped_ = false, strategy_ready_ = false;
    EffectiveParamsValidator parameters_;
    PublicationClock publication_clock_;
    std::int64_t clock_us_ = 0, motion_origin_us_ = 0;
    std::function<void(const std::string&)> log_;
    Value worker_manifest_fingerprint_;
    std::int64_t simulation_progress_frame_seq_ = -1;
    std::optional<std::int64_t> result_reset_tick_;
    std::int64_t next_planner_tick_ = 0, last_receipt_delivery_tick_ = -1;
    struct FrozenSection {
        std::int64_t revision = -1, round = -1, epoch = -1;
        Value value;
    };
    mutable FrozenSection manifest_cache_, orders_cache_, destructibles_cache_;
    std::map<std::int64_t, Value> catalogs_;
    Value roster_ = array(), detached_turrets_ = array();
    void hello(Peer, const Value&, const std::string& address);
    void command(Peer, Connection&, const Value&);
    void overlay(Peer, const Value&);
    void worker(const Value&);
    void start(Peer, const Value&);
    void ready(Peer, const Value&);
    void resetRound();
    Value lobby(const char* type = "roster") const;
    Value welcome(const Connection&) const;
    Value timing() const;
    Value publicActor(const Value&, bool bot, bool full = false) const;
    Value snapshot() const;
    Value teamSizes() const;
    void emit(Peer, Value, std::optional<std::string> key = {});
    void broadcast(Value, std::optional<std::string> key = {});
    void error(Peer, const std::string& code, const std::string& detail);
    void flush(Connection&, Peer);
    void publishState();
    void drainDeliveries();
    void drainFacts();
    void assignSlots(bool starting = false);
    void makeRoster();
    void selectVehicle(Peer, Connection&, const Value&);
    Value admitPlayer(const Value& message, std::int64_t id, const std::string& address);
    bool lobbyCommand(Peer, Connection&, const Value&);
    std::vector<int> slots(int team) const;
};
}
