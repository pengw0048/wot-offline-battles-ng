#pragma once
#include "stock_native_client_weapon.hpp"
#include "stock_native_client_state.hpp"
#include "stock_native_bot_combat.hpp"
#include <set>
#include <array>
#include <cstdint>
#include <map>
#include <optional>
#include <string>
#include <vector>
struct _object;
namespace stock_native_client_weapon_runtime {
using Gun=stock_native_client_weapon::Gun;
bool audioDue(std::optional<double> deadline,double now);
struct Checkpoint {
    double reload_time=0.,reload_duration=0.,dispersion=0.;
    int clip=0,clip_size=0,shot_index=0;
};
struct Metadata {
    std::array<double,3> descriptor_factors{};
    double raw_dispersion=0.,turret_speed=0.,gun_speed=0.,maximum_health=0.;
    double crew_factor=1.,gun_rotation_factor=1.;
    std::vector<std::int64_t> shells;
    int current_shell_setting=0;
    int ready_stage=0,cooldown_stage=0,exhausted_stage=0;
};
struct Factors {
    double dispersion=1.,aim_time=1.,reload=1.,turret_speed=1.,gun_stun=1.;
    double bloom_move=1.,bloom_rotation=1.,bloom_turret=1.;
};
struct Conditional {double reload=1.,dispersion=1.;};
Conditional conditional(const stock_native_client_state::Value& effective,
    const std::set<std::string>& casualties,bool fire,double health,double maximum,
    const wot::offline_bot_step::CriticalState&);
struct Equipment {
    std::int64_t compact=0;int uses_left=0;bool rpm_limiter=false,active=false;
    double ready_at=0.;
    bool fuel=false;
    double engine_power_factor=1.;
    bool repairkit=false;
    double repair_bonus=0.;
};
struct AmmoEdge {std::int64_t compact=0;int quantity=0,clip=0;};
struct EquipmentEdge {std::int64_t compact=0;int quantity=0,stage=0,remaining=0;};
struct ReloadEdge {double remaining=0.,duration=0.;std::string reason;};
struct Edges {
    std::vector<AmmoEdge> ammunition;
    std::optional<std::int64_t> current_shell;
    std::vector<EquipmentEdge> equipment;
    std::optional<ReloadEdge> reload;
    std::optional<std::array<double,7>> targeting;
};
class Runtime {
    Gun*gun_;
    Metadata metadata_;
    std::optional<double> last_tick_;
    std::optional<std::pair<double,double>> reload_;
    std::optional<std::array<double,7>> targeting_;
    std::optional<std::pair<std::pair<int,int>,std::vector<int>>> ammunition_;
    std::map<std::int64_t,std::array<int,3>> equipment_;
    std::vector<Equipment> equipment_state_;
public:
    explicit Runtime(Gun&,Metadata);
    Gun&gun(){return *gun_;}const Metadata&metadata()const{return metadata_;}
    void metadata(Metadata value){metadata_=std::move(value);}
    void lastTick(std::optional<double> value){last_tick_=value;}
    std::optional<double> lastTick()const{return last_tick_;}
    void seedReload(std::optional<std::pair<double,double>> value){reload_=value;}
    void seedTargeting(std::optional<std::array<double,7>> value){targeting_=value;}
    void seedAmmo(int shot,int clip,std::vector<int> values){ammunition_=std::make_pair(std::make_pair(shot,clip),std::move(values));}
    void equipment(std::vector<Equipment> value){equipment_state_=std::move(value);}
    void seedEquipment(std::map<std::int64_t,std::array<int,3>> value){equipment_=std::move(value);}
    void ammo(Edges&,double now,bool equipment_ready,bool force=false);
    void equipment(Edges&,double now,bool ready);
    void reload(Edges&,double remaining,double duration,bool force=false);
    void targeting(Edges&,const Factors&);
    Edges advance(double now,bool live,double speed,double turn,const Factors&,bool equipment_ready);
    Checkpoint checkpoint()const;
    double enginePowerFactor()const;
    double repairBonus()const;
};
// These preserve one gun identity and exact shared-state/scene epoch. They do
// not call Python gun/critical/equipment business methods. Only changed stock
// HUD/audio presentation edges may enter the interpreter.
Gun* advanceWeapon(_object* battle,double now);
// A queued contact checkpoint retains the motion that produced that input.
Gun* advanceWeaponMotion(_object* battle,double now,double speed,double turn);
double reloadFactor(_object* battle);
double activeEnginePowerFactor(_object* battle);
double activeRepairBonus(_object* battle);
Checkpoint checkpoint(_object* battle,double now);
Checkpoint currentCheckpoint(_object* battle);
int livingSkillCount(_object* battle,const std::string& skill);
int livingSkillCount(const stock_native_client_state::Value&,const std::string&,const std::set<std::string>&);
_object* bind(_object*,_object*);
_object* refresh(_object*,_object*);
_object* advance(_object*,_object*);
_object* audioFrame(_object*,_object*);
_object* equipmentIngress(_object*,_object*);
_object* publish(_object*,_object*);
_object* close(_object*,_object*);
}
