#pragma once
#include "stock_native_bot_artillery.hpp"
#include "stock_native_bot_combat.hpp"
#include "stock_native_bot_weapon.hpp"

namespace wot::offline_bot_step {
struct EnvironmentTotals;
struct DirectAimToken {
    std::uint64_t record_revision=0,descriptor_revision=0;
    std::size_t sample=0;
    bool operator==(const DirectAimToken& b)const {
        return record_revision==b.record_revision && descriptor_revision==b.descriptor_revision && sample==b.sample;
    }
};
// Per-actor memories are bound to one authority/round, never reconstructed
// from Python while the slice is running.
struct CombatActorRuntime {
    GunnerState gunner;
    BallisticCache ballistic;
    std::optional<DirectAimToken> direct_aim_token;
    ArtilleryIntentState artillery;
    std::optional<ArcCandidate> artillery_solution,artillery_continuity;
    ArcResult artillery_planning;
    BallisticSignature artillery_continuity_signature;
};
struct CombatSharedRuntime {
    ArtilleryController artillery;
    int final_lane_budget=0;
    struct LaneReceipt {double at=0;bool clear=false;};
    std::map<std::tuple<std::int64_t,std::string,std::int64_t>,LaneReceipt> lanes;
};
struct CombatActorRefs {
    offline_simulation::ActorKey key;
    CombatState& combat;
    SiegeState& siege;
    std::vector<EquipmentState>& equipment;
    CombatLedger& ledger;
    offline_simulation::weapons::Store& weapons;
    CombatActorRuntime& memory;
    EnvironmentTotals* environment=nullptr;
};
struct CombatDescriptor {
    std::optional<Point> water_sensor;
    SiegeConfig siege;
    double base_repair_factor=0,repair_speed_factor=1;
};
struct CombatSlice {
    double elapsed=0,now=0,equipment_now=0;
    bool stunned=false;
};
struct CombatPreparation {
    bool alive=false,siege_motion_locked=false,siege_descriptor_changed=false;
    std::vector<CriticalEvent> events;
    std::vector<EquipmentEffect> equipment_effects;
};
// One call per actor in the source's drive order, before decision/movement.
// Equipment time belongs to the shared slice and is already advanced once.
CombatPreparation prepare_combat_actor(CombatActorRefs, const CombatDescriptor&,
    const CombatSlice&, CombatWorld&);
// After decision, before movement and gun preparation. Mutates the exact
// command supplied by the native driver; changing mode emits a descriptor edge.
SiegeResult prepare_combat_siege(CombatActorRefs,const CombatDescriptor&,
    const CombatSlice&,SiegeCommand&);
// Rescale/tick reload and stage the requested shell before movement.
double prepare_combat_weapon(CombatActorRefs,const CombatSlice&,int requested_shell);
struct DirectAimInput {
    std::optional<GunnerTarget> target;
    std::optional<AimSample> selected_sample;
    std::optional<DirectAimToken> selected_token;
    std::optional<Point> command_aim_position;
    AimPose target_pose;
    BallisticSignature signature;
    std::optional<GunMount> mount;
    std::optional<DirectShot> shell;
    GunnerConfig gunner;
    std::int64_t round_id=0;
    bool refresh_control=false,force=false;
};
struct DirectAimResult {
    BallisticResult ballistic;
    WeaponAimResult aim;
};
// Invoked after motion with the selected part retained by select_exposed_aim.
DirectAimResult advance_direct_aim(CombatActorRefs,const CombatSlice&,
    const DirectAimInput&,WeaponFrame&,const HydraulicConfig&,WeaponWorld&);
bool direct_aim_matches(CombatActorRuntime&,const std::optional<DirectAimToken>& current);
struct DirectFireInput {
    DirectAimInput aim;
    AimSelection* selection=nullptr;
    std::uint64_t target_record_revision=0,target_descriptor_revision=0;
    double rating=0,fire_range=0;
    bool publish=false,fire_allowed=false,source_available=false,target_available=false;
    std::int64_t start_us=0,end_us=0;
    std::vector<LaneBody> roster;
};
class DirectCombatWorld:public DirectWorld,public AimSelectionWorld {};
struct DirectFireResult {FireResult fire;std::optional<bool> shot_clear;};
DirectFireResult advance_direct_fire(CombatActorRefs,CombatSharedRuntime&,const CombatSlice&,
    const DirectFireInput&,const DirectAimResult&,const WeaponFrame&,DirectCombatWorld&);
struct ArtilleryAimInput {
    ArtilleryInput physical;
    std::optional<GunnerTarget> target;
    std::optional<GunMount> mount;
    std::optional<Point> command_aim_position;
    BallisticSignature signature;
    GunnerConfig gunner;
    std::int64_t round_id=0;
    bool refresh_control=false,force=false,fire_allowed=false;
};
struct ArtilleryAimResult {
    bool fresh=false,planning_pending=false;
    std::optional<ArcCandidate> solution;
    ArcResult planning;
    WeaponAimResult aim;
};
struct ArtilleryHold {std::optional<Point> aim_position;bool brake=false;};
ArtilleryHold prepare_artillery_hold(CombatActorRefs,CombatSharedRuntime&,const CombatSlice&,const ArtilleryAimInput&);
ArtilleryAimResult advance_artillery_aim(CombatActorRefs,CombatSharedRuntime&,const CombatSlice&,
    const ArtilleryAimInput&,WeaponFrame&,const HydraulicConfig&,WeaponWorld&);
struct ArtilleryFireInput {
    ArtilleryAimInput aim;
    double fire_range=0,splash_radius=0;
    int team=0;
    bool publish=false;
    std::int64_t start_us=0,end_us=0;
    std::vector<LaneBody> roster;
};
struct ArtilleryFireResult {FireResult fire;ArtilleryProof proof;std::optional<bool> shot_clear;};
ArtilleryFireResult advance_artillery_fire(CombatActorRefs,CombatSharedRuntime&,const CombatSlice&,
    const ArtilleryFireInput&,const ArtilleryAimResult&,const WeaponFrame&,DirectWorld&);
} // namespace wot::offline_bot_step
