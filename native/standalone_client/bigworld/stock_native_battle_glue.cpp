// Integration glue: small adapters where one subsystem's call and another's definition were written apart.
#if !defined(WOT_NATIVE_BATTLE_HOST_BUILD)
#include "pch.hpp"
#endif
#include "stock_native_battle_subsystems.hpp"
#include <cmath>
#include "stock_native_battle_startup_needs.hpp"
#include "stock_native_battle_vehicles_internal.hpp"
#include "stock_native_battle_pending.hpp"
#include "stock_native_battle_feedback_internal.hpp"
#include "stock_native_battle_world_imports.hpp"
#include "stock_native_battle_visibility_core.hpp"
#include "stock_native_battle_link.hpp"

namespace native_battle {
// The local vehicle state is created by startup::start; its owners attach on the first drive slice.
void local::start(Battle& b) {
    if (!b.local) b.local = std::make_unique<LocalVehicle>();
}
// The local gun's reload sound and timeline end with the round.
void gun::retire(Battle& b) {
    if (!b.gun) return;
    gun::retireSound(b);
    b.gun.reset();
}

namespace startup { void teardownAfterFailure(Battle& b); }
// The core's failure path ends the round through startup's teardown.
void teardownAfterFailure(Battle& b) { startup::teardownAfterFailure(b); }

// Vehicle creation reads startup's descriptors, placement and outfit season.
p::Ref vehicles::descriptorFor(Battle& b, const std::string& name) { return startup::descriptorFor(b, name); }
p::Ref vehicles::playerDescriptor(Battle& b, const Value& state) { return startup::playerDescriptor(b, state); }
std::pair<Point, double> vehicles::worldPose(Battle& b, const Value& state) { return startup::worldPose(b, state); }
std::optional<std::int64_t> vehicles::outfitSeason(Battle& b) { return startup::outfitSeason(b); }

// BattleRuntime._bot_authority_is_local
bool vehicles::botAuthorityIsLocal(Battle& b, const Value& player_id) {
    const auto& id = player_id.resolved();
    if (id.kind == Value::Kind::Null || !b.local_player) return false;
    if (id.kind == Value::Kind::Text) {
        try { return std::stoll(id.text) == b.local_player; }
        catch (...) { throw p::Error("bot authority identity is invalid"); }
    }
    if (!std::isfinite(id.numeric(NAN))) throw p::Error("bot authority identity is invalid");
    return std::int64_t(id.numeric()) == b.local_player;
}

// ai.planner.bot_initially_visible(team, own team, spotting enabled): an enemy model starts hidden.
bool vehicles::botInitiallyVisible(Battle& b, std::int64_t team) {
    if (b.worker) return team == link::team();
    auto own = py::optional(b.engine.client.get(), "team");
    if (own.get() == Py_None) return team == 1;
    return team == p::wide(own.get());
}

namespace vehicles {
bool showDamageInfo(Battle& b, std::int64_t engine_id, int damage_index, int extra_index, std::int64_t attacker);
}
// The local critical owner publishes with int indices.
bool vehicles::showDamageInfo(Battle& b, std::int64_t engine_id, int damage_index, int extra_index, std::int64_t attacker) {
    return showDamageInfo(b, engine_id, std::int64_t(damage_index), std::int64_t(extra_index), attacker);
}

// Visibility: inputs come from startup's accepted local snapshot; the query owner re-ingests on vehicle changes.
void visibility::start(Battle& b) {
    LocalVisibilityInputs inputs;
    inputs.arena_type = b.startup->arena_type;
    inputs.sixth_sense = b.startup->has_sixth_sense;
    inputs.expert = b.startup->has_expert;
    inputs.sixth_sense_delay = params::boosterSkillValue(b.startup->effective_params, "sixth_sense_delay", 3.0);
    configureLocal(b, std::move(inputs));
    bind(b);
    if (b.startup->client_ready_received && !b.feature("Expert damaged-device presentation", [&] { enableExpertVisibility(b); }))
        disableExpert(b);
}
void visibility::enableExpert(Battle& b) { enableExpertVisibility(b); }
void visibility::disableExpertPresentation(Battle& b) { disableExpert(b); }
bool visibility::monitorDamagedDevices(Battle& b, std::int64_t vehicle_id) {
    return monitorExpert(b, p::integer(vehicle_id).get(), "target_clear");
}
void visibility::vehicleChanged(Battle& b, Actor&) { if (b.visibility && !b.worker) ingest(b); }
void world::vehicleChanged(Battle& b, Actor&) { if (b.visibility && !b.worker) visibility::ingest(b); }
// spotting.SHOT_CAMOUFLAGE_SECONDS
double visibility::shotCamouflageSeconds() { return 0.75; }
// A viewpoint switch re-evaluates spotting at once (the pinned _next_spotting_time = 0).
void visibility::resetSpottingTimer(Battle& b) {
    if (b.visibility && b.live && !b.worker) b.feature("spotting", [&] { spotting(b, b.clock(), false); }, false);
}

// Battle hooks are native (compat::); lobby, account and garage stay on the Python OfflineCompatibility object.
void compat::setBattleNetworkClient(Battle& b, PyObject* client) { setNetworkClient(b, client); }
void compat::configureBattle(Battle& b, PyObject* gui_type, PyObject* bonus_type, const std::string& name, std::int64_t team, PyObject* arena_type_id) {
    configure(b, gui_type, bonus_type, p::text(name).get(), p::integer(team).get(), arena_type_id);
}
void compat::deactivateMap(Battle& b) { deactivate(b); }
void compat::synchroniseVehicleMarkerIdentity(Battle& b, std::int64_t vehicle_id) {
    if (!synchroniseMarkerIdentity(b, vehicle_id)) throw p::Error("#1513 vehicle marker identity did not synchronise");
}
void compat::assertVehicleMarkerIdentity(Battle& b, std::int64_t vehicle_id) {
    if (!assertMarkerIdentity(b, vehicle_id)) throw p::Error("#1513 vehicle marker identity does not match the player vehicle");
}
bool compat::retireCurrentPlayer(Battle& b) { return p::flag(py::call(b.engine.compatibility.get(), "retire_current_player").get()); }
void compat::restoreLobbyAccount(Battle& b) { py::call(b.engine.compatibility.get(), "restore_lobby_account"); }
void compat::disconnect(Battle& b) { py::call(b.engine.compatibility.get(), "disconnect"); }
p::Ref compat::garageState(Battle& b) { return py::call(b.engine.compatibility.get(), "garage_state"); }
std::pair<p::Ref, p::Ref> compat::dispatchAccountIntCommand(Battle& b, PyObject* command, PyObject* values) {
    auto result = py::call(b.engine.compatibility.get(), "dispatch_account_int_command", command, values);
    return {p::item(result.get(), 0), p::item(result.get(), 1)};
}

// BattleRuntime._echo_local_gun_angles: the rotator angle (or the given one) as the server's echoed aim.
bool vehicles::echoLocalGunAngles(Battle& b, std::optional<double> turret_yaw, std::optional<double> gun_pitch) {
    if (!b.engine.server || !b.local) throw p::Error("player gun-angle echo is not attached");
    auto rotator = py::optional(b.engine.avatar.get(), "gunRotator");
    if (rotator.get() == Py_None) throw p::Error("#1513 gun rotator is unavailable");
    auto angle = [&](const char* name) {
        auto value = py::optional(rotator.get(), name);
        if (value.get() == Py_None) throw p::Error(std::string("#1513 ") + name + " is unavailable for server echo");
        return p::scalar(value.get());
    };
    const double turret = turret_yaw ? *turret_yaw : angle("turretYaw");
    const double pitch = gun_pitch ? *gun_pitch : angle("gunPitch");
    const double hull = b.local->yaw;
    startup::binding::updateVehicleAim(b, startup::server::vehicleId(b), hull, hull + turret, pitch);
    return true;
}

// BattleRuntime._pose_motion_time_us/_pose_motion_local_time: the latest authority motion time and its local clock.
std::optional<std::pair<std::int64_t, double>> vehicles::poseMotionAnchor(const Battle& b) {
    if (!b.projectiles || !b.projectiles->motion_time_us) return std::nullopt;
    return std::make_pair(*b.projectiles->motion_time_us, b.projectiles->motion_local_time);
}
}
