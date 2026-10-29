#include "stock_native_bot_armor_law.hpp"
#include <algorithm>
#include <cmath>
#include <set>
#include <stdexcept>

namespace stock_native_bot_armor {
namespace {
constexpr double pi=3.14159265358979323846;
bool ap(const Shell& s){return s.kind=="ARMOR_PIERCING"||s.kind=="ARMOR_PIERCING_CR";}
void ordered(std::vector<Layer>& layers)
{std::stable_sort(layers.begin(),layers.end(),[](const Layer&a,const Layer&b){return a.distance<b.distance;});}
}
double piercing(const Shell&s,double d)
{
    if(d<=100.)return s.piercing_near;
    if(s.maximum_distance<=0.||d>=s.maximum_distance)return 0.;
    return std::max(0.,s.piercing_near+(s.piercing_far-s.piercing_near)*(d-100.)/400.);
}
Penetration penetration(const Shell&s,double d,double armor,double cosine,double spent,
    const Material&m,bool allow_ricochet)
{
    Penetration p;p.piercing=std::max(0.,piercing(s,d)-spent);
    armor=std::max(0.,armor);const double ac=m.use_hit_angle?cosine:1.;
    double norm=s.kind=="ARMOR_PIERCING_CR"?2.*pi/180.:(s.kind=="ARMOR_PIERCING"?5.*pi/180.:0.);
    if(m.use_hit_angle&&ap(s)&&m.normalize_caliber&&s.caliber>armor*2.&&armor*2.>0.)norm*=1.4*s.caliber/(armor*2.);
    if(allow_ricochet&&m.use_hit_angle&&m.ricochet) {
        const bool ap_bounce=ap(s)&&!(m.ricochet_caliber&&s.caliber>armor*3.)&&ac<=std::cos(70.*pi/180.);
        const bool heat_bounce=s.kind=="HOLLOW_CHARGE"&&ac<=std::cos(85.*pi/180.);
        if(ap_bounce||heat_bounce){p.result=0;p.effective=armor/std::max(1.e-5,ac);return p;}
    }
    double effective_cosine=ac;
    if(m.use_hit_angle&&norm>0.&&effective_cosine<1.) {
        if(effective_cosine< -1.)throw std::domain_error("native armor cosine outside the reviewed domain");
        const double angle=std::acos(effective_cosine)-norm;
        effective_cosine=angle<0.?1.:std::cos(std::min(angle,pi/2.-1.e-5));
    }
    p.effective=armor/std::max(1.e-5,effective_cosine);
    p.result=p.piercing>0.&&p.piercing>=p.effective?2:1;return p;
}
std::optional<Contact> contact(const Shell&s,double distance,std::vector<Layer> layers)
{
    ordered(layers);double spent=0.;std::optional<double> last_plate;
    std::set<std::pair<unsigned,int>> once;const double base=piercing(s,distance);
    for(std::size_t index=0;index<layers.size();++index) {
        const auto& h=layers[index];
        if(s.kind=="HOLLOW_CHARGE"&&last_plate)
            spent+=std::max(0.,base-spent)*std::min(1.,.5*std::max(0.,h.distance-*last_plate));
        if(!h.material){if(base-spent<=0.)break;if(s.kind=="HOLLOW_CHARGE")last_plate=h.distance;continue;}
        const auto& m=*h.material;const auto key=std::make_pair(h.component,m.kind);
        if(m.once&&once.count(key))continue;
        const double armor=m.armor.value_or(0.);if(armor<0.)continue;
        const auto p=penetration(s,distance,armor,h.cosine,spent,m,!(s.kind=="HOLLOW_CHARGE"&&last_plate));
        Contact c;static_cast<Penetration&>(c)=p;c.layer=index;c.spent=spent;c.structural=m.vehicle_damage_factor.value_or(1.)!=0.;
        if(c.structural)return c;
        if(s.kind=="HIGH_EXPLOSIVE"){c.result=1;return c;}
        if(c.result!=2)return c;
        spent+=c.effective;if(base-spent<=0.){c.result=1;return c;}
        if(s.kind=="HOLLOW_CHARGE")last_plate=h.distance+armor*.001;
        if(m.once)once.insert(key);
    }
    return {};
}
std::optional<double> nominal_damage(const Shell&s,double distance,std::vector<Layer> layers)
{
    ordered(layers);const auto hit=contact(s,distance,layers);if(!hit)return {};
    if(!hit->structural&&s.kind!="HIGH_EXPLOSIVE")return 0.;
    const auto damage=std::int64_t(std::max(0.,s.damage));if(hit->result==2)return double(damage);
    if(s.kind!="HIGH_EXPLOSIVE")return 0.;
    double nominal=0.;
    for(const auto& h:layers)if(h.material&&h.material->vehicle_damage_factor.value_or(1.)!=0.&&h.material->armor.value_or(0.)>0.){nominal=*h.material->armor;break;}
    return double(std::int64_t(std::max(0.,double(damage)*s.explosion_damage_factor-s.explosion_absorption_factor*nominal)));
}
}
