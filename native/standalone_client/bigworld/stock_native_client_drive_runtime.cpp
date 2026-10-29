#include "stock_native_client_drive_runtime.hpp"
#include <algorithm>
#include <cmath>
#include <stdexcept>
#include <limits>
namespace stock_native_client_drive {
namespace {
constexpr double pi=3.14159265358979323846;
double angle(double value){while(value>pi)value-=2.*pi;while(value<-pi)value+=2.*pi;return value;}
int sign(double value){return value>0.?1:value<0.?-1:0;}
double bleed(double value,double budget){return budget<=0.?value:value>budget?value-budget:value<-budget?value+budget:0.;}
double grip(const PlayerDescriptor&d,double normal){return normal>=d.motion.side_full_y?d.motion.side_full:normal<=d.motion.side_min_y?d.motion.side_min:
    d.motion.side_min+(normal-d.motion.side_min_y)/(d.motion.side_full_y-d.motion.side_min_y)*(d.motion.side_full-d.motion.side_min);}
Vec horizontal_normal(const Point&normal){const double length=std::hypot(normal[0],normal[2]);if(!std::isfinite(length)||length<=1.e-6*std::max(1.,std::abs(normal[1])))return {};return {normal[0]/length,normal[2]/length};}
Point contact_velocity(Point velocity,const Point&normal){const auto n=horizontal_normal(normal);const double closing=-(velocity[0]*n[0]+velocity[2]*n[1]);if(closing>0.){velocity[0]+=closing*n[0];velocity[2]+=closing*n[1];}return velocity;}
void commit_plane(PlayerState&s,const step::SupportPlane&p,double yaw){s.ground_plane=p;const double sn=std::sin(yaw),cs=std::cos(yaw),f=p.gradient_x*sn+p.gradient_z*cs,r=p.gradient_x*cs-p.gradient_z*sn;
    s.pitch=s.terrain_pitch=-std::atan2(f,1.);s.roll=std::atan2(r,std::sqrt(1.+f*f));s.slope_tangent=std::hypot(p.gradient_x,p.gradient_z);
    s.downhill=s.slope_tangent>.001?Vec{-p.gradient_x/s.slope_tangent,-p.gradient_z/s.slope_tangent}:Vec{};s.surface_up_cosine=1./std::sqrt(1.+s.slope_tangent*s.slope_tangent);}
void restore_vertical(PlayerState&s,const PlayerState&saved){s.vertical_speed=saved.vertical_speed;s.airborne=saved.airborne;s.grounded_once=saved.grounded_once;s.pitch=saved.pitch;s.terrain_pitch=saved.terrain_pitch;s.roll=saved.roll;
    s.pitch_speed=saved.pitch_speed;s.roll_speed=saved.roll_speed;s.support_vertical_speed=saved.support_vertical_speed;s.support_gradient=saved.support_gradient;
    s.left_flying=saved.left_flying;s.right_flying=saved.right_flying;s.surface_up_cosine=saved.surface_up_cosine;s.downhill=saved.downhill;s.slope_tangent=saved.slope_tangent;
    s.slide_speed=saved.slide_speed;s.air_lateral_x=saved.air_lateral_x;s.air_lateral_z=saved.air_lateral_z;s.turn_speed=saved.turn_speed;s.drive_turn=saved.drive_turn;s.ground_plane=saved.ground_plane;}
}
double pivot_offset(const PlayerDescriptor&d,double speed,double omega,bool airborne,double throttle){if(airborne||d.rotation_around_center||std::abs(omega)<=1.e-9)return 0.;
    const double radius=std::max(0.,d.motion.track_center-std::abs(speed/omega));const double direction=speed<-1.e-9||(std::abs(speed)<=1.e-9&&throttle<0.)?-1.:1.;return radius*direction*(omega>0.?1.:-1.);}
Vec push_step(const PlayerDescriptor&d,const PlayerState&s,Vec push,double dt,bool rolling){if(dt<=0.)return push;const double normal=std::cos(s.pitch)*std::cos(s.roll);
    const auto&law=d.motion.drive;const double rr=law.rolling(0,0.)*law.mass/std::max(law.mass,1.),hold=law.t[18]*law.t[0]*std::max(normal,.1);
    const double forward_budget=rolling?rr:std::max(hold,rr),side_budget=std::max(rr,hold*grip(d,normal));const double sn=std::sin(s.yaw),cs=std::cos(s.yaw);
    const double f=bleed(push[0]*sn+push[1]*cs,forward_budget*dt),r=bleed(push[0]*cs-push[1]*sn,side_budget*dt);return {f*sn+r*cs,f*cs-r*sn};}
std::optional<FixedReaction> fixed_reaction(const PlayerDescriptor&d,const PlayerState&s,const Point&position,double yaw,double turn,double dt,const ContactTrace&trace,double previous,double traverse_factor){
    if(dt<=0.||turn==0.||d.motion.drive.mass<=0.||!trace.point||!trace.normal)return {};
    const auto&n=*trace.normal;const double length=std::sqrt(n[0]*n[0]+n[1]*n[1]+n[2]*n[2]),horizontal=std::hypot(n[0],n[2]);
    if(!std::isfinite(length)||length<=1.e-9||horizontal<=1.e-9||std::abs(n[1])/length>.65)return {};
    const double nx=n[0]/horizontal,nz=n[2]/horizontal,width=d.motion.collision_shape[0],chassis_length=d.motion.collision_shape[1],sn=std::sin(yaw),cs=std::cos(yaw);
    std::vector<Vec> corners;for(double x:{d.motion.body_minimum[0],d.motion.body_maximum[0]})for(double z:{d.motion.body_minimum[2],d.motion.body_maximum[2]})corners.push_back({cs*x+sn*z,-sn*x+cs*z});
    const double plane=((*trace.point)[0]-position[0])*nx+((*trace.point)[2]-position[2])*nz;double gap=std::numeric_limits<double>::infinity();for(const auto&p:corners)gap=std::min(gap,p[0]*nx+p[1]*nz-plane);
    if(gap>.011||gap<-.25)return {};auto law=d.motion.drive;law.rotation*=traverse_factor;
    const double omega=law.traverse(0.,turn,s.speed,std::max(dt,law.t[24]),0,s.drive_throttle);
    const double torque=std::abs(law.engine(std::abs(s.speed)+std::abs(omega)*width,turn,s.pitch))*(1.-.75*std::min(1.,std::abs(s.drive_throttle)))*width;
    if(omega==0.||torque<=0.)return {};const double direction=omega>0.?1.:-1.;double arm=0.,radius=0.;for(const auto&p:corners){arm=std::max(arm,-direction*(p[1]*nx-p[0]*nz));radius=std::max(radius,std::hypot(p[0],p[1]));}
    if(arm<=1.e-9)return {};arm+=radius*std::min(std::abs(omega)*dt,pi);const double inertia=law.mass*(width*width+chassis_length*chassis_length)/3.+law.mass*arm*arm;
    const double additional=std::max(0.,std::abs(omega)*width-std::abs(s.speed)),resistance=law.rolling(0,1.)*law.mass*additional/std::abs(omega),net=std::max(0.,torque-resistance);
    if(net<=0.)return {};const double initial=std::min(std::abs(omega),std::max(0.,direction*previous)),final=std::min(std::abs(omega),initial+net/inertia*dt),delta=direction*(initial+final)*.5*dt;
    if(std::abs(delta)<=1.e-10)return {};const double distance=arm*std::abs(delta);return FixedReaction{{nx*distance,nz*distance},{nx,nz},delta,direction*final,torque,net,inertia,arm,gap,law.mass*arm*(final-initial)};
}
PlayerDriver::PlayerDriver(PlayerDescriptor descriptor,PlayerState state):descriptor_(std::move(descriptor)),state_(std::move(state)){
    stock_native_bot_motion::validate(descriptor_.motion);
    if(!std::isfinite(descriptor_.network_seconds)||descriptor_.network_seconds<=0.)throw std::invalid_argument("Player input publication cadence is invalid");
}
void PlayerDriver::landing_admitted(){input_elapsed_=std::fmod(input_elapsed_,descriptor_.network_seconds);}
void PlayerDriver::canonicalize(double&yaw){double pitch=state_.pitch,roll=state_.roll;if(pitch>pi||pitch<-pi){pitch=std::fmod(pitch+pi,2.*pi);if(pitch<0.)pitch+=2.*pi;pitch-=pi;}
    bool inverted=false;if(pitch>pi*.5){pitch=pi-pitch;inverted=true;}else if(pitch<-pi*.5){pitch=-pi-pitch;inverted=true;}
    if(inverted){yaw+=pi;roll+=pi;state_.speed=-state_.speed;state_.pitch_speed=-state_.pitch_speed;state_.drive_pitch_history.clear();state_.smooth_drive_pitch=0.;}
    state_.pitch=state_.terrain_pitch=pitch;state_.roll=angle(roll);yaw=angle(yaw);
}
Point PlayerDriver::slide(PlayerWorld&world,Point position,double yaw,double dt,bool suspension){auto&s=state_;const auto&d=descriptor_;const auto&law=d.motion.drive;
    if(s.airborne||(suspension&&!s.ground_plane)){s.slide_speed=0.;double x=s.air_lateral_x,z=s.air_lateral_z;if(std::abs(x)<=.0001&&std::abs(z)<=.0001)return position;
        const double speed=std::hypot(x,z),travel_yaw=std::atan2(x,z);if(world.motion(s,position,travel_yaw,speed,dt,false,yaw)){position[0]+=x*dt;position[2]+=z*dt;}
        else if(s.trace.status!=ContactStatus::Soft){world.impact(s,s.trace,speed,travel_yaw);if(s.trace.normal){const auto v=contact_velocity({x,s.vertical_speed,z},*s.trace.normal);x=v[0];s.vertical_speed=v[1];z=v[2];}else x=z=0.;}
        s.air_lateral_x=x;s.air_lateral_z=z;return position;}
    const double theta=std::atan(s.slope_tangent),hold=law.t[18]*(suspension?grip(d,std::cos(theta)):1.);
    if(s.slope_tangent<=hold)s.slide_speed=std::max(0.,s.slide_speed-law.t[2]*law.t[0]*dt);
    else s.slide_speed=std::max(0.,std::min(d.slide_max,s.slide_speed+(law.t[0]*(std::sin(theta)-hold*std::cos(theta))-d.slide_drag*s.slide_speed)*dt));
    const double rx=std::cos(yaw),rz=-std::sin(yaw),dot=s.downhill[0]*rx+s.downhill[1]*rz,x=rx*dot,z=rz*dot;
    s.air_lateral_x=x*s.slide_speed;s.air_lateral_z=z*s.slide_speed;if(s.slide_speed<=.01||(std::abs(x)<=.0001&&std::abs(z)<=.0001))return position;
    const double speed=std::abs(dot)*s.slide_speed;if(!world.motion(s,position,std::atan2(x,z),speed,dt,false,yaw)){if(s.trace.status!=ContactStatus::Soft){s.slide_speed=s.air_lateral_x=s.air_lateral_z=0.;}return position;}
    auto end=position;end[0]+=x*s.slide_speed*dt;end[2]+=z*s.slide_speed*dt;if(suspension)return end;
    if(!s.ground_plane)return position;const auto previous=*s.ground_plane;const double expected=previous.center_y+previous.gradient_x*(end[0]-position[0])+previous.gradient_z*(end[2]-position[2]);
    const double pitch=std::atan(s.slope_tangent*std::abs(dot));const double follow=std::max(d.ground_follow_min,std::min(d.ground_follow_max,d.ground_follow_base+
        std::max(0.,speed*std::tan(std::max(-.96,std::min(.96,pitch)))*dt)+law.t[0]*dt*dt));
    const auto plane=world.sample_plane(end,yaw);if(!plane){const auto support=world.terrain_support(end,yaw);const auto height=support[1]?support[1]:support[0];
        if(height&&*height>expected+.35)return position;if(height&&expected-*height<=follow){auto bridged=previous;bridged.center_y=expected;commit_plane(s,bridged,yaw);s.vertical_speed=0.;s.airborne=false;end[1]=expected;return end;}}
    else{const double delta=plane->center_y-expected;if(std::abs(delta)<=.35){commit_plane(s,*plane,yaw);s.vertical_speed=0.;s.airborne=false;end[1]=plane->center_y;return end;}if(delta>=-follow)return position;}
    s.ground_plane.reset();s.surface_up_cosine.reset();s.vertical_speed=-law.t[0]*dt;s.airborne=true;end[1]+=s.vertical_speed*dt;return end;
}
Point PlayerDriver::resettle(PlayerWorld&world,const Point&start,Point position,double yaw){const double x=position[0]-start[0],z=position[2]-start[2];if(x*x+z*z<=1.e-10)return position;
    auto gradient=state_.support_gradient;if(state_.ground_plane)gradient=std::array<double,2>{state_.ground_plane->gradient_x,state_.ground_plane->gradient_z};
    const double delta=gradient?std::max(0.,(*gradient)[0]*x+(*gradient)[1]*z):0.;world.vertical(state_,position,yaw,0.,position,start,delta,0.);return position;}
bool PlayerDriver::slice(PlayerWorld&world,double dt){auto&s=state_;const auto&d=descriptor_;const auto condition=world.conditions();if(!condition.ready)return false;
    dt=std::max(0.,std::min(.1,dt));SliceTrace trace;trace.initial_position=s.position;trace.initial_yaw=s.yaw;
    if(condition.battle_over||!condition.alive||(!condition.has_alive_method&&!condition.crew_active)){s.speed=s.turn_speed=s.drive_turn=s.drive_throttle=s.direction_command=0.;s.service_brake=false;controls_.forward=controls_.turn=0.;world.present(s,dt,trace,true);return true;}
    auto position=s.position;const auto tick=position;double yaw=s.yaw;canonicalize(yaw);s.yaw=yaw;const auto before=s;const double tick_yaw=yaw;double motion_yaw=yaw;const bool airborne_at_start=s.airborne;
    const bool turret_guard=world.detached_turrets();if(turret_guard)world.checkpoint();
    double slope=0.;if(!s.airborne){const double raw=world.drive_pitch(s);if(s.drive_pitch_history.empty())s.drive_pitch_history.assign(5,raw);s.drive_pitch_history.push_back(raw);while(s.drive_pitch_history.size()>5)s.drive_pitch_history.pop_front();
        auto values=s.drive_pitch_history;std::sort(values.begin(),values.end());slope=s.smooth_drive_pitch+(values[2]-s.smooth_drive_pitch)*.5;s.smooth_drive_pitch=slope;s.last_drive_pitch=slope;}
    const bool siege_locked=condition.siege_locked;if(condition.engine_dead)s.siege_braking.reset();const bool siege_braking=s.siege_braking.has_value();
    if(condition.overturned)controls_.forward=controls_.turn=0.;double throttle=siege_locked||siege_braking||condition.overturned?0.:controls_.forward;
    double turn=siege_locked||siege_braking||condition.overturned||s.airborne?0.:controls_.turn;
    if(turn==0.&&throttle==0.&&!controls_.handbrake&&controls_.autorotation&&d.gun_yaw_limits){double relative=world.local_aim_yaw(s,controls_).value_or(controls_.aim_yaw-yaw);relative=angle(relative);
        if(relative<(*d.gun_yaw_limits)[0]-d.gun_traverse_epsilon)turn=-1.;else if(relative>(*d.gun_yaw_limits)[1]+d.gun_traverse_epsilon)turn=1.;}
    if(condition.tracked||condition.engine_dead)throttle=0.;else if(throttle!=0.)throttle*=condition.mobility;
    const bool hand=(controls_.handbrake&&!condition.overturned)||condition.tracked||siege_locked||siege_braking;
    s.service_brake=sign(throttle)&&throttle*s.speed<0.&&(s.service_brake||sign(throttle)!=sign(s.direction_command));s.direction_command=throttle;
    const double previous=s.speed;trace.previous_speed=previous;trace.slope_pitch=slope;
    if(siege_locked&&!s.airborne)s.speed=s.turn_speed=0.;else{auto law=d.motion.drive;law.power*=condition.power;law.forward*=condition.speed;law.reverse*=condition.speed;s.speed=law.longitudinal(s.speed,throttle,turn,slope,dt,s.airborne,0,hand,s.service_brake);}
    trace.drive_speed=s.speed;
    if(std::abs(s.speed)>.0001&&dt>0.){if(world.motion(s,position,yaw,s.speed,dt,throttle*s.speed>0.&&!hand)){position[0]+=std::sin(yaw)*s.speed*dt;position[2]+=std::cos(yaw)*s.speed*dt;s.grind=std::max(0,s.grind-1);trace.path="advance";}
        else if(!s.airborne){trace.primary=s.trace;if(s.trace.status==ContactStatus::CapCrushed){s.speed=previous;s.grind=1;trace.path="cap_hold";}
            else if(s.trace.status==ContactStatus::Soft){s.grind=1;trace.path="soft_hold";}
            else{world.impact(s,*trace.primary,s.speed,yaw);std::optional<Vec>normal;if(trace.primary->normal){auto n=horizontal_normal(*trace.primary->normal);if(std::hypot(n[0],n[1])>0.){const double direction=s.speed<0.?-1.:1.;if(direction*(std::sin(yaw)*n[0]+std::cos(yaw)*n[1])>0.){n[0]=-n[0];n[1]=-n[1];}normal=n;}}
                bool deflected=false;for(double delta:d.motion.glancing_yaws){const double candidate=yaw+delta,direction=s.speed<0.?-1.:1.;if(trace.primary->normal&&(!normal||direction*(std::sin(candidate)*(*normal)[0]+std::cos(candidate)*(*normal)[1])<-1.e-9))continue;
                    if(world.motion(s,position,candidate,s.speed,dt,false,yaw)){if(s.grind<=0)s.speed*=d.motion.hard_entry_factor;s.speed*=std::pow(d.motion.hard_slide_decay,dt*60.);position[0]+=std::sin(candidate)*s.speed*dt;position[2]+=std::cos(candidate)*s.speed*dt;s.grind=d.motion.hard_grind_ticks;deflected=true;trace.path="deflect";break;}}
                if(!deflected){s.speed*=std::pow(d.motion.hard_brake_decay,dt*60.);if(std::abs(s.speed)<d.motion.hard_stop_speed)s.speed=0.;s.grind=d.motion.hard_grind_ticks;trace.path="brake";}}}
        else{trace.primary=s.trace;if(s.trace.status!=ContactStatus::Soft){world.impact(s,*trace.primary,s.speed,yaw);if(trace.primary->normal){const double sn=std::sin(yaw),cs=std::cos(yaw);const auto v=contact_velocity({sn*s.speed,s.vertical_speed,cs*s.speed},*trace.primary->normal);s.speed=v[0]*sn+v[2]*cs;s.vertical_speed=v[1];s.air_lateral_x=v[0]-sn*s.speed;s.air_lateral_z=v[2]-cs*s.speed;}
                else{s.speed=s.air_lateral_x=s.air_lateral_z=0.;}trace.path="air_contact";}}}
    trace.horizontal_speed=s.speed;if(siege_locked||condition.overturned||condition.tracked||condition.engine_dead){turn=0.;if(!s.airborne)s.turn_speed=0.;}
    s.drive_turn=turn;s.drive_throttle=throttle;s.contact_reaction_paid=false;if(turn==0.||s.airborne)s.reaction_rate=0.;
    if(!siege_locked&&!s.airborne)s.turn_speed=d.motion.drive.traverse(s.turn_speed,turn,s.speed,dt,0,throttle)*condition.traverse;
    double pivot=pivot_offset(d,s.speed,s.turn_speed,s.airborne,throttle),candidate=angle(yaw+s.turn_speed*dt);
    if(std::abs(angle(candidate-yaw))>1.e-9){const double allowed=world.rotation_fraction(s,position,yaw,candidate,pivot,dt);if(allowed<1.){const auto recovery=world.held_peer_turn(s,position,yaw,turn,dt);
        if(recovery){position=recovery->position;yaw=recovery->yaw;candidate=yaw;pivot=0.;s.turn_speed=s.reaction_rate;if(trace.path.empty())trace.path="held_peer_reaction_turn";}
        else{candidate=yaw+angle(candidate-yaw)*allowed;s.turn_speed=0.;if(trace.path.empty())trace.path="tank_turn_contact";}}}
    Point candidate_position=position;candidate_position[0]+=pivot*(std::cos(yaw)-std::cos(candidate));candidate_position[2]+=pivot*(std::sin(candidate)-std::sin(yaw));
    if(world.arena_rotation(s,position,yaw,candidate_position,candidate)){const bool changed=std::abs(angle(candidate-yaw))>1.e-8;if(changed)s.trace={};
        if(!changed||s.airborne||world.pose_sweep(s,position,yaw,candidate_position,candidate,s.speed,dt)){if(!s.contact_reaction_paid)s.reaction_rate=0.;position=candidate_position;yaw=candidate;if(changed&&s.trace.status==ContactStatus::Crushed&&trace.path.empty())trace.path="turn_crush";}
        else{const auto primary=s.trace;const auto recovery=world.world_turn(s,position,yaw,turn,dt,primary);if(recovery){position=recovery->position;yaw=recovery->yaw;s.turn_speed=s.reaction_rate;if(trace.path.empty())trace.path="world_reaction_turn";}
            else{s.turn_speed=0.;if(trace.path.empty())trace.path="turn_contact";}}}
    else{s.turn_speed=0.;s.trace.kinds="arena";s.trace.status=ContactStatus::Hard;if(trace.path.empty())trace.path="arena_turn";}
    bool suspension_active=world.detailed_suspension();s.support_blocked=false;s.suspension_slide_applied=false;
    const auto vertical_before=s;if(suspension_active){if(dt>0.){position=slide(world,position,yaw,dt,true);s.suspension_slide_applied=true;}world.vertical(s,position,yaw,dt,tick,tick);
        if(s.support_blocked&&s.suspension_slide_applied){s.slide_speed=vertical_before.slide_speed;s.air_lateral_x=vertical_before.air_lateral_x;s.air_lateral_z=vertical_before.air_lateral_z;}}
    else world.vertical(s,position,yaw,dt,tick,tick);
    if((pivot!=0.||s.contact_reaction_paid)&&s.support_blocked){yaw=tick_yaw;s.turn_speed=s.drive_turn=s.reaction_rate=0.;s.contact_reaction_paid=false;}
    double uncanonical=yaw;canonicalize(yaw);motion_yaw+=angle(yaw-uncanonical);
    const bool slide_applied=s.suspension_slide_applied;suspension_active=world.detailed_suspension();bool support_blocked=s.support_blocked,ram_resolved=false,slide_moved=false;
    if(suspension_active){const auto settled=position;position=world.contacts(s,position,yaw,dt,tick);ram_resolved=true;position=resettle(world,settled,position,yaw);support_blocked=support_blocked||s.support_blocked;suspension_active=world.detailed_suspension();
        if(suspension_active&&!support_blocked&&!slide_applied){const auto start=position;position=slide(world,position,yaw,dt,true);const double x=position[0]-start[0],z=position[2]-start[2];slide_moved=x*x+z*z>1.e-10;position=resettle(world,start,position,yaw);support_blocked=support_blocked||s.support_blocked;suspension_active=world.detailed_suspension();}s.support_blocked=support_blocked;}
    if(support_blocked){s.speed*=std::pow(.35,dt*60.);if(std::abs(s.speed)<.05)s.speed=0.;s.grind=4;trace.path="support";}
    if(!suspension_active&&!s.suspension_failed){if(!s.airborne)world.ground_pose(s,position,yaw);if(!slide_moved)position=slide(world,position,yaw,dt,false);if(!ram_resolved)position=world.contacts(s,position,yaw,dt,tick);}
    if(turret_guard&&!world.complete_pose_clear(before,tick,tick_yaw,s,position,yaw)){restore_vertical(s,before);world.rollback_suspension();position=tick;yaw=tick_yaw;s.speed=s.turn_speed=s.drive_turn=s.drive_throttle=s.direction_command=s.push_x=s.push_z=0.;s.service_brake=false;s.trace.kinds="detached_turret";s.trace.status=ContactStatus::Hard;trace.path="detached_turret";}
    uncanonical=yaw;canonicalize(yaw);motion_yaw+=angle(yaw-uncanonical);if(airborne_at_start&&s.airborne){const double x=std::sin(motion_yaw)*s.speed+s.air_lateral_x,z=std::cos(motion_yaw)*s.speed+s.air_lateral_z,sn=std::sin(yaw),cs=std::cos(yaw);s.speed=x*sn+z*cs;s.air_lateral_x=x-sn*s.speed;s.air_lateral_z=z-cs*s.speed;}
    s.position=position;s.yaw=yaw;world.present(s,dt,trace,false);return false;
}
Publication PlayerDriver::advance(PlayerWorld&world,double elapsed){if(!std::isfinite(elapsed))throw std::invalid_argument("Player elapsed time is nonfinite");elapsed=std::max(0.,elapsed);input_sent_=false;double remaining=elapsed;bool stopped=false;
    if(remaining<=0.)stopped=slice(world,0.);while(remaining>1.e-7&&!stopped){const double dt=std::min(.1,remaining);stopped=slice(world,dt);remaining=std::max(0.,remaining-dt);}
    Publication out;out.stopped=stopped;if(stopped){state_.siege_braking.reset();out.landing=world.pending_landing();out.input=out.landing||world.pending_damage()||world.terminal_environment();return out;}
    if(state_.siege_braking&&state_.speed==0.&&state_.turn_speed==0.)out.siege_request=state_.siege_braking;
    if(input_sent_)input_elapsed_=0.;else input_elapsed_+=elapsed;
    if(world.pending_landing()){out.landing=true;out.input=true;}
    else if(!input_sent_&&input_elapsed_>=descriptor_.network_seconds){input_elapsed_=std::fmod(input_elapsed_,descriptor_.network_seconds);out.input=true;}
    return out;
}
}
