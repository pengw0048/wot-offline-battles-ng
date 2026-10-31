#pragma once
// Calls startup makes into other subsystems that their headers do not declare yet.
// Integration moves each declaration into its owner's header.
#include "stock_native_battle_subsystems.hpp"
#include "stock_native_battle.hpp"

namespace native_battle {
namespace vehicles {
// The player's own vehicle entered presentation (BattleRuntime installs its record as local, not ready).
// Teardown: stop every remote marker, destroy every remote entity, forget the table.
}
namespace remote {
// Creates the remote factory for this round's space (native presentation on the visible client).
void start(Battle& b, PyObject* local_descriptor);
bool prewarmWrecksEnabled(Battle& b);
std::size_t wreckPrewarmPending(Battle& b);
void abandonWreckPrewarm(Battle& b);
}
namespace events {
}
namespace projectiles {
void start(Battle& b);
void reconcile(Battle& b, const Value& snapshot);
void retire(Battle& b);
}
namespace gun {
// Creates the local gun from the descriptor and the frozen garage loadout (visible client only).
void start(Battle& b, PyObject* descriptor);
void restoreEquipment(Battle& b, const Value& snapshot, bool present);
void setLocked(Battle& b, bool locked);
void publishReload(Battle& b, bool force);
// The gun row a replay records: ammo, shot_index, clip, reload_time, reload_duration, load_started, pending_index, dispersion.
p::Ref replayState(Battle& b);
void applyReplayLocal(Battle& b, PyObject* message);
}
namespace visibility {
void start(Battle& b);
void enableExpert(Battle& b);
bool monitorDamagedDevices(Battle& b, std::int64_t vehicle_id);
}
namespace bots {
// Bot lineup: (team, slot) -> vehicle type, derived identically on every process.
bool prepareVehicleAssignments(Battle& b, PyObject* player_descriptor);
const std::map<std::pair<std::int64_t, std::int64_t>, std::string>& vehicleAssignments(const Battle& b);
// Creates the Bot runtime and publishes its opening manifest/messages.
void start(Battle& b);
void applySnapshot(Battle& b, const Value& snapshot);
void reconcileAuthority(Battle& b, const Value& player_id);
}
namespace critical {
}
namespace compat {
// compat.py battle-time hooks (nb-compat): the calls startup makes into the stock compatibility layer.
void setBattleNetworkClient(Battle& b, PyObject* client);
void configureBattle(Battle& b, PyObject* gui_type, PyObject* bonus_type, const std::string& name, std::int64_t team, PyObject* arena_type_id);
bool retireCurrentPlayer(Battle& b);
void restoreLobbyAccount(Battle& b);
void disconnect(Battle& b);
void attachAvatarServer(Battle& b, PyObject* avatar, PyObject* server);
void setControlModeListener(Battle& b, PyObject* listener);
void deactivateMap(Battle& b);
void synchroniseVehicleMarkerIdentity(Battle& b, std::int64_t vehicle_id);
void assertVehicleMarkerIdentity(Battle& b, std::int64_t vehicle_id);
p::Ref garageState(Battle& b);
std::pair<p::Ref, p::Ref> dispatchAccountIntCommand(Battle& b, PyObject* command, PyObject* values);
}
namespace params {
// effective_params.py / loadout.py / equipment_mechanics.py (nb-params).
// effective_params.canonical(raw): the canonical local snapshot, or Null when unusable.
Value canonical(Battle& b, const Value& raw);
std::int64_t livingSkillCount(Battle& b, const Value& snapshot, const char* skill, PyObject* crew_critical);
double boosterSkillValue(Battle& b, const Value& snapshot, const char* name, double fallback);
}
namespace projectiles {
void applyHeTuning(Battle& b, const Value& tuning);
}
namespace gun {
// Keeps the 10 Hz gun clock scheduled (starts it when it is not).
void ensureTicking(Battle& b);
}
namespace critical {
// The local vehicle's physical crew-KO set ({'crew_ko': [...]} or its critical state).
p::Ref localCrewCritical(Battle& b);
}
namespace visibility {
void disableExpertPresentation(Battle& b);
}
namespace vehicles {
// The raw LAN snapshot: the replica reads its receive-thread `_native_snapshot` capsule.
}
// Integration glue (stock_native_battle_glue.cpp).
namespace local { void start(Battle& b); }
namespace gun { void retire(Battle& b); }
}
