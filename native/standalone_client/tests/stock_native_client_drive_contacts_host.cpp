#include "stock_native_client_drive_contacts.hpp"
#include <algorithm>
using namespace stock_native_client_drive;
namespace {
struct World:ContactWorld {
    std::vector<ContactPeer> roster;bool clear=true,safe=true,outside=false;
    void retry_ram_proofs()override{}
    std::vector<ContactPeer> peers(const PlayerState&,const Point&,double,double)override{return roster;}
    void poll_ram(const offline_contact::Body&,const std::vector<ContactPeer>&)override{}
    void collision_feedback(const offline_contact::Body&,const ContactPeer&,double)override{}
    std::optional<ID> motion_time(double)override{return 5000000;}
    bool motion(PlayerState&,const Point&,double,double,double,double)override{return clear;}
    bool arena_outside(const PlayerState&,const Point&,double)override{return outside;}
    bool baked_safe(const Point&)override{return safe;}
    Point turret_contacts(PlayerState&,const Point&p,double,double)override{return p;}
};
}
extern "C" int player_contacts_fixture(const double*constants,const double*initial,const double*peers,int count,const double*options,double*out){try{
    PlayerDescriptor descriptor;auto&d=descriptor.motion;auto&law=d.drive;unsigned n=0;for(auto&v:law.t)v=constants[n++];
    double*fields[]={&law.mass,&law.power,&law.ratio,&law.forward,&law.reverse,&law.friction,&law.brake,&law.rotation};for(auto*v:fields)*v=constants[n++];for(auto&v:law.terrain)v=constants[n++];
    d.side_min_y=constants[n++];d.side_full_y=constants[n++];d.side_min=constants[n++];d.side_full=constants[n++];for(auto&v:d.collision_shape)v=constants[n++];descriptor.team=int(constants[n++]);
    PlayerState state;n=0;state.position={initial[n],initial[n+1],initial[n+2]};n+=3;state.yaw=initial[n++];state.pitch=initial[n++];state.roll=initial[n++];state.speed=initial[n++];state.turn_speed=initial[n++];
    state.push_x=initial[n++];state.push_z=initial[n++];state.vertical_speed=initial[n++];state.drive_throttle=initial[n++];state.drive_turn=initial[n++];Controls controls;controls.forward=initial[n++];state.airborne=initial[n++]!=0.;state.contact_reaction_paid=initial[n++]!=0.;
    World world;n=0;for(int i=0;i<count;++i){ContactPeer p;auto&b=p.body;b.id=ID(peers[n++]);p.network_id=ID(peers[n++]);p.engine_id=ID(peers[n++]);p.bot=peers[n++]!=0.;b.flags=unsigned(peers[n++]);
        b.x=peers[n++];b.y=peers[n++];b.z=peers[n++];b.yaw=peers[n++];b.mass=peers[n++];b.vx=peers[n++];b.vy=peers[n++];b.vz=peers[n++];b.pitch=peers[n++];b.roll=peers[n++];for(auto&v:b.shape)v=peers[n++];
        b.grip[0]=peers[n++];b.grip[1]=peers[n++];b.push_yaw=peers[n++];b.traverse_speed=peers[n++];b.traverse_torque=peers[n++];const bool physical=peers[n++]!=0.;const Vec velocity{peers[n],peers[n+1]};n+=2;if(physical)p.physical_velocity=velocity;world.roster.push_back(p);}
    const double dt=options[0],now=options[1];std::optional<Point>start;if(options[2])start=Point{options[3],options[4],options[5]};world.clear=options[6]!=0.;world.safe=options[7]!=0.;world.outside=options[8]!=0.;
    PlayerContacts owner;const auto position=owner.settle(state,descriptor,controls,world,state.position,state.yaw,dt,now,start);n=0;for(auto v:position)out[n++]=v;out[n++]=state.speed;out[n++]=state.push_x;out[n++]=state.push_z;out[n++]=owner.pushes().size();
    for(const auto&row:owner.pushes()){out[n++]=row.first;out[n++]=row.second.sequence;for(auto v:row.second.totals)out[n++]=v;}return int(n);
}catch(...){return -1;}}
