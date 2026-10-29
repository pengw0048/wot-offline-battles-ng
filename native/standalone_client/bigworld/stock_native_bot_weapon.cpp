#include "stock_native_bot_weapon.hpp"
#include <stdexcept>
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <string>
#include <vector>

namespace wot::offline_bot_step {
namespace {
std::uint32_t rotl(std::uint32_t x,unsigned n) {return (x<<n)|(x>>(32-n));}
std::uint32_t seed_sha1(const std::string& value) {
    std::vector<std::uint8_t> bytes(value.begin(),value.end());
    const std::uint64_t bits=bytes.size()*8ull;
    bytes.push_back(0x80);while (bytes.size()%64!=56) bytes.push_back(0);
    for (int i=7;i>=0;--i) bytes.push_back(static_cast<std::uint8_t>(bits>>(8*i)));
    std::uint32_t h0=0x67452301u,h1=0xefcdab89u,h2=0x98badcfeu,h3=0x10325476u,h4=0xc3d2e1f0u;
    for (std::size_t offset=0;offset<bytes.size();offset+=64) {
        std::uint32_t w[80]{};
        for (int i=0;i<16;++i) for (int j=0;j<4;++j) w[i]=(w[i]<<8)|bytes[offset+4*i+j];
        for (int i=16;i<80;++i) w[i]=rotl(w[i-3]^w[i-8]^w[i-14]^w[i-16],1);
        std::uint32_t a=h0,b=h1,c=h2,d=h3,e=h4;
        for (int i=0;i<80;++i) {
            std::uint32_t f=0,k=0;
            if (i<20) {f=(b&c)|((~b)&d);k=0x5a827999u;}
            else if (i<40) {f=b^c^d;k=0x6ed9eba1u;}
            else if (i<60) {f=(b&c)|(b&d)|(c&d);k=0x8f1bbcdcu;}
            else {f=b^c^d;k=0xca62c1d6u;}
            const auto tmp=rotl(a,5)+f+e+k+w[i];e=d;d=c;c=rotl(b,30);b=a;a=tmp;
        }
        h0+=a;h1+=b;h2+=c;h3+=d;h4+=e;
    }
    return h0&0x7fffffffu;
}
class PythonRandom {
    std::uint32_t state_[624]{};int index_=624;
    std::uint32_t next() {
        if (index_>=624) {
            for (int i=0;i<624;++i) {
                const auto y=(state_[i]&0x80000000u)|(state_[(i+1)%624]&0x7fffffffu);
                state_[i]=state_[(i+397)%624]^(y>>1)^((y&1)?0x9908b0dfu:0u);
            }
            index_=0;
        }
        auto y=state_[index_++];y^=y>>11;y^=(y<<7)&0x9d2c5680u;
        y^=(y<<15)&0xefc60000u;y^=y>>18;return y;
    }
public:
    explicit PythonRandom(std::uint32_t key) {
        state_[0]=19650218u;
        for (int i=1;i<624;++i) state_[i]=1812433253u*(state_[i-1]^(state_[i-1]>>30))+i;
        int i=1;
        for (int k=624;k>0;--k) {
            state_[i]=(state_[i]^((state_[i-1]^(state_[i-1]>>30))*1664525u))+key;
            if (++i>=624) {state_[0]=state_[623];i=1;}
        }
        for (int k=623;k>0;--k) {
            state_[i]=(state_[i]^((state_[i-1]^(state_[i-1]>>30))*1566083941u))-i;
            if (++i>=624) {state_[0]=state_[623];i=1;}
        }
        state_[0]=0x80000000u;
    }
    double random() {const auto a=next()>>5,b=next()>>6;return (a*67108864.0+b)*(1.0/9007199254740992.0);}
};
bool close_to_segment(const Point& p,const Point& a,const Point& b) {
    constexpr double radius=15.;
    for (int i=0;i<3;++i) if (p[i]<std::min(a[i],b[i])-radius || p[i]>std::max(a[i],b[i])+radius) return false;
    double length=0.,dot=0.;
    for (int i=0;i<3;++i) {const double d=b[i]-a[i];length+=d*d;dot+=(p[i]-a[i])*d;}
    const double fraction=length>0.?std::max(0.,std::min(1.,dot/length)):0.;
    double square=0.;for (int i=0;i<3;++i) {const double d=p[i]-(a[i]+fraction*(b[i]-a[i]));square+=d*d;}
    return square<=radius*radius;
}
}
std::vector<AimSample> vehicle_aim_samples(const std::vector<AimBox>& boxes) {
    std::vector<AimSample> centres,edges;
    for (const auto& box:boxes) {
        if (std::any_of(box.lower.begin(),box.lower.end(),[](double x){return !std::isfinite(x);}) ||
            std::any_of(box.upper.begin(),box.upper.end(),[](double x){return !std::isfinite(x);})) continue;
        if (box.upper[0]<=box.lower[0] || box.upper[1]<=box.lower[1] || box.upper[2]<=box.lower[2]) continue;
        AimSample sample;sample.mount=box.mount;sample.turret=box.turret;sample.static_yaw=box.static_yaw;
        for (int i=0;i<3;++i) sample.point[i]=(box.lower[i]+box.upper[i])*.5;
        centres.push_back(sample);
        for (const auto& offset:std::array<std::pair<int,double>,3>{{{0,-1.},{0,1.},{1,1.}}}) {
            auto edge=sample;edge.point[offset.first]+=(box.upper[offset.first]-box.lower[offset.first])*.25*offset.second;
            edges.push_back(edge);
        }
    }
    centres.insert(centres.end(),edges.begin(),edges.end());return centres;
}
Point vehicle_aim_point(const AimSample& sample,const AimPose& pose) {
    auto p=sample.point;
    if (sample.turret) {const double a=sample.static_yaw.value_or(pose.turret_yaw),c=std::cos(a),s=std::sin(a);
        p={c*p[0]+s*p[2],p[1],-s*p[0]+c*p[2]};}
    for (int i=0;i<3;++i)p[i]+=sample.mount[i];
    const double cr=std::cos(pose.roll),sr=std::sin(pose.roll),cp=std::cos(pose.pitch),sp=std::sin(pose.pitch),cy=std::cos(pose.yaw),sy=std::sin(pose.yaw);
    Point z{cr*p[0]-sr*p[1],sr*p[0]+cr*p[1],p[2]};
    Point x{z[0],cp*z[1]-sp*z[2],sp*z[1]+cp*z[2]};
    return {pose.position[0]+cy*x[0]+sy*x[2],pose.position[1]+x[1],pose.position[2]-sy*x[0]+cy*x[2]};
}
bool select_exposed_aim(AimSelection& selection,const AimPose& pose,const Point& origin,
    double rating,std::int64_t round,std::int64_t bot,const GunnerTarget& target,
    const GunnerState& gunner,AimSelectionWorld& world) {
    if (selection.samples.empty()) {selection.selected.reset();return false;}
    std::vector<std::size_t> attempts;
    if (selection.selected && *selection.selected<selection.samples.size())attempts.push_back(*selection.selected);
    for (std::size_t visited=0;attempts.size()<2 && visited<selection.samples.size();++visited) {
        const auto index=selection.cursor%selection.samples.size();selection.cursor=(index+1)%selection.samples.size();
        if (std::find(attempts.begin(),attempts.end(),index)==attempts.end())attempts.push_back(index);
    }
    selection.selected.reset();
    const auto epoch=gunner.held && gunner.target_kind==target.kind && gunner.target_id==target.id ? gunner.epoch : 0;
    const std::string identity="('"+target.kind+"', "+std::to_string(target.id)+")";
    const double probability=std::isfinite(rating)?std::max(0.,std::min(1.,rating)):.34;
    const bool ranks=probability>=1. || (probability>0. && PythonRandom(seed_sha1(
        "bot-capability-v1|weak_spot|"+std::to_string(round)+"|"+std::to_string(bot)+"|"+identity+"|"+std::to_string(epoch))).random()<probability);
    std::optional<std::size_t> exposed,best;std::optional<double> best_score;
    try { for (auto index:attempts) {
        const auto point=vehicle_aim_point(selection.samples[index],pose);
        if (world.static_segment_blocked(origin,point) || world.wreck_segment_blocked(origin,point))continue;
        const auto score=world.nominal_damage(origin,point);if (!exposed)exposed=index;
        if (score && *score==0.)continue;
        if (!best || (ranks && score && best_score && *score>*best_score)) {best=index;best_score=score;}
        if (!score || !ranks)break;
    }} catch (...) {selection.selected.reset();return false;}
    selection.selected=best?best:exposed;return selection.selected.has_value();
}
std::array<double,2> dispersed_shot_angles(std::int64_t round,std::int64_t bot,
    const offline_simulation::weapons::BurstEdge& edge,const AimPose& pose,double dispersion) {
    if (!std::isfinite(dispersion) || dispersion<=0.) throw std::invalid_argument("Invalid shot dispersion");
    const std::uint32_t seed=(std::uint64_t(round&0xffff)*1000003ull+
        std::uint64_t(bot&0xffff)*9176ull+std::uint64_t(edge.group_seq&0x7fffffff)*6113ull+
        std::uint64_t(edge.index&0xffff)*3571ull)&0x7fffffffu;
    PythonRandom random(seed);
    const double angle=random.random()*2.*std::acos(-1.);
    double radius=std::abs(std::cos(angle)*std::sqrt(-2.*std::log(1.-random.random()))*dispersion*.5);
    if (radius>dispersion) radius=dispersion*random.random();
    const double azimuth=random.random()*2.*std::acos(-1.);
    const bool posed=std::abs(pose.pitch)>1.e-12 || std::abs(pose.roll)>1.e-12;
    const double h=std::cos(pose.gun_pitch);
    const Point base=posed ? shot_direction(pose) : Point{std::sin(pose.aim_yaw)*h,-std::sin(pose.gun_pitch),std::cos(pose.aim_yaw)*h};
    return offline_simulation::weapons::scatter(base,radius,azimuth,posed);
}
LaneResult prove_friendly_path(const std::vector<Point>& path,int team,double splash,
    const std::vector<LaneBody>& roster,DirectWorld& world) {
    LaneResult out;
    if (path.size()<2 || !std::isfinite(splash) || splash<0.) return out;
    for (const auto& point:path) for (double value:point) if (!std::isfinite(value)) return out;
    const auto contacts=[&](const LaneBody& body,bool use_splash) {
        if (use_splash && splash>0.) {
            double d=0.;for (int i=0;i<3;++i) {const double n=body.position[i]-path.back()[i];d+=n*n;}
            if (d<=splash*splash) return true;
        }
        for (std::size_t i=1;i<path.size();++i)
            if (close_to_segment(body.position,path[i-1],path[i]) && world.vehicle_segment(body,path[i-1],path[i])) return true;
        return false;
    };
    try {
        for (const auto& body:roster) if (body.available && !body.source && !body.alive && contacts(body,false)) return out;
        for (std::size_t i=1;i<path.size();++i) if (world.debris_segment(path[i-1],path[i])) return out;
        for (const auto& body:roster) {
            if (!body.available || body.source || !body.alive || body.team!=team) continue;
            if (contacts(body,true)) {out.has_blocker=true;out.blocker=body;return out;}
        }
    } catch (const std::exception&) {return out;}
    out.clear=true;return out;
}
LaneResult prove_direct_launch(offline_simulation::ActorKey key,const AimPose& pose,
    const GunMount& mount,const DirectShot& shot,const offline_simulation::weapons::BurstEdge& edge,
    std::int64_t time,const std::vector<LaneBody>& roster,DirectWorld& world) {
    LaneResult out;
    if (!(shot.speed>0.) || !(shot.gravity>0.) || !(shot.maximum_distance>0.) ||
        !(shot.flight_time>0.) || shot.flight_time>20. || shot.speed*shot.flight_time>shot.maximum_distance+1.e-6 ||
        !std::isfinite(shot.speed) || !std::isfinite(shot.gravity) || !std::isfinite(shot.maximum_distance) ||
        !std::isfinite(shot.flight_time) || !std::isfinite(shot.dispersion) || shot.dispersion<=0. ||
        !std::isfinite(shot.splash_radius) || shot.splash_radius<0.) return out;
    try {if (world.barrel_under_water(shot.barrel_point)) return out;}
    catch (const std::exception&) {return out;}
    const auto origin=shot_origin(pose,mount);
    for (double x:origin) if (!std::isfinite(x)) return out;
    const auto angles=dispersed_shot_angles(shot.round_id,key.id,edge,pose,shot.dispersion);
    const int count=std::max(1,static_cast<int>(std::ceil(shot.flight_time/.05)));
    std::vector<Point> path;path.reserve(count+1);
    for (int i=0;i<=count;++i) path.push_back(offline_simulation::weapons::ballistic_position(
        origin,angles[0],-angles[1],shot.speed,shot.gravity,shot.flight_time*double(i)/count));
    out=prove_friendly_path(path,shot.team,shot.splash_radius,roster,world);
    if (!out.clear) return out;
    offline_simulation::weapons::LaunchRecord launch;
    launch.actor=key;launch.edge=edge;launch.launch_time_us=time;launch.pose.position=pose.position;
    launch.pose.yaw=pose.yaw;launch.pose.pitch=pose.pitch;launch.pose.roll=pose.roll;
    launch.yaw=angles[0];launch.pitch=angles[1];launch.has_origin=true;launch.origin=origin;
    launch.class_tag=shot.class_tag;out.launch=launch;out.clear=true;return out;
}
std::optional<GunnerResult> advance_gunner(GunnerState& state,const GunnerConfig& config,
    std::optional<GunnerTarget> target,const Point& source,double dispersion,double factor,
    std::int64_t round,std::int64_t bot,std::int64_t fire,double now) {
    if (!target) return std::nullopt;
    if (!state.held || state.target_kind!=target->kind || state.target_id!=target->id) {
        state.held=true;state.target_kind=target->kind;state.target_id=target->id;
        state.since=state.laid_since=now;state.fire_sequence=fire;state.fired=false;state.error_valid=false;
    }
    if (state.fire_sequence!=fire) {state.fire_sequence=fire;state.laid_since=now;state.fired=true;}
    const double held=std::max(0.,now-state.since),laying=std::max(0.,now-state.laid_since);
    const auto epoch=static_cast<std::int64_t>(held/1.5);
    if (!state.error_valid || state.epoch!=epoch) {
        state.epoch=epoch;state.error_valid=true;
        const std::string tuple="('"+target->kind+"', "+std::to_string(target->id)+")";
        PythonRandom random(seed_sha1("bot-gunner-v1|"+std::to_string(round)+"|"+
            std::to_string(bot)+"|"+tuple+"|"+std::to_string(epoch)));
        const double angle=random.random()*2.*std::acos(-1.);
        state.radius=std::abs(std::cos(angle)*std::sqrt(-2.*std::log(1.-random.random()))*.5);
        if (state.radius>1.) state.radius=random.random();
        state.azimuth=random.random()*2.*std::acos(-1.);
        state.lead_scale=1.+(random.random()*2.-1.)*config.lead_error;
    }
    GunnerResult result;result.aimed=*target;
    const double dx=target->position[0]-source[0],dy=target->position[1]-source[1],dz=target->position[2]-source[2];
    const double horizontal=std::hypot(dx,dz),distance=std::sqrt(dx*dx+dy*dy+dz*dz);
    if (horizontal>1.) {
        const double magnitude=std::isfinite(dispersion) && dispersion>0. && std::isfinite(distance) && distance>0. ?
            std::min(8.,config.aim_bias_factor*dispersion*distance*state.radius) : 0.;
        const double lateral=magnitude*std::cos(state.azimuth),vertical=magnitude*std::sin(state.azimuth)*.45;
        result.aimed.position[0]+=dz/horizontal*lateral;result.aimed.position[1]+=vertical;
        result.aimed.position[2]-=dx/horizontal*lateral;
        for (auto& value:result.aimed.velocity) value*=state.lead_scale;
    }
    result.ready=std::isfinite(held) && !std::isnan(laying) && !std::isnan(factor) && held>=config.reaction_seconds &&
        (state.fired || (std::isfinite(factor) && factor<=config.converged_factor) || laying>=config.patience_seconds);
    return result;
}
bool world_solution_reachable(const WeaponFrame& frame,const HydraulicConfig& hydraulic,
    const offline_simulation::weapons::AimConfig& config,double yaw,double pitch) {
    AimIntent intent;intent.world_yaw=yaw;intent.world_pitch=pitch;
    const auto classify=[&](double correction) {
        if (!(frame.valid_pitch || frame.pitch_locked)) return 2;
        auto pose=frame.pose;pose.pitch=frame.terrain_pitch+correction;
        const auto local=local_aim_intent(pose,intent);
        const auto limits=frame.pitch_locked?std::array<double,2>{frame.static_pitch,frame.static_pitch}:
            offline_simulation::weapons::pitch_limits(local.raw_yaw,config);
        if (local.raw_pitch<limits[0]-1.e-8) return -1;
        if (local.raw_pitch>limits[1]+1.e-8) return 1;
        return 0;
    };
    bool reachable=false;
    if (hydraulic.present && hydraulic.available && hydraulic.enabled && frame.siege_enabled) {
        const double neutral=std::max(hydraulic.minimum,std::min(hydraulic.maximum,0.));
        const int status=classify(neutral);
        if (status==0) reachable=true;
        else if (status!=2) {
            const int edge=classify(status<0?hydraulic.minimum:hydraulic.maximum);
            reachable=edge!=2 && !((status<0 && edge<0) || (status>0 && edge>0));
        }
    } else {
        // Non-hydraulic admission uses the source's wider 1e-4 envelope.
        if (frame.valid_pitch || frame.pitch_locked) {
            const auto local=local_aim_intent(frame.pose,intent);
            const auto limits=frame.pitch_locked?std::array<double,2>{frame.static_pitch,frame.static_pitch}:
                offline_simulation::weapons::pitch_limits(local.raw_yaw,config);
            reachable=limits[0]-.0001<=local.raw_pitch && local.raw_pitch<=limits[1]+.0001;
        }
    }
    return reachable;
}
std::optional<AimTarget> direct_ballistic_solution(const BallisticInput& input,
    const WeaponFrame& frame,const HydraulicConfig& hydraulic,
    const offline_simulation::weapons::AimConfig& config) {
    const double pi=std::acos(-1.);
    const auto solution=offline_simulation::weapons::ballistic_intercept(input.origin,input.target,input.velocity,
        input.speed,input.gravity,-pi*.5,pi*.5,false,20.);
    if (!solution.valid || solution.time>20. || input.speed*solution.time>input.maximum_distance+1.e-6)
        return std::nullopt;
    const double yaw=std::atan2(solution.aim[0]-input.origin[0],solution.aim[2]-input.origin[2]);
    if (!world_solution_reachable(frame,hydraulic,config,yaw,solution.pitch)) return std::nullopt;
    AimTarget target;target.target_present=target.solution_present=target.solution_origin_present=true;
    target.aim_position=solution.aim;target.solution_origin=input.origin;
    target.solution_yaw=yaw;target.solution_pitch=solution.pitch;target.flight_time=solution.time;
    return target;
}
bool BallisticSignature::operator==(const BallisticSignature& b) const {
    return target_kind==b.target_kind && target_id==b.target_id && descriptor_revision==b.descriptor_revision &&
        fire_sequence==b.fire_sequence && shell==b.shell && siege_state==b.siege_state && target_alive==b.target_alive;
}
BallisticResult advance_direct_ballistic(BallisticCache& cache,const BallisticSignature& signature,
    const BallisticInput& input,const WeaponFrame& frame,const HydraulicConfig& hydraulic,
    const offline_simulation::weapons::AimConfig& config,std::int64_t bot_id,
    double now,bool refresh_control,bool force) {
    const bool fresh=force || !cache.initialized || !(cache.signature==signature) ||
        (refresh_control && now+1.e-9>=cache.deadline);
    if (fresh) {
        const bool stagger=!cache.initialized;
        cache.solution=direct_ballistic_solution(input,frame,hydraulic,config);
        cache.signature=signature;cache.deadline=now+.1;
        if (stagger) {
            const auto id=bot_id<0?std::uint64_t(-(bot_id+1))+1:std::uint64_t(bot_id);
            const auto phase=((id%29)*17+13*11)%29;
            cache.deadline+=double(phase)/29.*.1;
        }
        cache.initialized=true;
    }
    return {fresh,cache.solution};
}
WeaponAimResult advance_weapon_aim(offline_simulation::weapons::Store& store,
    offline_simulation::ActorKey key,WeaponFrame& frame,const AimTarget& target,
    const std::optional<GunMount>& mount,const HydraulicConfig& hydraulic,WeaponWorld& world) {
    namespace weapons=offline_simulation::weapons;
    auto& actor=store.at(key);
    WeaponAimResult out;
    const auto world_intent=world_aim_intent(frame.pose,target,mount);
    if (!world_intent) {
        out.origin_available=false;actor.aim.aligned=false;
        weapons::AimInput input;input.dt=frame.elapsed;
        out.edges=store.after_motion(key,input,std::abs(frame.speed),std::abs(frame.turn_speed),
            0.,frame.dispersion_factor,frame.aim_factor,false);
        return out;
    }
    out.desired_world_yaw=world_intent->world_yaw;out.horizontal=world_intent->horizontal;
    const bool valid_pitch=frame.pitch_locked || frame.valid_pitch;
    const auto limits=[&](double yaw) {
        return frame.pitch_locked ? std::array<double,2>{frame.static_pitch,frame.static_pitch}
                                  : weapons::pitch_limits(yaw,actor.aim_config);
    };
    double target_correction=0.;
    if (hydraulic.present && hydraulic.available && hydraulic.enabled && frame.siege_enabled) {
        const double neutral=std::max(hydraulic.minimum,std::min(hydraulic.maximum,0.));
        const auto classify=[&](double correction) {
            if (!valid_pitch) return 2;
            auto pose=frame.pose;pose.pitch=frame.terrain_pitch+correction;
            const auto local=local_aim_intent(pose,*world_intent);
            const auto range=limits(local.raw_yaw);
            if (local.raw_pitch<range[0]-1.e-8) return -1;
            if (local.raw_pitch>range[1]+1.e-8) return 1;
            return 0;
        };
        const int status=classify(neutral); target_correction=neutral;
        if (status==0) out.hydraulic_reachable=true;
        else if (status!=2) {
            const double edge=status<0?hydraulic.minimum:hydraulic.maximum;
            const int edge_status=classify(edge);target_correction=edge;
            if (edge_status!=2 && !((status<0 && edge_status<0) || (status>0 && edge_status>0))) {
                double blocked=neutral,feasible=edge;
                for (int i=0;i<48;++i) {
                    const double candidate=(blocked+feasible)*.5;
                    if (classify(candidate)==status) blocked=candidate;else feasible=candidate;
                }
                target_correction=feasible;out.hydraulic_reachable=true;
            }
        }
    }
    double correction=0.;
    if (hydraulic.present) {
        const double current=std::max(hydraulic.minimum,std::min(hydraulic.maximum,frame.hydraulic_pitch));
        const double limit=std::max(0.,hydraulic.speed)*std::max(0.,frame.elapsed);
        correction=current+std::max(-limit,std::min(limit,target_correction-current));
    }
    auto after=frame.pose;after.pitch=frame.terrain_pitch+correction;
    if (world.turret_clear(frame.pose,after)) {
        frame.hydraulic_pitch=correction;frame.pose.pitch=after.pitch;
    }
    out.hydraulic_pitch=frame.hydraulic_pitch;
    const auto local=local_aim_intent(frame.pose,*world_intent);
    double slew_yaw=local.raw_yaw;
    if (frame.limited_yaw && frame.rear_turn!=0.)
        slew_yaw=limited_traverse_target(local.raw_yaw,frame.minimum_yaw,frame.maximum_yaw,frame.rear_turn);
    weapons::AimInput input;
    input.raw_yaw=slew_yaw;input.raw_pitch=local.raw_pitch;input.dt=frame.elapsed;
    input.target=frame.target;input.limited_yaw=frame.limited_yaw;input.valid_pitch=valid_pitch;
    input.minimum_yaw=frame.minimum_yaw;input.maximum_yaw=frame.maximum_yaw;
    input.crew_factor=frame.crew_factor;input.turret_factor=frame.turret_factor;input.gun_factor=frame.gun_factor;
    if (frame.pitch_locked) {input.override_pitch=true;input.pitch_limits={frame.static_pitch,frame.static_pitch};}
    out.edges=store.after_motion(key,input,std::abs(frame.speed),std::abs(frame.turn_speed),
        0.,frame.dispersion_factor,frame.aim_factor,true);
    frame.pose.turret_yaw=actor.aim.turret_yaw;frame.pose.gun_pitch=actor.aim.gun_pitch;
    if (mount) frame.pose.aim_yaw=world_barrel_yaw(frame.pose);
    else frame.pose.aim_yaw=std::atan2(std::sin(frame.pose.yaw+frame.pose.turret_yaw),std::cos(frame.pose.yaw+frame.pose.turret_yaw));
    const double yaw_error=std::atan2(std::sin(local.raw_yaw-actor.aim.turret_yaw),std::cos(local.raw_yaw-actor.aim.turret_yaw));
    actor.aim.aligned=actor.aim.aligned && !frame.planning_pending && std::abs(yaw_error)<=1.e-6 &&
        std::abs(local.raw_pitch-actor.aim.gun_pitch)<=1.e-6;
    out.aligned=actor.aim.aligned;return out;
}
FireResult advance_weapon_fire(offline_simulation::weapons::Store& store,
    offline_simulation::ActorKey key,const WeaponFrame& frame,const FireConditions& c,
    const std::vector<offline_simulation::weapons::BurstEdge>& edges,
    std::int64_t start_us,std::int64_t end_us,const GunMount& mount,const DirectShot& shot,
    const std::vector<LaneBody>& roster,DirectWorld& scene,
    const std::optional<offline_simulation::weapons::LaunchRecord>& artillery_launch) {
    FireResult out;
    auto& actor=store.at(key);
    const auto prepare=[&](const offline_simulation::weapons::BurstEdge& edge,std::int64_t time,bool artillery) {
        auto physical=shot;
        physical.dispersion=std::max(actor.gun.dispersion,actor.gun.config.fully_aimed_dispersion*c.dispersion_factor);
        std::optional<offline_simulation::weapons::LaunchRecord> launch;
        if (artillery) launch=artillery_launch;
        else {const auto proof=prove_direct_launch(key,frame.pose,mount,physical,edge,time,roster,scene);
            launch=proof.launch;if(proof.has_blocker)out.blocker=proof.blocker;}
        if (launch && artillery) launch->launch_time_us=time;
        return launch;
    };
    for (const auto& edge:edges) {
        const auto offset=std::max<std::int64_t>(0,static_cast<std::int64_t>(std::round(edge.due_offset*1000000.)));
        const auto time=std::min(end_us,start_us+offset);
        if (!c.alive || !actor.aim.aligned || c.drowning || c.overturned || c.gun_destroyed ||
            c.siege_switching || !actor.ammo.can_fire(true)) {
            store.cancel(key);out.cancelled=true;break;
        }
        const auto launch=prepare(edge,time,false);
        if (!launch || !store.commit(key,*launch,c.dispersion_factor)) {
            store.cancel(key);out.cancelled=true;break;
        }
        ++out.committed;
    }
    const bool in_range=c.has_target && c.target_distance>1. && c.has_ballistic &&
        (c.fire_range<=0. || c.target_distance<c.fire_range);
    if (!(c.publish && c.action_fresh && c.fire_allowed && in_range && !c.drowning &&
          !c.overturned && !c.gun_destroyed && actor.aim.aligned && !actor.burst.active &&
          actor.gun.ready(c.reload_factor) && actor.ammo.can_fire() && c.shot_clear && c.gunner_ready &&
          !c.wire_failure)) return out;
    const auto& config=actor.gun.config;
    const int count=std::min(c.artillery?1:config.burst_count,
        std::min(actor.ammo.remaining[actor.ammo.loaded],actor.gun.clip));
    const double interval=c.artillery?0.:config.burst_interval;
    if (count<=0) return out;
    offline_simulation::weapons::BurstEdge expected;
    expected.shot_seq=expected.group_seq=actor.fire_seq+1;expected.count=count;
    expected.shell=actor.ammo.loaded;expected.final_round=count==1;
    const auto launch=prepare(expected,end_us,c.artillery);
    if (!launch) return out;
    if (!store.begin(key,count,interval,c.reload_factor)) return out;
    const auto first=actor.burst.advance(0.);
    if (first.empty() || !store.commit(key,*launch,c.dispersion_factor)) {
        store.cancel(key);out.cancelled=true;return out;
    }
    out.started=true;++out.committed;return out;
}
} // namespace wot::offline_bot_step
