#include "stock_native_bot_planner.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <stdexcept>
#include <tuple>

namespace stock_native_bot_planner {
namespace {
constexpr double pi = 3.141592653589793238462643383279502884;
using V2 = std::array<double, 2>;
double delta(double target, double current) {
    double v = target-current;
    while (v > pi) v -= 2.*pi;
    while (v < -pi) v += 2.*pi;
    return v;
}
double clamp(double v, double a, double b) { return std::max(a,std::min(b,v)); }
double round_two_decimals(double value) {
    if(value==0.||!std::isfinite(value))return value;
    // Evaluate the exact binary significand times 100, rather than rounding
    // an already rounded double product (2.675 * 100 becomes exactly 267.5).
    int exponent=0;
    const double fraction=std::frexp(std::abs(value),&exponent);
    const auto numerator=static_cast<std::uint64_t>(std::ldexp(fraction,53))*25u;
    const int shift=exponent-51;
    if(shift>=0)return value;
    const int bits=-shift;
    std::uint64_t integral=0;
    if(bits<64) {
        integral=numerator>>bits;
        const auto remainder=numerator&((std::uint64_t(1)<<bits)-1u);
        if(remainder>=(std::uint64_t(1)<<(bits-1)))++integral;
    }
    return std::copysign(static_cast<double>(integral)/100.,value);
}
double distance(const Point &a,const Point &b) { return std::hypot(a[0]-b[0],a[2]-b[2]); }
double yaw_to(const Point &a,const Point &b) { return std::atan2(b[0]-a[0],b[2]-a[2]); }
double dot(V2 a,V2 b) { return a[0]*b[0]+a[1]*b[1]; }
V2 forward(double yaw) { return {{std::sin(yaw),std::cos(yaw)}}; }
V2 side(double yaw) { return {{std::cos(yaw),-std::sin(yaw)}}; }
double radius(const Body &b,V2 axis) {
    return b.half_width*std::abs(dot(side(b.yaw),axis))+
        b.half_length*std::abs(dot(forward(b.yaw),axis));
}
std::array<double,3> overlap(const Body &a,const Body &b) {
    std::array<double,3> hit{{0.,0.,std::numeric_limits<double>::infinity()}};
    const V2 d{{a.position[0]-b.position[0],a.position[2]-b.position[2]}};
    for (auto axis: {side(a.yaw),forward(a.yaw),side(b.yaw),forward(b.yaw)}) {
        const double signed_distance=dot(d,axis);
        const double depth=radius(a,axis)+radius(b,axis)-std::abs(signed_distance);
        if(depth<hit[2]) {
            if(signed_distance<0.) { axis[0]=-axis[0];axis[1]=-axis[1]; }
            hit={{axis[0],axis[1],depth}};
        }
    }
    return hit;
}
bool vertical(const Body &a,const Body &b) {
    return std::min(a.position[1]+a.upper_y,b.position[1]+b.upper_y) >
        std::max(a.position[1]+a.lower_y,b.position[1]+b.lower_y)+.02;
}
// LocalDriver's exact longitudinal swept rectangle. Existing contact is
// ignored only when travel separates both the first contact face and centres.
Optional<std::int64_t> reverse_blocker(const Body &body,double yaw,
    const std::vector<Body> &peers,double length=-1.) {
    const double reach=length<0.?std::max(0.,body.half_length)*1.6:std::max(0.,length);
    Body own=body;own.yaw=yaw;
    Body sweep=own;
    const V2 back{{-std::sin(yaw),-std::cos(yaw)}};
    sweep.position[0]+=back[0]*reach*.5;sweep.position[2]+=back[1]*reach*.5;
    sweep.half_length+=reach*.5;
    for(const auto &peer:peers) {
        if(std::abs(peer.position[1]-body.position[1])>5.)continue;
        const auto hit=overlap(own,peer);
        const V2 away{{body.position[0]-peer.position[0],body.position[2]-peer.position[2]}};
        if(hit[2]>=-1.e-9 && dot(back,{{hit[0],hit[1]}})>=-1.e-9 && dot(back,away)>=-1.e-9)continue;
        if(overlap(sweep,peer)[2]>=0.) { Optional<std::int64_t> out;out.set(peer.id);return out; }
    }
    return {};
}
bool static_ahead(const Body &body,double yaw,const std::vector<Body>&peers) {
    Body sweep=body;sweep.yaw=yaw;
    const double reach=body.half_length*1.6;
    sweep.position[0]+=std::sin(yaw)*reach*.5;sweep.position[2]+=std::cos(yaw)*reach*.5;
    sweep.half_length+=reach*.5;
    for(const auto &peer:peers) {
        if(peer.alive&&(!peer.velocity_known||std::hypot(peer.velocity[0],peer.velocity[2])>.65))continue;
        if(std::abs(peer.position[1]-body.position[1])>5.)continue;
        if(overlap(sweep,peer)[2]>=0.)return true;
    }
    return false;
}
int occupancy(const Body &body,double sign,const std::vector<Body>&peers) {
    int count=0;
    for(const auto &peer:peers) {
        if(std::abs(peer.position[1]-body.position[1])>5.)continue;
        for(double f:{.25,.5,.75,1.}) {
            Body sample=body;sample.yaw+=sign*.85*f;
            if(overlap(sample,peer)[2]>=0.)++count;
        }
    }
    return count;
}
double phase(const DriverState&s) { return (double((s.slot*7)%15)+.5)/15.; }
double fallback(const DriverState&s) { return ((s.slot+s.recovery_count)&1)?1.:-1.; }
double recovery_side(const DriverState&s,const Body &body,const std::vector<Body>&peers) {
    const int negative=occupancy(body,-1.,peers),positive=occupancy(body,1.,peers);
    return negative<positive?-1.:positive<negative?1.:fallback(s);
}
int yaw_key(double yaw) {
    double value=std::fmod(yaw+pi,2.*pi);if(value<0.)value+=2.*pi;
    return int(std::floor(value*24./(2.*pi)+.5))%24;
}
double penalty(DriverState&s,double yaw) {
    const auto i=s.failed_yaws.find(yaw_key(yaw));
    if(i==s.failed_yaws.end())return 0.;
    if(i->second<=s.clock) {s.failed_yaws.erase(i);return 0.;}
    return 3.+(i->second-s.clock)/2.;
}
void prune(DriverState&s) {
    for(auto i=s.failed_yaws.begin();i!=s.failed_yaws.end();)
        if(i->second<=s.clock)i=s.failed_yaws.erase(i);else ++i;
}
bool real_failure(const DriverState&s,double yaw) {
    const auto i=s.real_failed_yaws.find(yaw_key(yaw));
    return i!=s.real_failed_yaws.end()&&i->second>s.clock;
}
bool clear(World&w,const Input&in,double yaw,double maximum=-1.,double sign=1.) {
    Optional<double> reach;if(maximum>=0.)reach.set(maximum);
    return w.direction(in.body.id,yaw,reach,sign,in.implicit_probe_distance);
}
bool pivot(World&w,const Input&in,double side_value,double extent=.85) {
    for(double f:{.25,.5,.75,1.})
        if(!w.pose(in.body.id,in.body.yaw+side_value*extent*f,false))return false;
    return true;
}
Command local(const Input&in,const char*mode,double throttle=0.,double turn=0.) {
    Command c;c.bot_id=in.body.id;c.target_yaw=in.body.yaw;c.recovery=mode;
    c.throttle=throttle;c.turn=turn;return c;
}
bool retained_escape(DriverState&s,const Input&in,const std::vector<Body>&peers,
    World&w,bool short_move,Command&out) {
    auto &optional=short_move?s.short_escape:s.translation_escape;
    if(!optional.present)return false;
    const auto &e=optional.value;
    const double heading=e.yaw+(e.sign<0.?pi:0.);
    const double travelled=short_move?
        (in.body.position[0]-e.start[0])*std::sin(heading)+(in.body.position[2]-e.start[2])*std::cos(heading):
        distance(in.body.position,e.start);
    const double remaining=std::max(0.,e.distance-travelled);
    const bool failed=short_move?real_failure(s,heading):penalty(s,in.body.yaw)>0.;
    if(failed||remaining<=.05||s.clock>=e.until||std::abs(delta(in.body.yaw,e.yaw))>.30||
        !clear(w,in,short_move?heading:in.body.yaw,remaining)||
        reverse_blocker(in.body,e.sign<0.?in.body.yaw:in.body.yaw+pi,peers,remaining).present) {
        optional.clear();return false;
    }
    out=local(in,short_move?(e.sign<0.?"short_reverse_escape":"short_forward_escape"):"forward_escape",e.sign);
    out.target_yaw=e.yaw;out.probe_distance.set(remaining);return true;
}
bool short_escape(DriverState&s,const Input&in,const std::vector<Body>&peers,World&w,Command&out) {
    const double minimum=std::max(.5,(in.stopping_distance.present?in.stopping_distance.value:0.)+
        std::abs(in.speed)*std::max(std::max(0.,in.dt),in.decision_horizon));
    for(double reach:{std::max(2.,minimum),std::max(1.,minimum),minimum})for(double sign:{-1.,1.}) {
        const double heading=in.body.yaw+(sign<0.?pi:0.);
        if(real_failure(s,heading)||!clear(w,in,heading,reach)||
            reverse_blocker(in.body,sign<0.?in.body.yaw:in.body.yaw+pi,peers,reach).present)continue;
        Escape e;e.start=in.body.position;e.yaw=in.body.yaw;e.sign=sign;e.distance=reach;e.until=s.clock+4.;
        s.short_escape.set(e);
        out=local(in,sign<0.?"short_reverse_escape":"short_forward_escape",sign);
        out.probe_distance.set(reach);return true;
    }
    return false;
}
bool partial_pivot(const Input&in,double sign,World&w,Command&out) {
    if(!in.turn_speed_limit.present||in.turn_speed_limit.value<=0.)return false;
    for(double f:{.5,.25,.125})for(double side_value:{sign,-sign})if(pivot(w,in,side_value,.85*f)) {
        out=local(in,"partial_pivot_recovery",0.,side_value*std::min(1.,.85*f/
            (in.turn_speed_limit.value*std::max(.001,std::max(in.dt,in.decision_horizon)))));
        out.target_yaw=in.body.yaw+side_value*.85*f;out.pivot_extent.set(.85*f);return true;
    }
    return false;
}
Command wait_navigation(DriverState&s,const Input&in,const std::vector<Body>&peers,World&w) {
    const double step=std::max(0.,in.dt);s.last_step=step;s.clock+=step;prune(s);
    if(!s.navigation_wait.present)s.navigation_wait.set(NavigationWait{});
    auto &wait=s.navigation_wait.value;wait.age+=step;
    auto out=local(in,"nav_wait");out.brake=true;
    if(wait.completed)return out;
    if(!wait.active) {
        if(!in.navigation_recovery_allowed||wait.age<4.+phase(s)*.42||std::abs(in.speed)>.35)return out;
        wait.active=true;wait.elapsed=0.;wait.origin=in.body.position;
        s.recovery_time=s.recovery_side=s.stuck_time=0.;s.steering_yaw.clear();s.progress_yaw.clear();
        s.braking_target.clear();s.alignment_target.clear();
    } else wait.elapsed+=step;
    const double travelled=distance(in.body.position,wait.origin),reach=std::max(.5,in.body.half_length)*1.6;
    const double remaining=std::max(0.,reach-travelled);
    bool finished=wait.elapsed>=.85+phase(s)*.28||travelled>=reach;
    if(!finished)finished=penalty(s,in.body.yaw+pi)>0.||!clear(w,in,in.body.yaw+pi,remaining,-1.)||
        reverse_blocker(in.body,in.body.yaw,peers,remaining).present;
    if(finished) {wait.completed=true;out.navigation_replan=true;return out;}
    out.throttle=-1.;out.brake=false;out.recovery="reverse_turn";out.navigation_recovery=true;return out;
}
bool replan(DriverState&s,const Input&in) {
    if(!s.recovery_count)return false;
    if(!s.replan.present||distance(in.body.position,s.replan.value.origin)>=2.*in.body.half_length+1.5) {
        Episode e;e.origin=in.body.position;e.at=s.clock;e.count=s.recovery_count;s.replan.set(e);
    }
    auto&e=s.replan.value;
    if(s.clock-(e.issued?e.issued_at:e.at)<8.||s.recovery_count<=(e.issued?e.issued_count:e.count))return false;
    e.issued=true;e.issued_at=s.clock;e.issued_count=s.recovery_count;return true;
}

bool mode_in(const std::string&s,std::initializer_list<const char*> names) {
    for(const char*name:names)if(s==name)return true;return false;
}
Optional<Contact> hull_contact(ActorMemory&m,const Input&in,const std::vector<Body>&peers) {
    for(const auto&peer:peers) {
        if(!vertical(in.body,peer))continue;
        const auto hit=overlap(in.body,peer);
        const double margin=!peer.alive||(m.contact_peer.present&&m.contact_peer.value==peer.id)?.25:.01;
        if(hit[2]>=-margin) {
            Contact c;c.peer=peer.id;c.team=peer.team;c.alive=peer.alive;c.position=peer.position;c.normal={{hit[0],hit[1]}};
            m.contact_peer.set(peer.id);Optional<Contact> result;result.set(c);return result;
        }
    }
    m.contact_peer.clear();return {};
}
ContactMemory& contact_memory(ActorMemory&m,const Input&in,const Contact&contact) {
    if(!m.contact_attempt.present||m.contact_attempt.value.contact.peer!=contact.peer||
        m.contact_attempt.value.contact.alive!=contact.alive||
        std::abs(delta(in.body.yaw,m.contact_attempt.value.yaw))>.30||
        distance(in.body.position,m.contact_attempt.value.position)>=2.*in.body.half_length+1.5||
        distance(contact.position,m.contact_attempt.value.contact.position)>=1.) {
        ContactMemory memory;memory.contact=contact;memory.position=in.body.position;memory.yaw=in.body.yaw;
        m.contact_attempt.set(memory);
    }
    return m.contact_attempt.value;
}
bool retain_contact(ActorMemory&m,const Input&in,const std::vector<Body>&peers,World&w,Command&out) {
    if(!m.contact_escape.present)return false;
    auto&e=m.contact_escape.value;
    const double heading=e.yaw+(e.sign<0.?pi:0.);
    const double progress=(in.body.position[0]-e.origin[0])*std::sin(heading)+
        (in.body.position[2]-e.origin[2])*std::cos(heading);
    const double remaining=std::max(0.,e.distance-progress),step=std::max(0.,in.dt);
    e.elapsed+=step;
    if(progress>=e.best_progress+.25) {e.best_progress=progress;e.stalled=0.;}else e.stalled+=step;
    const auto peer=std::find_if(peers.begin(),peers.end(),[&](const Body&b){return b.id==e.contact.peer;});
    const bool completed=remaining<=.05,peer_changed=peer==peers.end()||peer->alive!=e.contact.alive;
    const bool turned=std::abs(delta(in.body.yaw,e.yaw))>.30;
    bool failed=e.elapsed>=20.||e.stalled>=3.6;
    if(!completed&&!peer_changed&&!turned&&!failed) {
        const double current=in.body.yaw+(e.sign<0.?pi:0.);
        failed=!clear(w,in,current,std::min(remaining,std::max(.5,e.half_length)*1.6))||
            reverse_blocker(in.body,current+pi,peers).present;
    }
    if(completed||peer_changed||turned||failed) {
        if(!completed&&!peer_changed&&!turned&&m.contact_attempt.present&&m.contact_attempt.value.contact.peer==e.contact.peer) {
            if(e.sign<0.)m.contact_attempt.value.failed_reverse=true;else m.contact_attempt.value.failed_forward=true;
        }
        m.contact_escape.clear();return false;
    }
    out=local(in,"contact_escape",e.sign);out.target_yaw=e.yaw;out.move=e.target;
    out.probe_distance.set(std::min(remaining,std::max(.5,e.half_length)*1.6));return true;
}
bool choose_contact(ActorMemory&m,const Input&in,const Contact&contact,const std::vector<Body>&peers,World&w,Command&out) {
    if(m.contact_escape.present)return retain_contact(m,in,peers,w,out);
    const V2 f=forward(in.body.yaw);
    const double alignment=dot(f,contact.normal);
    const double offset=f[0]*(in.body.position[0]-contact.position[0])+f[1]*(in.body.position[2]-contact.position[2]);
    const double preferred=std::abs(alignment)>.01?(alignment>0.?1.:-1.):std::abs(offset)>.01?(offset>0.?1.:-1.):-1.;
    auto&memory=contact_memory(m,in,contact);
    for(double sign:{preferred,-preferred}) {
        if(sign<0.?memory.failed_reverse:memory.failed_forward)continue;
        const double heading=in.body.yaw+(sign<0.?pi:0.),reach=std::max(.5,in.body.half_length)*1.6;
        if(!clear(w,in,heading,reach)||reverse_blocker(in.body,heading+pi,peers).present)continue;
        ContactEscape e;e.contact=contact;e.origin=in.body.position;e.yaw=in.body.yaw;e.sign=sign;
        e.distance=2.*in.body.half_length+1.5;e.half_length=in.body.half_length;e.half_width=in.body.half_width;
        e.target={{in.body.position[0]+std::sin(heading)*e.distance,in.body.position[1],in.body.position[2]+std::cos(heading)*e.distance}};
        m.contact_escape.set(e);out=local(in,"contact_escape",sign);out.move=e.target;out.probe_distance.set(reach);return true;
    }
    return false;
}
bool rotation_clear(const Body&body,double target,const std::vector<Body>&peers) {
    const double angle=delta(target,body.yaw),corner=std::hypot(body.half_width,body.half_length);
    if(std::abs(angle)*corner<=1.e-9)return true;
    const int samples=std::max(1,int(std::ceil(std::abs(angle)*corner/.005)));
    for(const auto&peer:peers) {
        if(!vertical(body,peer)||distance(body.position,peer.position)>corner+std::hypot(peer.half_width,peer.half_length))continue;
        const double allowed=std::max(.01,overlap(body,peer)[2]);
        for(int i=1;i<=samples;++i) {
            Body rotated=body;rotated.yaw+=angle*double(i)/double(samples);
            if(overlap(rotated,peer)[2]>allowed+1.e-9)return false;
        }
    }
    return true;
}
bool wreck_plan(ActorMemory&m,const Input&in,const Order&order,const Point&goal,
    const Optional<Contact>&contact,const std::vector<Body>&peers,World&w,Command&out) {
    const bool wreck_contact=contact.present&&!contact.value.alive;
    const bool side_wreck=wreck_contact&&std::abs(dot(forward(in.body.yaw),contact.value.normal))<.35;
    if(!wreck_contact&&!m.active_wreck.present&&(!mode_in(order.mode,{"route","advance"})||order.throttle.present))return false;
    const double length=in.body.half_length,width=in.body.half_width;
    for(auto i=m.wreck_history.begin();i!=m.wreck_history.end();) {
        const auto&receipt=i->second;
        if(distance(in.body.position,receipt.exhausted?receipt.exhausted_position:receipt.origin)>2.*length+2.*width) {
            if(m.active_wreck.present&&m.active_wreck.value==i->first)m.active_wreck.clear();
            i=m.wreck_history.erase(i);
        } else ++i;
    }
    const double desired=yaw_to(in.body.position,goal);
    auto wreck=peers.end();
    for(auto i=peers.begin();i!=peers.end();++i)if(!i->alive&&(
        (wreck_contact&&i->id==contact.value.peer)||
        (m.active_wreck.present&&i->id==m.active_wreck.value&&distance(i->position,in.body.position)<=2.*length+2.*width)||
        static_ahead(in.body,desired,std::vector<Body>{*i}))) {wreck=i;break;}
    if(wreck==peers.end()) {m.active_wreck.clear();return false;}
    auto found=m.wreck_history.find(wreck->id);
    if(found==m.wreck_history.end()||distance(wreck->position,found->second.position)>=1.) {
        WreckAttempt fresh;fresh.key=wreck->id;fresh.position=wreck->position;
        fresh.heading=side_wreck?yaw_to(in.body.position,wreck->position):desired;
        fresh.origin=fresh.own_origin=in.body.position;fresh.own_heading=fresh.heading;
        if(found!=m.wreck_history.end()&&!found->second.exhausted) {
            fresh.own_origin=found->second.own_origin;fresh.own_heading=found->second.own_heading;
            fresh.own_best_progress=found->second.own_best_progress;fresh.own_elapsed=found->second.own_elapsed;
        }
        m.wreck_history[wreck->id]=fresh;
    }
    m.active_wreck.set(wreck->id);auto&a=m.wreck_history.at(wreck->id);
    const double progress=(in.body.position[0]-a.origin[0])*std::sin(a.heading)+(in.body.position[2]-a.origin[2])*std::cos(a.heading);
    const double own_progress=(in.body.position[0]-a.own_origin[0])*std::sin(a.own_heading)+(in.body.position[2]-a.own_origin[2])*std::cos(a.own_heading);
    if(a.exhausted)return false;
    if(progress>=a.best_progress+.5) {a.best_progress=progress;a.elapsed=0.;}
    if(own_progress>=a.own_best_progress+.5) {a.own_best_progress=own_progress;a.own_elapsed=0.;}
    a.elapsed+=std::max(0.,in.dt);a.own_elapsed+=std::max(0.,in.dt);
    if(a.elapsed>=14.||a.own_elapsed>=14.) {a.exhausted=true;a.exhausted_position=in.body.position;a.elapsed=std::max(14.,a.elapsed);return false;}
    const double reach=2.*length+1.5;
    if(!clear(w,in,in.body.yaw,reach))return false;
    std::vector<Body> living;for(const auto&peer:peers)if(peer.alive)living.push_back(peer);
    if(reverse_blocker(in.body,in.body.yaw+pi,living).present)return false;
    double turn=clamp(delta(a.heading,in.body.yaw),-.8,.8);
    if(std::abs(turn)>.01&&!w.pose(in.body.id,in.body.yaw+std::copysign(std::min(.45,std::abs(turn)),turn),true)) {
        a.elapsed=14.;a.exhausted=true;a.exhausted_position=in.body.position;return false;
    }
    if(!side_wreck&&a.elapsed>=8.) {
        const double direction=(int((a.elapsed-6.)/2.)%2)?1.:-1.,sample=in.body.yaw+direction*.45;
        if(clear(w,in,sample,reach)&&w.pose(in.body.id,sample,true)&&rotation_clear(in.body,sample,living))turn=direction*.7;
    }
    out=local(in,"wreck_push",1.,turn);out.target_yaw=a.heading;out.probe_distance.set(reach);
    out.move={{in.body.position[0]+std::sin(in.body.yaw)*reach,in.body.position[1],in.body.position[2]+std::cos(in.body.yaw)*reach}};
    return true;
}
bool reverse_withdraw(ActorMemory&m,const Input&in,const Order&order,const Point&aim,
    const Point&target,const Point&destination,const std::vector<Body>&peers,World&w,Command&out) {
    if(!mode_in(order.mode,{"withdraw","low_health_retreat","under_fire_withdraw","crossfire_withdraw"})||order.throttle.present)return false;
    const double d=distance(target,in.body.position),goal_distance=distance(destination,in.body.position);
    if(d<=1.5)return false;
    const bool exposed=order.target.present&&distance(aim,in.body.position)<=order.fire_range;
    if(goal_distance>30.&&!exposed)return false;
    if(!m.withdrawal.present||distance(destination,m.withdrawal.value.goal)>2.) {
        Withdrawal a;a.goal=destination;a.position=in.body.position;a.distance=goal_distance;m.withdrawal.set(a);
    }
    auto&a=m.withdrawal.value;
    if(goal_distance+(a.fallback?2.:.08)<=a.distance) {a.position=in.body.position;a.distance=goal_distance;a.age=0.;a.fallback=false;}
    a.age+=std::max(0.,in.dt);if(a.age>=8.)a.fallback=true;if(a.fallback)return false;
    const double face=yaw_to(in.body.position,target)+pi,error=delta(face,in.body.yaw);
    if(std::abs(error)>pi/3.)return false;
    const double reach=std::min(d,in.body.half_length*1.6);
    bool valid=true;Optional<std::int64_t> blocker;
    for(double f:{0.,.25,.5,.75,1.}) {
        const double sample=in.body.yaw+error*f;
        valid=valid&&clear(w,in,sample+pi,reach);
        if(!blocker.present)blocker=reverse_blocker(in.body,sample,peers);
        valid=valid&&w.pose(in.body.id,sample,false);
    }
    if(!valid||blocker.present) {a.fallback=true;return false;}
    const double stopping=std::max(0.,in.stopping_distance.present?in.stopping_distance.value:0.);
    const bool stopped=in.speed<-.05&&d<=std::max(1.5,stopping);
    out=local(in,"reverse_withdraw",stopped?0.:-1.,stopped?0.:-clamp(error/.58,-.5,.5));
    out.target_yaw=face;out.withdrawal_aim=goal_distance<=30.&&exposed;return true;
}
}

Command Planner::decide(const Input&in,const Order&order,const std::vector<Body>&all_peers,World&w) {
    auto&m=actor(in);std::vector<Body> peers;
    for(const auto&peer:all_peers)if(!peer.alive||!in.body.team_known||!peer.team_known||peer.team==in.body.team)peers.push_back(peer);
    Optional<Point> aim=order.aim,move=order.move,face=order.face;
    const bool parked=order.parking_phase=="waiting"||order.parking_phase=="queue";
    if(parked&&!face.present&&!aim.present) {
        if(!order.parking_heading.present)face.set(in.body.position);
        else {const double angle=order.parking_heading.value*pi/180.;face.set({{in.body.position[0]+std::sin(angle)*20.,in.body.position[1],in.body.position[2]+std::cos(angle)*20.}});}
    }
    if(!aim.present)aim.set(move.present?move.value:in.body.position);
    if(!move.present)move=aim;if(!face.present)face=move;
    const Navigation nav=w.navigate(in,order,move.value);Point target=nav.target;
    const auto contact=hull_contact(m,in,peers);
    if(parked)m.contact_escape.clear();
    Command c;bool contact_plan=!parked&&retain_contact(m,in,peers,w,c);
    if(!parked&&!contact_plan)contact_plan=wreck_plan(m,in,order,move.value,contact,peers,w,c);
    const bool side_contact=contact.present&&std::abs(dot(forward(in.body.yaw),contact.value.normal))<.35;
    const auto wreck=contact.present?m.wreck_history.find(contact.value.peer):m.wreck_history.end();
    const bool exhausted=contact.present&&!contact.value.alive&&wreck!=m.wreck_history.end()&&wreck->second.exhausted;
    if(!parked&&!exhausted&&!contact_plan&&contact.present&&
        (side_contact||(order.throttle.present&&(!contact.value.alive||contact.value.team!=in.body.team))))
        contact_plan=choose_contact(m,in,contact.value,peers,w,c);
    if(contact_plan)target=c.move;
    bool stop=nav.stop_at_target.present?nav.stop_at_target.value:!mode_in(order.mode,{"route","advance"});
    Optional<double> throttle=order.throttle;if(contact_plan) {throttle.clear();stop=false;}
    const bool movement=!(throttle.present&&throttle.value<=0.);
    if(!mode_in(order.mode,{"withdraw","low_health_retreat","under_fire_withdraw","crossfire_withdraw"}))m.withdrawal.clear();
    const bool navigation_wait=movement&&distance(move.value,in.body.position)>15.&&distance(target,in.body.position)<=1.5&&
        mode_in(nav.status,{"pending","blocked"});
    Input planning=in;planning.implicit_probe_distance.clear();
    if(movement)planning.implicit_probe_distance.set(std::max(distance(target,in.body.position),
        in.body.half_length+std::max(.5,std::abs(in.speed)*std::max(0.,in.decision_horizon))));
    if(!navigation_wait)m.driver.navigation_wait.clear();
    if(!contact_plan) {
        if(navigation_wait)c=wait_navigation(m.driver,planning,peers,w);
        else if(!reverse_withdraw(m,planning,order,aim.value,target,move.value,peers,w,c))
            c=drive(planning,nav,move.value,movement,stop,stop?order.arrival_radius:Optional<double>{},peers,w);
    }
    const bool blockage=contact.present||m.active_wreck.present||c.forward_blocker.present||c.reverse_blocker.present;
    const bool active_replan=m.driver.replan.present&&m.driver.replan.value.issued;
    if((blockage||in.rotation_refused||active_replan)&&movement&&!navigation_wait&&!c.navigation_recovery&&
        mode_in(c.recovery,{"blocked","pivot_recovery","reverse_turn","short_forward_escape","short_reverse_escape","forward_escape","avoid"})&&replan(m.driver,in))c.navigation_replan=true;
    if(c.recovery=="arrived"&&distance(face.value,in.body.position)>.1) {
        c.target_yaw=yaw_to(in.body.position,face.value);c.turn=clamp(delta(c.target_yaw,in.body.yaw)/.58,-1.,1.);
    }
    c.bot_id=in.body.id;c.target=order.target;c.aim=aim.value;c.face=face.value;c.move=target;c.mode=order.mode;
    c.fire_range=order.fire_range;c.fire_allowed=order.fire_allowed;c.shell_index=order.shell_index;c.stable_hull_face=order.stable_hull_face;
    c.movement_intent=movement;c.parking_phase=order.parking_phase;c.hull_angle_degrees=order.hull_angle_degrees;
    c.driver_progress_target=nav.progress_target;
    c.navigation_probe_distance=planning.implicit_probe_distance;
    if(throttle.present&&mode_in(c.recovery,{"drive","arrived"})&&std::abs(delta(c.target_yaw,in.body.yaw))<.65)c.throttle=clamp(throttle.value,-1.,1.);
    if(throttle.present&&throttle.value==0.) {c.throttle=0.;c.brake=true;}
    return c;
}

ActorMemory &Planner::actor(const Input&in) {
    if(in.slot<0||in.slot>=15)throw std::invalid_argument("bot team slot is outside 0..14");
    auto i=actors_.find(in.body.id);
    if(i==actors_.end()) {
        ActorMemory state;state.driver.slot=in.slot;state.driver.last_position=in.body.position;
        i=actors_.emplace(in.body.id,std::move(state)).first;
    } else if(i->second.driver.slot!=in.slot)throw std::invalid_argument("bot team slot changed during battle");
    return i->second;
}
void Planner::forget(std::int64_t id) { actors_.erase(id); }
void Planner::remember_failure(std::int64_t id,double yaw,double ttl) {
    const auto found=actors_.find(id);if(found==actors_.end())return;
    auto&s=found->second.driver;ttl=std::max(.1,ttl);
    s.failed_yaws[yaw_key(yaw)]=s.real_failed_yaws[yaw_key(yaw)]=s.clock+ttl;
    const double offset=s.desired_yaw.present?delta(yaw,s.desired_yaw.value):0.;
    s.escape_side=std::abs(offset)>=.10?(offset>0.?1.:-1.):fallback(s);
    s.escape_until=s.clock+std::min(2.,std::max(.8,ttl));s.steering_yaw.clear();s.plan_age=999.;
}
bool Planner::wait_for_traffic(std::int64_t id,double elapsed) {
    const auto i=actors_.find(id);if(i==actors_.end())return false;
    auto&s=i->second.driver;s.traffic_waiting=true;s.traffic_wait_time+=std::max(0.,elapsed);
    if(s.traffic_wait_time<=1.5)s.stuck_time=s.recovery_time=s.recovery_side=0.;return true;
}

Command Planner::drive(const Input&in,const Navigation&nav,const Point&destination,
    bool movement,bool stop,const Optional<double>&arrival_field,const std::vector<Body>&peers,World&w) {
    auto&s=actor(in).driver;
    const double arrival=arrival_field.present?std::max(.1,arrival_field.value):1.5;
    if(!movement||in.speed<-.05||!in.turn_speed_limit.present||
        (s.alignment_target.present&&distance(s.alignment_target.value,nav.target)>2.))s.alignment_target.clear();
    const double step=std::max(0.,in.dt);
    if(!s.traffic_waiting)s.traffic_wait_time=0.;s.traffic_waiting=false;
    s.last_step=step;s.clock+=step;prune(s);s.steering_age+=step;s.plan_age+=step;
    const double desired=yaw_to(in.body.position,nav.target),heading=std::abs(delta(desired,in.body.yaw));
    const double target_distance=distance(in.body.position,nav.target);s.desired_yaw.set(desired);
    if(!movement) {
        s.native_translation_stalled=false;s.short_escape.clear();s.translation_escape.clear();
        s.translation_progress_at.set(s.clock);s.objective.clear();s.replan.clear();s.pocket.clear();
        s.stuck_time=s.recovery_time=s.recovery_side=0.;s.steering_yaw.clear();s.progress_yaw.clear();s.braking_target.clear();
        return local(in,"arrived");
    }
    const double length=std::max(.5,in.body.half_length);
    if(s.pocket.present&&(distance(in.body.position,s.pocket.value.origin)>=2.*length+arrival||
        distance(s.pocket.value.goal,destination)>2.))s.pocket.clear();
    const bool pocket_stalled=s.pocket.present&&s.clock-s.pocket.value.at>=8.&&s.recovery_count>=2;
    const double displacement=distance(in.body.position,s.last_position);
    if(displacement>=.08||!s.translation_progress_at.present)s.translation_progress_at.set(s.clock);
    const bool translation_stalled=s.clock-s.translation_progress_at.value>=8.;
    s.native_translation_stalled=in.rotation_refused&&translation_stalled&&distance(in.body.position,destination)>arrival;
    bool objective_stalled=false,objective_advanced=false;
    const Point &objective_target=nav.progress_target.present?nav.progress_target.value:destination;
    const double objective_distance=distance(in.body.position,objective_target);
    if(!s.objective.present||distance(s.objective.value.goal,objective_target)>2.||objective_distance<=arrival) {
        Objective p;p.goal=objective_target;p.best=objective_distance;p.at=s.clock;s.objective.set(p);
    } else if(objective_distance+.08<=s.objective.value.best) {
        objective_advanced=true;s.objective.value.best=objective_distance;s.objective.value.at=s.clock;
        s.objective.value.alignment_yaw.clear();s.objective.value.alignment_best.clear();
    } else if(objective_distance+.002<s.objective.value.best)objective_advanced=true;
    auto&objective=s.objective.value;
    if(s.alignment_target.present&&s.recovery_time<=0.) {
        if(!objective.alignment_yaw.present) {
            objective.alignment_yaw.set(yaw_to(in.body.position,s.alignment_target.value));
            objective.alignment_best.set(std::abs(delta(objective.alignment_yaw.value,in.body.yaw)));
        } else {
            const double error=std::abs(delta(objective.alignment_yaw.value,in.body.yaw));
            if(error+.002<objective.alignment_best.value) {objective.alignment_best.set(error);objective.at=s.clock;}
        }
    }
    objective_stalled=s.clock-objective.at>=8.;
    if(target_distance<=arrival&&!objective_stalled&&!s.native_translation_stalled) {
        s.short_escape.clear();s.stuck_time=s.recovery_time=s.recovery_side=0.;s.steering_yaw.clear();
        s.progress_yaw.clear();s.last_position=in.body.position;s.braking_target.clear();return local(in,"arrived");
    }
    Command out;
    if(retained_escape(s,in,peers,w,false,out)||retained_escape(s,in,peers,w,true,out))return out;
    bool heading_progress=false;
    if(displacement>=.08||!s.progress_yaw.present||std::abs(delta(desired,s.progress_yaw.value))>.12) {
        s.progress_yaw.set(desired);s.best_heading_error.set(heading);
    } else {
        const double error=std::abs(delta(s.progress_yaw.value,in.body.yaw));
        if(error+.002<s.best_heading_error.value) {s.best_heading_error.set(error);heading_progress=true;}
    }
    if(displacement>=.08) {s.last_position=in.body.position;s.stuck_time=0.;}
    else if(heading_progress||objective_advanced)s.stuck_time=std::max(0.,s.stuck_time-step);
    else s.stuck_time+=step;
    const double threshold=1.8+phase(s)*.42;
    if(objective_stalled||s.native_translation_stalled)s.stuck_time=std::max(s.stuck_time,threshold);
    if(s.recovery_time>0.) {
        s.recovery_time=std::max(0.,s.recovery_time-step);
        if(s.recovery_time==0.) {++s.recovery_count;s.recovery_side=s.stuck_time=0.;s.progress_yaw.clear();}
    } else if(s.stuck_time>=threshold) {
        objective.at=s.clock;
        if(in.rotation_refused&&!s.pocket.present) {Episode p;p.origin=in.body.position;p.at=s.clock;p.goal=destination;s.pocket.set(p);}
        if(s.last_clear_yaw.present)s.failed_yaws[yaw_key(s.last_clear_yaw.value)]=s.clock+2.;
        s.recovery_time=.85+phase(s)*.28;s.recovery_side=recovery_side(s,in.body,peers);
    }
    if(s.recovery_time>0.) {
        double sign=s.recovery_side;
        if(sign!=-1.&&sign!=1.)s.recovery_side=sign=recovery_side(s,in.body,peers);
        double target=in.body.yaw+sign*.85;
        const double reach=length*1.6,reverse_yaw=in.body.yaw+pi;
        const bool reverse_clear=penalty(s,reverse_yaw)<=0.&&clear(w,in,reverse_yaw,reach);
        Optional<std::int64_t> reverse;
        if(reverse_clear)reverse=reverse_blocker(in.body,in.body.yaw,peers);
        if(!reverse_clear||reverse.present) {
            if((objective_stalled||translation_stalled||pocket_stalled)&&short_escape(s,in,peers,w,out))return out;
            if(penalty(s,in.body.yaw)>0.&&short_escape(s,in,peers,w,out))return out;
            if(!pivot(w,in,sign)) {
                if(pivot(w,in,-sign)) {s.recovery_side=sign=-sign;target=in.body.yaw+sign*.85;}
                else {
                    const auto forward_block=reverse_blocker(in.body,in.body.yaw+pi,peers);
                    if(!forward_block.present&&penalty(s,in.body.yaw)<=0.&&clear(w,in,in.body.yaw,reach)) {
                        Escape e;e.start=in.body.position;e.yaw=in.body.yaw;e.distance=reach;e.until=s.clock+20.;
                        s.translation_escape.set(e);return local(in,"forward_escape",1.);
                    }
                    if(short_escape(s,in,peers,w,out)||partial_pivot(in,sign,w,out))return out;
                    out=local(in,"blocked");out.brake=true;out.reverse_blocker=reverse;out.forward_blocker=forward_block;return out;
                }
            }
            out=local(in,"pivot_recovery",0.,sign);out.target_yaw=target;out.reverse_blocker=reverse;return out;
        }
        out=local(in,"reverse_turn",-1.,-sign);out.target_yaw=target;
        for(double f:{.25,.5,.75,1.}) {
            const double hull=in.body.yaw+sign*.85*f,travel=hull+pi;
            if(!w.pose(in.body.id,hull,false)||penalty(s,travel)>0.||!clear(w,in,travel,reach)) {
                out.turn=0.;out.target_yaw=in.body.yaw;break;
            }
        }
        return out;
    }
    Optional<double> chosen;
    if(s.steering_yaw.present&&s.plan_age<(s.avoiding?1.20:.35)&&
        std::abs(delta(desired,s.steering_yaw.value))<2.15&&penalty(s,s.steering_yaw.value)<=0.&&
        w.pose(in.body.id,s.steering_yaw.value,false)&&clear(w,in,s.steering_yaw.value)&&
        !static_ahead(in.body,s.steering_yaw.value,peers))chosen=s.steering_yaw;
    if(!chosen.present) {
        std::vector<std::pair<double,double>> candidates;
        for(double offset:{0.,.42,-.42,.78,-.78,1.18,-1.18,1.55,-1.55}) {
            const double candidate=desired+offset;
            double score=std::abs(offset)+penalty(s,candidate);
            if(s.escape_until>s.clock&&offset*s.escape_side<-.01)score+=1.25;
            candidates.emplace_back(score,candidate);
        }
        std::stable_sort(candidates.begin(),candidates.end(),[](const auto&a,const auto&b){return a.first<b.first;});
        for(const auto &candidate:candidates)if(w.pose(in.body.id,candidate.second,false)&&clear(w,in,candidate.second)&&
            !static_ahead(in.body,candidate.second,peers)) {
            chosen.set(candidate.second);s.avoiding=std::abs(delta(candidate.second,desired))>.05;
            if(s.avoiding) {s.escape_side=delta(candidate.second,desired)>0.?1.:-1.;s.escape_until=s.clock+2.5;}
            break;
        }
        s.plan_age=0.;
    }
    if(!chosen.present) {s.stuck_time=std::max(s.stuck_time,threshold);out=local(in,"blocked");out.brake=true;return out;}
    s.last_clear_yaw=chosen;
    if(!s.steering_yaw.present||std::abs(delta(chosen.value,s.steering_yaw.value))>.04) {s.steering_yaw=chosen;s.steering_age=0.;}
    const double difference=delta(chosen.value,in.body.yaw);
    out=local(in,s.avoiding?"avoid":"drive",1.,clamp(difference/.58,-1.,1.));out.target_yaw=chosen.value;
    if(s.avoiding)s.alignment_target.clear();
    else if(in.turn_speed_limit.present&&in.turn_speed_limit.value>0.&&in.speed>=-.05) {
        const double turning_radius=std::max(1.,std::abs(in.speed))/in.turn_speed_limit.value;
        if(target_distance<=std::max(8.,2.*turning_radius)&&std::abs(difference)>.30)s.alignment_target.set(nav.target);
        if(s.alignment_target.present) {
            if(std::abs(difference)>.12) {out.throttle=0.;out.brake=true;}else s.alignment_target.clear();
        }
    }
    if(target_distance<=std::max(8.,length*2.)&&std::abs(difference)>.45) {out.throttle=0.;out.brake=true;}
    const double grade=(nav.target[1]-in.body.position[1])/std::max(.1,target_distance);
    if((std::abs(grade)>std::tan(pi/8.)&&std::abs(difference)>pi/4.)||
        std::abs(difference)>pi*.5||(!s.avoiding&&heading>pi*.5)) {out.throttle=0.;out.brake=true;}
    if(stop&&!s.avoiding&&in.stopping_distance.present) {
        Point key{{round_two_decimals(nav.target[0]),0.,round_two_decimals(nav.target[2])}};
        if(s.braking_target.present&&s.braking_target.value!=key)s.braking_target.clear();
        if(target_distance<=arrival+std::max(0.,in.stopping_distance.value)+std::abs(in.speed)*std::max(0.,in.decision_horizon))s.braking_target.set(key);
        if(s.braking_target.present&&s.braking_target.value==key) {
            if(std::abs(in.speed)<=.35&&target_distance>arrival+(arrival_field.present?0.:.5))s.braking_target.clear();
            else {out.throttle=0.;out.brake=true;}
        }
    } else if(!stop)s.braking_target.clear();
    return out;
}

namespace {
V2 position2(const Body&b) { return {{b.position[0],b.position[2]}}; }
V2 velocity2(const Body&b) { return {{b.velocity[0],b.velocity[2]}}; }
double cross(V2 a,V2 b) { return a[0]*b[1]-a[1]*b[0]; }
bool same_level(const Body&a,const Body&b) {
    auto interval=[](const Body&body) {
        const double cp=std::cos(body.pitch),sp=std::sin(body.pitch),cr=std::cos(body.roll),sr=std::sin(body.roll);
        const double up=cp*cr,centre=body.position[1]+(body.lower_y+body.upper_y)*.5*up;
        const double extent=std::abs(cp*sr)*body.half_width+std::abs(sp)*body.half_length+
            std::abs(up)*(body.upper_y-body.lower_y)*.5;
        return std::pair<double,double>(centre-extent,centre+extent);
    };
    const auto first=interval(a),second=interval(b);
    return std::min(first.second,second.second)-std::max(first.first,second.first)>0.;
}
bool contact_time(const Body&a,const Body&b) {
    const V2 difference{{b.position[0]-a.position[0],b.position[2]-a.position[2]}};
    const V2 relative{{b.velocity[0]-a.velocity[0],b.velocity[2]-a.velocity[2]}};
    double enter=0.,leave=1.;
    for(const auto axis:{side(a.yaw),forward(a.yaw),side(b.yaw),forward(b.yaw)}) {
        const double distance_value=dot(difference,axis),rate=dot(relative,axis),extent=radius(a,axis)+radius(b,axis);
        if(std::abs(rate)<=1.e-9) {if(std::abs(distance_value)>=extent)return false;continue;}
        const double start=(-extent-distance_value)/rate,end=(extent-distance_value)/rate;
        enter=std::max(enter,std::min(start,end));leave=std::min(leave,std::max(start,end));
        if(enter>=leave)return false;
    }
    return true;
}
std::pair<V2,double> travel(const Body&body) {
    V2 v=velocity2(body);const double speed=std::hypot(v[0],v[1]);
    if(speed>1.e-9) {v[0]/=speed;v[1]/=speed;}else v=forward(body.yaw);
    return {v,speed};
}
bool blocks_requester(const Body&body,const Body&peer,const Command&order,double margin) {
    if(-overlap(body,peer)[2]<=margin)return true;
    if(order.forward_blocker.present&&order.forward_blocker.value==body.id)
        return reverse_blocker(peer,peer.yaw+pi,{body}).present;
    if(!order.reverse_blocker.present||order.reverse_blocker.value!=body.id)return false;
    return reverse_blocker(peer,peer.yaw,{body}).present;
}
Optional<TrafficLease> begin_traffic(const Body&a,const Body&b,double now,bool a_held,bool b_held) {
    if(dot(velocity2(a),forward(a.yaw))<-1.e-9||dot(velocity2(b),forward(b.yaw))<-1.e-9)return {};
    const auto av=travel(a),bv=travel(b);const double alignment=dot(av.first,bv.first);
    if(alignment>=std::cos(.35))return {};
    const bool contact=contact_time(a,b);
    const V2 difference{{b.position[0]-a.position[0],b.position[2]-a.position[2]}};
    TrafficLease result;result.until=now+1.5;
    if(alignment<=-std::cos(.60)) {
        const double ahead=dot(difference,av.first),second=-dot(difference,bv.first);
        const V2 lateral{{av.first[1],-av.first[0]}};
        const double width=radius(a,lateral)+radius(b,lateral),gap=ahead-radius(a,av.first)-radius(b,av.first);
        if(ahead<=0.||second<=0.||std::abs(dot(difference,lateral))>=width||(!contact&&gap>width*.5))return {};
        result.head_on=true;result.axis=av.first;result.targets={{a.yaw+.42,b.yaw+.42}};
    } else {
        if(!contact)return {};
        const double denominator=cross(av.first,bv.first);if(std::abs(denominator)<=1.e-9)return {};
        const double reach=cross(difference,bv.first)/denominator;
        const V2 gate{{a.position[0]+av.first[0]*reach,a.position[2]+av.first[1]*reach}};
        const double pace=std::max(av.second,bv.second);
        auto rank=[&](const Body&body,const std::pair<V2,double>&v,bool held) {
            const double front=dot({{gate[0]-body.position[0],gate[1]-body.position[2]}},v.first)-radius(body,v.first);
            const double speed=v.second>1.e-9?v.second:held?pace:0.;
            return std::tuple<int,double,std::int64_t>(front<=0.?0:1,front<=0.?front:
                speed>1.e-9?front/speed:std::numeric_limits<double>::infinity(),body.id);
        };
        const bool first=rank(a,av,a_held)<rank(b,bv,b_held);result.winner=first?a.id:b.id;result.axis=first?av.first:bv.first;
        result.clear_after=dot(gate,result.axis)+radius(a,result.axis)+radius(b,result.axis);
    }
    Optional<TrafficLease> out;out.set(result);return out;
}
bool cleared(const TrafficLease&lease,const Body&a,const Body&b) {
    if(!lease.head_on)return dot(position2(a.id==lease.winner?a:b),lease.axis)>lease.clear_after;
    const V2 difference{{b.position[0]-a.position[0],b.position[2]-a.position[2]}};
    const V2 lateral{{lease.axis[1],-lease.axis[0]}};
    return dot(difference,lease.axis)<=0.||std::abs(dot(difference,lateral))>=radius(a,lateral)+radius(b,lateral);
}
}

void Traffic::forget(std::int64_t id) {
    held_.erase(id);orders_.erase(id);parked_.erase(id);
    for(auto i=parked_.begin();i!=parked_.end();)if(i->second.requester==id)i=parked_.erase(i);else ++i;
    for(auto i=jams_.begin();i!=jams_.end();)if(i->first.first==id||i->first.second==id)i=jams_.erase(i);else ++i;
    for(auto i=pairs_.begin();i!=pairs_.end();)if(i->first.first==id||i->first.second==id)i=pairs_.erase(i);else ++i;
}
Optional<double> Traffic::clearance_distance(std::int64_t id,const Point&position)const {
    const auto i=parked_.find(id);if(i==parked_.end())return {};
    Optional<double> out;out.set(std::max(0.,i->second.distance-distance(position,i->second.origin)));return out;
}
bool Traffic::parked_yield(const Input&in,const Command&command,const std::map<std::int64_t,Body>&peers,
    const std::vector<Body>&neighbours,World&w,Command&out) {
    const auto id=in.body.id;const double now=in.now;
    auto clear_jams=[&] {for(auto i=jams_.begin();i!=jams_.end();)if(i->first.first==id)i=jams_.erase(i);else ++i;};
    if(mode_in(command.parking_phase,{"waiting","queue"})) {parked_.erase(id);clear_jams();return false;}
    const bool hold=command.recovery=="arrived"&&!mode_in(command.mode,{"physical_hold","nav_wait"});
    const bool route=mode_in(command.mode,{"route","advance"})&&mode_in(command.recovery,
        {"drive","avoid","blocked","reverse_turn","pivot_recovery","forward_escape","short_forward_escape","short_reverse_escape","contact_escape","nav_wait"});
    std::set<std::int64_t> requests;
    for(const auto&peer:peers) {
        const auto order=orders_.find(peer.first);
        if(order!=orders_.end()&&now-order->second.first<=1.5&&!parked_.count(peer.first)) {
            const auto&c=order->second.second;
            if((c.reverse_blocker.present&&c.reverse_blocker.value==id)||(c.forward_blocker.present&&c.forward_blocker.value==id))requests.insert(peer.first);
        }
    }
    for(auto i=jams_.begin();i!=jams_.end();)if(i->first.first==id&&!peers.count(i->first.second))i=jams_.erase(i);else ++i;
    if(!in.body.alive||!(hold||(route&&(!requests.empty()||parked_.count(id))))) {parked_.erase(id);clear_jams();return false;}
    auto lease=parked_.find(id);
    if(lease!=parked_.end()) {
        const auto peer=peers.find(lease->second.requester);
        const auto observation=orders_.find(lease->second.requester);
        if(peer==peers.end()||observation==orders_.end()||now-observation->second.first>1.5||
            !observation->second.second.movement_intent||mode_in(observation->second.second.recovery,{"arrived","nav_wait","physical_hold"})||
            !blocks_requester(in.body,peer->second,observation->second.second,.45)) {
            jams_.erase({id,lease->second.requester});parked_.erase(lease);lease=parked_.end();
        } else if(now>=lease->second.until)return false;
    }
    if(lease==parked_.end()) {
        const Body* selected=nullptr;
        for(const auto&entry:peers) {
            const auto&peer=entry.second;const auto order=orders_.find(peer.id);const Pair pair{id,peer.id};
            const bool blocked=order!=orders_.end()&&now-order->second.first<=1.5&&order->second.second.movement_intent&&
                (hold||requests.count(peer.id))&&!mode_in(order->second.second.recovery,{"arrived","nav_wait","physical_hold"})&&
                std::hypot(in.body.velocity[0],in.body.velocity[2])<=.65&&std::hypot(peer.velocity[0],peer.velocity[2])<=.65&&
                blocks_requester(in.body,peer,order->second.second,.15);
            if(!blocked) {jams_.erase(pair);continue;}
            const auto since=jams_.emplace(pair,now).first->second;
            if((!hold&&requests.count(peer.id))||now-since>=1.5)if(!selected)selected=&peer;
        }
        if(!selected)return false;
        const V2 away{{in.body.position[0]-selected->position[0],in.body.position[2]-selected->position[2]}};
        const double preferred=dot(away,forward(in.body.yaw))>=0.?1.:-1.;double sign=0.;
        for(double candidate:{preferred,-preferred}) {
            const double heading=in.body.yaw+(candidate<0.?pi:0.);
            if(clear(w,in,heading,in.body.half_length*1.6)&&!reverse_blocker(in.body,heading+pi,neighbours).present) {sign=candidate;break;}
        }
        if(!sign)return false;
        ParkedLease value;value.requester=selected->id;value.until=now+4.;value.sign=sign;value.origin=in.body.position;
        value.distance=in.body.half_length+radius(*selected,forward(in.body.yaw))+.45;
        lease=parked_.emplace(id,value).first;
    }
    auto&value=lease->second;
    if(distance(in.body.position,value.origin)>=value.distance) {value.until=now;return false;}
    const double heading=in.body.yaw+(value.sign<0.?pi:0.);
    const double remaining=std::min(in.body.half_length*1.6,clearance_distance(id,in.body.position).value);
    const bool valid=clear(w,in,heading,remaining)&&!reverse_blocker(in.body,heading+pi,neighbours,remaining).present;
    out=command;out.throttle=valid?.70*value.sign:0.;out.brake=!valid;out.turn=0.;out.target_yaw=in.body.yaw;
    out.movement_intent=true;out.recovery=out.traffic_mode="friendly_yield";out.fire_allowed=false;return true;
}
Command Traffic::adjust(const Input&in,const Command&command,const std::vector<Body>&neighbours,World&w) {
    Command out=command;const auto id=in.body.id;orders_[id]={in.now,command};
    std::map<std::int64_t,Body> peers;
    for(const auto&peer:neighbours)if(peer.id!=id&&peer.alive&&peer.team_known==in.body.team_known&&
        (!peer.team_known||peer.team==in.body.team)&&same_level(in.body,peer))peers[peer.id]=peer;
    if(parked_yield(in,command,peers,neighbours,w,out))return out;
    bool retreat=false;
    for(const auto&pair:pairs_)if((pair.first.first==id||pair.first.second==id)&&pair.second.head_on&&
        pair.second.blocked.size()==2&&pair.second.until<=in.now&&in.now<pair.second.until+6.) {retreat=true;break;}
    if(!in.body.alive||!command.movement_intent||!mode_in(command.mode,{"route","advance"})||
        (!retreat&&(command.recovery!="drive"||command.throttle<=0.||dot(velocity2(in.body),forward(in.body.yaw))<-1.e-9)))return out;
    for(auto i=pairs_.begin();i!=pairs_.end();) {
        const auto pair=i->first;
        if((pair.first==id&&!peers.count(pair.second))||(pair.second==id&&!peers.count(pair.first)))i=pairs_.erase(i);else ++i;
    }
    for(const auto&entry:peers) {
        const Body&a=id<entry.first?in.body:entry.second,&b=id<entry.first?entry.second:in.body;
        const Pair pair{a.id,b.id};auto lease=pairs_.find(pair);
        if(lease!=pairs_.end()&&cleared(lease->second,a,b)) {pairs_.erase(lease);lease=pairs_.end();}
        if(lease==pairs_.end()) {
            const auto ah=held_.find(a.id),bh=held_.find(b.id);
            const auto created=begin_traffic(a,b,in.now,ah!=held_.end()&&in.now-ah->second<=1.5,bh!=held_.end()&&in.now-bh->second<=1.5);
            if(!created.present)continue;lease=pairs_.emplace(pair,created.value).first;
        }
        auto&value=lease->second;
        if(!value.head_on) {
            if(id!=value.winner&&in.now<value.until) {out.throttle=out.turn=0.;out.brake=true;out.traffic_mode="yield";held_[id]=in.now;}
            continue;
        }
        if(out.traffic_mode=="yield")continue;
        if(in.now>=value.until) {
            if(value.blocked.size()==2&&in.now<value.until+6.) {
                const bool reversing=id==b.id;const double target=in.body.yaw+(reversing?pi:0.);
                bool valid=clear(w,in,target,in.body.half_length*1.6);
                if(reversing)valid=valid&&!reverse_blocker(in.body,in.body.yaw,neighbours).present;
                if(valid) {out.throttle=reversing?-.70:.70;out.brake=false;out.turn=0.;out.target_yaw=in.body.yaw;out.traffic_mode="head_on_retreat";}
            }
            continue;
        }
        const double target=value.targets[id==a.id?0:1];
        if(!clear(w,in,target)||!w.pose(id,target,false)) {
            value.blocked.insert(id);out.throttle=out.turn=0.;out.target_yaw=in.body.yaw;out.brake=true;
            out.traffic_mode="head_on_blocked";held_[id]=in.now;continue;
        }
        out.turn=clamp(delta(target,in.body.yaw)/.58,-1.,1.);out.target_yaw=target;out.traffic_mode="head_on";
    }
    return out;
}
Command Traffic::safe_controls(const Input&in,const Command&command,const std::vector<Body>&neighbours,double stopping_distance) {
    Command out=command;
    const double throttle=command.throttle,turn=command.turn,speed=dot(velocity2(in.body),forward(in.body.yaw));
    const double sign=throttle<-.01||(std::abs(throttle)<=.01&&speed<0.)?-1.:1.;
    V2 axis=forward(in.body.yaw);axis[0]*=sign;axis[1]*=sign;
    std::vector<Body> peers,candidates;
    for(const auto&peer:neighbours)if(peer.id!=in.body.id&&peer.alive&&
        (!in.body.team_known||!peer.team_known||peer.team==in.body.team)&&same_level(in.body,peer)) {
        peers.push_back(peer);
        const V2 difference{{peer.position[0]-in.body.position[0],peer.position[2]-in.body.position[2]}};
        if(dot(difference,axis)<=0.)continue;
        const double reach=in.body.half_length+in.body.half_width+peer.half_length+peer.half_width;
        if(std::hypot(difference[0],difference[1])>reach+std::max(2.,speed*speed))continue;
        candidates.push_back(peer);
    }
    Optional<std::int64_t> blocker;
    if(!candidates.empty()&&(std::abs(throttle)>.01||std::abs(speed)>.01)) {
        const double brake=std::max(0.,stopping_distance),reach=std::min(80.,brake)+std::abs(speed)*in.dt+.12;
        const double horizon=std::min(1.,std::max(in.dt,2.*std::min(brake,80.)/std::max(std::abs(speed),.1)));
        Body swept=in.body;swept.velocity={{axis[0]*reach,0.,axis[1]*reach}};
        for(const auto&peer:candidates) {
            const auto hit=overlap(in.body,peer);
            if(hit[2]>=-1.e-9) {
                if(dot(axis,{{hit[0],hit[1]}})>=-1.e-9)continue;
                blocker.set(peer.id);break;
            }
            Body predicted=peer;predicted.velocity={{peer.velocity[0]*horizon,0.,peer.velocity[2]*horizon}};
            if(contact_time(swept,predicted)) {blocker.set(peer.id);break;}
        }
    }
    if(blocker.present&&speed*throttle>=-.01) {
        out.throttle=0.;out.brake=true;out.traffic_mode="vehicle_brake";
        (sign>0.?out.forward_blocker:out.reverse_blocker)=blocker;
    }
    if(std::abs(turn)>.01&&!peers.empty()&&
        !rotation_clear(in.body,in.body.yaw+turn*(out.throttle<0.?-1.:1.)*std::max(in.dt,.1),peers)) {
        out.turn=0.;out.traffic_mode="vehicle_brake";
    }
    const auto observed=orders_.find(in.body.id);
    if(observed!=orders_.end()) {
        auto order=observed->second.second;
        for(int gear=0;gear<2;++gear) {
            auto&field=gear?order.reverse_blocker:order.forward_blocker;
            const auto peer=std::find_if(peers.begin(),peers.end(),[&](const Body&b){return field.present&&b.id==field.value;});
            if(command.recovery!="blocked"||peer==peers.end()||
                !reverse_blocker(in.body,in.body.yaw+(gear?0.:pi),{*peer}).present)field.clear();
        }
        if(blocker.present&&speed*throttle>=-.01)(sign>0.?order.forward_blocker:order.reverse_blocker)=blocker;
        observed->second={in.now,order};
    }
    return out;
}
}
