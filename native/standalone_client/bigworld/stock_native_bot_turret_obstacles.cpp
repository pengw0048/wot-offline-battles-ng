#include "stock_native_bot_turret_obstacles.hpp"

#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace stock_native_bot_turret {
namespace {
constexpr double pi=3.14159265358979323846,epsilon=1.e-7;
Point add(const Point&a,const Point&b){return {{a[0]+b[0],a[1]+b[1],a[2]+b[2]}};}
Point sub(const Point&a,const Point&b){return {{a[0]-b[0],a[1]-b[1],a[2]-b[2]}};}
Point scale(const Point&a,double b){return {{a[0]*b,a[1]*b,a[2]*b}};}
double dot(const Point&a,const Point&b){double value=0.;for(unsigned i=0;i<3;++i)value+=a[i]*b[i];return value;}
Point cross(const Point&a,const Point&b){return {{a[1]*b[2]-a[2]*b[1],a[2]*b[0]-a[0]*b[2],a[0]*b[1]-a[1]*b[0]}};}
Point rotate(const Point&p,const Point&a) {
    const double sr=std::sin(a[2]),cr=std::cos(a[2]),sp=std::sin(a[1]),cp=std::cos(a[1]),sy=std::sin(a[0]),cy=std::cos(a[0]);
    const Point roll{{cr*p[0]-sr*p[1],sr*p[0]+cr*p[1],p[2]}};
    const Point pitch{{roll[0],cp*roll[1]-sp*roll[2],sp*roll[1]+cp*roll[2]}};
    return {{cy*pitch[0]+sy*pitch[2],pitch[1],-sy*pitch[0]+cy*pitch[2]}};
}
Point inverse(const Point&p,const Point&a) {
    const double sy=std::sin(a[0]),cy=std::cos(a[0]),sp=std::sin(a[1]),cp=std::cos(a[1]),sr=std::sin(a[2]),cr=std::cos(a[2]);
    const Point yaw{{cy*p[0]-sy*p[2],p[1],sy*p[0]+cy*p[2]}};
    const Point pitch{{yaw[0],cp*yaw[1]+sp*yaw[2],-sp*yaw[1]+cp*yaw[2]}};
    return {{cr*pitch[0]+sr*pitch[1],-sr*pitch[0]+cr*pitch[1],pitch[2]}};
}
Point rounded(const Point&p){return {{float(p[0]),float(p[1]),float(p[2])}};}
double angle(double value){double out=std::fmod(value+pi,2.*pi);if(out<0.)out+=2.*pi;return out-pi;}
void finite(const Point&p){for(const auto value:p)if(!std::isfinite(value))throw std::invalid_argument("Detached turret geometry is nonfinite");}
void valid(const Component& c){finite(c.offset);finite(c.bounds.minimum);finite(c.bounds.maximum);for(unsigned i=0;i<3;++i)if(c.bounds.minimum[i]>=c.bounds.maximum[i])throw std::invalid_argument("Detached turret component bounds are empty");}
std::vector<Point> corners(const Component& c) {
    std::vector<Point> out;for(const double x:{c.bounds.minimum[0],c.bounds.maximum[0]})for(const double y:{c.bounds.minimum[1],c.bounds.maximum[1]})for(const double z:{c.bounds.minimum[2],c.bounds.maximum[2]})out.push_back(add({{x,y,z}},c.offset));return out;
}
double radius(const Component & c){double out=0.;for(const auto&p:corners(c))out=std::max(out,std::sqrt(dot(p,p)));return out;}
double segmentDistance(const Point&p,const Point&a,const Point&b) {
    const auto delta=sub(b,a);const double n=dot(delta,delta);const double f=n<=0.?0.:std::max(0.,std::min(1.,dot(sub(p,a),delta)/n));const auto d=sub(p,add(a,scale(delta,f)));return dot(d,d);
}
double gap(const Box&a,const Box&b,const Point&normal) {
    double r=0.;for(const auto&axis:a.axes)r+=std::abs(dot(axis,normal));for(const auto&axis:b.axes)r+=std::abs(dot(axis,normal));return dot(sub(a.center,b.center),normal)-r;
}
bool same(const Box&a,const Box&b){return a.center==b.center&&a.axes==b.axes;}
}
Box worldBox(const Component & component,const Point & position,const Point & attitude) {
    valid(component);finite(position);finite(attitude);
    Box box;Point center{};for(unsigned i=0;i<3;++i)center[i]=(component.bounds.minimum[i]+component.bounds.maximum[i])*.5+component.offset[i];
    box.center=add(position,rotate(center,attitude));
    for(unsigned i=0;i<3;++i){Point local{};local[i]=(component.bounds.maximum[i]-component.bounds.minimum[i])*.5;box.axes.push_back(rotate(local,attitude));}return box;
}
std::vector<Box> componentSweeps(const Component & component,const Point & start,const Point & start_angle,const Point & end,const Point & end_angle,const std::optional<std::pair<Point,double>> & pivot) {
    valid(component);finite(start);finite(end);finite(start_angle);finite(end_angle);
    if(pivot){finite(pivot->first);if(!std::isfinite(pivot->second))throw std::invalid_argument("Detached turret pivot is nonfinite");}
    auto travel=sub(end,start);Point deltas{};double rotation=0.;for(unsigned i=0;i<3;++i){deltas[i]=angle(end_angle[i]-start_angle[i]);rotation+=std::abs(deltas[i]);}
    const double pivot_angle=pivot?std::abs(pivot->second):0.;const auto count=std::max(1.,std::ceil(std::max(rotation,pivot_angle)/(pi/36.)));
    const unsigned steps=unsigned(count);double padding=2.*radius(component)*std::sin(rotation/(4.*steps));Point radial{};
    if(pivot){radial=sub(start,pivot->first);const auto arc_end=add(pivot->first,rotate(radial,{{pivot->second,0.,0.}}));travel=sub(end,arc_end);padding+=2.*std::hypot(radial[0],radial[2])*std::sin(pivot_angle/(4.*steps));}
    std::vector<Box> out;
    for(unsigned index=0;index<steps;++index) {
        const double middle=(double(index)+.5)/steps;Point attitude{};for(unsigned i=0;i<3;++i)attitude[i]=start_angle[i]+deltas[i]*middle;
        const Point position=pivot?add(add(pivot->first,rotate(radial,{{pivot->second*middle,0.,0.}})),scale(travel,middle)):add(start,scale(travel,middle));
        auto box=worldBox(component,position,attitude);box.axes.push_back(scale(travel,.5/steps));
        if(padding>0.){box.axes.push_back({{padding,0.,0.}});box.axes.push_back({{0.,padding,0.}});box.axes.push_back({{0.,0.,padding}});}out.push_back(std::move(box));
    }
    return out;
}
bool leavesOverlap(const Box & initial,const Box & final,const std::vector<Box> & sweeps,const Box & obstacle) {
    auto generators=initial.axes;generators.insert(generators.end(),obstacle.axes.begin(),obstacle.axes.end());
    const auto delta=sub(initial.center,obstacle.center),motion=sub(final.center,initial.center);
    std::vector<std::pair<double,Point>> candidates;
    for(std::size_t i=0;i<generators.size();++i)for(std::size_t j=i+1;j<generators.size();++j) {
        auto axis=cross(generators[i],generators[j]);const auto length=std::sqrt(dot(axis,axis));if(length<=1.e-8)continue;axis=scale(axis,1./length);
        const double facing=dot(delta,axis);if(facing<0.||(std::abs(facing)<=epsilon&&dot(motion,axis)<0.))axis=scale(axis,-1.);candidates.push_back({gap(initial,obstacle,axis),axis});
    }
    if(candidates.empty())return false;
    const auto best=std::max_element(candidates.begin(),candidates.end(),[](const auto&a,const auto&b){return a.first<b.first;});const double initial_gap=best->first;const auto&normal=best->second;
    auto preserves=[&](const Point& n,double before){for(const auto&s:sweeps)if(gap(s,obstacle,n)<before-epsilon)return false;return true;};
    if(gap(final,obstacle,normal)>initial_gap+epsilon)return preserves(normal,initial_gap);
    if(normal[1]>=0.||std::abs(normal[1])<=std::max(std::abs(normal[0]),std::abs(normal[2]))||!preserves(normal,initial_gap))return false;
    if(same(final,initial))return true;
    for(const auto&candidate:candidates)if(std::abs(candidate.second[1])<=epsilon&&gap(final,obstacle,candidate.second)>candidate.first+epsilon&&preserves(candidate.second,candidate.first))return true;
    return false;
}
bool Obstacles::put(Record record,const DetachedDescriptor & descriptor) {
    const auto previous=entries_.find(record.owner);if(previous!=entries_.end()&&record.motion_sequence<=previous->second.record.motion_sequence)return false;
    if(!record.landed&&!record.has_body){if(previous==entries_.end())return false;entries_.erase(previous);return true;}
    try {
        if(record.owner.id<1||record.owner.id>2147483647||record.motion_sequence<0||!std::isfinite(record.created_time_ms)||!std::isfinite(record.duration)||record.duration<0.)return false;
        finite(record.rest);finite(record.attitude);finite(record.spin);valid(descriptor.turret);valid(descriptor.gun);
        Point attitude;
        if(record.rest_attitude){attitude=*record.rest_attitude;finite(attitude);}
        else {attitude={{record.attitude[0]+record.spin[0]*record.duration,std::round((record.attitude[1]+record.spin[1]*record.duration)/pi)*pi,std::round((record.attitude[2]+record.spin[2]*record.duration)/pi)*pi}};}
        Entry entry;entry.record=record;entry.descriptor=descriptor;entry.attitude=attitude;entry.radius=std::max(radius(descriptor.turret),radius(descriptor.gun));
        entry.boxes={worldBox(descriptor.turret,record.rest,attitude),worldBox(descriptor.gun,record.rest,attitude)};
        entry.settles_at=record.created_time_ms+(record.has_body?0.:1000.*record.duration);entries_[record.owner]=std::move(entry);return true;
    }catch(const std::exception&){return false;}
}
bool Obstacles::blocks(const stock_native_world::TurretSweep & sweep,const MovingDescriptor & descriptor,double server_time_ms)const {
    if(!std::isfinite(server_time_ms))throw std::invalid_argument("Detached turret clock is nonfinite");
    std::vector<const Entry*> ready;for(const auto&entry:entries_)if(server_time_ms>=entry.second.settles_at&&!entry.second.record.has_body)ready.push_back(&entry.second);if(ready.empty())return false;
    valid(descriptor.chassis);valid(descriptor.hull);
    const auto & root=sweep.start;const auto & finish=sweep.end;std::optional<std::pair<Point,double>> pivot;
    if(descriptor.pivot_available&&!descriptor.rotation_around_center) {
        const double x=std::cos(root.yaw)-std::cos(finish.yaw),z=std::sin(finish.yaw)-std::sin(root.yaw),norm=x*x+z*z;
        if(norm>1.e-18) {
            const double offset=((finish.position[0]-root.position[0])*x+(finish.position[2]-root.position[2])*z)/norm;
            if(std::abs(offset)<=descriptor.track_center+epsilon&&std::abs(finish.position[0]-root.position[0]-offset*x)<=epsilon&&std::abs(finish.position[2]-root.position[2]-offset*z)<=epsilon&&std::abs(finish.position[1]-root.position[1])<=epsilon&&offset!=0.)pivot=std::make_pair(add(root.position,rotate({{offset,0.,0.}},{{root.yaw,0.,0.}})),angle(finish.yaw-root.yaw));
        }
    }
    for(unsigned component=0;component<2;++component) {
        const auto&shape=component?descriptor.hull:descriptor.chassis;
        const bool separate=component&&root.separate_hull,final_separate=component&&finish.separate_hull;
        const Point start=separate?root.hull_position:root.position,end=final_separate?finish.hull_position:finish.position;
        const Point start_angle=separate?Point{{root.hull_yaw,root.hull_pitch,root.hull_roll}}:Point{{root.yaw,root.pitch,root.roll}};
        const Point end_angle=final_separate?Point{{finish.hull_yaw,finish.hull_pitch,finish.hull_roll}}:Point{{finish.yaw,finish.pitch,finish.roll}};
        const auto initial=worldBox(shape,start,start_angle),final=worldBox(shape,end,end_angle);const auto sweeps=componentSweeps(shape,start,start_angle,end,end_angle,pivot);
        double moving_radius=radius(shape);if(pivot){const auto radial=sub(start,pivot->first);moving_radius+=std::hypot(radial[0],radial[2])*(1.-std::cos(std::abs(pivot->second)*.5));}
        for(const auto*entry:ready) {
            if(entry->record.support&&*entry->record.support==Owner{false,std::int64_t(sweep.actor)})continue;
            if(segmentDistance(entry->record.rest,start,end)>std::pow(entry->radius+moving_radius,2.))continue;
            for(const auto&obstacle:entry->boxes) {
                bool intersects=false;for(const auto&box:sweeps)intersects=intersects||offline_destructible::intersects(box,obstacle);if(!intersects)continue;
                if(offline_destructible::intersects(initial,obstacle)&&leavesOverlap(initial,final,sweeps,obstacle))continue;return true;
            }
        }
    }
    return false;
}
std::optional<double> Obstacles::block_distance(const Point& start,const Point& end,double now,
    DetachedHits& hits,std::optional<double> start_time)const {
    finite(start);finite(end);const double begin=start_time.value_or(now);
    if(!std::isfinite(now)||!std::isfinite(begin)||begin>now)throw std::invalid_argument("Detached turret chord clock is invalid");
    const auto travel=sub(end,start);const double length=std::sqrt(dot(travel,travel));std::optional<double> nearest;
    for(const auto& row:entries_) {
        const auto& entry=row.second;if(now<entry.settles_at||segmentDistance(entry.record.rest,start,end)>entry.radius*entry.radius)continue;
        const auto a=inverse(sub(start,entry.record.rest),entry.attitude),b=inverse(sub(end,entry.record.rest),entry.attitude);
        for(unsigned component=0;component<2;++component) {
            const auto&shape=component?entry.descriptor.gun:entry.descriptor.turret;
            // Original localHitTest failure is contained to this component;
            // its sibling and other accepted turrets remain queryable.
            try {
                for(double distance:hits.segment(entry.record.owner,component!=0,rounded(sub(a,shape.offset)),rounded(sub(b,shape.offset)))) {
                    if(!std::isfinite(distance))throw std::invalid_argument("Detached turret BSP distance is nonfinite");
                    if(distance>=0.&&distance<=length&&(!nearest||distance<*nearest)) {
                        const double at=begin+(now-begin)*(length>0.?distance/length:0.);
                        if(at>=entry.settles_at)nearest=distance;
                    }
                }
            }catch(const std::exception&){}
        }
    }
    return nearest;
}
std::vector<stock_native_bot_navigation::Hull> Obstacles::navigation(double now)const {
    std::vector<stock_native_bot_navigation::Hull> out;
    for(const auto&entry:entries_)if(now>=entry.second.settles_at&&!entry.second.record.has_body&&!entry.second.record.support) {
        const auto&e=entry.second;const double c=std::cos(e.attitude[0]),s=std::sin(e.attitude[0]);
        for(std::size_t index=0;index<e.boxes.size();++index) {
            double x0=INFINITY,x1=-INFINITY,z0=INFINITY,z1=-INFINITY;const auto&box=e.boxes[index];
            for(unsigned mask=0;mask<(1u<<box.axes.size());++mask) {
                auto p=box.center;for(unsigned i=0;i<box.axes.size();++i)p=add(p,scale(box.axes[i],mask&(1u<<i)?1.:-1.));p=sub(p,e.record.rest);
                const double x=c*p[0]-s*p[2],z=s*p[0]+c*p[2];x0=std::min(x0,x);x1=std::max(x1,x);z0=std::min(z0,z);z1=std::max(z1,z);
            }
            const double x=(x0+x1)*.5,z=(z0+z1)*.5;
            out.push_back({-(e.record.owner.id*4+(e.record.owner.human?2:0)+std::int64_t(index)+1),e.record.rest[0]+c*x+s*z,e.record.rest[2]-s*x+c*z,e.attitude[0],(z1-z0)*.5,(x1-x0)*.5});
        }
    }
    return out;
}
}
