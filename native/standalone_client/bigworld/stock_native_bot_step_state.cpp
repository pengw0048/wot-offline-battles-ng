#include "stock_native_bot_step_state.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <stdexcept>

namespace wot::offline_bot_step {
namespace {
constexpr double pi = 3.14159265358979323846;
Point add(const Point& a, const Point& b) {
    return {a[0]+b[0], a[1]+b[1], a[2]+b[2]};
}
Point rx(const Point& p, double a) {
    const double s=std::sin(a), c=std::cos(a);
    return {p[0], c*p[1]-s*p[2], s*p[1]+c*p[2]};
}
Point ry(const Point& p, double a) {
    const double s=std::sin(a), c=std::cos(a);
    return {c*p[0]+s*p[2], p[1], -s*p[0]+c*p[2]};
}
Point rz(const Point& p, double a) {
    const double s=std::sin(a), c=std::cos(a);
    return {c*p[0]-s*p[1], s*p[0]+c*p[1], p[2]};
}
Point transform(const Point& p, const AimPose& pose) {
    return ry(rx(rz(p, pose.roll), pose.pitch), pose.yaw);
}
}

Point shot_origin(const AimPose& pose, const GunMount& mount) {
    const auto pivot = add(add(mount.hull_position, mount.turret_position),
                           ry(mount.gun_position, pose.turret_yaw));
    return add(pose.position, transform(pivot, pose));
}

Point shot_direction(const AimPose& pose) {
    return transform(ry(rx({0., 0., 1.}, pose.gun_pitch), pose.turret_yaw), pose);
}

double world_barrel_yaw(const AimPose& pose) {
    const auto direction = shot_direction(pose);
    return std::atan2(direction[0], direction[2]);
}

std::optional<AimIntent> world_aim_intent(const AimPose& pose, const AimTarget& target,
                                          const std::optional<GunMount>& mount) {
    AimIntent out;
    if (!target.target_present && !target.solution_present) {
        out.world_yaw = pose.aim_yaw;
        return out;
    }
    if (!(target.solution_present && target.solution_origin_present) && !mount)
        return std::nullopt;
    const auto origin = target.solution_present && target.solution_origin_present
        ? target.solution_origin : shot_origin(pose, *mount);
    const double dx=target.aim_position[0]-origin[0];
    const double dz=target.aim_position[2]-origin[2];
    out.horizontal=std::sqrt(dx*dx+dz*dz);
    out.world_yaw=target.solution_present ? target.solution_yaw :
        (out.horizontal > .1 ? std::atan2(dx,dz) : pose.yaw);
    out.world_pitch=target.solution_present ? target.solution_pitch :
        -std::atan2(target.aim_position[1]+1.-origin[1], std::max(.5,out.horizontal));
    return out;
}

AimIntent local_aim_intent(const AimPose& pose, AimIntent intent) {
    const double c=std::cos(intent.world_pitch);
    Point v{std::sin(intent.world_yaw)*c, -std::sin(intent.world_pitch),
            std::cos(intent.world_yaw)*c};
    v=rz(rx(ry(v,-pose.yaw),-pose.pitch),-pose.roll);
    const double length=std::sqrt(v[0]*v[0]+v[1]*v[1]+v[2]*v[2]);
    if (!(length > 1.e-12) || !std::isfinite(length))
        throw std::invalid_argument("Invalid world aim direction");
    for (double& x : v) x /= length;
    intent.raw_yaw=std::atan2(v[0],v[2]);
    intent.raw_pitch=-std::atan2(v[1],std::max(1.e-12,std::sqrt(v[0]*v[0]+v[2]*v[2])));
    return intent;
}

double limited_traverse_target(double relative, double minimum,
                               double maximum, double rear_turn) {
    if (rear_turn != 0 && std::abs(relative) > pi*.5 &&
        !(minimum <= relative && relative <= maximum))
        return rear_turn > 0 ? maximum : minimum;
    return std::max(minimum,std::min(maximum,relative));
}

ContactVelocity partition_contact_velocity(ContactVelocity state, double yaw,
    bool alive, double dx, double dz) {
    if (dx != 0. || dz != 0.) state.contact_dynamics=true;
    const double sine=std::sin(yaw), cosine=std::cos(yaw);
    const double applied=alive ? dx*sine+dz*cosine : 0.;
    state.speed += applied;
    const double forced=state.forward+applied;
    state.forward=state.speed >= 0. ? std::max(0.,std::min(state.speed,forced))
                                   : std::min(0.,std::max(state.speed,forced));
    state.push_x += dx-applied*sine;
    state.push_z += dz-applied*cosine;
    return state;
}
} // namespace wot::offline_bot_step

