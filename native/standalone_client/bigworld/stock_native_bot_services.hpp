#pragma once
#include "stock_native_bot_runtime.hpp"
#include "stock_native_bot_weapon_world.hpp"
#include "stock_native_bot_internal_geometry.hpp"
#include "stock_native_projectile_damage_python.hpp"
#include <memory>

namespace stock_native_bot {
// All descriptors and model owners are frozen at lifecycle installation.
// Human identity uses ActorKind::Human and its unmodified network id.
struct ServiceEntity {
    Key key;
    std::array<geometry::Vehicle,2> geometry;
    std::array<step::InternalLayout,2> interior;
    std::array<std::optional<stock_native_projectile_damage::TargetDescriptor>,2> projectile;
    std::array<std::vector<stock_native_projectile_damage::LaunchDescriptor>,2> launch_shots;
    std::array<bool,2> pitch_hull_aiming{{false,false}};
    std::array<std::map<int,std::array<std::optional<unsigned>,2>>,2> projectile_stickers;
    std::array<std::vector<step::AimSample>,2> aim_samples;
    step::WeaponTarget target;
    int team=0,mode=0;
    double radius=0.;
    bool alive=true,available=false,record_ready=false;
};
class NativeServices final:public Services,public observation::World,public step::ImpactWorld {
    class WeaponAdapter;
    stock_native_world::Registry& registry_;
    stock_native_world::Scene& scene_;
    geometry::Vehicles& vehicles_;
    stock_native_world::Pipeline queries_;
    stock_native_bot_turret::Obstacles turrets_;
    std::map<ID,ServiceEntity> entities_;
    std::map<ID,GeometryRow> rows_;
    Runtime* runtime_=nullptr;
    std::unique_ptr<WeaponWorldContext> context_;
    std::unique_ptr<NativeWeaponWorld> weapon_world_;
    std::unique_ptr<WeaponAdapter> adapter_;
    ID impact_actor_=0;
    double now_=0.,server_time_ms_=0.;
    std::optional<double> support(double,double,double);
    void updatePose(ServiceEntity&,const step::AimPose&,bool alive);
public:
    NativeServices(stock_native_world::Registry&,stock_native_world::Scene&,geometry::Vehicles&);
    ~NativeServices();
    // Install all required models/layouts before Runtime is constructed.
    void install(ServiceEntity);
    const ServiceEntity* find(Key) const;
    const std::map<ID,ServiceEntity>& entities() const { return entities_; }
    void recordReady(Key,bool);
    void remove(Key);
    // Call after Runtime construction, before the first update.
    void bind(Runtime&);
    void updateHuman(Key,const step::WeaponTarget&,int team,bool alive,bool available,double radius);
    step::WeaponRuntimeWorld& weaponWorld()override;
    observation::World& tacticalWorld()override{return *this;}
    stock_native_bot_turret::Obstacles& turrets()override{return turrets_;}
    step::ImpactWorld& impactWorld(ID)override;
    std::optional<contacts::Armor> armor(const contacts::Body&,const contacts::Body&,const std::array<double,3>&)override;
    void actorChanged(const Actor&,const step::AimPose&)override;
    void modeChanged(ID,const Mode&)override;
    void beginCallback(double now,double server_time_ms)override;
    void scanDestructibles(const motion::State&)override;
    bool prewarmDestructibles(const Actor&,double now)override;
    bool prewarmPlayerDestructibles(const Human&,double now)override;
    bool prewarmChunks(double x,double z,double yaw);
    bool outgoingLane(const observation::Actor&,const observation::Actor&,double now)override;
    bool staticClear(const Point&,const Point&)override;
    std::optional<double> ground(double x,double z,double hint)override;
    double waterDepth(const Point&)override;
    std::vector<Point> ground_witnesses()override;
    std::vector<step::ImpactHit> material_segment(const Point&,const Point&)override;
    std::vector<step::ImpactHit> interior_segment(const Point&,const Point&)override;
};
}
