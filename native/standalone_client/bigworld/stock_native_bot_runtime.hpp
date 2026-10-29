#pragma once
#include "stock_native_bot_clock.hpp"
#include "stock_native_bot_motion_snapshot.hpp"
#include "stock_native_bot_physics_snapshot.hpp"
#include "stock_native_bot_planning_runtime.hpp"
#include "stock_native_bot_weapon_runtime.hpp"
#include "stock_native_bot_observation_runtime.hpp"
#include "stock_native_bot_publication.hpp"
#include "stock_native_bot_environment.hpp"

namespace stock_native_bot {
namespace motion=stock_native_bot_motion;
namespace physics=stock_native_bot_physics;
namespace planning=stock_native_bot_planning;
namespace nav=stock_native_bot_navigation;
namespace decision=stock_native_bot_decision;
namespace plan=stock_native_bot_planner;
namespace contacts=stock_native_bot_contacts;
namespace perception=stock_native_bot_perception;
namespace observation=stock_native_bot_observation;
namespace publication=stock_native_bot_publication;
namespace step=wot::offline_bot_step;
using ID=std::int64_t;
using Key=offline_simulation::ActorKey;
using Point=step::Point;
struct Mode {
    MotionModeSnapshot motion;
    step::CombatModeSnapshot combat;
    physics::PhysicsSnapshot physics;
    nav::ExactDescriptor navigation;
    perception::Actor perception;
    std::optional<double> spawn_drive_cap;
};
struct ActorSnapshot {
    MotionSnapshot motion;
    step::CombatSnapshot combat;
    std::array<Mode,2> modes;
    step::WeaponActorState weapon;
    decision::Target metadata;
    std::optional<std::array<double,2>> shot_angles;
    std::optional<Point> spawn_anchor;
    bool has_siege=false,world_pose=true;
};
struct Snapshot {
    offline_simulation::TickToken identity;
    planning::PlanningSnapshot planning;
    native_visibility::FoliageSnapshot foliage;
    std::vector<ActorSnapshot> actors;
    Clock clock;
    double equipment_time=0.;
    std::optional<Point> camera;
    std::size_t slope_cursor=0;
};
struct Actor {
    motion::State motion;
    motion::Descriptor descriptor;
    step::SuspensionDescriptor suspension;
    std::array<Mode,2> modes;
    step::CombatState combat;
    step::CombatLedger ledger;
    step::SiegeState siege;
    step::SiegeConfig siege_config;
    std::vector<step::EquipmentState> equipment;
    step::CombatActorRuntime weapon_memory;
    step::WeaponActorState weapon;
    decision::Target metadata;
    std::optional<decision::Identity> selected_target;
    std::optional<std::array<double,2>> shot_angles;
    std::optional<Point> spawn_anchor;
    int mode=0,team=0,slot=0;
    bool has_siege=false,world_pose=true,route_wreck_blocked=false;
};
struct Human {
    contacts::Human contacts;
    perception::Actor perception;
    observation::Actor observation;
    nav::NavigationHuman navigation;
    plan::Body neighbour;
    Point body_minimum{},body_maximum{};
    std::optional<Point> spawn_anchor;
    std::optional<double> spawn_drive_cap;
    double contact_drive_forward=0.,contact_drive_reverse=0.,contact_rotation_speed=0.;
    double contact_mass=0.;
    bool pivot_available=false,rotation_around_center=true;
    double track_center=0.;
};
// All services are native capabilities. Python is restricted to lifecycle
// installation and compact event ingress/publication outside this owner.
class Services {
public:
    virtual ~Services()=default;
    virtual step::WeaponRuntimeWorld& weaponWorld()=0;
    virtual observation::World& tacticalWorld()=0;
    virtual stock_native_bot_turret::Obstacles& turrets()=0;
    virtual step::ImpactWorld& impactWorld(ID)=0;
    virtual std::optional<contacts::Armor> armor(const contacts::Body&,const contacts::Body&,
        const std::array<double,3>&)=0;
    virtual void actorChanged(const Actor&,const step::AimPose&)=0;
    virtual void modeChanged(ID,const Mode&)=0;
    virtual void beginCallback(double now,double server_time_ms)=0;
    virtual void scanDestructibles(const motion::State&)=0;
    virtual bool prewarmDestructibles(const Actor&,double now)=0;
    virtual bool prewarmPlayerDestructibles(const Human&,double now)=0;
};
struct Output {
    publication::Batch state;
    std::optional<observation::Observation> observation;
    std::vector<contacts::PushProof> pushes;
    std::vector<ID> hazards,environment_fires;
    std::vector<std::array<ID,2>> falls;
    struct CriticalTotal {ID actor;std::string cause;std::uint32_t mask;ID count;};
    std::vector<CriticalTotal> critical;
};
struct ServerSnapshot {
    step::ServerCombat combat;
    ID fire_sequence=0,server_time_ms=0;
    double reload_time=0.,reload_duration=0.;
    bool has_reload=false;
    std::optional<std::array<int,2>> clip;
    std::optional<offline_simulation::weapons::AmmoState> ammunition;
};
struct ExternalContactsSnapshot {std::map<ID,std::array<double,5>> actors;};
struct Statistics {
    std::uint64_t callbacks=0,slices=0,refreshes=0,actors=0;
    std::uint64_t refresh_slices=0,reuse_slices=0,refresh_ns=0,reuse_ns=0,refresh_actors=0,reuse_actors=0;
    // No interpreter/API object exists in the physical owner. Resource
    // presentation exceptions are counted separately by NativeBackend.
    std::uint64_t python_business_callbacks=0;
};
class Runtime final:public planning::PlanningHost,public physics::ImpactTransactions,
    public contacts::World,public step::CombatWorld {
    Services& services_;
    stock_native_world::Pipeline pipeline_;
    offline_simulation::control::Store control_;
    perception::Perception perception_;
    std::map<ID,Actor> actors_;
    std::map<ID,contacts::Actor> contact_actors_;
    std::vector<contacts::Human> contact_humans_;
    std::vector<Human> humans_;
    std::vector<ID> insertion_order_,dictionary_order_,publication_order_;
    std::map<ID,physics::Geometry> geometry_;
    std::map<ID,physics::HostMemory> host_memory_;
    std::map<ID,step::VerticalMemory> vertical_memory_;
    std::map<ID,int> slots_;
    planning::PlanningRuntime planning_;
    physics::Context physics_context_;
    physics::NativeHost physics_;
    step::WeaponRuntime weapons_runtime_;
    observation::Runtime observations_;
    offline_simulation::weapons::Store weapons_;
    contacts::Contacts contacts_{1000000};
    publication::Publisher publisher_;
    std::vector<contacts::Report> reports_;
    step::EnvironmentTotals environment_;
    std::map<ID,std::unique_ptr<physics::PhysicsWorld>> worlds_;
    offline_simulation::TickToken identity_;
    Clock clock_;
    double equipment_time_=0.;
    std::optional<Point> camera_;
    std::size_t slope_cursor_=0;
    std::size_t registry_cursor_=0;
    std::set<ID> spawn_overlap_done_;
    std::map<ID,double> spawn_overlap_retry_;
    int repair_budget_=0;
    ID order_revision_=-1;
    Statistics statistics_;
    bool callback_open_=false;
    std::vector<BurstDeadline> bursts()const;
    bool clearSpawnOverlap(ID,const Point&,double,const Point&,const Point&,
        double,const std::optional<Point>&,const std::optional<double>&,double);
    void adoptMode(Actor&);
    void updateCombatPose(Actor&);
    void applyCombatPose(Actor&);
    step::AimPose aimPose(const Actor&)const;
    step::CombatActorRefs combatRefs(Actor&);
    step::CombatDescriptor combatDescriptor(const Actor&)const;
    perception::Actor perceptionActor(const Actor&,const Slice&);
    observation::Actor observationActor(const Actor&)const;
    plan::Body body(const Actor&)const;
    std::vector<plan::Body> neighbours(ID)const;
    int detail(const Actor&)const;
    decision::Input decisionInput(const Actor&,const Slice&)const;
    void planningActor(const Actor&,const Slice&);
    void publishPose(Actor&,const Slice&,bool perception);
    std::optional<step::WeaponTarget> target(const decision::Output&,const plan::Command&);
    publication::Input publicationActor(const Actor&)const;
    std::optional<Output> advance(const Slice&);
public:
    Runtime(Snapshot,stock_native_world::Registry&,stock_native_world::Scene&,Services&);
    ~Runtime();
    Runtime(const Runtime&)=delete;Runtime&operator=(const Runtime&)=delete;
    std::vector<Output> update(double elapsed,double now,double server_time_ms);
    void humans(std::vector<Human>);
    void orders(std::map<ID,planning::StrategicOrder>,std::map<ID,std::uint64_t>);
    bool acceptOrders(std::map<ID,planning::StrategicOrder>,ID revision);
    bool outgoingLane(ID,const observation::Actor&,double now);
    bool reconcile(ID,const step::ServerCombat&);
    bool serverSnapshot(ID,const ServerSnapshot&);
    bool acknowledgeLaunch(ID,ID fire_sequence);
    void acknowledgeRam(ID player,ID sequence);
    void camera(std::optional<Point> value){camera_=value;}
    void foliage(offline_visibility::Update value){perception_.foliage(std::move(value));}
    std::size_t prewarm(double now);
    std::size_t prewarmPlayers(double now,bool battle_live);
    bool externalContact(ID,const motion::ContactResponse&,double dt,double now);
    ExternalContactsSnapshot snapshotExternalContacts()const;
    void restoreExternalContacts(const ExternalContactsSnapshot&,double now);
    const std::map<ID,Actor>& actors()const{return actors_;}
    const offline_simulation::weapons::Store& weapons()const{return weapons_;}
    const step::EnvironmentTotals& environment()const{return environment_;}
    const Clock& clock()const{return clock_;}
    double equipmentTime()const{return equipment_time_;}
    const contacts::Actor* contactActor(ID id)const{const auto i=contact_actors_.find(id);return i==contact_actors_.end()?nullptr:&i->second;}
    const Statistics& statistics()const{return statistics_;}
    physics::NativeHost& physics(){return physics_;}
    planning::PlanningRuntime& planning(){return planning_;}
    step::WeaponRuntime& weaponRuntime(){return weapons_runtime_;}
    const offline_simulation::TickToken& identity()const{return identity_;}
    double planningRotationFraction(ID,const Point&,double,double,bool)override;
    void cancelArtillery(ID)override;
    int fall_damage(motion::State&,double,const std::vector<Point>&,const Point&,bool)override;
    motion::World& motion_world(ID)override;
    std::optional<contacts::Armor> armor(const contacts::Body&,const contacts::Body&,const std::array<double,3>&)override;
    double water_depth(const Point&)override;
};
}
