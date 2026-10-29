#include "stock_native_bot_physics_host.hpp"
#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace stock_native_bot_physics {
namespace contacts=stock_native_bot_contacts;
namespace nav=stock_native_bot_navigation;
namespace {
stock_native_world::TurretPose pose(const motion::State& state) {
    stock_native_world::TurretPose out;out.position=out.hull_position=state.position;
    out.yaw=out.hull_yaw=state.yaw;out.pitch=state.terrain_pitch;
    out.roll=out.hull_roll=state.roll;out.hull_pitch=state.pitch;out.separate_hull=true;return out;
}
void restore(step::VerticalState& state,const step::VerticalState& old) {
    state.pitch=old.pitch;state.terrain_pitch=old.terrain_pitch;state.roll=old.roll;
    state.vertical_speed=old.vertical_speed;state.airborne=old.airborne;state.grounded_once=old.grounded_once;
    state.pitch_speed=old.pitch_speed;state.roll_speed=old.roll_speed;
    state.left_flying=old.left_flying;state.right_flying=old.right_flying;
    state.ground_height=old.ground_height;state.ground_plane=old.ground_plane;
    state.support_vertical_speed=old.support_vertical_speed;state.support_gradient=old.support_gradient;
    state.slide_speed=old.slide_speed;state.air_lateral_x=old.air_lateral_x;state.air_lateral_z=old.air_lateral_z;
    state.slope_target=old.slope_target;state.slope_basis_yaw=old.slope_basis_yaw;
    state.basic_fall_origin=old.basic_fall_origin;state.straddle_reference=old.straddle_reference;
    state.basic_continuous_support=old.basic_continuous_support;
}
}
offline_math::Body NativeHost::body(const motion::State& state,const motion::Descriptor& descriptor,bool rotation)const {
    return {state.id,true,state.position[0],state.position[1],state.position[2],state.yaw,
        rotation?0.:state.pitch,rotation?0.:state.roll,descriptor.collision_shape};
}
std::vector<offline_math::Body> NativeHost::peers(ID id,bool rotation,bool planning,bool wreck_push)const {
    std::vector<offline_math::Body> out;const auto source=context_.actors.find(id);
    const int team=source==context_.actors.end()?0:source->second.team;
    auto admitted=[&](bool alive,int other_team){return !planning||(!alive&&!wreck_push)||(alive&&other_team==team);};
    auto human=[&]{for(const auto& peer:context_.humans)if(admitted(peer.body.has(offline_contact::Alive),int(peer.body.team))) {
        auto b=contacts::geometry(peer.body);if(rotation)b.pitch=b.roll=0.;out.push_back(b);
    }};
    auto order=contacts::python27_integer_order(context_.insertion_order);
    if(!rotation)std::stable_sort(order.begin(),order.end(),[&](ID left,ID right){
        const auto l=context_.actors.find(left),r=context_.actors.find(right);
        const int ls=context_.slots.count(left)?context_.slots.at(left):0,rs=context_.slots.count(right)?context_.slots.at(right):0;
        const int lt=l==context_.actors.end()?1:l->second.team,rt=r==context_.actors.end()?1:r->second.team;
        return std::tie(ls,lt)<std::tie(rs,rt);
    });
    if(rotation)human();
    for(ID peer:order) {
        const auto found=context_.actors.find(peer);if(found==context_.actors.end()||peer==id)continue;
        const auto& a=found->second;if(!a.motion||!a.descriptor||!admitted(a.motion->alive,a.team))continue;
        out.push_back(body(*a.motion,*a.descriptor,rotation));
    }
    if(!rotation)human();return out;
}
double NativeHost::rotation_fraction(const motion::State& state,const motion::Descriptor& descriptor,
    const Point& position,double from,double to,const Vec&) {
    auto owner=body(state,descriptor,true);owner.x=position[0];owner.y=position[1];owner.z=position[2];owner.yaw=from;
    return offline_math::rotation(owner,to,0.,{0.,0.},peers(state.id,true));
}
double NativeHost::planning_rotation_fraction(ID id,const Point& position,double from,double to,bool wreck_push)const {
    const auto found=context_.actors.find(id);if(found==context_.actors.end()||!found->second.motion||!found->second.descriptor)
        throw std::invalid_argument("Planning rotation actor is unavailable");
    auto owner=body(*found->second.motion,*found->second.descriptor,true);
    owner.x=position[0];owner.y=position[1];owner.z=position[2];owner.yaw=from;
    return offline_math::rotation(owner,to,0.,{0.,0.},peers(id,true,true,wreck_push));
}
std::optional<std::array<double,4>> NativeHost::overflow(const motion::State& state,const Point& p,double yaw)const {
    const auto& map=context_.grid.map;if(!map.has_bounds)return {};
    const auto& b=map.bounds;if(b[0]>=b[2]||b[1]>=b[3]||!std::all_of(b.begin(),b.end(),[](double v){return std::isfinite(v);}))return {};
    const auto a=context_.actors.find(state.id);if(a==context_.actors.end()||!a->second.descriptor)throw std::invalid_argument("Boundary descriptor is unavailable");
    const auto& d=*a->second.descriptor;const double length=std::max(.5,d.half_length),width=std::max(.3,d.half_width);
    const double sn=std::abs(std::sin(yaw)),cs=std::abs(std::cos(yaw)),x=cs*width+sn*length,z=sn*width+cs*length;
    return std::array<double,4>{{std::max(0.,b[0]+x-p[0]),std::max(0.,p[0]+x-b[2]),std::max(0.,b[1]+z-p[2]),std::max(0.,p[2]+z-b[3])}};
}
bool NativeHost::pose_progress(const motion::State& state,const Point& start,double from,const Point& end,double to) {
    auto before=overflow(state,start,from),after=overflow(state,end,to);if(!before||!after)return true;
    for(unsigned i=0;i<4;++i)if((*after)[i]>(*before)[i]+1.e-6)return false;return true;
}
stock_native_world::TurretSweep NativeHost::turret_sweep(const motion::State& before,const motion::State& after) {
    return {std::uint64_t(before.id),pose(before),pose(after)};
}
bool NativeHost::turret_blocks(const stock_native_world::TurretSweep& sweep)const {
    const auto found=context_.geometry.find(ID(sweep.actor));
    if(found==context_.geometry.end())throw std::invalid_argument("Turret sweep descriptor is unavailable");
    return context_.turrets.blocks(sweep,found->second.turret,context_.server_time_ms);
}
bool NativeHost::corridor(const motion::State& state,double yaw,bool wet,bool shallow,std::optional<double> maximum) {
    const auto actor=context_.actors.find(state.id);if(actor==context_.actors.end()||!actor->second.descriptor)throw std::invalid_argument("Corridor descriptor is unavailable");
    const auto& d=*actor->second.descriptor;nav::CandidateState candidate;
    candidate.id=state.id;candidate.position=state.position;candidate.yaw=state.yaw;candidate.speed=state.speed;candidate.now=context_.now;
    candidate.half_length=d.half_length;candidate.half_width=d.half_width;
    nav::CandidateProbe probe(context_.grid,context_.navigator,context_.candidates,candidate,{true,d.half_width},context_.bake);
    return probe.corridor(yaw,wet,shallow,false,maximum).value_or(true);
}
void NativeHost::impact(motion::State& state,double speed,double now) {
    const auto found=worlds_.find(state.id);if(found==worlds_.end())return;
    const auto trace=found->second->consume_trace();if(!state.airborne||!trace.present)return;
    const auto& n=trace.response.normal;const double length=std::sqrt(n[0]*n[0]+n[1]*n[1]+n[2]*n[2]);
    if(!std::isfinite(length)||length<=1.e-8)return;
    const double value=std::max(0.,-(std::sin(state.yaw)*speed*n[0]+std::cos(state.yaw)*speed*n[2])/length);
    auto& memory=context_.memory[state.id];if(value<=10.||now-memory.last_world_impact_time<.25)return;
    const Point normal{{n[0],n[1],n[2]}},hit{{trace.response.point[0],trace.response.point[1],trace.response.point[2]}};
    if(context_.impacts.fall_damage(state,value,{hit},normal,false)>0)memory.last_world_impact_time=now;
}
void NativeHost::invalidate(ID id,double yaw,bool) {
    context_.invalidated_decisions.insert(id);context_.motion_plan.cache.erase(id);context_.planner.remember_failure(id,yaw,5.);
}
void NativeHost::report_contact(const motion::State& state,const Point& start,const Point& target,double yaw,double now) {
    context_.navigator.reportHardContact(state.id,start,target,yaw,now);
}
void NativeHost::motion_finished(const motion::State& state,motion::MotionStatus status,double before,double after) {
    context_.observations.push_back({state.id,status,before,after});
}
Vec NativeHost::slide(const motion::State& state,const motion::Descriptor& descriptor,const Vec& movement,double& fraction) {
    const auto owner=body(state,descriptor,false);const auto others=peers(state.id,false);const offline_math::Vec move{movement[0],movement[1]};
    fraction=offline_math::translation(owner,move,others);const auto accepted=offline_math::slide(owner,move,others,true,fraction);return {{accepted.x,accepted.z}};
}
Vec NativeHost::passive_move(ID id,step::VerticalState& state,double x,double z,double elapsed) {
    const auto a=context_.actors.find(id);const auto w=worlds_.find(id);
    if(a==context_.actors.end()||!a->second.motion||!a->second.descriptor||w==worlds_.end())throw std::invalid_argument("Passive actor is unavailable");
    auto& actor=*a->second.motion;static_cast<step::VerticalState&>(actor)=state;const auto before=actor.position;
    motion::ContactResponse response;response.correction={{x,z}};motion::ContactOptions options;
    options.advance_push=false;options.advance_forward=true;options.apply_friction=false;
    motion::contact(actor,*a->second.descriptor,response,elapsed,context_.now,*w->second,options);
    state=actor;return {{actor.position[0]-before[0],actor.position[2]-before[2]}};
}
void NativeHost::landing(motion::State& state,double impact,bool normal_impact,const std::vector<Point>& witnesses,const Point& normal,bool ground_observed) {
    if(!state.alive)return;const double lateral=std::sqrt(state.air_lateral_x*state.air_lateral_x+state.air_lateral_z*state.air_lateral_z);
    if(lateral>.01)state.slide_speed=std::max(state.slide_speed,lateral);state.air_lateral_x=state.air_lateral_z=0.;
    if(!normal_impact)impact=std::sqrt(impact*impact+lateral*lateral);
    context_.impacts.fall_damage(state,impact,witnesses,normal,ground_observed);
}
void NativeHost::vertical_outcome(motion::State& state,const step::VerticalOutcome& outcome) {
    auto& memory=context_.memory[state.id];if(outcome.clear_contact_memory)context_.vertical_memory[state.id]={};
    if(outcome.clear_pose_sample)memory.pose_sample.reset();
    if(outcome.pose_sample_yaw&&memory.pose_sample)(*memory.pose_sample)[2]=*outcome.pose_sample_yaw;
    if(outcome.invalidate_motion)invalidate(state.id,outcome.invalidated_yaw.value_or(state.yaw),true);
    if(outcome.landing_impact)landing(state,*outcome.landing_impact,outcome.normal_impact,outcome.landing_witnesses,outcome.landing_normal,outcome.ground_observed);
}
bool NativeHost::pose_safe(const Point& position)const {
    return bool(context_.grid.index(context_.grid.cellFor(position),false))&&!context_.grid.pointHazard(position,9);
}
bool NativeHost::guard_realised_pose(motion::State& state,const Point& tick,bool was_safe,double attempted,
    const std::optional<SuspensionSnapshot>& snapshot,bool hazards,std::optional<Point> navigation_pose) {
    if(pose_progress(state,tick,state.yaw,state.position,state.yaw)&&(!hazards||!was_safe||pose_safe(navigation_pose.value_or(state.position))))return false;
    state.position=tick;state.speed=0.;state.movement_direction=state.rotation_direction=0;state.push_x=state.push_z=0.;
    if(snapshot) {
        restore(state,snapshot->state);context_.vertical_memory[state.id]=snapshot->memory;context_.memory[state.id].pose_sample=snapshot->pose_sample;
    } else {
        state.vertical_speed=0.;state.airborne=false;
        if(context_.geometry.at(state.id).detailed_suspension&&!state.suspension_disabled) {
            state.pitch_speed=state.roll_speed=state.support_vertical_speed=0.;state.support_gradient.reset();state.ground_plane.reset();context_.vertical_memory[state.id]={};
        }
    }
    invalidate(state.id,attempted,true);return true;
}
bool NativeHost::update_slope_pose(motion::State& state,int tier,bool allow_ungrounded) {
    const auto& geometry=context_.geometry.at(state.id);
    if((geometry.detailed_suspension&&!state.suspension_disabled)||state.airborne||(!allow_ungrounded&&!state.grounded_once))return false;
    if(tier<0||tier>2)throw std::invalid_argument("Slope detail tier is invalid");
    const double travel[]={.35,1.5,4.},turn[]={.05,.15,.40};const bool exact=!geometry.pitch_hull_aiming;
    auto& memory=context_.memory[state.id];double pitch,roll;std::optional<std::array<double,2>> target;
    if(memory.pose_sample&&std::abs(state.position[0]-(*memory.pose_sample)[0])<travel[tier]&&
       std::abs(state.position[2]-(*memory.pose_sample)[1])<travel[tier]&&std::abs(state.yaw-(*memory.pose_sample)[2])<turn[tier]) {
        if(!state.slope_target||std::max(std::abs((*state.slope_target)[0]-state.terrain_pitch),std::abs((*state.slope_target)[1]-state.roll))<.001)return false;
        ++memory.slope_attempt;
        pitch=exact?(*state.slope_target)[0]:state.terrain_pitch+((*state.slope_target)[0]-state.terrain_pitch)*.5;
        roll=exact?(*state.slope_target)[1]:state.roll+((*state.slope_target)[1]-state.roll)*.5;
    } else {
        ++memory.slope_attempt;
        const auto w=worlds_.find(state.id);if(w==worlds_.end())throw std::invalid_argument("Slope ground world is unavailable");
        const auto& d=*context_.actors.at(state.id).descriptor;const double length=std::max(3.,2.*d.half_length),width=std::max(2.,2.*d.half_width);
        const double sn=std::sin(state.yaw),cs=std::cos(state.yaw),x=state.position[0],z=state.position[2],y=state.position[1];
        const auto front=w->second->basic_ground(x+sn*length*.5,z+cs*length*.5,y,{}),rear=w->second->basic_ground(x-sn*length*.5,z-cs*length*.5,y,{}),
            right=w->second->basic_ground(x+cs*width*.5,z-sn*width*.5,y,{}),left=w->second->basic_ground(x-cs*width*.5,z+sn*width*.5,y,{});
        if(!front||!rear||!right||!left){if(exact)return false;pitch=state.terrain_pitch;roll=state.roll;}
        else {
            const double grade=(*front-*rear)/length;
            pitch=exact?-std::atan(grade):-std::atan2(*front-*rear,length)*.9;
            roll=exact?std::atan2((*right-*left)/width,std::sqrt(1.+grade*grade)):std::atan2(*right-*left,width)*.9;
            const double tilt=exact?std::hypot(pitch,roll):std::sqrt(pitch*pitch+roll*roll);
            if(tilt>.61){if(exact){pitch=pitch*.61/tilt;roll=roll*.61/tilt;}else{const double scale=.61/tilt;pitch*=scale;roll*=scale;}}
            if(!exact){pitch=state.terrain_pitch+(pitch-state.terrain_pitch)*.5;roll=state.roll+(roll-state.roll)*.5;}
        }
        target=exact?std::array<double,2>{{pitch,roll}}:std::array<double,2>{{pitch*2.-state.terrain_pitch,roll*2.-state.roll}};
    }
    auto after=state;after.terrain_pitch=pitch;after.pitch=pitch+state.hydraulic_pitch;after.roll=roll;
    if(turret_blocks(turret_sweep(state,after)))return false;
    state.terrain_pitch=pitch;state.pitch=after.pitch;state.roll=roll;state.slope_basis_yaw=state.yaw;
    if(target){state.slope_target=target;memory.pose_sample=std::array<double,3>{{state.position[0],state.position[2],state.yaw}};}return true;
}
}
