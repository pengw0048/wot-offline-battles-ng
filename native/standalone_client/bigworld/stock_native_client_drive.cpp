#include "stock_native_client_drive.hpp"
#include <algorithm>
#include <cmath>
#include <limits>
#include <stdexcept>

namespace stock_native_client_drive {
namespace {
Point rz(Point p,double a){const double s=std::sin(a),c=std::cos(a);return {c*p[0]-s*p[1],s*p[0]+c*p[1],p[2]};}
Point rx(Point p,double a){const double s=std::sin(a),c=std::cos(a);return {p[0],c*p[1]-s*p[2],s*p[1]+c*p[2]};}
void witnesses(Output& out,const std::vector<step::SuspensionContact>& contacts) {
    const auto& state=out.state;std::vector<std::pair<double,Point>> rows;
    for(const auto& c:contacts)if(c.ground){const auto p=rx(rz({c.x,c.y,c.z},state.roll),state.pitch);
        const Point q{state.position[0]+std::cos(state.yaw)*p[0]+std::sin(state.yaw)*p[2],*c.ground,
            state.position[2]-std::sin(state.yaw)*p[0]+std::cos(state.yaw)*p[2]};
        rows.push_back({state.position[1]+p[1]-*c.ground,q});}
    if(rows.empty())return;
    const double deepest=std::min_element(rows.begin(),rows.end(),[](const auto&a,const auto&b){return a.first<b.first;})->first;
    std::vector<Point> points;for(const auto& row:rows)if(row.first<=deepest+.05)points.push_back(row.second);
    std::sort(points.begin(),points.end());points.erase(std::unique(points.begin(),points.end()),points.end());
    while(!points.empty()&&out.outcome.landing_witnesses.size()<8){std::size_t chosen=0;
        if(!out.outcome.landing_witnesses.empty()){double best=-1.;for(std::size_t i=0;i<points.size();++i){double near=std::numeric_limits<double>::infinity();
            for(const auto& q:out.outcome.landing_witnesses){const double x=points[i][0]-q[0],z=points[i][2]-q[2];near=std::min(near,x*x+z*z);}
            if(near>best){best=near;chosen=i;}}}
        out.outcome.landing_witnesses.push_back(points[chosen]);points.erase(points.begin()+chosen);}
}
}
Owner::Owner(step::SuspensionDescriptor descriptor):descriptor_(std::move(descriptor)) {
    for(const auto v:descriptor_.parameters)if(!std::isfinite(v))throw std::invalid_argument("Nonfinite player suspension descriptor");
    if(descriptor_.parameters[0]<=0.||descriptor_.parameters[1]<=0.||descriptor_.parameters[2]<=0.||
       descriptor_.parameters[3]<=0.||descriptor_.springs.empty())throw std::invalid_argument("Invalid player suspension descriptor");
}
std::array<Point,2> Owner::region(const Input& input)const {
    const auto norm=[](Point p){return std::sqrt(p[0]*p[0]+p[1]*p[1]+p[2]*p[2]);};
    double radius=0.;
    const auto contacts=[&](const auto&rows,bool footprint){for(const auto&c:rows){const double extension=footprint?
        std::hypot(descriptor_.footprint_half_width,std::max(c.footprint_front,c.footprint_rear)):0.;radius=std::max(radius,norm({c.x,c.y,c.z})+extension);}};
    contacts(descriptor_.springs,true);contacts(descriptor_.pseudo,false);contacts(descriptor_.rigid,false);
    if(descriptor_.turret_origin)for(const auto&p:descriptor_.turret_contacts)radius=std::max(radius,norm(*descriptor_.turret_origin)+norm(p));
    Point lower=input.state.position,upper=lower;
    if(input.motion_position)for(unsigned i:{0u,2u}){lower[i]=std::min(lower[i],(*input.motion_position)[i]);upper[i]=std::max(upper[i],(*input.motion_position)[i]);}
    for(unsigned i:{0u,2u}){lower[i]-=radius;upper[i]+=radius;}return {lower,upper};
}
Output Owner::advance(const Input& input,Ground& world) {
    using namespace step;
    if(!std::isfinite(input.elapsed)||input.elapsed<0.||input.elapsed>.1||
       !std::isfinite(input.height_delta)||!std::isfinite(input.speed_delta))throw std::invalid_argument("Invalid player suspension step");
    ++statistics_.steps;
    auto state=input.state;const auto& descriptor=descriptor_;auto& memory=memory_;
    const double elapsed=input.elapsed,yaw=state.yaw,turret_yaw=input.turret_yaw;
    const auto motion=input.motion_position;const bool armed_before=state.grounded_once;
    const auto column=[&](double x,double z,double low,double high,std::optional<double>flat){++statistics_.columns;return world.column(x,z,low,high,flat);};
    const auto old_plane=state.ground_plane;
    auto gradient=state.support_gradient;
    if (old_plane) gradient=std::array<double,2>{old_plane->gradient_x,old_plane->gradient_z};
    if (!state.grounded_once && prepare_vertical_spawn(state,
            world.initial(state.position[0],state.position[2],state.position[1])))
        state.pitch=state.terrain_pitch;
    const auto height_at=[](const SupportPlane& p,double x,double z) {
        return p.center_y+p.gradient_x*(x-p.center_x)+p.gradient_z*(z-p.center_z);
    };
    double probe_height=state.position[1];
    if (motion && old_plane)
        probe_height+=std::max(0.,height_at(*old_plane,state.position[0],state.position[2])-
                                  height_at(*old_plane,(*motion)[0],(*motion)[2]));
    const double sweep=std::max(0.,-(state.vertical_speed+input.speed_delta)*std::max(0.,elapsed)+
        descriptor.tuning[0]*std::max(0.,elapsed)*std::max(0.,elapsed));
    const auto posed=suspension_pose_contacts(descriptor.pseudo,descriptor.rigid,
        descriptor.turret_origin,descriptor.turret_contacts,state.pitch,state.roll,
        state.pitch_speed,state.roll_speed,elapsed,turret_yaw);
    std::vector<Probe> probes;
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
            const double v=probe_height+offset(s,state.pitch,state.roll)[1]+
                s.maximum-s.compression+.05;
            flat=flat ? std::max(*flat,v) : v;
        }
    }
    const bool inverted=std::cos(state.pitch)*std::cos(state.roll)<=.1;
    if (inverted) memory.springs.assign(descriptor.springs.size(),std::nullopt);
    for (std::size_t i=0;i<contacts.size();++i) {
        auto& c=contacts[i];
        const bool spring=i<descriptor.springs.size();
        c.spring=spring;
        if (spring && inverted) { c.ground.reset(); continue; }
        const auto local=offset(c,state.pitch,state.roll);
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
                if (posed.expanded && old_plane) high=std::max(high,height_at(*old_plane,point[0],point[1])+.1);
            }
            if (c.track) flat_high=ph+.1;
        }
        c.ground=column(point[0],point[1],low,high,flat_high);
        const auto direct=c.ground;const auto layers=spring?world.layers():std::vector<GroundLayer>{};
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
                        const auto o=rx(rz({side,0.,forward},state.roll),state.pitch);
                        const double px=point[0]+std::cos(yaw)*o[0]+std::sin(yaw)*o[2];
                        const double pz=point[1]-std::sin(yaw)*o[0]+std::cos(yaw)*o[2];
                        const double delta=gradient ? (*gradient)[0]*(px-point[0])+(*gradient)[1]*(pz-point[1]) : 0.;
                        const double lo=*expected-.12+delta,hi=std::min(ceiling+delta,ph+o[1]+rise);
                        if (hi<lo) continue;
                        const auto hit=column(px,pz,lo,hi,hi);
                        if (hit && *hit>=lo-.01 && *hit<=hi+.01) { c.ground=*hit-delta; found=true; break; }
                    }
                    if (found) break;
                }
            }
        }
        if (c.rigid) saved.reset();
        else { const auto retained=retain_ground(point[0],point[1],c.ground,saved,
            descriptor.contact_memory_distance,gradient); c.ground=retained.height; saved=retained.memory; }
        if(spring)probes.push_back({point[0],point[1],low,high,direct,c.ground,flat_high,layers});
    }
    // Fit the source's spring-only least-squares plane in the pitched body frame.
    std::vector<std::array<double,3>> rows;
    for (std::size_t i=0;i<descriptor.springs.size();++i) if (contacts[i].ground) {
        const auto p=offset(contacts[i],state.pitch,state.roll);
        rows.push_back({p[0],p[2],*contacts[i].ground});
    }
    std::optional<SupportPlane> plane;double plane_residual=0.;
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
                p.normal={nx/length,ny/length,nz/length}; plane=p;plane_residual=residual;
            }
        }
    }

    Output out;out.contacts=contacts;out.plane=plane;out.state=state;out.probes=std::move(probes);
    out.plane_residual=plane_residual;out.plane_contacts=rows.size();
    const bool no_ground=std::none_of(contacts.begin(),contacts.end(),[](const auto&c){return c.ground.has_value();});
    if(!armed_before&&no_ground){out.state.vertical_speed=out.state.support_vertical_speed=0.;out.state.airborne=false;
        out.state.support_gradient.reset();out.outcome.pending_spawn=true;return out;}
    const auto before=state;
    const double vertical_before=state.vertical_speed+input.speed_delta;
    double support=state.support_vertical_speed;
    auto support_gradient=state.support_gradient;
    if(elapsed>0.){const double vx=motion?(state.position[0]-(*motion)[0])/elapsed:0.;
        const double vz=motion?(state.position[2]-(*motion)[2])/elapsed:0.;support_gradient.reset();
        if(plane)support_gradient=std::array<double,2>{plane->gradient_x,plane->gradient_z};
        else if(!state.airborne)support_gradient=state.support_gradient;
        support=support_gradient?(*support_gradient)[0]*vx+(*support_gradient)[1]*vz:0.;}
    support+=input.speed_delta;
    const auto solved=suspension_solve(descriptor.parameters,{state.position[1]+input.height_delta,vertical_before,
        state.pitch,state.pitch_speed,state.roll,state.roll_speed},contacts,elapsed,support,descriptor.tuning,descriptor.maximum_steps);
    const auto& p=solved.state;for(double value:p)if(!std::isfinite(value))throw std::runtime_error("Player suspension produced a nonfinite pose");
    const bool invalid=std::abs(p[0]-state.position[1])>5.+std::abs(vertical_before)*elapsed+descriptor.tuning[0]*elapsed*elapsed||
        std::abs(p[2]-state.pitch)>1.2||std::abs(p[4]-state.roll)>1.2;
    const bool raised=armed_before&&solved.contacts&&!solved.rigid&&p[0]-state.position[1]>.62;
    bool continuous=false;
    if(raised&&old_plane&&plane&&motion){const auto checks=support_checkpoints(*old_plane,*plane,*motion,state.position,p[0]);
        if(checks){std::vector<std::optional<double>> heights;for(const auto& check:*checks){heights.push_back(column(check.x,check.z,
            check.expected_height-.35,check.expected_height+.35,check.expected_height+.35));}continuous=support_checkpoints_proved(*checks,heights);}}
    if(invalid||(raised&&!continuous)){out.state.position=input.tick_position;
        out.state.vertical_speed=out.state.pitch_speed=out.state.roll_speed=out.state.support_vertical_speed=0.;
        out.state.support_gradient.reset();out.state.airborne=false;out.outcome.blocked=true;memory_={};return out;}
    auto& s=out.state;s.position[1]=p[0];s.vertical_speed=p[1];s.pitch=s.terrain_pitch=p[2];s.pitch_speed=p[3];s.roll=p[4];s.roll_speed=p[5];
    s.airborne=solved.contacts==0;s.left_flying=!solved.left;s.right_flying=!solved.right;
    s.support_vertical_speed=support;s.support_gradient=support_gradient;
    if(solved.contacts)s.grounded_once=true;
    if(s.airborne){s.support_vertical_speed=0.;s.support_gradient.reset();memory_={};}
    s.ground_plane=solved.contacts&&!s.airborne?plane:std::nullopt;
    if(before.airborne&&!s.airborne&&vertical_before<0.){const double vy=solved.impact_speed.value_or(vertical_before);double impact=std::max(0.,-vy);
        if(motion&&elapsed>0.&&plane){const auto&n=plane->normal;const double scale=std::max({std::abs(n[0]),std::abs(n[1]),std::abs(n[2])});
            if(std::isfinite(scale)&&scale>0.&&n[1]>0.){const double nx=n[0]/scale,ny=n[1]/scale,nz=n[2]/scale;
                const double length=std::sqrt(nx*nx+ny*ny+nz*nz),vx=(before.position[0]-(*motion)[0])/elapsed,vz=(before.position[2]-(*motion)[2])/elapsed;
                const double closing=-(vx*nx+vy*ny+vz*nz)/length;if(std::isfinite(closing))impact=std::max(0.,closing);}}
        out.outcome.landing_impact=impact;out.outcome.normal_impact=true;if(plane)out.outcome.landing_normal=plane->normal;witnesses(out,contacts);
    }else if(!before.airborne&&s.airborne){s.turn_speed=0.;s.rotation_direction=0;}
    return out;
}
}
