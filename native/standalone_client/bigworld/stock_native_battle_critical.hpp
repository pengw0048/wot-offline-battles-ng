#pragma once
// The #1513 device and crew damage model of every vehicle, and the local player's critical lineage.
#include "stock_native_battle.hpp"
#include "stock_native_bot_internal_geometry.hpp"
#include <map>
#include <memory>
#include <set>
#include <string>
#include <vector>

namespace native_battle {
// One vehicle's accepted module and crew damage.
struct VehicleDamage {
    std::map<std::string, double> devices;          // module HP by extra name ("engineHealth", ...)
    std::set<std::string> destroyed, critical, crew_ko;
    bool fire = false, ammo_rack_death = false;
    std::set<std::string> crew_impaired;            // base roles with a knocked-out member
    bool tracked() const { return destroyed.count("leftTrackHealth") || destroyed.count("rightTrackHealth"); }
    bool engineDead() const { return destroyed.count("engineHealth") != 0; }
    bool gunDestroyed() const { return destroyed.count("gunHealth") != 0; }
    bool turretLocked() const { return destroyed.count("turretRotatorHealth") != 0; }
};

struct LocalCriticalOwner;

struct Critical {
    Critical();
    ~Critical();
    std::map<std::int64_t, VehicleDamage> vehicles;   // by stock entity id
    // The local player's critical lineage: server CAS revisions and the pending track-repair checkpoint.
    std::int64_t base_revision = 0, next_sequence = 0, server_revision = 0;
    bool owned = false;
    double next_report_time = 0.;
    Value report;                                     // {tracks, critical_base_revision, critical_seq} or Null
    std::unique_ptr<LocalCriticalOwner> local;
};

namespace critical {
// Installs one canonical critical payload on a vehicle; returns its presentation events
// (an Array of {kind, name, state, old_state, cause} objects for the stock UI callbacks).
Value applyPayload(Battle& b, PyObject* entity, const Value& payload);
// Every module and crew member dead for one death cause; the canonical payload, or Null when nothing changed.
Value applyDeath(Battle& b, PyObject* entity, const char* cause);
// The accepted damage of a vehicle, or null before its first payload.
const VehicleDamage* damage(const Battle& b, std::int64_t engine_id);
// crew x module x stun multiplier of one stat ("reload", "aim_time", "dispersion", "turret_speed", "mobility",
// "vision", "signal") for a stock entity; >1 lengthens a time, <1 slows a speed or shrinks a range.
double statFactor(Battle& b, PyObject* entity, const std::string& stat);
// The local player's stat factor, with the accepted crew's conditional effects (adrenaline, armorer).
double localStatFactor(Battle& b, PyObject* entity, const std::string& stat, const Value* effective_params);
double moduleFactor(Battle& b, PyObject* entity, const std::string& stat);
double clampVisionFactor(double factor);

// The local critical lineage (visible client and replay).
// Applies one player's critical revision contract; false when the revision is stale. Rewrites
// `canonical` so the owning client's tracks survive while its repair checkpoint is unacknowledged.
bool reconcileAuthority(Battle& b, Actor& vehicle, const Value& source, Value* canonical);
bool acknowledgeLocalDamageReport(Battle& b, std::int64_t base_revision, std::int64_t ack_seq, std::int64_t server_revision);
// Repair timers, owner-CAS track repair and the stock repair HUD of the local vehicle.
void tick(Battle& b, double dt);
void retire(Battle& b);

// The interior hit layout of one installed configuration (internal_hit_layouts.build_layout via
// critical_damage._offh_internal_layout): the validated layout, else an empty invalid one. Cached per configuration.
wot::offline_bot_step::InternalLayout internalLayout(PyObject* descriptor);
// Builds the layout once every component hit-tester bound exists; false while one is still loading.
bool prewarmLayout(PyObject* descriptor);
}
}
