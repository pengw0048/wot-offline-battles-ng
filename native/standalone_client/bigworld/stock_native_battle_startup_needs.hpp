#pragma once
// Calls startup makes into other subsystems that their headers do not declare yet.
// Integration moves each declaration into its owner's header.
#include "stock_native_battle.hpp"

namespace native_battle {
namespace vehicles {
void receiveManifest(Battle& b, const Value& message);
void receiveSnapshot(Battle& b, const Value& message, double now);
// The player's own vehicle entered presentation (BattleRuntime installs its record as local, not ready).
void installLocal(Battle& b, std::int64_t engine_id, const Value& local_state);
void markLocalReady(Battle& b);
// Teardown: stop every remote marker, destroy every remote entity, forget the table.
void quiesce(Battle& b);
}
namespace remote {
// Creates the remote factory for this round's space (native presentation on the visible client).
void start(Battle& b, PyObject* local_descriptor);
bool prewarmWrecksEnabled(Battle& b);
std::size_t wreckPrewarmPending(Battle& b);
void abandonWreckPrewarm(Battle& b);
}
namespace events {
void applyRules(Battle& b, const Value& rules);
void applyBattleResult(Battle& b, const Value& result);
bool hasPendingPresentation(const Battle& b);
}
namespace projectiles {
void start(Battle& b);
void observe(Battle& b, const Value& message);
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
void retire(Battle& b);
}
namespace local {
// The stock local Vehicle entered (server bridge prepareVehicleEnter): publish the canonical pose first.
void prepare(Battle& b, PyObject* entity);
// Bind the prepared pose to the stock model/providers, swinging and engine audio.
void attach(Battle& b);
void detach(Battle& b);
void installRamContactHook(Battle& b);
void restoreRamContactHook(Battle& b);
void bindArcadeCamera(Battle& b);
void publishRpm(Battle& b, double now, bool force);
void sendCurrent(Battle& b);
// LAN relay of one stock movement/aim/shoot mailbox; false when a shot was rejected locally.
p::Ref sendAvatarInput(Battle& b, std::int64_t vehicle_id, const char* kind, PyObject* payload);
bool changeVehicleSetting(Battle& b, std::int64_t vehicle_id, PyObject* code, PyObject* value);
void rejectNativeShotWait(Battle& b);
bool switchViewpoint(Battle& b, bool is_viewpoint, std::int64_t id);
void releasePostmortemVisibility(Battle& b);
void controlModeChanged(Battle& b, PyObject* mode);
// The local scalars a replay records (yaw, pitch, roll, speeds, flying flags, Siege aim pitch, drive turn/throttle).
p::Ref replayState(Battle& b);
void seedSiegeHud(Battle& b);
void enableSiegeHints(Battle& b);
void retire(Battle& b);
}
namespace visibility {
void start(Battle& b);
void enableExpert(Battle& b);
bool monitorDamagedDevices(Battle& b, std::int64_t vehicle_id);
void retire(Battle& b);
}
namespace world {
// Loads the destructible catalog and foliage for the map; installs the destructible compat layer.
void start(Battle& b);
void bindSpace(Battle& b, std::int64_t space_id);
void applyDestructibleState(Battle& b, const Value& events);
void disableDestructibles(Battle& b, const std::string& reason);
bool destructiblesEnabled(const Battle& b);
void reconcileTurrets(Battle& b, const Value& message);
void rememberRamSnapshot(Battle& b, const Value& snapshot);
void ackDestructibleContacts(Battle& b, const Value& snapshot);
void retire(Battle& b);
}
namespace local {
void ackRamContacts(Battle& b, const Value& snapshot);
}
namespace bots {
// Bot lineup: (team, slot) -> vehicle type, derived identically on every process.
bool prepareVehicleAssignments(Battle& b, PyObject* player_descriptor);
const std::map<std::pair<std::int64_t, std::int64_t>, std::string>& vehicleAssignments(const Battle& b);
// Creates the Bot runtime and publishes its opening manifest/messages.
void start(Battle& b);
void applySnapshot(Battle& b, const Value& snapshot);
void reconcileAuthority(Battle& b, const Value& player_id);
void retire(Battle& b);
}
namespace critical {
void retire(Battle& b);
}
namespace compat {
// compat.py battle-time hooks (nb-compat): the calls startup makes into the stock compatibility layer.
void install(Battle& b);
void uninstall(Battle& b);
void setBattleNetworkClient(Battle& b, PyObject* client);
void configureBattle(Battle& b, PyObject* gui_type, PyObject* bonus_type, const std::string& name, std::int64_t team, PyObject* arena_type_id);
bool retireCurrentPlayer(Battle& b);
void restoreLobbyAccount(Battle& b);
void disconnect(Battle& b);
void attachAvatarServer(Battle& b, PyObject* avatar, PyObject* server);
void setControlModeListener(Battle& b, PyObject* listener);
void clearTargetFocus(Battle& b);
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
namespace world {
// The #1513 fifth-argument ground filter for a column (callable or None) and the vertical motion probe.
p::Ref groundFilter(Battle& b, double x, double z);
// (position, normal) of the first hit between start and end, or None.
p::Ref collideDown(Battle& b, PyObject* start, PyObject* end, PyObject* ground_filter);
void startEnvironment(Battle& b);
}
namespace local {
void applyPhysicsTuning(Battle& b, const Value& tuning);
void start(Battle& b);
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
void receiveSnapshotMessage(Battle& b, PyObject* message, double now);
}
}