// Typed copy of the fixed gameplay release_suspension numeric kernel.
namespace release_suspension {
using State = std::array<double,6>;
struct Point {
    double x,y,z,k=0.,damping=0.,compression=0.,maximum=0.,force=0.,penetration=0.,ground=0.;
    int side=0; bool spring=false,rigid=false,has_ground=false;
};
using Rotation = std::array<double,4>;
Rotation rotation(const State &s) {return {{std::sin(s[2]),std::cos(s[2]),std::sin(s[4]),std::cos(s[4])}};}
double height(const State &s,const Point &p,const Rotation &r) {return s[0]+(r[1]*(r[2]*p.x+r[3]*p.y)-r[0]*p.z);}
std::array<double,2> gradients(const Point &p,const Rotation &r) {return {{-r[0]*(r[2]*p.x+r[3]*p.y)-r[1]*p.z,r[1]*(r[3]*p.x-r[2]*p.y)}};}
// CPython's finite fsum partials, including its final half-even correction.
double sum(const std::vector<double> &values) {
    std::vector<double> partials;
    for(double x:values) {
        std::size_t i=0;
        for(double y:partials) {
            if(std::abs(x)<std::abs(y))std::swap(x,y);
            const double hi=x+y,lo=y-(hi-x);
            if(lo!=0.)partials[i++]=lo;
            x=hi;
        }
        partials.resize(i);partials.push_back(x);
    }
    double hi=0.,lo=0.;std::size_t n=partials.size();
    if(n) {
        hi=partials[--n];
        while(n) {const double x=hi,y=partials[--n];hi=x+y;lo=y-(hi-x);if(lo!=0.)break;}
        if(n&&((lo<0.&&partials[n-1]<0.)||(lo>0.&&partials[n-1]>0.))) {
            const double y=lo*2.,x=hi+y;if(y==x-hi)hi=x;
        }
    }
    return hi;
}
bool stable(const State &s,const std::vector<Point> &points,const std::vector<bool> &contact) {
    using XZ=std::array<double,2>;const auto r=rotation(s);std::vector<XZ> samples;
    for(std::size_t i=0;i<points.size();++i)if(contact[i]) {
        const auto &p=points[i];samples.push_back({{r[3]*p.x-r[2]*p.y,r[0]*(r[2]*p.x+r[3]*p.y)+r[1]*p.z}});
    }
    std::sort(samples.begin(),samples.end());samples.erase(std::unique(samples.begin(),samples.end()),samples.end());
    if(samples.size()<3)return false;
    auto cross=[](const XZ &a,const XZ &b,const XZ &c){return (b[0]-a[0])*(c[1]-a[1])-(b[1]-a[1])*(c[0]-a[0]);};
    std::vector<XZ> lower,upper;
    auto push=[&](std::vector<XZ> &chain,const XZ &p){while(chain.size()>=2&&cross(chain[chain.size()-2],chain.back(),p)<=0.)chain.pop_back();chain.push_back(p);};
    for(const auto &p:samples)push(lower,p);
    for(auto it=samples.rbegin();it!=samples.rend();++it)push(upper,*it);
    lower.pop_back();upper.pop_back();lower.insert(lower.end(),upper.begin(),upper.end());
    if(lower.size()<3)return false;
    const XZ origin={{0.,0.}};
    for(std::size_t i=0;i<lower.size();++i)if(cross(lower[(i+lower.size()-1)%lower.size()],lower[i],origin)<=1.e-10)return false;
    return true;
}
struct Result {State state;int contacts=0,rigid=0,touched=0;bool left=false,right=false,impact=false;double impact_speed=0.,compression=0.,excess=0.;};
Result solve(const std::array<double,5> &params,State s,const std::vector<Point> &points,double dt,double support,const std::array<double,9> &tuning,int max_steps) {
    const double im=1./params[0],ip=1./params[1],ir=1./params[2];
    const double total=std::max(0.,std::min(.2,dt));
    const int steps=static_cast<int>(std::min(double(max_steps),std::max(1.,std::ceil(total/params[3]))));
    const double step=total/steps;const int iterations=static_cast<int>(params[4]);
    Result out;std::vector<bool> touched(points.size(),false),contact(points.size(),false);bool transition=false;
    double va=0.,pa=0.,ra=0.;
    auto impact=[&](double speed){if(!out.impact){out.impact_speed=speed;out.impact=true;}else out.impact_speed=std::min(out.impact_speed,speed);};
    for(int sub=0;sub<steps;++sub) {
        auto r=rotation(s);const double force_offset=-support*std::max(0.,total-step*sub),projection_offset=-support*std::max(0.,total-step*(sub+1));
        double force=-params[0]*tuning[0],pitch_torque=0.,roll_torque=0.;int contacts=0;
        const double initial_speed=s[1],relative_speed=initial_speed-support;
        for(std::size_t i=0;i<points.size();++i) {
            const auto &p=points[i];if(!p.has_ground)continue;
            const double gap=p.ground+force_offset-height(s,p,r),compression=p.compression+gap;
            if(p.spring?compression<=0.:gap< -tuning[1])continue;
            touched[i]=true;++contacts;if(!transition&&relative_speed<0.)impact(initial_speed);
            if(!p.spring)continue;
            out.compression=std::max(out.compression,compression);const auto g=gradients(p,r);
            const double velocity=(s[1]+g[0]*s[3]+g[1]*s[5])-support;
            double f=p.k*compression-p.damping*velocity;
            if(compression>p.maximum){const double excess=compression-p.maximum;f+=p.k*8.*excess*(1.+excess/std::max(p.maximum,.01));}
            f=std::max(0.,std::min(p.force,f));force+=f;pitch_torque+=g[0]*f;roll_torque+=g[1]*f;
        }
        if(contacts)transition=true;
        if(!contacts){const double decay=std::exp(-tuning[2]*step);s[3]=std::max(-tuning[3],std::min(tuning[3],s[3]))*decay;s[5]=std::max(-tuning[3],std::min(tuning[3],s[5]))*decay;}
        va=force/params[0];pa=pitch_torque/params[1];ra=roll_torque/params[2];
        s[1]+=va*step;s[3]+=pa*step;s[5]+=ra*step;s[0]+=s[1]*step;s[2]+=s[3]*step;s[4]+=s[5]*step;
        const double before_v=s[1],before_p=s[3],before_r=s[5];bool projected=false;
        struct Row {double excess,pg,rg,denom;};
        for(int iteration=0;iteration<iterations;++iteration) {
            r=rotation(s);std::vector<Row> rows;double maximum=0.;
            for(std::size_t i=0;i<points.size();++i) {
                const auto &p=points[i];if(!p.has_ground)continue;
                const double excess=p.ground+projection_offset-height(s,p,r)-p.penetration;if(excess<=1.e-5)continue;
                touched[i]=true;projected=true;const auto g=gradients(p,r);const double pg=-g[0],rg=-g[1],denom=im+pg*pg*ip+rg*rg*ir;
                if(denom<=1.e-12)continue;
                rows.push_back({excess,pg,rg,denom});maximum=std::max(maximum,excess);
            }
            if(rows.empty())break;
            const double tolerance=std::max(1.e-10,maximum*1.e-8);std::array<std::vector<double>,6> changes;
            for(const auto &row:rows)if(maximum-row.excess<=tolerance) {
                const double velocity=(s[1]+ -row.pg*s[3]+ -row.rg*s[5])-support;
                const double impulse=velocity<0.? -velocity/row.denom:0.,position=row.excess/row.denom;
                changes[0].push_back(impulse*im);changes[1].push_back(-impulse*row.pg*ip);changes[2].push_back(-impulse*row.rg*ir);
                changes[3].push_back(position*im);changes[4].push_back(-position*row.pg*ip);changes[5].push_back(-position*row.rg*ir);
            }
            const double scale=1./changes[0].size();s[1]+=sum(changes[0])*scale;s[3]+=sum(changes[1])*scale;s[5]+=sum(changes[2])*scale;s[0]+=sum(changes[3])*scale;s[2]+=sum(changes[4])*scale;s[4]+=sum(changes[5])*scale;
        }
        r=rotation(s);double remaining=0.;
        for(std::size_t i=0;i<points.size();++i){const auto &p=points[i];if(!p.has_ground)continue;const double excess=p.ground+projection_offset-height(s,p,r)-p.penetration;if(excess>0.){remaining=std::max(remaining,excess);touched[i]=true;projected=true;}}
        if(remaining>0.)s[0]+=remaining+1.e-9;
        if(projected){if(step>0.){va+=(s[1]-before_v)/step;pa+=(s[3]-before_p)/step;ra+=(s[5]-before_r)/step;}if(!transition&&before_v-support<0.)impact(before_v);transition=true;}
    }
    const auto r=rotation(s);
    for(std::size_t i=0;i<points.size();++i) {
        const auto &p=points[i];if(!p.has_ground)continue;const double gap=p.ground-height(s,p,r);
        const bool active=p.spring?p.compression+gap>0.:gap>=-tuning[1];
        out.excess=std::max(out.excess,gap-p.penetration);
        if(active){contact[i]=touched[i]=true;++out.contacts;if(p.rigid)++out.rigid;if(p.side<0)out.left=true;else if(p.side>0)out.right=true;}
    }
    if(out.contacts&&!transition&&s[1]-support<0.)impact(s[1]);
    if(out.contacts&&total>0.&&stable(s,points,contact)) {
        if(std::abs(va)<tuning[4]&&std::abs(s[1]-support)<tuning[5])s[1]=support;
        if(std::abs(pa)<tuning[6]&&std::abs(s[3])<tuning[7])s[3]=0.;
        if(std::abs(ra)<tuning[6]&&std::abs(s[5])<tuning[7])s[5]=0.;
    }
    out.touched=static_cast<int>(std::count(touched.begin(),touched.end(),true));out.state=s;return out;
}
}

