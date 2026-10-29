#include "stock_native_client_drive_runtime.hpp"
using namespace stock_native_client_drive;
namespace {
struct World:PlayerWorld {
    Conditions flags;double slope=0.,fraction=1.,dy=0.,impulse=0.;Point correction{};bool arena=true,sweep=true,complete=true,detailed=true,blocked=false,guard=false;int air=-1;
    std::vector<int>statuses;std::size_t cursor=0;std::optional<Point>normal;bool held=false,recover=false;int presentations=0;
    Conditions conditions()const override{return flags;}double clock()const override{return 5.;}
    double drive_pitch(const PlayerState&)override{return slope;}
    std::optional<double> local_aim_yaw(const PlayerState&,const Controls&c)override{return c.aim_yaw;}
    bool motion(PlayerState&s,const Point&p,double,double,double,bool,std::optional<double>)override{const int status=cursor<statuses.size()?statuses[cursor++]:0;s.trace={};s.trace.status=ContactStatus(status);if(normal){s.trace.normal=normal;s.trace.point=p;}return status==0||status==1||status==2;}
    double rotation_fraction(const PlayerState&,const Point&,double,double,double,double)override{return fraction;}
    std::optional<Recovery> held_peer_turn(PlayerState&s,const Point&p,double yaw,double,double)override{if(!held)return {};s.reaction_rate=.12;s.contact_reaction_paid=true;auto q=p;q[0]+=.02;return Recovery{q,yaw+.01};}
    std::optional<Recovery> world_turn(PlayerState&s,const Point&p,double yaw,double,double,const ContactTrace&)override{if(!recover)return {};s.reaction_rate=.12;s.contact_reaction_paid=true;auto q=p;q[0]+=.02;return Recovery{q,yaw+.01};}
    bool arena_rotation(const PlayerState&,const Point&,double,const Point&,double)override{return arena;}
    bool pose_sweep(PlayerState&s,const Point&,double,const Point&,double,double,double)override{s.trace={};s.trace.status=sweep?ContactStatus::Clear:ContactStatus::Hard;return sweep;}
    void impact(PlayerState&,const ContactTrace&,double,double)override{}
    bool detailed_suspension()const override{return detailed;}
    bool detached_turrets()const override{return guard;}
    void checkpoint()override{}void rollback_suspension()override{}
    void vertical(PlayerState&s,Point&p,double,double dt,const Point&tick,const std::optional<Point>&,double height,double)override{p[1]+=dy*dt+height;s.support_blocked=blocked;s.suspension_failed=false;if(blocked)p=tick;if(air>=0)s.airborne=air!=0;}
    void ground_pose(PlayerState&,const Point&,double)override{}
    std::optional<step::SupportPlane>sample_plane(const Point&,double)override{return {};}
    std::array<std::optional<double>,2>terrain_support(const Point&,double)override{return {};}
    Point contacts(PlayerState&s,const Point&p,double,double,const Point&)override{s.speed+=impulse;return {p[0]+correction[0],p[1]+correction[1],p[2]+correction[2]};}
    bool complete_pose_clear(const PlayerState&,const Point&,double,const PlayerState&,const Point&,double)override{return complete;}
    void present(PlayerState&,double,const SliceTrace&,bool)override{++presentations;}
    bool pending_landing()const override{return false;}bool pending_damage()const override{return false;}bool terminal_environment()const override{return false;}
};
}
extern "C" int player_driver_fixture(const double*constants,const double*initial,const double*input,const double*conditions,const double*policy,double*out){try{
    PlayerDescriptor descriptor;auto&d=descriptor.motion;auto&law=d.drive;unsigned n=0;for(auto&v:law.t)v=constants[n++];double*fields[]={&law.mass,&law.power,&law.ratio,&law.forward,&law.reverse,&law.friction,&law.brake,&law.rotation};for(auto*v:fields)*v=constants[n++];for(auto&v:law.terrain)v=constants[n++];
    d.half_length=3.;d.half_width=1.5;d.track_center=1.4;d.collision_shape={1.5,3.,-.5,2.};d.body_minimum={-1.5,-.5,-3.};d.body_maximum={1.5,2.,3.};
    for(auto&v:d.glancing_yaws)v=constants[n++];d.hard_brake_decay=constants[n++];d.hard_stop_speed=constants[n++];d.hard_entry_factor=constants[n++];d.hard_slide_decay=constants[n++];d.hard_grind_ticks=int(constants[n++]);
    d.side_min_y=constants[n++];d.side_full_y=constants[n++];d.side_min=constants[n++];d.side_full=constants[n++];descriptor.slide_drag=constants[n++];descriptor.slide_max=constants[n++];descriptor.network_seconds=.1;
    descriptor.ground_follow_base=.6;descriptor.ground_follow_min=.8;descriptor.ground_follow_max=2.5;descriptor.rotation_around_center=constants[n++]!=0.;
    descriptor.gun_yaw_limits=std::array<double,2>{-.4,.4};descriptor.gun_traverse_epsilon=.0001;
    PlayerState state;n=0;state.position={initial[n],initial[n+1],initial[n+2]};n+=3;state.yaw=initial[n++];state.pitch=state.terrain_pitch=initial[n++];state.roll=initial[n++];state.speed=initial[n++];state.turn_speed=initial[n++];state.vertical_speed=initial[n++];state.pitch_speed=initial[n++];state.roll_speed=initial[n++];state.airborne=initial[n++]!=0.;state.direction_command=initial[n++];state.service_brake=initial[n++]!=0.;state.grind=int(initial[n++]);
    state.air_lateral_x=initial[n++];state.air_lateral_z=initial[n++];state.slide_speed=initial[n++];state.slope_tangent=initial[n++];state.downhill={initial[n],initial[n+1]};n+=2;
    if(initial[n++]){step::SupportPlane p;p.gradient_x=initial[n++];p.gradient_z=initial[n++];p.center_y=0.;state.ground_plane=p;}
    const int siege=int(initial[n]);if(siege>=0)state.siege_braking=siege!=0.;
    PlayerDriver driver(descriptor,state);auto&controls=driver.controls();controls.forward=input[0];controls.turn=input[1];controls.handbrake=input[2]!=0.;controls.autorotation=input[3]!=0.;controls.aim_yaw=input[4];
    World world;auto&c=world.flags;c.ready=conditions[0]!=0.;c.alive=conditions[1]!=0.;c.crew_active=conditions[2]!=0.;c.battle_over=conditions[3]!=0.;c.tracked=conditions[4]!=0.;c.engine_dead=conditions[5]!=0.;c.overturned=conditions[6]!=0.;c.siege_locked=conditions[7]!=0.;c.mobility=conditions[8];c.traverse=conditions[9];c.power=conditions[10];c.speed=conditions[11];
    n=0;const double dt=policy[n++];world.slope=policy[n++];world.fraction=policy[n++];world.arena=policy[n++]!=0.;world.sweep=policy[n++]!=0.;world.complete=policy[n++]!=0.;world.detailed=policy[n++]!=0.;world.blocked=policy[n++]!=0.;world.guard=policy[n++]!=0.;world.air=int(policy[n++]);world.dy=policy[n++];world.impulse=policy[n++];world.correction={policy[n],policy[n+1],policy[n+2]};n+=3;world.held=policy[n++]!=0.;world.recover=policy[n++]!=0.;const bool normal=policy[n++]!=0.;Point hitnormal{policy[n],policy[n+1],policy[n+2]};n+=3;if(normal)world.normal=hitnormal;const unsigned count=unsigned(policy[n++]);for(unsigned i=0;i<count;++i)world.statuses.push_back(int(policy[n++]));
    driver.advance(world,dt);const auto&s=driver.state();n=0;for(double v:s.position)out[n++]=v;for(double v:{s.yaw,s.pitch,s.roll,s.speed,s.turn_speed,s.vertical_speed,s.pitch_speed,s.roll_speed,s.direction_command,double(s.service_brake),double(s.grind),s.air_lateral_x,s.air_lateral_z,s.slide_speed,s.drive_throttle,s.drive_turn,s.reaction_rate,double(s.airborne),double(s.support_blocked),double(world.presentations)})out[n++]=v;return int(n);
}catch(...){return -1;}}
