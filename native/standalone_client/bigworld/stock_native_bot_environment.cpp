#include "stock_native_bot_environment.hpp"
#include <algorithm>
#include <cmath>
#include <limits>

namespace wot::offline_bot_step {
namespace {
const std::map<std::string,int> device_indices={{"engineHealth",0},{"ammoBayHealth",1},{"fuelTankHealth",2},
 {"radioHealth",3},{"leftTrackHealth",4},{"rightTrackHealth",4},{"gunHealth",5},{"turretRotatorHealth",6},{"surveyingDeviceHealth",7}};
const std::map<std::string,int> crew_indices={{"commander",0},{"driver",1},{"radioman1",2},{"radioman2",2},
 {"gunner1",3},{"gunner2",3},{"loader1",4},{"loader2",4}};
bool critical(const Device* d) {return d && d->hp && !d->destroyed && (d->critical ||
    (d->maximum && *d->hp>0. && *d->hp<=*d->maximum*.5));}
bool destroyed(const Device* d) {return d && d->destroyed;}
const Device* device(const CriticalState& state,const std::string& name) {
    const auto i=state.devices.find(name);return i==state.devices.end()?nullptr:&i->second;
}
bool crew_name(const std::string& name) {
    if(name.size()<=6 || name.substr(name.size()-6)!="Health")return false;
    return crew_indices.count(name.substr(0,name.size()-6))!=0;
}
Point cross(const Point&a,const Point&b){return {a[1]*b[2]-a[2]*b[1],a[2]*b[0]-a[0]*b[2],a[0]*b[1]-a[1]*b[0]};}
double norm(const Point& a){return std::sqrt(a[0]*a[0]+a[1]*a[1]+a[2]*a[2]);}
}
std::uint32_t environment_critical_mask(const CriticalState& before,const CriticalState& after) {
    std::uint32_t mask=0;
    for(const auto& row:device_indices){const auto* old=device(before,row.first);const auto* now=device(after,row.first);
        if(critical(now) && !critical(old))mask|=std::uint32_t(1)<<row.second;
        if(destroyed(now) && !destroyed(old))mask|=std::uint32_t(1)<<(12+row.second);}
    for(const auto& row:crew_indices)if(after.crew_ko.count(row.first) && !before.crew_ko.count(row.first))
        mask|=std::uint32_t(1)<<(24+row.second);
    return mask;
}
void accumulate_environment_critical(EnvironmentTotals& totals,std::int64_t actor,const std::string& cause,
    const CriticalState& before,const CriticalState& after,bool crew_only) {
    const auto mask=environment_critical_mask(before,after) & (crew_only?0xff000000u:0xffffffffu);
    for(int i=0;i<32;++i){const auto bit=std::uint32_t(1)<<i;if(mask&bit)++totals.critical[{actor,cause,bit}];}
}
std::vector<ImpactContact> collect_impact_contacts(const std::vector<Point>& witnesses,
    const Point& surface_normal,double speed,ImpactWorld& world) {
    const double length=norm(surface_normal);if(!std::isfinite(length)||length<=.0001)return {};
    Point normal=surface_normal;for(auto& x:normal)x/=length;
    const Point axis=std::abs(normal[1])<.9?Point{0.,1.,0.}:Point{1.,0.,0.};
    auto tangent=cross(normal,axis);const double tlength=norm(tangent);for(auto& x:tangent)x/=tlength;
    const auto other=cross(normal,tangent);std::vector<Point> directions{normal};
    const double fan=std::tan(25.*std::acos(-1.)/180.);
    for(const auto& lateral:{tangent,other})for(double sign:{-1.,1.}) {Point direction;
        for(int i=0;i<3;++i)direction[i]=normal[i]+sign*fan*lateral[i];
        const double n=norm(direction);for(auto& x:direction)x/=n;directions.push_back(direction);}
    const double depth=std::min(6.,std::max(.25,(std::abs(speed)-10.)*.6));std::map<std::string,double> merged;
    for(std::size_t w=0;w<std::min<std::size_t>(8,witnesses.size());++w)for(const auto& direction:directions){
        Point start,end;for(int i=0;i<3;++i){start[i]=witnesses[w][i]-normal[i]*.03;end[i]=witnesses[w][i]+direction[i]*depth;}
        for(int source=0;source<2;++source)try {
            const auto hits=source?world.interior_segment(start,end):world.material_segment(start,end);
            for(const auto& hit:hits)if((device_indices.count(hit.name)||crew_name(hit.name)) && std::isfinite(hit.distance) && hit.distance>=0. && hit.distance<=depth+.031)
                merged[hit.name]=std::max(merged[hit.name],1./(1.+.25*hit.distance));
        }catch(...){}
    }
    std::vector<ImpactContact> result;for(const auto& row:merged)result.push_back({row.first,row.second});return result;
}
ImpactResult apply_environment_fall_damage(std::int64_t actor,CombatState& state,VerticalState& vertical,
    double impact_speed,bool ground_observed,const std::vector<Point>& witnesses,
    const Point& normal,ImpactWorld& world,EnvironmentTotals& totals) {
    ImpactResult out;if(!state.alive)return out;
    const double excess=std::max(0.,std::abs(impact_speed)-10.);
    int damage=static_cast<int>(std::max(1,state.maximum_health)*excess*.03);
    if(damage<=0)return out;
    if(!witnesses.empty() || ground_observed){
        auto points=witnesses;if(points.empty())try {points=world.ground_witnesses();}catch(...){}
        out.contacts=collect_impact_contacts(points,normal,impact_speed,world);
        const auto before=state.critical;auto c=before;bool changed=false;
        // _BotCriticalVehicle rebuilds its yellow set from current HP.
        for(auto& row:c.devices)row.second.critical=row.second.hp && row.second.maximum &&
            !row.second.destroyed && *row.second.hp>0. && *row.second.hp<=*row.second.maximum*.5;
        for(const auto& contact:out.contacts){const double loss=excess*25.*contact.weight;
            if(crew_name(contact.name)){const auto role=contact.name.substr(0,contact.name.size()-6);
                if(loss>=100. && state.descriptor_crew_roster.count(role))changed=c.crew_ko.insert(role).second || changed;continue;}
            auto found=c.devices.find(contact.name);if(found==c.devices.end() || !found->second.maximum)continue;
            auto& d=found->second;const double old=std::max(0.,d.hp.value_or(*d.maximum));if(old<=0. || loss<=0.)continue;
            d.hp=std::max(0.,old-loss);changed=true;if(*d.hp<=0.) {d.destroyed=true;d.critical=false;}
            else d.critical=d.critical || *d.hp<=*d.maximum*.5;
            if(contact.name=="fuelTankHealth" && *d.hp<=0. && !c.fire)c.fire=true;
            if(contact.name=="ammoBayHealth" && *d.hp<=0.) {c.ammo_rack_death=true;break;}
        }
        const bool crew_dead=!state.descriptor_crew_roster.empty() && std::all_of(state.descriptor_crew_roster.begin(),state.descriptor_crew_roster.end(),[&](const auto& role){return c.crew_ko.count(role)!=0;});
        if(c.ammo_rack_death || crew_dead)damage=std::max(damage,state.health);
        if(changed) {
            if(!before.fire && c.fire)totals.fire_provenance.insert(actor);
            out.events=critical_events(before,c,"world_collision");
            c.crew_roster.clear();state.critical=c;state.critical=combat_record(state).critical;
            accumulate_environment_critical(totals,actor,"world_collision",before,state.critical);
            state.has_critical_payload=true;
        }
    }
    const int before_health=state.health;state.health=std::max(0,state.health-damage);
    totals.fall_damage[actor]+=before_health-state.health;state.display_health=state.health;state.alive=state.health>0;
    if(!state.alive){terminal_critical(state);state.has_critical_payload=true;state.critical.fire_elapsed=state.critical.fire_timer=0.;
        state.has_target=state.has_reposition=false;state.death_reason=3;state.speed=state.turn_speed=0.;
        state.movement_direction=state.rotation_direction=0;vertical.speed=vertical.turn_speed=vertical.push_x=vertical.push_z=0.;
        vertical.movement_direction=vertical.rotation_direction=0;vertical.alive=false;}
    out.damage=damage;return out;
}
ImpactResult apply_environment_impact(std::int64_t actor,CombatState& state,VerticalState& vertical,
    double impact_speed,bool normal_impact,bool ground_observed,const std::vector<Point>& witnesses,
    const Point& normal,ImpactWorld& world,EnvironmentTotals& totals) {
    if(!state.alive)return {};
    const double lateral=std::hypot(vertical.air_lateral_x,vertical.air_lateral_z);
    if(lateral>.01)vertical.slide_speed=std::max(vertical.slide_speed,lateral);
    vertical.air_lateral_x=vertical.air_lateral_z=0.;
    if(!normal_impact)impact_speed=std::hypot(impact_speed,lateral);
    return apply_environment_fall_damage(actor,state,vertical,impact_speed,ground_observed,witnesses,normal,world,totals);
}
}