namespace wot::offline_bot_step {
RetainedGround retain_ground(double x, double z, std::optional<double> ground,
    std::optional<GroundMemory> memory, double distance,
    std::optional<std::array<double,2>> gradient) {
    if (!std::isfinite(x) || !std::isfinite(z) || !std::isfinite(distance)) return {};
    distance=std::max(0.,distance);
    if (ground) {
        if (!std::isfinite(*ground)) return {};
        return {*ground,GroundMemory{x,z,*ground,x,z,0.,false}};
    }
    if (!memory || !gradient) return {};
    auto m=*memory;
    for (double value : {m.origin_x,m.origin_z,m.height,m.last_x,m.last_z,m.travelled})
        if (!std::isfinite(value)) return {};
    m.travelled=std::max(0.,m.travelled);
    const double sx=x-m.last_x,sz=z-m.last_z,step=sx*sx+sz*sz;
    if (step<=1.e-10) {
        if (m.last_query_missed) return {};
        m.last_x=x; m.last_z=z; m.last_query_missed=true;
        return {m.height,m};
    }
    const double dx=x-m.origin_x,dz=z-m.origin_z;
    m.travelled+=std::sqrt(step);
    if (dx*dx+dz*dz>distance*distance+1.e-12 || m.travelled>distance+1.e-9 ||
        !std::isfinite((*gradient)[0]) || !std::isfinite((*gradient)[1])) return {};
    const double retained=m.height+(*gradient)[0]*dx+(*gradient)[1]*dz;
    m.last_x=x; m.last_z=z; m.last_query_missed=true;
    return {retained,m};
}
std::optional<std::vector<SupportCheckpoint>> support_checkpoints(
    const SupportPlane& old, const SupportPlane& now, const Point& start,
    const Point& end, double proposed_height) {
    const auto height=[](const SupportPlane& p,double x,double z) {
        return p.center_y+p.gradient_x*(x-p.center_x)+p.gradient_z*(z-p.center_z);
    };
    const double os=height(old,start[0],start[2]), oe=height(old,end[0],end[2]);
    const double ns=height(now,start[0],start[2]), ne=height(now,end[0],end[2]);
    const double gx=now.gradient_x-old.gradient_x, gz=now.gradient_z-old.gradient_z;
    if (!std::isfinite(os) || !std::isfinite(oe) || !std::isfinite(ns) ||
        !std::isfinite(ne) || !std::isfinite(proposed_height) ||
        std::abs(os-ns)>.35 || std::abs(oe-ne)>.35 ||
        gx*gx+gz*gz>.35*.35+1.e-12 ||
        proposed_height-end[1]>std::max(0.,ne-os)+.62)
        return std::nullopt;
    const double dx=end[0]-start[0], dz=end[2]-start[2];
    const double distance=std::hypot(dx,dz);
    if (!std::isfinite(distance)) return std::nullopt;
    const int count=std::min(3,std::max(0,static_cast<int>(std::ceil(std::min(distance,4.)))-1));
    std::vector<SupportCheckpoint> result;
    for (int i=0;i<count;++i) {
        const double fraction=double(i+1)/double(count+1);
        const double x=start[0]+dx*fraction,z=start[2]+dz*fraction;
        result.push_back({x,z,height(old,x,z)});
    }
    return result;
}
bool support_checkpoints_proved(const std::vector<SupportCheckpoint>& points,
                                const std::vector<std::optional<double>>& heights) {
    if (points.size()!=heights.size()) return false;
    for (std::size_t i=0;i<points.size();++i)
        if (!heights[i] || !std::isfinite(*heights[i]) ||
            std::abs(*heights[i]-points[i].expected_height)>.35) return false;
    return true;
}
PosedContacts suspension_pose_contacts(const std::vector<SuspensionContact>& pseudo,
    const std::vector<SuspensionContact>& rigid, const std::optional<Point>& origin,
    const std::vector<Point>& turret, double pitch, double roll,
    double pitch_velocity, double roll_velocity, double elapsed, double turret_yaw) {
    PosedContacts out;
    out.contacts=pseudo;
    out.sweep_pitch=pitch+pitch_velocity*std::max(0.,elapsed);
    out.sweep_roll=roll+roll_velocity*std::max(0.,elapsed);
    if (std::min(std::cos(pitch)*std::cos(roll),
                 std::cos(out.sweep_pitch)*std::cos(out.sweep_roll)) >= .9)
        return out;
    out.expanded=true;
    out.contacts.insert(out.contacts.end(),rigid.begin(),rigid.end());
    if (origin) {
        const double s=std::sin(turret_yaw), c=std::cos(turret_yaw);
        for (const auto& point : turret) {
            SuspensionContact contact;
            contact.x=(*origin)[0]+c*point[0]+s*point[2];
            contact.y=(*origin)[1]+point[1];
            contact.z=(*origin)[2]-s*point[0]+c*point[2];
            contact.penetration=.01;
            contact.rigid=true;
            out.contacts.push_back(contact);
        }
    }
    return out;
}
SuspensionSolveResult suspension_solve(const std::array<double, 5>& params,
    const std::array<double, 6>& state, const std::vector<SuspensionContact>& contacts,
    double elapsed, double support, const std::array<double, 9>& tuning, int max_steps) {
    if (!(params[0] > 0 && params[1] > 0 && params[2] > 0 && params[3] > 0) ||
        params[4] < 0 || params[4] > 256 || std::floor(params[4]) != params[4] ||
        max_steps < 1 || max_steps > 128)
        throw std::invalid_argument("Invalid suspension parameters");
    std::vector<release_suspension::Point> points;
    points.reserve(contacts.size());
    for (const auto& c : contacts) {
        release_suspension::Point p{};
        p.x=c.x; p.y=c.y; p.z=c.z; p.k=c.stiffness; p.damping=c.damping;
        p.compression=c.compression; p.maximum=c.maximum; p.force=c.force;
        p.penetration=c.spring ? c.maximum-c.compression : c.penetration;
        p.side=c.side; p.spring=c.spring; p.rigid=c.rigid;
        p.has_ground=bool(c.ground); p.ground=c.ground.value_or(0);
        points.push_back(p);
    }
    const auto result=release_suspension::solve(params,state,points,elapsed,support,tuning,max_steps);
    SuspensionSolveResult out;
    out.state=result.state; out.contacts=result.contacts; out.rigid=result.rigid;
    out.touched=result.touched; out.left=result.left; out.right=result.right;
    if (result.impact) out.impact_speed=result.impact_speed;
    out.compression=result.compression; out.excess=result.excess;
    return out;
}

bool prepare_vertical_spawn(VerticalState& state, std::optional<double> ground) {
    if (state.grounded_once || !ground) return false;
    if (!std::isfinite(*ground)) throw std::invalid_argument("Invalid spawn support");
    state.position[1]=*ground; state.vertical_speed=0.; state.terrain_pitch=0.;
    state.roll=0.; state.pitch_speed=state.roll_speed=state.support_vertical_speed=0.;
    state.support_gradient.reset(); state.ground_plane.reset();
    return true;
}

VerticalTrial vertical_trial(const VerticalState& state, Point tick,
    std::optional<Point> motion, std::optional<SupportPlane> plane,
    const std::vector<SuspensionContact>& contacts, const std::array<double,5>& params,
    const std::array<double,9>& tuning, int maximum_steps, double elapsed) {
    VerticalTrial trial;
    trial.original=state; trial.tick_position=tick; trial.motion_position=motion;
    trial.current_plane=plane; trial.contacts=contacts; trial.elapsed=elapsed;
    const bool no_ground=std::none_of(contacts.begin(),contacts.end(),
        [](const SuspensionContact& p) { return p.ground.has_value(); });
    if (!state.grounded_once && no_ground) { trial.pending_spawn=true; return trial; }
    auto gradient=state.support_gradient;
    if (state.grounded_once && state.ground_plane)
        gradient=std::array<double,2>{state.ground_plane->gradient_x,state.ground_plane->gradient_z};
    double height_delta=0.;
    if (elapsed<=0. && motion && gradient)
        height_delta=(*gradient)[0]*(state.position[0]-(*motion)[0])+
                     (*gradient)[1]*(state.position[2]-(*motion)[2]);
    double support=state.support_vertical_speed;
    if (elapsed>0.) {
        const double vx=motion ? (state.position[0]-(*motion)[0])/elapsed : 0.;
        const double vz=motion ? (state.position[2]-(*motion)[2])/elapsed : 0.;
        gradient.reset();
        if (plane) gradient=std::array<double,2>{plane->gradient_x,plane->gradient_z};
        else if (!state.airborne) gradient=state.support_gradient;
        support=gradient ? (*gradient)[0]*vx+(*gradient)[1]*vz : 0.;
    }
    trial.support_gradient=elapsed>0. ? gradient : state.support_gradient;
    trial.support_speed=support;
    trial.solved=suspension_solve(params,{state.position[1]+height_delta,state.vertical_speed,
        state.terrain_pitch,state.pitch_speed,state.roll,state.roll_speed},contacts,
        elapsed,support,tuning,maximum_steps);
    const auto& p=trial.solved.state;
    for (double x : p) if (!std::isfinite(x))
        throw std::runtime_error("Bot suspension produced a non-finite pose");
    trial.invalid=std::abs(p[0]-state.position[1])>5. ||
        std::abs(p[2]-state.terrain_pitch)>1.2 || std::abs(p[4]-state.roll)>1.2;
    trial.raised=state.grounded_once && trial.solved.contacts && !trial.solved.rigid &&
        p[0]-state.position[1]>.62;
    return trial;
}

VerticalOutcome commit_vertical(VerticalState& state, const VerticalTrial& trial,
                                bool continuous_support_proved) {
    VerticalOutcome out;
    const auto& before=trial.original;
    state=before;
    if (trial.pending_spawn) {
        state.vertical_speed=state.support_vertical_speed=0.; state.airborne=false;
        state.ground_plane.reset(); state.support_gradient.reset();
        out.pending_spawn=true; return out;
    }
    if (trial.invalid || (trial.raised && !continuous_support_proved)) {
        state.position=trial.tick_position;
        state.speed=state.turn_speed=state.push_x=state.push_z=state.vertical_speed=0.;
        state.pitch_speed=state.roll_speed=state.support_vertical_speed=0.;
        state.movement_direction=state.rotation_direction=0; state.airborne=false;
        state.support_gradient.reset(); out.blocked=true; out.clear_contact_memory=true;
        return out;
    }
    const auto& p=trial.solved.state;
    state.position[1]=p[0]; state.vertical_speed=p[1]; state.terrain_pitch=p[2];
    state.pitch_speed=p[3]; state.roll=p[4]; state.roll_speed=p[5];
    state.pitch=p[2]+state.hydraulic_pitch;
    state.airborne=trial.solved.contacts==0;
    state.left_flying=!trial.solved.left; state.right_flying=!trial.solved.right;
    state.support_vertical_speed=trial.support_speed;
    state.support_gradient=trial.support_gradient;
    if (trial.solved.contacts) {
        state.grounded_once=true;
        double sum=0.; std::size_t count=0;
        for (const auto& c : trial.contacts) if (c.ground) { sum+=*c.ground; ++count; }
        if (count) state.ground_height=sum/count;
    }
    if (state.airborne) {
        state.support_vertical_speed=0.; state.support_gradient.reset();
        out.clear_contact_memory=true;
    }
    state.ground_plane=trial.solved.contacts && !state.airborne ? trial.current_plane : std::nullopt;
    if (before.airborne && !state.airborne && before.vertical_speed<0.) {
        const double vy=trial.solved.impact_speed.value_or(before.vertical_speed);
        double impact=std::max(0.,-vy);
        if (trial.motion_position && trial.elapsed>0. && trial.current_plane) {
            const auto& n=trial.current_plane->normal;
            const double scale=std::max({std::abs(n[0]),std::abs(n[1]),std::abs(n[2])});
            if (std::isfinite(scale) && scale>0. && n[1]>0.) {
                const double nx=n[0]/scale, ny=n[1]/scale, nz=n[2]/scale;
                const double length=std::sqrt(nx*nx+ny*ny+nz*nz);
                const double vx=(before.position[0]-(*trial.motion_position)[0])/trial.elapsed;
                const double vz=(before.position[2]-(*trial.motion_position)[2])/trial.elapsed;
                const double closing=-(vx*nx+vy*ny+vz*nz)/length;
                if (std::isfinite(closing)) impact=std::max(0.,closing);
            }
        }
        out.landing_impact=impact;
        out.normal_impact=true;
    } else if (!before.airborne && state.airborne) {
        state.turn_speed=0.; state.rotation_direction=0;
    }
    return out;
}

static VerticalOutcome advance_detailed_vertical(VerticalState& state, VerticalMemory& memory,
    const SuspensionDescriptor& descriptor, VerticalWorld& world, double yaw,
    double turret_yaw, Point tick, std::optional<Point> motion, double elapsed) {
    const auto before=state;
    if (!motion) motion=tick;
    if (elapsed>0.) {
        double vx=state.air_lateral_x,vz=state.air_lateral_z;
        if (state.airborne || !state.ground_plane) state.slide_speed=0.;
        else {
            const double gx=state.ground_plane->gradient_x,gz=state.ground_plane->gradient_z;
            const double tangent=std::hypot(gx,gz),theta=std::atan(tangent);
            const double ny=std::max(0.,std::min(1.,std::cos(theta)));
            const double full=std::cos(24.5*pi/180.), minimum=std::cos(29.*pi/180.);
            const double grip=ny>=full ? 1. : ny<=minimum ? .1 : .1+.9*(ny-minimum)/(full-minimum);
            const double hold=descriptor.slide_hold_tan*grip;
            if (tangent<=hold) state.slide_speed=std::max(0.,state.slide_speed-descriptor.cohesion*descriptor.tuning[0]*elapsed);
            else state.slide_speed=std::max(0.,std::min(descriptor.slide_max,state.slide_speed+
                (descriptor.tuning[0]*(std::sin(theta)-hold*std::cos(theta))-
                 descriptor.slide_drag*state.slide_speed)*elapsed));
            double dx=tangent>.001 ? -gx/tangent : 0., dz=tangent>.001 ? -gz/tangent : 0.;
            if (state.alive) { const double rx=std::cos(yaw),rz=-std::sin(yaw),projection=dx*rx+dz*rz;
                dx=rx*projection; dz=rz*projection; }
            vx=dx*state.slide_speed; vz=dz*state.slide_speed;
        }
        state.air_lateral_x=vx; state.air_lateral_z=vz;
        if (std::hypot(vx,vz)>.0001) {
            const auto accepted=world.passive_move(state,vx*elapsed,vz*elapsed,elapsed);
            if (std::abs(accepted[0]-vx*elapsed)>1.e-8 || std::abs(accepted[1]-vz*elapsed)>1.e-8)
                state.slide_speed=state.air_lateral_x=state.air_lateral_z=0.;
        }
    }
    const auto old_plane=state.grounded_once ? state.ground_plane : std::nullopt;
    auto gradient=state.support_gradient;
    if (old_plane) gradient=std::array<double,2>{old_plane->gradient_x,old_plane->gradient_z};
    if (!state.grounded_once && prepare_vertical_spawn(state,
            world.initial_ground(state.position[0],state.position[2],state.position[1])))
        memory={};
    const auto height_at=[](const SupportPlane& p,double x,double z) {
        return p.center_y+p.gradient_x*(x-p.center_x)+p.gradient_z*(z-p.center_z);
    };
    double probe_height=state.position[1];
    if (motion && old_plane)
        probe_height+=std::max(0.,height_at(*old_plane,state.position[0],state.position[2])-
                                  height_at(*old_plane,(*motion)[0],(*motion)[2]));
    const double sweep=std::max(0.,-state.vertical_speed*std::max(0.,elapsed)+
        descriptor.tuning[0]*std::max(0.,elapsed)*std::max(0.,elapsed));
    const auto posed=suspension_pose_contacts(descriptor.pseudo,descriptor.rigid,
        descriptor.turret_origin,descriptor.turret_contacts,state.terrain_pitch,state.roll,
        state.pitch_speed,state.roll_speed,elapsed,turret_yaw);
    std::vector<SuspensionContact> contacts=descriptor.springs;
    contacts.insert(contacts.end(),posed.contacts.begin(),posed.contacts.end());
    if (memory.springs.size()!=descriptor.springs.size()) memory.springs.assign(descriptor.springs.size(),std::nullopt);
    if (memory.pseudo.size()!=posed.contacts.size()) memory.pseudo.assign(posed.contacts.size(),std::nullopt);
    const auto offset=[&](const SuspensionContact& c,double pitch,double roll) {
        return rx(rz({c.x,c.y,c.z},roll),pitch);
    };
    const auto world_xz=[&](const Point& p) {
        return std::array<double,2>{state.position[0]+std::cos(yaw)*p[0]+std::sin(yaw)*p[2],
            state.position[2]-std::sin(yaw)*p[0]+std::cos(yaw)*p[2]};
    };
    std::optional<double> flat;
    if (!state.airborne && !descriptor.springs.empty()) {
        for (const auto& s : descriptor.springs) {
            const double v=probe_height+offset(s,state.terrain_pitch,state.roll)[1]+
                s.maximum-s.compression+.05;
            flat=flat ? std::max(*flat,v) : v;
        }
    }
    const bool inverted=std::cos(state.terrain_pitch)*std::cos(state.roll)<=.1;
    if (inverted) memory.springs.assign(descriptor.springs.size(),std::nullopt);
    for (std::size_t i=0;i<contacts.size();++i) {
        auto& c=contacts[i];
        const bool spring=i<descriptor.springs.size();
        c.spring=spring;
        if (spring && inverted) { c.ground.reset(); continue; }
        const auto local=offset(c,state.terrain_pitch,state.roll);
        const auto point=world_xz(local);
        const double ph=probe_height+local[1];
        double low=ph-(spring?c.rest_length:descriptor.rest_length)-.1-sweep;
        double high=0.; std::optional<double> flat_high;
        std::optional<double> reference;
        if (!state.airborne && old_plane) reference=height_at(*old_plane,point[0],point[1]);
        auto& saved=spring ? memory.springs[i] : memory.pseudo[i-descriptor.springs.size()];
        if (spring) {
            const double sh=ph+c.maximum-c.compression+.05;
            high=std::max(sh,ph+descriptor.clearance+.1);
            flat_high=flat ? std::max(sh,*flat) : sh;
            if (flat && reference) high=std::max(high,std::min(*flat,*reference+.1));
        } else {
            high=ph+(c.track?descriptor.clearance:0.)+.1;
            if (c.rigid) {
                const double future=probe_height+offset(c,posed.sweep_pitch,posed.sweep_roll)[1];
                low=std::min(low,future-sweep-.1); high=std::max(high,probe_height+.1);
                if (old_plane) high=std::max(high,height_at(*old_plane,point[0],point[1])+.1);
            }
            if (c.track) flat_high=ph+.1;
        }
        c.ground=world.ground(point[0],point[1],low,high,flat_high);
        if (spring) {
            std::optional<double> expected;
            if (saved) {
                const double dx=point[0]-saved->origin_x,dz=point[1]-saved->origin_z;
                if (dx*dx+dz*dz<=descriptor.contact_memory_distance*descriptor.contact_memory_distance) {
                    expected=saved->height;
                    if (gradient) *expected+=(*gradient)[0]*dx+(*gradient)[1]*dz;
                }
            }
            if (reference) expected=std::max(*reference-.12,expected.value_or(*reference));
            if (expected && (!c.ground || *c.ground<*expected-.12)) {
                const double width=descriptor.footprint_half_width;
                const double sides[]={0.,-width,width,-width*.5,width*.5};
                const double forwards[]={0.,-c.footprint_rear,c.footprint_front,-c.footprint_rear*.5,c.footprint_front*.5};
                const double rise=std::min(.35,descriptor.clearance);
                const double ceiling=std::min(*expected+rise,ph+rise);
                bool found=false;
                for (double side : sides) {
                    side=std::max(-descriptor.width*.5-c.x,std::min(descriptor.width*.5-c.x,side));
                    for (double forward : forwards) {
                        const auto o=rx(rz({side,0.,forward},state.roll),state.terrain_pitch);
                        const double px=point[0]+std::cos(yaw)*o[0]+std::sin(yaw)*o[2];
                        const double pz=point[1]-std::sin(yaw)*o[0]+std::cos(yaw)*o[2];
                        const double delta=gradient ? (*gradient)[0]*(px-point[0])+(*gradient)[1]*(pz-point[1]) : 0.;
                        const double lo=*expected-.12+delta,hi=std::min(ceiling+delta,ph+o[1]+rise);
                        if (hi<lo) continue;
                        const auto hit=world.ground(px,pz,lo,hi,hi);
                        if (hit && *hit>=lo-.01 && *hit<=hi+.01) { c.ground=*hit-delta; found=true; break; }
                    }
                    if (found) break;
                }
            }
        }
        if (c.rigid) saved.reset();
        else { const auto retained=retain_ground(point[0],point[1],c.ground,saved,
            descriptor.contact_memory_distance,gradient); c.ground=retained.height; saved=retained.memory; }
    }
    // Fit the source's spring-only least-squares plane in the pitched body frame.
    std::vector<std::array<double,3>> rows;
    for (std::size_t i=0;i<descriptor.springs.size();++i) if (contacts[i].ground) {
        const auto p=offset(contacts[i],state.terrain_pitch,state.roll);
        rows.push_back({p[0],p[2],*contacts[i].ground});
    }
    std::optional<SupportPlane> plane;
    if (rows.size()>=3) {
        double mx=0,mz=0,my=0,xx=0,xz=0,zz=0,xy=0,zy=0;
        for (const auto& r:rows) {mx+=r[0];mz+=r[1];my+=r[2];}
        mx/=rows.size();mz/=rows.size();my/=rows.size();
        for (const auto& r:rows) {const double x=r[0]-mx,z=r[1]-mz,y=r[2]-my;
            xx+=x*x;xz+=x*z;zz+=z*z;xy+=x*y;zy+=z*y;}
        const double det=xx*zz-xz*xz;
        if (det>1.e-8) {
            const double gx=(xy*zz-zy*xz)/det,gz=(zy*xx-xy*xz)/det,cy=my-gx*mx-gz*mz;
            double residual=0.; for (const auto& r:rows) residual=std::max(residual,std::abs(r[2]-(cy+gx*r[0]+gz*r[1])));
            if (residual<=.35) {
                SupportPlane p; p.center_x=state.position[0];p.center_z=state.position[2];p.center_y=cy;
                p.gradient_x=gz*std::sin(yaw)+gx*std::cos(yaw);
                p.gradient_z=gz*std::cos(yaw)-gx*std::sin(yaw);
                const double scale=std::max({1.,std::abs(p.gradient_x),std::abs(p.gradient_z)});
                const double nx=-p.gradient_x/scale,ny=1./scale,nz=-p.gradient_z/scale;
                const double length=std::sqrt(nx*nx+ny*ny+nz*nz);
                p.normal={nx/length,ny/length,nz/length}; plane=p;
            }
        }
    }
    auto trial=vertical_trial(state,tick,motion,plane,contacts,descriptor.parameters,
                              descriptor.tuning,descriptor.maximum_steps,elapsed);
    bool continuous=false;
    if (trial.raised && old_plane && plane && motion) {
        const auto checks=support_checkpoints(*old_plane,*plane,*motion,state.position,trial.solved.state[0]);
        if (checks) {
            std::vector<std::optional<double>> heights;
            for (const auto& p:*checks) heights.push_back(world.ground(p.x,p.z,p.expected_height-.35,
                p.expected_height+.35,p.expected_height+.35));
            continuous=support_checkpoints_proved(*checks,heights);
        }
    }
    auto result=commit_vertical(state,trial,continuous);
    if (result.blocked) {
        const auto restored_plane=before.ground_plane;
        state=before; state.position=tick; state.speed=state.turn_speed=state.push_x=state.push_z=state.vertical_speed=0.;
        state.pitch_speed=state.roll_speed=state.support_vertical_speed=0.;
        state.movement_direction=state.rotation_direction=0; state.airborne=false;
        state.support_gradient.reset(); state.ground_plane=restored_plane;
    }
    if (result.landing_impact) {
        if (plane) result.landing_normal=plane->normal;
        std::vector<std::pair<double,Point>> rows;
        for (const auto& c : contacts) if (c.ground) {
            const auto local=offset(c,state.pitch,state.roll);
            const auto xz=world_xz(local);
            rows.push_back({state.position[1]+local[1]-*c.ground,{xz[0],*c.ground,xz[1]}});
        }
        if (!rows.empty()) {
            const double deepest=std::min_element(rows.begin(),rows.end(),
                [](const auto& a,const auto& b){return a.first<b.first;})->first;
            std::vector<Point> points;
            for (const auto& row:rows) if (row.first<=deepest+.05) points.push_back(row.second);
            std::sort(points.begin(),points.end()); points.erase(std::unique(points.begin(),points.end()),points.end());
            while (!points.empty() && result.landing_witnesses.size()<8) {
                std::size_t selected=0;
                if (!result.landing_witnesses.empty()) {
                    double best=-1.;
                    for (std::size_t i=0;i<points.size();++i) {
                        double nearest=std::numeric_limits<double>::infinity();
                        for (const auto& q:result.landing_witnesses) {
                            const double x=points[i][0]-q[0],z=points[i][2]-q[2];
                            nearest=std::min(nearest,x*x+z*z);
                        }
                        if (nearest>best) {best=nearest;selected=i;}
                    }
                }
                result.landing_witnesses.push_back(points[selected]); points.erase(points.begin()+selected);
            }
        }
    }
    if (result.clear_contact_memory) memory={};
    return result;
}

VerticalOutcome advance_vertical(VerticalState& state, VerticalMemory& memory,
    const SuspensionDescriptor& descriptor, VerticalWorld& world, double yaw,
    double turret_yaw, Point tick, std::optional<Point> motion, double elapsed) {
    const auto before=state;
    const auto saved_memory=memory;
    VerticalOutcome result;
    try {
        result=advance_detailed_vertical(state,memory,descriptor,world,yaw,turret_yaw,tick,motion,elapsed);
    } catch (const std::logic_error&) {
        // A failed trial is retired after restoring the original pose; do not
        // run a second vertical authority on the same slice.
        result.suspension_retired=true;
    } catch (const std::runtime_error&) { result.suspension_retired=true; }
    if (result.suspension_retired) {
        state=before; state.position=tick; state.suspension_disabled=true;
        state.speed=state.turn_speed=state.push_x=state.push_z=0.;
        state.pitch_speed=state.roll_speed=state.support_vertical_speed=0.;
        state.movement_direction=state.rotation_direction=0;
        state.support_gradient.reset(); state.ground_plane.reset(); memory={};
        result.blocked=true; result.clear_contact_memory=true;
    }
    if (!world.turret_clear(before,state)) {
        const bool retired=result.suspension_retired;
        state=before; state.suspension_disabled=true;
        if (!retired) state.suspension_disabled=before.suspension_disabled;
        state.speed=state.turn_speed=state.push_x=state.push_z=0.;
        state.movement_direction=state.rotation_direction=0;
        memory=saved_memory; result={}; result.blocked=true; result.suspension_retired=retired;
    }
    if (result.blocked) {result.invalidate_motion=true;result.invalidated_yaw=yaw;}
    return result;
}

VerticalOutcome advance_basic_vertical(VerticalState& state, const SuspensionDescriptor& descriptor,
    VerticalWorld& world, Point tick, double elapsed, int& budget) {
    const auto before=state;
    VerticalOutcome out;
    state.basic_continuous_support=false;
    const double step=std::max(0.,elapsed), gravity=descriptor.tuning[0];
    const double pitch=std::max(-.96,std::min(.96,state.terrain_pitch));
    const double support_speed=-state.speed*std::tan(pitch);
    if (state.grounded_once && !state.airborne) state.vertical_speed=support_speed;
    const double drive_pitch=std::max(-.96,std::min(.96,state.last_drive_pitch));
    const double follow=std::max(.8,std::min(2.5,.6+
        std::max(0.,state.speed*std::tan(drive_pitch)*step)+gravity*step*step));
    const double fall_origin=state.position[1];
    const bool was_airborne=state.airborne;
    const auto previous_fall=state.basic_fall_origin;
    bool continuous=false;
    const double sine=std::sin(state.yaw),cosine=std::cos(state.yaw);
    const double length=std::max(1.5,descriptor.half_length),width=std::max(.3,descriptor.half_width);
    const std::array<std::array<double,2>,4> offsets{{
        {sine*length,cosine*length},{-sine*length,-cosine*length},
        {cosine*width,-sine*width},{-cosine*width,sine*width}}};
    const double dx=state.position[0]-tick[0],dz=state.position[2]-tick[2];
    const double travel=std::hypot(dx,dz);
    const auto column=[&](double x,double z,double hint,std::optional<double> ceiling) {
        auto hit=world.basic_ground(x,z,hint,ceiling);
        if (hit && ceiling && *hit>*ceiling) hit.reset();
        return hit;
    };
    const auto path=[&](double centre,bool descending) {
        const double rise=centre-tick[1];
        if (travel<=.0001 || (rise<=0. && !descending) || std::abs(rise)>travel*.55+.02) return false;
        const double raw=std::ceil(travel/1.5);
        if (raw>64.) return false;
        const int segments=std::max(2,static_cast<int>(raw));
        const double limit=travel/segments*.55+.02;
        double previous=tick[1];
        for (int i=1;i<segments;++i) {
            const double f=double(i)/segments;
            const auto h=column(tick[0]+dx*f,tick[2]+dz*f,tick[1]+rise*f,std::nullopt);
            if (!h || std::abs(*h-previous)>limit) return false;
            previous=*h;
        }
        return std::abs(centre-previous)<=limit;
    };
    std::optional<double> ceiling;
    if (state.airborne) ceiling=std::max(state.position[1],state.basic_fall_origin.value_or(state.position[1]))+.1;
    const auto straddled=[&](double gap) -> std::optional<double> {
        std::optional<double> support;
        for (int pair=0;pair<2;++pair) {
            const auto& a=offsets[2*pair];const auto& b=offsets[2*pair+1];
            const double hint=ceiling.value_or(state.position[1]);
            const auto h1=column(state.position[0]+a[0],state.position[2]+a[1],hint,ceiling);
            const auto h2=column(state.position[0]+b[0],state.position[2]+b[1],hint,ceiling);
            if (!h1 || !h2 || *h1-state.position[1]<-gap || *h2-state.position[1]<-gap ||
                *h1-state.position[1]>.12 || *h2-state.position[1]>.12) continue;
            const double midpoint=(*h1+*h2)*.5;
            support=support ? std::max(*support,midpoint) : midpoint;
        }
        return support;
    };
    auto centre=column(state.position[0],state.position[2],ceiling.value_or(state.position[1]),ceiling);
    std::optional<double> highest;
    if (ceiling && (!centre || state.position[1]-*centre>2.5) && travel>.0001) {
        const double path_ceiling=tick[1]+travel*.55+.02;
        const auto candidate=column(state.position[0],state.position[2],std::max(state.position[1],tick[1]),path_ceiling);
        if (candidate && *candidate>*ceiling && path(*candidate,false)) { centre=candidate; continuous=true; }
    }
    if (centre) {
        double ref=state.position[1];
        bool bridged=false;
        if (state.grounded_once && !state.airborne) ref=std::max(ref,state.straddle_reference.value_or(ref));
        state.straddle_reference=ref;
        if (ref-*centre>std::min(std::min(follow,.8),.01)) {
            const auto bridge=straddled(std::min(follow,.8));
            if (bridge && *bridge>*centre+1.e-6) {centre=bridge;bridged=true;}
            else state.straddle_reference=state.position[1];
        }
        if (!bridged && state.grounded_once && (state.airborne || *centre<tick[1]-1.e-6) && path(*centre,true)) continuous=true;
        highest=centre;
    } else if (!state.grounded_once) {
        const auto front=column(state.position[0]+offsets[0][0],state.position[2]+offsets[0][1],state.position[1],std::nullopt);
        const auto rear=column(state.position[0]+offsets[1][0],state.position[2]+offsets[1][1],state.position[1],std::nullopt);
        if (front && rear) highest=std::max(*front,*rear); else highest=front ? front : rear;
    } else highest=straddled(.8);
    state.basic_continuous_support=continuous;
    const auto ground=centre ? centre : highest;
    if (ground) {
        double max_climb=std::max(.6,std::abs(state.speed)*step*2.5);
        const bool crossed=was_airborne && previous_fall && state.vertical_speed<=0. &&
            state.position[1]<=*ground && *ground<=*previous_fall+.1;
        const bool obstacle=state.grounded_once && !crossed && !continuous && centre &&
            *centre-state.position[1]>std::min(max_climb,.85)+.02;
        bool proved=false;
        if (obstacle) {
            proved=path(*centre,false);
            if (!proved) {
                state.support_repair_elapsed+=step;
                const double rise=*centre-state.position[1];
                if (state.support_repair_elapsed>=1. && budget>0 && !state.airborne &&
                    travel<=.35 && rise>0. && rise<=std::min(length,width)*.55+.02) {
                    budget=0; state.support_repair_elapsed=0.;
                    std::array<double,4> values{}; bool valid=true;
                    for (std::size_t i=0;i<offsets.size();++i) {
                        const auto h=column(state.position[0]+offsets[i][0],state.position[2]+offsets[i][1],*centre,std::nullopt);
                        if (!h || std::abs(*h-*centre)>std::hypot(offsets[i][0],offsets[i][1])*.55+.02) {valid=false;break;}
                        values[i]=*h;
                    }
                    if (valid && *std::min_element(values.begin(),values.end())<=state.position[1]+.02 &&
                        std::abs((values[0]+values[1])*.5-*centre)<=.1 &&
                        std::abs((values[2]+values[3])*.5-*centre)<=.1) {
                        const double fg=(values[0]-values[1])/(2.*length),lg=(values[2]-values[3])/(2.*width);
                        double p=-std::atan(fg),r=std::atan2(lg,std::sqrt(1.+fg*fg));
                        const double tilt=std::hypot(p,r); if (tilt>.61) {p*=.61/tilt;r*=.61/tilt;}
                        state.slope_target=std::array<double,2>{p,r}; out.pose_sample_yaw=state.yaw; proved=true;
                    }
                }
            }
            if (proved) max_climb=std::max(max_climb,std::max(0.,*centre-tick[1]));
        } else state.support_repair_elapsed=0.;
        const double land=centre.value_or(*ground);
        if (!state.grounded_once) { state.position[1]=land; state.vertical_speed=0.; state.airborne=false;state.grounded_once=true; }
        else if (obstacle && !proved) {
            state.position=tick;state.speed=state.turn_speed=state.push_x=state.push_z=state.vertical_speed=0.;
            state.movement_direction=state.rotation_direction=0;state.airborne=false;out.blocked=true;out.invalidate_motion=true;out.invalidated_yaw=state.yaw;
            if (!world.turret_clear(before,state)) {
                state=before;state.speed=state.turn_speed=state.push_x=state.push_z=0.;
                state.movement_direction=state.rotation_direction=0;
            }
            return out;
        } else if (continuous || state.position[1]<*ground-.002 ||
                   (state.position[1]<=*ground && state.vertical_speed<=0.) ||
                   (!state.airborne && *ground>=state.position[1]+state.vertical_speed*step-gravity*step*step-.002)) {
            const double impact=state.airborne && !continuous ? state.vertical_speed : 0.;
            if (crossed || continuous) state.position[1]=land;
            else if (state.position[1]<*ground) state.position[1]+=std::min(*ground-state.position[1],max_climb);
            else state.position[1]=*ground;
            state.vertical_speed=state.airborne ? 0. : support_speed;state.airborne=false;
            if (was_airborne && continuous) out.clear_pose_sample=true;
            if (impact<0.) { out.landing_impact=impact; out.ground_observed=true; }
        } else {
            state.airborne=true;
            const int substeps=std::min(8,std::max(1,static_cast<int>(std::abs(state.vertical_speed*step)/.5)+1));
            const double dt=step/substeps;
            for (int i=0;i<substeps;++i) {
                state.vertical_speed-=gravity*dt;state.position[1]+=state.vertical_speed*dt;
                if (state.position[1]<=land) { out.landing_impact=state.vertical_speed;out.ground_observed=true;state.position[1]=land;
                    state.vertical_speed=0.;state.airborne=false;break; }
            }
        }
    } else if (state.grounded_once) {
        state.airborne=true;state.vertical_speed-=gravity*step;state.position[1]+=state.vertical_speed*step;
    } else { state.vertical_speed=0.;state.airborne=false; }
    if (state.airborne) state.basic_fall_origin=fall_origin; else state.basic_fall_origin.reset();
    if (!state.airborne && state.grounded_once && state.slope_basis_yaw) {
        const double delta=std::atan2(std::sin(state.yaw-*state.slope_basis_yaw),std::cos(state.yaw-*state.slope_basis_yaw));
        if (std::abs(delta)>=1.e-9) {
            const auto orient=[&](double p,double r) {
                const auto n=rx(rz({0.,1.,0.},r),p);
                const double x=std::cos(delta)*n[0]-std::sin(delta)*n[2];
                const double z=std::sin(delta)*n[0]+std::cos(delta)*n[2];
                return std::array<double,2>{std::atan2(z,n[1]),std::atan2(-x,std::hypot(n[1],z))};
            };
            auto previous=state;previous.yaw=*state.slope_basis_yaw;
            auto proposed=state; const auto pr=orient(state.terrain_pitch,state.roll);
            proposed.terrain_pitch=pr[0];proposed.pitch=pr[0]+state.hydraulic_pitch;proposed.roll=pr[1];
            if (!world.turret_clear(previous,proposed)) {
                out.invalidated_yaw=state.yaw;state.yaw=*state.slope_basis_yaw;state.rotation_direction=0;state.turn_speed=0.;out.invalidate_motion=true;
            } else {
                state=proposed; if (state.slope_target) state.slope_target=orient((*state.slope_target)[0],(*state.slope_target)[1]);
                out.pose_sample_yaw=state.yaw;state.vertical_speed=-state.speed*std::tan(std::max(-.96,std::min(.96,pr[0])));
                state.slope_basis_yaw=state.yaw;
            }
        }
    }
    if (!world.turret_clear(before,state)) {
        state=before;state.speed=state.turn_speed=state.push_x=state.push_z=0.;
        state.movement_direction=state.rotation_direction=0;out={};out.blocked=true;out.invalidate_motion=true;out.invalidated_yaw=before.yaw;
    }
    return out;
}
} // namespace wot::offline_bot_step
