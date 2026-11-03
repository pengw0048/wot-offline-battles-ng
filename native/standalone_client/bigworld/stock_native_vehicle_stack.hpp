#pragma once
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <map>
#include <optional>
#include <set>
#include <stdexcept>
#include <tuple>
#include <vector>

namespace stock_native_vehicle_stack {
using Point=std::array<double,3>;
struct Identity {
    bool human=false;std::int64_t id=0;
    bool operator<(const Identity& other)const{return std::tie(human,id)<std::tie(other.human,other.id);}
    bool operator==(const Identity& other)const{return human==other.human&&id==other.id;}
};
struct Body {
    Identity key;Point position{};std::array<double,4> shape{};
    double yaw=0.,pitch=0.,roll=0.,mass=0.;bool alive=true;
};
struct Surface {Identity carrier;Point point{},normal{0.,1.,0.};};
inline Point offset(const Point& point,double pitch,double roll) {
    const double sr=std::sin(roll),cr=std::cos(roll),sp=std::sin(pitch),cp=std::cos(pitch);
    const double side=sr*point[0]+cr*point[1];
    return {cr*point[0]-sr*point[1],cp*side-sp*point[2],sp*side+cp*point[2]};
}
inline bool roof_candidate(const Body& upper,const Body& lower,double x,double z) {
    if(upper.key==lower.key||upper.position[1]<lower.position[1]+.5||
       std::hypot(upper.position[0]-lower.position[0],upper.position[2]-lower.position[2])>12.)return false;
    const double dx=x-lower.position[0],dz=z-lower.position[2],sn=std::sin(lower.yaw),cs=std::cos(lower.yaw);
    return std::abs(cs*dx-sn*dz)<=lower.shape[0]+.5&&std::abs(sn*dx+cs*dz)<=lower.shape[1]+.5;
}
inline bool admit_roof(const Body& upper,const Surface& surface,const Point& start,const Point& end,double best) {
    const double reach=std::abs(std::sin(upper.pitch))*upper.shape[1]+std::abs(std::sin(upper.roll))*upper.shape[0];
    return surface.normal[1]>.5&&surface.point[1]<=upper.position[1]+reach+.15&&
        end[1]<=surface.point[1]&&surface.point[1]<=start[1]&&surface.point[1]>best;
}
inline std::optional<Surface> settled_carrier(const std::vector<Point>& contacts,const std::vector<Surface>& hits) {
    std::map<Identity,std::pair<unsigned,Surface>> matches;
    for(const auto& point:contacts)for(const auto& hit:hits) {
        if(std::abs(hit.point[1]-point[1])>.03||std::hypot(hit.point[0]-point[0],hit.point[2]-point[2])>.05)continue;
        auto& row=matches[hit.carrier];++row.first;row.second=hit;break;
    }
    std::optional<Surface> selected;unsigned count=0;
    for(const auto& row:matches)if(row.second.first>=count){count=row.second.first;selected=row.second.second;}
    return selected;
}
inline double vertical_closing(const Surface& surface,double velocity) {
    const auto&n=surface.normal;const double length=std::hypot(std::hypot(n[0],n[1]),n[2]);
    return std::isfinite(length)&&length>0.?std::max(0.,-velocity*n[1]/length):std::max(0.,-velocity);
}
inline std::optional<Surface> impact_surface(const std::vector<Point>&contacts,const std::vector<Surface>&hits,double vertical_velocity) {
    std::optional<Surface> selected;double speed=-1.;
    for(const auto&point:contacts)for(const auto&hit:hits) {
        if(std::abs(hit.point[1]-point[1])>.03||std::hypot(hit.point[0]-point[0],hit.point[2]-point[2])>.05)continue;
        const double closing=vertical_closing(hit,vertical_velocity);
        if(closing>speed){selected=hit;speed=closing;}break;
    }
    return selected;
}
struct Descent {
    bool active=false;std::uint64_t sequence=0;std::set<Identity> charged;
    void observe(bool before_airborne,double before_velocity,bool airborne,std::optional<double> impact,double dt) {
        if(dt<=0.)return;if(before_velocity>=-10.)active=false;
        if(!active&&std::min(before_velocity,impact.value_or(0.))<-10.&&(before_airborne||airborne)) {
            active=true;++sequence;charged.clear();
        }
    }
    bool admit(Identity carrier,double speed,bool legacy,double dt)const {
        return dt>0.&&(legacy||active)&&speed>10.&&!charged.count(carrier);
    }
    void commit(Identity carrier){charged.insert(carrier);}
};
struct Row {
    Identity upper,lower;
    double upper_total=0.,lower_total=0.,lower_environment_total=0.,contact_seconds=0.;
    std::int64_t upper_impact_total=0;
};
class Ledger {
    std::map<std::pair<Identity,Identity>,Row> rows_;
public:
    void advance(Identity upper,Identity lower,double dt,bool upper_alive,bool lower_alive,double upper_mass,double lower_mass) {
        if(!std::isfinite(dt))throw std::invalid_argument("Stack elapsed time is not finite");
        auto&row=rows_[{upper,lower}];row.upper=upper;row.lower=lower;
        if(!lower_alive)return;
        if(!std::isfinite(upper_mass)||!std::isfinite(lower_mass)||upper_mass<=0.||lower_mass<=0.)throw std::invalid_argument("Stack contact requires descriptor masses");
        const double seconds=std::max(0.,dt),upper_rate=5.*lower_mass/upper_mass,lower_rate=5.*upper_mass/lower_mass;
        row.contact_seconds+=seconds;
        if(upper_alive){row.lower_total+=lower_rate*seconds;row.upper_total+=upper_rate*seconds;}
        else row.lower_environment_total+=lower_rate*seconds;
    }
    void strike(Identity upper,Identity lower,std::int64_t damage,std::int64_t upper_damage=0) {
        advance(upper,lower,0.,false,false,1.,1.);auto&row=rows_.at({upper,lower});
        row.lower_total+=std::max<std::int64_t>(0,damage);row.upper_impact_total+=std::max<std::int64_t>(0,upper_damage);
    }
    std::vector<Row> publication()const {
        std::vector<Row> out;for(const auto&entry:rows_){auto row=entry.second;
            row.upper_total=std::floor(row.upper_total+1.e-8);row.lower_total=std::floor(row.lower_total+1.e-8)+std::floor(row.lower_environment_total+1.e-8);
            row.lower_environment_total=std::floor(row.lower_environment_total+1.e-8);
            row.contact_seconds=std::round(row.contact_seconds*1.e6)/1.e6;out.push_back(row);}return out;
    }
};
inline int impact_damage(int maximum_health,double speed,double upper_mass,double lower_mass) {
    if(upper_mass<=0.||lower_mass<=0.)throw std::invalid_argument("Stack impact requires descriptor masses");
    const int fall=int(maximum_health*std::max(0.,std::abs(speed)-10.)*.03);
    return int(std::round(fall*(5.*upper_mass/lower_mass)/5.));
}
}
