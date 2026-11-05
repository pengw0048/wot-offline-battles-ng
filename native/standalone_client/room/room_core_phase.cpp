#include "room_core.hpp"
#include "room_inputs.hpp"
#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <cctype>
#include <limits>
#include <iomanip>
#include <locale>
#include <sstream>

namespace wot::room {
namespace {
double finiteCoordinate(const Value& raw) {
    const auto& v = raw.resolved();
    if (v.kind == Value::Kind::Integer || v.kind == Value::Kind::Number) return v.numeric();
    if (v.kind == Value::Kind::Boolean) return v.boolean ? 1. : 0.;
    if (v.kind == Value::Kind::Text) {
        char* end = nullptr; const double value = std::strtod(v.text.c_str(), &end);
        while (end && *end && std::isspace(static_cast<unsigned char>(*end))) ++end;
        if (end && end != v.text.c_str() && !*end) return value;
    }
    return std::numeric_limits<double>::quiet_NaN();
}
double baseCoordinate(double value) {
    std::ostringstream out; out.imbue(std::locale::classic());
    out << std::fixed << std::setprecision(3) << std::clamp(value, -2000., 2000.);
    return std::stod(out.str());
}
Value captureBases(const Value& raw) {
    auto result = object();
    if (raw.resolved().kind != Value::Kind::Object) return result;
    for (const char* team : {"1", "2"}) {
        auto points = array(); const auto& values = rows(raw.get(team));
        for (std::size_t i = 0; i < std::min<std::size_t>(4, values.size()); ++i) {
            const auto& value = values[i]; const bool named = value.resolved().kind == Value::Kind::Object;
            const auto& pair = rows(value);
            if (!named && pair.size() < 2) continue;
            const auto x = finiteCoordinate(named ? value.get("x") : pair[0]);
            const auto z = finiteCoordinate(named ? value.get("z") : pair[1]);
            if (!std::isfinite(x) || !std::isfinite(z)) continue;
            auto point = named && has(value, "radius") ? object() : array();
            if (point.kind == Value::Kind::Object) {
                const auto radius = finiteCoordinate(value.get("radius"));
                if (radius <= 0. || !std::isfinite(radius * radius)) continue;
                point.object["x"] = number(baseCoordinate(x)); point.object["z"] = number(baseCoordinate(z));
                point.object["radius"] = number(radius);
            } else point.array = {number(baseCoordinate(x)), number(baseCoordinate(z))};
            points.array.push_back(std::move(point));
        }
        if (!points.array.empty()) result.object[team] = std::move(points);
    }
    return result.object.size() == 2 ? result : object();
}
}

void Core::clock(std::int64_t microseconds) {
    clock_us_ = std::max(clock_us_, microseconds);
    state_.monotonic_seconds = double(clock_us_) / 1000000.;
    state_.motion_time_us = publication_clock_.motionTime(clock_us_ - motion_origin_us_);
}
Value Core::timing() const {
    const double hz = state_.resources.get("timing").get("tick_hz").numeric();
    const auto prebattle = std::int64_t(std::llround(state_.config.get("prebattle_seconds").numeric() * hz));
    const auto duration = std::int64_t(std::llround(state_.config.get("battle_duration_seconds").numeric() * hz));
    const auto tick = std::max<std::int64_t>(0, state_.server_tick), total = prebattle + duration;
    const auto phase = state_.phase == "loading" ? "loading" :
        (state_.battle_result.resolved().kind != Value::Kind::Null || tick >= total) ? "finished" :
        tick < prebattle ? "prebattle" : "battle";
    auto result = object(); result.object["phase"] = text(phase);
    result.object["start_in_ms"] = integer(std::max<std::int64_t>(0, std::llround(1000. * double(prebattle - tick) / hz)));
    result.object["remaining_ms"] = integer(std::max<std::int64_t>(0, std::llround(1000. * double(total - std::max(tick, prebattle)) / hz)));
    result.object["duration_ms"] = integer(std::llround(1000. * state_.config.get("battle_duration_seconds").numeric()));
    return result;
}
void Core::ready(Peer peer, const Value& message) {
    const auto found = peers_.find(peer);
    if (found == peers_.end() || found->second.probe || state_.phase != "loading") return;
    const auto& round = message.get("round_id").resolved();
    if (round.kind != Value::Kind::Integer || round.integer != state_.round_id) return;
    if (found->second.sender.worker && has(message, "bases")) {
        auto bases = captureBases(message.get("bases"));
        if (bases.truth()) state_.resources.object["capture_bases"] = std::move(bases);
    }
    found->second.ready_round = state_.round_id;
    if (log_) log_("NATIVE ROOM ready round=" + std::to_string(state_.round_id) +
        " peer=" + std::to_string(peer) + " worker=" + std::to_string(found->second.sender.worker) +
        " manifest=" + std::to_string(worker_manifest_fingerprint_.resolved().kind != Value::Kind::Null) +
        " strategy=" + std::to_string(strategy_ready_));
    if (!roster_.array.empty() && !strategy_ready_) return;
    for (const auto& connection : peers_) {
        if (connection.second.probe) continue;
        if (connection.second.sender.worker) {
            if (connection.second.ready_round != state_.round_id) return;
        } else {
            const auto* actor = state_.actor("player", connection.second.sender.id);
            if (actor && actor->get("connected").truth() && actor->get("participating").truth() &&
                connection.second.ready_round != state_.round_id) return;
        }
    }
    if (!worker_ready_ || worker_manifest_fingerprint_.resolved().kind == Value::Kind::Null) return;
    state_.phase = "battle"; state_.server_tick = 0; state_.server_time_ms = 0; ++state_.state_revision;
    battle_started_us_ = clock_us_;
    auto live = object(); live.object["type"] = text("battle_live");
    live.object["protocol"] = state_.resources.get("protocol"); live.object["client_build"] = state_.resources.get("client_build");
    live.object["round_id"] = integer(state_.round_id); live.object["server_tick"] = integer(0);
    live.object["state_revision"] = integer(state_.state_revision); live.object["authority_epoch"] = integer(state_.authority_epoch);
    live.object["bot_authority_id"] = integer(-1); live.object["server_time_ms"] = integer(0);
    live.object["countdown_seconds"] = state_.config.get("prebattle_seconds");
    live.object["battle_duration_seconds"] = state_.config.get("battle_duration_seconds");
    live.object["timing"] = timing(); live.object["worker_status"] = text("connected");
    live.object["worker_failure_reason"] = text("");
    // The live barrier is the complete tick-zero transaction. No tick state
    // may overtake it, including the internal engine connection.
    broadcast(std::move(live)); live_published_ = true;
}
void Core::configureRound(std::int64_t round, std::int64_t epoch, Value resources,
                          std::shared_ptr<const strategy::GraphData> graph) {
    if (round != state_.round_id || epoch != state_.authority_epoch ||
        (state_.phase != "loading" && state_.phase != "battle")) return;
    const auto& strategy = resources.get("strategy").resolved();
    if (strategy.kind != Value::Kind::Object) throw std::invalid_argument("Missing cold room strategy resources");
    if (resources.reference) resources = resources.resolved();
    for (auto& field : resources.object) state_.resources.object[field.first] = std::move(field.second);
    planner_.setGraph(std::move(graph)); strategy_ready_ = true;
    if (log_) log_("NATIVE ROOM resources configured round=" + std::to_string(round) + " epoch=" + std::to_string(epoch));
    const auto found = peers_.find(worker_peer);
    if (found != peers_.end() && found->second.ready_round == state_.round_id) {
        auto message = object(); message.object["round_id"] = integer(round); ready(worker_peer, message);
    }
}
}
