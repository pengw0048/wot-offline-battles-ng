#include "stock_native_bot_armor_law.hpp"
#include <iomanip>
#include <iostream>
using namespace stock_native_bot_armor;
int main(){Shell s;double distance;std::size_t count;std::cout<<std::setprecision(17);
while(std::cin>>s.kind>>s.caliber>>s.piercing_near>>s.piercing_far>>s.maximum_distance>>s.damage>>s.explosion_damage_factor>>s.explosion_absorption_factor>>distance>>count){
    std::vector<Material> materials(count);std::vector<Layer> layers(count);
    for(std::size_t i=0;i<count;++i){auto&m=materials[i];auto&h=layers[i];bool present;double armor,vdf;
        std::cin>>present>>h.distance>>h.cosine>>h.component>>m.kind>>armor>>vdf>>m.use_hit_angle>>m.normalize_caliber>>m.ricochet>>m.ricochet_caliber>>m.once;
        m.armor=armor;m.vehicle_damage_factor=vdf;h.material=present?&m:nullptr;}
    auto c=contact(s,distance,layers);auto d=nominal_damage(s,distance,layers);
    std::cout<<'[';if(c)std::cout<<'['<<c->result<<','<<c->effective<<','<<c->piercing<<','<<c->spent<<','<<c->structural<<']';else std::cout<<"null";
    std::cout<<',';if(d)std::cout<<*d;else std::cout<<"null";std::cout<<"]\n";
}}
