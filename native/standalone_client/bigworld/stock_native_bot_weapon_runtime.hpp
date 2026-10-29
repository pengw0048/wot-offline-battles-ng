#pragma once
#include "stock_native_bot_combat_runtime.hpp"
#include "stock_native_bot_combat_snapshot.hpp"
#include "stock_native_bot_decision.hpp"
#include "stock_native_bot_motion.hpp"
#include <map>

namespace wot::offline_bot_step {
struct WeaponTarget {
    GunnerTarget target;
    AimPose pose;
    bool available=false,alive=true;
    std::uint64_t record_revision=0,descriptor_revision=0;
    std::vector<AimSample> samples;
};
struct WeaponActorState {
    double aim_yaw=0,rating=0;
    std::int64_t round_id=0;
    int team=0;
    std::string class_tag;
    bool hull_aiming=false;
    GunnerConfig gunner;
    std::uint64_t descriptor_revision=0;
    struct SelectionRecord {std::uint64_t record_revision=0,descriptor_revision=0;AimSelection selection;};
    std::map<std::pair<std::string,std::int64_t>,SelectionRecord> targets;
};
class WeaponRuntimeWorld:public WeaponWorld,public DirectCombatWorld,public ArcWorld {
public:
    virtual void bind(offline_simulation::ActorKey,const std::optional<WeaponTarget>&,
        const stock_native_bot_armor::Shell*)=0;
    virtual std::optional<WeaponTarget> target(const std::string&,std::int64_t)=0;
    virtual std::optional<Point> barrel_point(offline_simulation::ActorKey,const AimPose&)=0;
    virtual bool source_available(offline_simulation::ActorKey)=0;
    virtual std::vector<LaneBody> lane_bodies(offline_simulation::ActorKey)=0;
};
struct WeaponRuntimeResult {
    FireResult fire;
    std::optional<ArtilleryProof> artillery_proof;
    std::optional<bool> shot_clear;
};
class WeaponRuntime {
    CombatSharedRuntime shared_;
    WeaponActorState::SelectionRecord& selection(WeaponActorState&,const WeaponTarget&);
public:
    CombatSharedRuntime& shared(){return shared_;}
    // Final lane budget is reset once by the total step owner.
    void beginSlice(int lane_budget){shared_.final_lane_budget=lane_budget;}
    std::size_t advanceArtillery(double now,std::size_t budget,WeaponRuntimeWorld& world){return shared_.artillery.advance(now,budget,world);}
    void preMotion(WeaponActorState&,CombatActorRefs,const CombatModeSnapshot&,
        stock_native_bot_motion::State&,stock_native_bot_planner::Command&,
        const std::optional<WeaponTarget>&,const CombatSlice&,stock_native_bot_decision::Decisions&);
    std::optional<bool> shotClear(WeaponActorState&,CombatActorRefs,const CombatModeSnapshot&,
        const stock_native_bot_motion::State&,const WeaponTarget&,double now,int& budget,bool force,WeaponRuntimeWorld&);
    WeaponRuntimeResult postMotion(WeaponActorState&,CombatActorRefs,const CombatModeSnapshot&,
        stock_native_bot_motion::State&,const stock_native_bot_decision::Input&,
        const stock_native_bot_planner::Command&,const std::optional<WeaponTarget>&,
        const CombatSlice&,bool publish,bool refresh_control,std::int64_t start_us,std::int64_t end_us,
        stock_native_bot_decision::Decisions&,WeaponRuntimeWorld&);
};
}
