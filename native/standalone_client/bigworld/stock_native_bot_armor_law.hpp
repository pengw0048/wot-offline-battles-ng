#pragma once
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace stock_native_bot_armor {
struct Material {
    std::optional<double> armor,vehicle_damage_factor;
    std::optional<double> chance_projectile,chance_explosion;
    std::optional<int> damage_kind;
    std::string device;
    int kind=0;
    bool use_hit_angle=true,normalize_caliber=true,ricochet=true,ricochet_caliber=true,once=false,use_lining=true;
};
struct Shell {
    std::string kind;
    double caliber=0.,piercing_near=0.,piercing_far=0.,maximum_distance=0.,damage=0.;
    double explosion_damage_factor=.5,explosion_absorption_factor=1.3;
};
struct Layer {
    double distance=0.,cosine=0.;
    unsigned component=0;
    const Material * material=nullptr;
};
struct Penetration {int result=1;double effective=0.,piercing=0.;};
struct Contact:Penetration {bool structural=false;std::size_t layer=0;double spent=0.;};
double piercing(const Shell&,double distance);
Penetration penetration(const Shell&,double distance,double armor,double cosine,
    double spent,const Material&,bool allow_ricochet=true);
std::optional<Contact> contact(const Shell&,double distance,std::vector<Layer>);
// Advisory mean rolls only. This never consumes real projectile randomness.
std::optional<double> nominal_damage(const Shell&,double distance,std::vector<Layer>);
}
