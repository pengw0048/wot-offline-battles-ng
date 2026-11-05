#include "room_replica.hpp"
#include <set>
#include <array>
#include <algorithm>

namespace wot::room {
namespace {
const std::set<std::string> player_motion = {"x", "y", "z", "yaw", "pitch", "roll", "up_cosine", "aim_yaw",
    "gun_pitch", "forward", "turn", "speed", "hydraulic_body"};
const std::set<std::string> player_owner = {"input_seq", "landing_observation_seq", "input_processed_seq",
    "equipment_intent_seq", "equipment_intent_result", "tank_pushes", "turret_pushes", "crew_receipt_id",
    "ram_contact_admitted_seq", "ram_contact_resolved_seq", "destructible_contact_admitted_seq",
    "destructible_contact_resolved_seq", "gun_checkpoint_seq", "gun_checkpoint", "ram_contact", "ram_contacts",
    "destructible_contacts", "destructible_contact_resolved_seqs", "destructible_contact_rejected_seqs"};
const std::set<std::string> bot_motion = {"x", "y", "z", "yaw", "pitch", "roll", "aim_yaw", "gun_pitch", "speed",
    "push_x", "push_z", "push_yaw", "movement_dir", "rotation_dir", "airborne", "service_brake"};
const std::set<std::string> countdown = {"siege_time_left_ms", "reload_time", "burst_time_left", "combat_fire_elapsed", "combat_fire_timer"};
const std::set<std::string> equipment_countdown = {"cooldownTimeLeft", "autoPendingElapsed", "aiPendingElapsed"};
Value signature(const Value& state) {
    auto out = object();
    for (const auto& field : state.resolved().object) if (!countdown.count(field.first)) out.object.emplace(field.first, field.second);
    const auto& equipment = out.get("equipment_states").resolved();
    if (equipment.kind == Value::Kind::Array) {
        auto values = array();
        for (const auto& row : equipment.array) {
            if (row.resolved().kind != Value::Kind::Object) { values.array.push_back(row); continue; }
            auto value = object();
            for (const auto& field : row.resolved().object) if (!equipment_countdown.count(field.first)) value.object.emplace(field.first, field.second);
            values.array.push_back(std::move(value));
        }
        out.object["equipment_states"] = std::move(values);
    }
    return out;
}
}
ReplicaEncoder::Prepared ReplicaEncoder::prepare(const Value& snapshot, std::int64_t own, bool full) const {
    const auto round = intField(snapshot, "round_id", -1);
    const auto epoch = intField(snapshot, "authority_epoch", -1);
    full = full || !baseline_ || baseline_->round != round || baseline_->epoch != epoch || baseline_->own != own;
    auto next = std::make_shared<Baseline>(); next->round = round; next->epoch = epoch; next->own = own;
    auto out = object();
    for (const auto& field : snapshot.resolved().object) {
        if (field.first != "players" && field.first != "bots" && field.first != "bot_equipment_contracts" && field.first != "_room_sections") out.object.emplace(field.first, field.second);
    }
    out.object["type"] = text("replica_snapshot"); out.object["full"] = boolean(full);
    const auto& metadata = snapshot.get("_room_sections");
    if (metadata.resolved().kind == Value::Kind::Object) {
        const auto tick = intField(snapshot, "server_tick");
        for (const auto& names : {std::array<const char*, 3>{"bot_manifest", "manifest_revision", "manifest_refresh_ticks"},
                                std::array<const char*, 3>{"bot_orders", "order_revision", "order_refresh_ticks"},
                                std::array<const char*, 3>{"destructibles", "destructible_revision", "destructible_refresh_ticks"}}) {
            if (!has(snapshot, names[0])) continue;
            const auto revision = intField(metadata, names[1], -1);
            const auto refresh = std::max<std::int64_t>(1, intField(metadata, names[2], 300));
            const auto old = baseline_ ? baseline_->sections.find(names[0]) : next->sections.end();
            const bool known = baseline_ && old != baseline_->sections.end();
            // Orders may change shell choice without a new tactical revision.
            const bool changed = !known || old->second.revision != revision || old->second.value != snapshot.get(names[0]);
            if (full || changed || tick - old->second.tick >= refresh) {
                next->sections[names[0]] = {revision, tick, snapshot.get(names[0])};
            } else {
                next->sections[names[0]] = old->second;
                out.object.erase(names[0]);
            }
        }
    }

    const auto& contracts = snapshot.get("bot_equipment_contracts");
    const bool contracts_changed = has(snapshot, "bot_equipment_contracts") && (full || contracts != baseline_->contracts);
    next->contracts = has(snapshot, "bot_equipment_contracts") ? contracts : (full ? Value{} : baseline_->contracts);
    if (contracts_changed) out.object["bot_equipment_contracts"] = contracts;
    for (bool bot : {false, true}) {
        const auto& names = bot ? bot_motion : player_motion;
        auto& stored = bot ? next->bots : next->players;
        const auto* previous = full ? nullptr : (bot ? &baseline_->bots : &baseline_->players);
        auto motion = array(), states = array(), removed = array();
        for (const auto& row : rows(snapshot.get(bot ? "bots" : "players"))) {
            const auto id = exactInteger(row.get("id"));
            if (!id) throw std::invalid_argument("Replica actor has no integer id");
            if (!bot && *id == own) { out.object["own"] = row; continue; }
            auto moved = object(), state = object(); moved.object["id"] = integer(*id);
            for (const auto& name : names) {
                if (name != "hydraulic_body" && !has(row, name)) throw std::invalid_argument("Replica actor lacks motion field " + name);
                moved.object[name] = row.get(name);
            }
            for (const auto& field : row.resolved().object) {
                if (!names.count(field.first) && (bot || !player_owner.count(field.first))) state.object.emplace(field.first, field.second);
            }
            auto sig = signature(state);
            const auto old = previous ? previous->find(*id) : stored.end();
            const bool known = previous && old != previous->end();
            if (!known || old->second.motion != moved) motion.array.push_back(moved);
            if (!known || (bot && contracts_changed) || old->second.signature != sig) states.array.push_back(std::move(state));
            stored.emplace(*id, Actor{std::move(moved), std::move(sig)});
        }
        if (previous) for (const auto& row : *previous) if (!stored.count(row.first)) removed.array.push_back(integer(row.first));
        out.object[bot ? "bot_motion" : "player_motion"] = std::move(motion);
        out.object[bot ? "bot_states" : "player_states"] = std::move(states);
        out.object[bot ? "removed_bots" : "removed_players"] = std::move(removed);
    }
    return {std::move(out), std::move(next)};
}
}
