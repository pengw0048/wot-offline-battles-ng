#pragma once
#include "stock_native_client_drive_runtime.hpp"
#include "offline_contact_roster.h"
#include "offline_math_batch.h"
#include <map>
namespace stock_native_client_state {struct Actor;}
namespace stock_native_client_drive {
using ID=std::int64_t;
struct ContactPeer {
    offline_contact::Body body;
    ID network_id=0,engine_id=0;
    bool bot=false;
    std::optional<Vec> physical_velocity;
    std::shared_ptr<stock_native_client_state::Actor> actor;
    const PlayerDescriptor* parameters=nullptr;
    std::optional<std::array<double,21>> ram_witness;
    std::optional<std::int64_t> ram_sequence;
};
struct PushCheckpoint {ID actor=0,sequence=0;std::array<double,5> totals{};};
struct PushImpulse {ID sequence=0,sample_us=0;double x=0.,z=0.,angular=0.;};
class ContactWorld {
public:
    virtual ~ContactWorld()=default;
    virtual void retry_ram_proofs()=0;
    virtual std::vector<ContactPeer> peers(const PlayerState&,const Point&,double dt,double reach)=0;
    virtual void poll_ram(const offline_contact::Body&,const std::vector<ContactPeer>&)=0;
    virtual void collision_feedback(const offline_contact::Body&,const ContactPeer&,double now)=0;
    virtual std::optional<ID> motion_time(double now)=0;
    virtual bool motion(PlayerState&,const Point&,double yaw,double speed,double dt,double hull_yaw)=0;
    virtual bool arena_outside(const PlayerState&,const Point&,double yaw)=0;
    virtual bool baked_safe(const Point&)=0;
    virtual Point turret_contacts(PlayerState&,const Point&,double yaw,double dt)=0;
};
class PlayerContacts {
    std::map<ID,PushCheckpoint> pushes_;
    std::map<ID,std::vector<PushImpulse>> impulses_;
    std::set<std::pair<ID,ID>> physical_contacts_;
public:
    Point settle(PlayerState&,const PlayerDescriptor&,const Controls&,ContactWorld&,
        const Point&,double yaw,double dt,double now,std::optional<Point> start);
    const std::map<ID,PushCheckpoint>& pushes()const{return pushes_;}
    const std::map<ID,std::vector<PushImpulse>>& impulses()const{return impulses_;}
    std::vector<PushImpulse>&impulses(ID actor){return impulses_[actor];}
};
}
