#pragma once
// The hidden worker's native LAN link: its connection state and the authority messages it sends,
// validated, built and encoded in C++ (no Python LAN client).
#include "stock_native_battle.hpp"
#include <cstdint>
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
bool authority(const Battle& b);
std::optional<std::int64_t> roundId(const Battle& b);
std::optional<std::int64_t> authorityEpoch(const Battle& b);
// The worker's own team from its LAN welcome, when known.
std::optional<std::int64_t> team(const Battle& b);
// Both the worker and the server advertised this capability.
bool negotiated(const Battle& b, const std::string& capability);
// Dispatches every message already received before a frame's urgent work (player fire intents).
void drain(Battle& b);
}
}
