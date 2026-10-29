#pragma once
#include "stock_native_bot_combat.hpp"
#include "stock_native_bot_weapon.hpp"
#include "stock_native_bot_armor_law.hpp"
struct _object;

namespace wot::offline_bot_step {
// Python objects are consumed synchronously at bootstrap; none are retained.
// A caller owns the native Store and installs this snapshot only after every
// row validates. This adapter must never be called from a physical step.
struct CombatSnapshot {
    CombatState state;
    std::int64_t fire_sequence=0;
    CombatLedger ledger;
    SiegeState siege;
    SiegeConfig siege_config;
    std::vector<EquipmentState> equipment;
    offline_simulation::weapons::GunConfig gun_config;
    offline_simulation::weapons::AmmoState ammo;
    offline_simulation::weapons::BurstState burst;
    std::optional<offline_simulation::weapons::GunState> gun;
    offline_simulation::weapons::AimConfig aim_config;
    offline_simulation::weapons::AimState aim;
    bool valid_pitch=false;
    bool limited_yaw=false;double minimum_yaw=0,maximum_yaw=0;std::optional<double> static_yaw;
    std::optional<GunMount> gun_mount;
    std::optional<Point> water_sensor;
    HydraulicConfig hydraulic;
    GunnerConfig gunner;
    std::vector<std::optional<DirectShot>> shells;
    std::vector<stock_native_bot_armor::Shell> armor_shells;
    std::vector<AimSample> target_samples;
    std::map<std::string,double> initial_critical_factors;
    double initial_repair_factor=0, base_repair_factor=0, repair_speed_factor=1;
    double crew_factor=1, gun_rotation_factor=1;
};
struct CombatModeSnapshot {
    offline_simulation::weapons::GunConfig gun_config;
    offline_simulation::weapons::AimConfig aim_config;
    bool valid_pitch=false;
    bool limited_yaw=false;double minimum_yaw=0,maximum_yaw=0;std::optional<double> static_yaw;
    std::optional<GunMount> gun_mount;
    std::optional<Point> water_sensor;
    HydraulicConfig hydraulic;
    std::vector<std::optional<DirectShot>> shells;
    std::vector<stock_native_bot_armor::Shell> armor_shells;
    std::vector<AimSample> target_samples;
    double base_repair_factor=0,repair_speed_factor=1,crew_factor=1,gun_rotation_factor=1;
};
// Returns false with a Python exception and leaves output untouched on error.
bool load_combat_snapshot(_object* runtime,_object* state,_object* descriptor,
                          _object* profile,CombatSnapshot& output);
bool load_combat_mode(_object* runtime,_object* state,_object* descriptor,
                      _object* profile,CombatModeSnapshot& output);
bool load_aim_samples(_object* descriptor,std::vector<AimSample>& output);
bool readServerCombat(_object* raw,_object* server_tick,const CriticalState& metadata,ServerCombat& output);
bool adopt_combat_mode(offline_simulation::weapons::Store&,offline_simulation::ActorKey,
                       const CombatModeSnapshot&);
// New owned reference containing only native-owned publication fields.
_object* combat_snapshot_value(const CombatState&,const SiegeState&,
    const offline_simulation::weapons::ActorState&,
    const std::vector<CriticalEvent>&);
} // namespace wot::offline_bot_step
