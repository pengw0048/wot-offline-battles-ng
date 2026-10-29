#pragma once
#include "stock_native_client_drive_motion.hpp"
#include "stock_native_client_drive_legacy.hpp"
#include "stock_native_client_drive_peers.hpp"
#include "stock_native_client_drive_impact.hpp"
#include "stock_native_client_drive_ram.hpp"
#include "stock_native_client_drive_feedback.hpp"
#include "stock_native_client_drive_geometry.hpp"
#include "stock_native_client_query.hpp"
#include "stock_native_bot_python.hpp"
#include "stock_native_bot_world_adapter.hpp"
namespace stock_native_client_drive {
struct BakedBoundary {int width=0,height=0;double cell_size=0.,origin_x=0.,origin_z=0.;std::vector<unsigned>hazards;bool safe(const Point&)const;};
struct NativeWorldConfig {
    std::optional<std::array<double,4>>arena;
    BakedBoundary baked;
    MotionConfig motion;
    double ground_plane_epsilon=0.,ground_pitch_limit=0.;
    double hydraulic_center_z=0.;
    BW::MatrixProviderPtr hydraulic_relative;
    stock_native_detached_turret::VehicleGeometry vehicle_geometry;
    stock_native_bot_turret::MovingDescriptor moving_descriptor;
};
NativeWorldConfig readNativeWorldConfig(_object*battle,_object*entity,const PlayerDescriptor&);
ImpactConfig readPlayerImpactConfig(_object*battle,_object*entity);
// The concrete visible player world owns no independent authority: it borrows
// the battle's sole scene, shared presented roster, native suspension and
// reliable publication owners. All hot queries and decisions remain native.
class NativePlayerWorld final:public PlayerWorld,public ContactWorld {
    stock_native_bot_python::Ref battle_,entity_;
    std::shared_ptr<stock_native_client_state::State>shared_;
    std::shared_ptr<stock_native_client_state::Actor>local_;
    std::uint64_t epoch_;
    PlayerDriver&driver_;stock_native_world::NativeScene&scene_;
    stock_native_world::Pipeline&pipeline_;Ground&ground_;Owner*detailed_;
    NativePeers&peers_;PlayerContacts&contacts_;PublicationLedger&publication_;
    PlayerImpact&impact_;PlayerRam&ram_;PlayerFeedback feedback_;
    std::shared_ptr<stock_native_client_query::ProjectileTurrets>turrets_;
    NativeWorldConfig config_;SceneMotion motion_;LegacySupport legacy_;
    LocalGeometry&local_geometry_;stock_native_bot_python::Ref sensor_;
    std::optional<std::array<Point,2>>geometry_region_;
    std::uint64_t geometry_stream_=0;
    std::optional<step::VerticalMemory>saved_memory_;
    stock_native_client_state::Value saved_landing_;
    stock_native_bot_geometry::Pose presented_pose_;
    std::unique_ptr<stock_native_bot_python::Ref>hydraulic_body_;
    BW::MatrixProviderPtr turret_provider_;
    std::map<stock_native_bot_turret::Owner,stock_native_detached_turret::Checkpoint>turret_pushes_;
    std::optional<std::pair<ID,Vec>>reaction_key_;
    std::vector<ContactPeer>rotation_peers_;
    std::map<stock_native_bot_turret::Owner,std::string>turret_failures_;
    std::optional<ID>motion_anchor_;double motion_anchor_local_=0.,now_=0.;
    int drown_level_=0,overturn_level_=0;bool damage_pending_=false,battle_over_=false;
    void current()const;
    std::array<double,4>violations(const Point&,double)const;
    stock_native_world::TurretPose turretPose(const PlayerState&,const Point&,double)const;
    bool turretClear(const PlayerState&,const Point&,double,const PlayerState&,const Point&,double)const;
    double serverClock()const;
    std::optional<Recovery>reaction(PlayerState&,const Point&,double,double,double,const ContactTrace&,const ContactPeer*);
    std::vector<Point>groundWitnesses(const Point&,double);
    void impactGeometry();
    void ensureRegion(const Point&,const Point&,double extra=0.);
public:
    NativePlayerWorld(_object*battle,_object*entity,std::shared_ptr<stock_native_client_state::State>,
        std::shared_ptr<stock_native_client_state::Actor>,PlayerDriver&,stock_native_world::NativeScene&,
        stock_native_world::Pipeline&,Ground&,Owner*,NativePeers&,PlayerContacts&,PublicationLedger&,
        PlayerImpact&,PlayerRam&,std::shared_ptr<stock_native_client_query::ProjectileTurrets>,NativeWorldConfig,
        LocalGeometry&,_object*sensor);
    void frame(double now,std::optional<ID>motion_anchor={},double motion_local=0.);
    void environment(int drowned,int overturned,bool pending_damage){drown_level_=drowned;overturn_level_=overturned;damage_pending_=pending_damage;}
    void battleOver(bool value){battle_over_=value;}
    void observeFeedback(ID first,ID second,const Point&a,const Point&b,double now){feedback_.observed(first,second,a,b,now);}
    Conditions conditions()const override;
    double clock()const override{return now_;}
    double drive_pitch(const PlayerState&)override;
    std::optional<double>local_aim_yaw(const PlayerState&,const Controls&)override;
    bool motion(PlayerState&,const Point&,double,double,double,bool,std::optional<double> hull={})override;
    double rotation_fraction(const PlayerState&,const Point&,double,double,double,double)override;
    std::optional<Recovery>held_peer_turn(PlayerState&,const Point&,double,double,double)override;
    std::optional<Recovery>world_turn(PlayerState&,const Point&,double,double,double,const ContactTrace&)override;
    bool arena_rotation(const PlayerState&,const Point&,double,const Point&,double)override;
    bool pose_sweep(PlayerState&,const Point&,double,const Point&,double,double,double)override;
    void impact(PlayerState&,const ContactTrace&,double,double)override;
    bool detailed_suspension()const override;
    bool detached_turrets()const override;
    void checkpoint()override;void rollback_suspension()override;
    void vertical(PlayerState&,Point&,double,double,const Point&,const std::optional<Point>&,double=0.,double=0.)override;
    void ground_pose(PlayerState&s,const Point&p,double yaw)override{legacy_.groundPose(s,p,yaw);}
    std::optional<step::SupportPlane>sample_plane(const Point&p,double yaw)override{return legacy_.samplePlane(p,yaw);}
    std::array<std::optional<double>,2>terrain_support(const Point&p,double yaw)override;
    Point contacts(PlayerState&,const Point&,double,double,const Point&)override;
    bool complete_pose_clear(const PlayerState&,const Point&,double,const PlayerState&,const Point&,double)override;
    void present(PlayerState&,double,const SliceTrace&,bool)override;
    bool pending_landing()const override{return !publication_.landing_impacts.array.empty();}
    bool pending_damage()const override{return damage_pending_;}
    bool terminal_environment()const override{return drown_level_==2||overturn_level_==2;}
    void retry_ram_proofs()override{ram_.retry();}
    std::vector<ContactPeer>peers(const PlayerState&,const Point&,double,double)override;
    void poll_ram(const offline_contact::Body&,const std::vector<ContactPeer>&)override;
    void collision_feedback(const offline_contact::Body&,const ContactPeer&,double)override;
    std::optional<ID>motion_time(double)override;
    bool motion(PlayerState&s,const Point&p,double yaw,double speed,double dt,double hull)override{return motion(s,p,yaw,speed,dt,false,hull);}
    bool arena_outside(const PlayerState&,const Point&p,double yaw)override;
    bool baked_safe(const Point&p)override{return config_.baked.safe(p);}
    Point turret_contacts(PlayerState&,const Point&,double,double)override;
};
}
