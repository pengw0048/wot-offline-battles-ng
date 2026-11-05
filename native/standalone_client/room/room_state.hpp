#pragma once
#include "room_value.hpp"
#include <functional>
#include <map>
#include <set>
#include <string>
#include <vector>

namespace wot::room {
using EffectiveParamsValidator = std::function<std::optional<Value>(const Value&)>;
struct Rejection : std::invalid_argument {
    std::string code;
    explicit Rejection(std::string reason) : std::invalid_argument(reason), code(std::move(reason)) {}
};
struct Sender {
    std::int64_t id = 0;
    bool worker = false;
};
struct Outcome {
    enum class Status { accepted, rejected, deferred, unhandled };
    Status status = Status::unhandled;
    std::string code, detail;
    std::vector<Value> replies;
    static Outcome accept() { Outcome o; o.status = Status::accepted; return o; }
    static Outcome reject(std::string code, std::string detail = {}) {
        Outcome o; o.status = Status::rejected; o.code = std::move(code);
        o.detail = std::move(detail); return o;
    }
};
// Only the room thread mutates State. Engine threads consume frozen messages.
struct State {
    std::int64_t round_id = 0, authority_epoch = 0, server_tick = 0;
    std::int64_t server_time_ms = 0, motion_time_us = 0, bot_state_time_us = 0;
    std::int64_t state_revision = 0, bot_state_revision = 0;
    std::int64_t bot_manifest_revision = 0, bot_order_revision = 0;
    std::int64_t destructible_revision = 0, projectile_revision = 0;
    double monotonic_seconds = 0.;
    std::optional<std::int64_t> bot_launch_clock_offset_us;
    std::string phase = "waiting", map_name;
    Value config = object(), resources = object();
    std::map<std::int64_t, Value> players, bots;
    Value bot_manifest = array(), bot_orders = array(), destructibles = array();
    Value rules = object(), battle_result, equipment_contracts = array();
    Value accepted_visibility = array(), latest_observation;
    Value team_commands = array();
    Value pending_ammo = array();
    Value pending_damage = array();
    bool bot_planner_dirty = false;
    // Target is a player actor ID, never a transport peer ID. The room maps
    // recipients to live connections when publishing an admitted operation.
    std::vector<std::pair<std::int64_t, Value>> pending_deliveries;
    std::map<std::int64_t, std::set<std::pair<std::string, std::int64_t>>> player_spotted;
    std::map<std::int64_t, std::set<std::pair<std::string, std::int64_t>>> bot_spotted;
    std::vector<Value> events;
    Value* actor(const std::string& kind, std::int64_t id) {
        auto& actors = kind == "bot" ? bots : players;
        const auto i = actors.find(id); return i == actors.end() ? nullptr : &i->second;
    }
    const Value* actor(const std::string& kind, std::int64_t id) const {
        const auto& actors = kind == "bot" ? bots : players;
        const auto i = actors.find(id); return i == actors.end() ? nullptr : &i->second;
    }
};
}
