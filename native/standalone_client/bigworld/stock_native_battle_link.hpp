#pragma once
// The hidden worker's native LAN link: its connection state and the authority messages it sends,
// validated, built and encoded in C++ (no Python LAN client).
#include "stock_native_battle.hpp"
#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <string>

namespace native_battle {
namespace link {
// One outgoing worker message, shaped as the former LAN client sender's arguments plus its "type":
//   bot_manifest {bots, player_collision_profiles?}
//   bot_state {rows, sample_time_us?, source_batch_horizon_us?, human_ram_armors?, edge_sample_time_us,
//              edge_revision, detached_turrets?, push_contacts?}   (a newer unsent state with the same edge replaces it)
//   bot_observation {contacts, affordances, radio_links?, player_vision_ranges?}
//   bot_ram {bot_id, target_kind, target_id, ram_seq, damage_to_bot, damage_to_target, ram_contact_player_id?,
//            ram_contact_seq?, contact_positions?, track_contacts_bot?, track_contacts_target?, track_impact_speed?}
//   rules_state {bases}            battle_result {winner, reason, base_team}
//   destructible {event}           player_destructible_contact_result {player_id, contact_seq, accepted, token}
//   player_environment {observations, sample_seq}
//   projectile_launch / projectile_progress / projectile_resolve / projectile_ricochet
//       {the send_projectile_<kind> parameters by name}
// False when the link refused it: not the Bot authority, an invalid payload, or the transport ended or is full.
bool send(Battle& b, const Value& message);

// The worker owns Bot authority for the current round (welcome/roster authority id and phase).
bool authority();
inline bool authority(const Battle&) { return authority(); }
std::optional<std::int64_t> roundId(const Battle& b);
std::optional<std::int64_t> authorityEpoch(const Battle& b);
// The worker's own team from its LAN welcome, when known.
std::optional<std::int64_t> team(const Battle& b);
// Both the worker and the server advertised this capability.
bool negotiated(const Battle& b, const std::string& capability);
// Dispatches every message already received (between the Bot control slice and shell advancement).
void drain(Battle& b);

// The worker session's connection. The handler receives each admitted message: welcome, roster, battle_start,
// battle_live, snapshot, events, bot_observation, player_destructible_contact, and error when the link ends.
using Handler = std::function<void(const std::string& kind, const std::shared_ptr<const Value>& message)>;
bool connect(PyObject* bigworld, const std::string& host, int port, Handler handler);
// Sends leave and closes; `flush` lets queued frames drain briefly first. Idempotent.
void disconnect(bool flush);
bool connected();
std::string phase();
std::optional<std::int64_t> botAuthorityId();
// The session gave up Bot authority for this connection.
void dropAuthority();
// The worker carrier's team (the first real player's).
std::int64_t team();
// The newest accepted snapshot of the current round's lineage.
std::shared_ptr<const Value> lastSnapshot();
// The link's receive clock in seconds: `_client_received_time` stamps use it.
double now();
std::optional<double> rttMs();
std::optional<double> minimumRttMs();
// Server battle timing projected onto now: seconds until combat starts and ends, its duration and phase.
struct Timing {
    std::optional<double> countdown, remaining, duration;
    std::string phase;
};
Timing timing();
// One hidden-render frame interval; worker_pong reports 1000 / the windowed frame rate.
void recordFrame(double seconds);
}
}
