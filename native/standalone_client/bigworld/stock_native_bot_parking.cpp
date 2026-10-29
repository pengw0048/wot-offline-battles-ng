#include "stock_native_bot_parking.hpp"
#include <algorithm>
#include <cmath>
#include <limits>
#include <queue>

namespace stock_native_bot_parking {
bool available(const Point&p,double clearance,int team,const std::vector<Point>&excluded,const std::vector<Occupied>&occupied)
{
    for(const auto&old:excluded)if(std::hypot(p[0]-old[0],p[2]-old[2])<std::max(12.,clearance*2.))return false;
    for(const auto&old:occupied){if(!old.team&&std::abs(p[1]-old.point[1])>std::max(3.,old.radius))continue;
        if((!old.team||*old.team==team)&&std::hypot(p[0]-old.point[0],p[2]-old.point[2])<clearance+old.radius+6.)return false;}return true;
}
Parking::Parking(const offline_navigation::MapInput&map,std::array<std::array<int,2>,8> directions):map_(map),directions_(directions){}
Point Parking::point(int i)const{return {map_.origin_x+(i%map_.width)*map_.cell_size,map_.heights.at(i),map_.origin_z+(i/map_.width)*map_.cell_size};}
bool Parking::usable(int i)const
{if(i<0||i>=map_.width*map_.height||!std::isfinite(map_.heights.at(i))||(map_.hazards.at(i)&15u))return false;const auto p=point(i);return !map_.has_bounds||(p[0]>=map_.bounds[0]&&p[0]<=map_.bounds[2]&&p[2]>=map_.bounds[1]&&p[2]<=map_.bounds[3]);}
std::optional<int> Parking::closest(const Point&p)const
{if(map_.has_bounds&&(p[0]<map_.bounds[0]||p[0]>map_.bounds[2]||p[2]<map_.bounds[1]||p[2]>map_.bounds[3]))return {};
    // Python 2 round(x) breaks exact half ties away from zero.
    const int x=int(std::round((p[0]-map_.origin_x)/map_.cell_size)),z=int(std::round((p[2]-map_.origin_z)/map_.cell_size));if(x<0||x>=map_.width||z<0||z>=map_.height)return {};const int i=z*map_.width+x;return usable(i)?std::optional<int>(i):std::nullopt;}
bool Parking::clear(int i,double radius)const
{
    if(!usable(i))return false;const auto p=point(i);if(map_.has_bounds&&!(p[0]>=map_.bounds[0]+radius&&p[0]<=map_.bounds[2]-radius&&p[2]>=map_.bounds[1]+radius&&p[2]<=map_.bounds[3]-radius))return false;
    const int row=i/map_.width,col=i%map_.width,extent=int(std::ceil(radius/map_.cell_size+.5));
    for(int dz=-extent;dz<=extent;++dz)for(int dx=-extent;dx<=extent;++dx){const double near_x=std::max(0.,std::abs(dx)*map_.cell_size-map_.cell_size*.5),near_z=std::max(0.,std::abs(dz)*map_.cell_size-map_.cell_size*.5);if(near_x*near_x+near_z*near_z>radius*radius)continue;
        const int x=col+dx,z=row+dz;if(x<0||x>=map_.width||z<0||z>=map_.height)return false;const int other=z*map_.width+x;if(!usable(other))return false;const double horizontal=map_.cell_size*std::hypot(dx,dz);if(horizontal&&std::abs(point(other)[1]-p[1])>horizontal*std::tan(12.*3.14159265358979323846/180.))return false;}
    return true;
}
std::vector<double> Parking::distances(const Point&p)const
{
    std::vector<double> out(map_.heights.size(),std::numeric_limits<double>::infinity());auto start=closest(p);if(!start)return out;
    using Item=std::pair<double,int>;std::priority_queue<Item,std::vector<Item>,std::greater<Item>> todo;out[*start]=0.;todo.push({0.,*start});
    while(!todo.empty()){const auto item=todo.top();todo.pop();const auto length=item.first;const int index=item.second;if(length!=out[index])continue;const int x=index%map_.width,z=index/map_.width;
        for(unsigned bit=0;bit<8;++bit){if(!(map_.links.at(index)&(1u<<bit)))continue;const auto&d=directions_[bit];const int nx=x+d[0],nz=z+d[1];if(nx<0||nx>=map_.width||nz<0||nz>=map_.height)continue;const int next=nz*map_.width+nx;if(!usable(next))continue;
            const double cost=length+map_.cell_size*std::hypot(d[0],d[1]);if(cost<out[next]){out[next]=cost;todo.push({cost,next});}}}return out;
}
bool Parking::displaced(std::int64_t id,const Plan&plan,const Point&p,bool airborne,bool overturned)const
{
    if(airborne||overturned)return false;auto catalog=catalogs.find(id);if(catalog==catalogs.end())return false;auto found=catalog->second.areas.find(plan.source=="launcher_manual_v1"?plan.zone:plan.cell);if(found==catalog->second.areas.end())return false;const auto&a=found->second;
    if(a.circle){if(std::hypot(p[0]-a.bounds[0],p[2]-a.bounds[1])>a.radius-2.)return false;}
    else if(!(p[0]>=a.bounds[0]+plan.radius&&p[0]<=a.bounds[2]-plan.radius&&p[2]>=a.bounds[1]+plan.radius&&p[2]<=a.bounds[3]-plan.radius))return false;
    auto index=closest(p);return index&&clear(*index,plan.clearance)&&std::abs(p[1]-point(*index)[1])<=1.5;
}
std::optional<Plan> Parking::select(std::int64_t id,int team,const Point&origin,const std::vector<Point>&excluded,const std::vector<Occupied>&occupied,const std::optional<std::string>&preferred)const
{
    auto found=catalogs.find(id);if(found==catalogs.end())return {};const auto&catalog=found->second;const auto reachable=distances(origin);const Candidate*best=nullptr;
    auto eligible=[&](const Candidate&c){return c.index>=0&&std::size_t(c.index)<reachable.size()&&std::isfinite(reachable[c.index])&&available(c.plan.point,c.plan.clearance,team,excluded,occupied);};
    auto order=[&](const Candidate&c){return std::make_tuple(!preferred||c.plan.zone==*preferred?0:1,catalog.manual?-c.priority:c.priority,catalog.manual?c.centre_distance:0.,reachable[c.index],c.plan.zone,catalog.manual?std::string():c.plan.cell,catalog.manual?c.plan.point:Point{},catalog.manual?0:c.index);};
    for(const auto&candidate:catalog.candidates)if(eligible(candidate)&&(!best||order(candidate)<order(*best)))best=&candidate;
    return best?std::optional<Plan>(best->plan):std::nullopt;
}
}
