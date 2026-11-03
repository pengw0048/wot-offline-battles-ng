#pragma once
#include "offline_bot_ammo_data.hpp"
#include <algorithm>
#include <cctype>
#include <numeric>
#include <stdexcept>

namespace stock_native_bot_ammo {
struct Shell { int index=0; std::string kind,name; };
struct Profile {
    std::string vehicle,gun,class_tag;
    std::vector<Shell> shells;
    bool manual=false;
    std::vector<double> ratios;
    int clip_size=1,ammo_limit=-1;
};
struct Allocation { std::vector<int> remaining,categories,order; };
inline std::string upper(std::string s) {
    std::transform(s.begin(),s.end(),s.begin(),[](unsigned char c){return char(std::toupper(c));});return s;
}
inline Allocation allocate(const Profile& p,int maximum,int count) {
    Allocation out;out.remaining.assign(count,0);out.categories.assign(count,0);
    auto shells=p.shells;const auto colon=p.vehicle.find(':');const auto nation=p.vehicle.substr(0,colon);
    const auto known=offline_bot_ammo_data::gun_ammunition.find(nation+":"+p.gun);
    if(!p.manual && known!=offline_bot_ammo_data::gun_ammunition.end()) {
        std::vector<Shell> selected;
        for(const auto& name:known->second) {
            auto found=std::find_if(shells.begin(),shells.end(),[&](const Shell&s){return s.name==name;});
            if(found==shells.end() || std::count_if(shells.begin(),shells.end(),[&](const Shell&s){return s.name==name;})!=1)
                throw std::invalid_argument("Special loadout shell missing or duplicated");
            selected.push_back(*found);
        }
        shells=std::move(selected);
    } else {
        const auto priority=[&](const Shell&s){const auto k=upper(s.kind);int kind=4;
            if(k=="ARMOR_PIERCING")kind=0;else if(k=="ARMOR_PIERCING_CR")kind=1;
            else if(k=="HOLLOW_CHARGE")kind=2;else if(k=="HIGH_EXPLOSIVE")kind=3;
            return 4*int(offline_bot_ammo_data::gold_shells.count(nation+":"+s.name))+kind;};
        std::sort(shells.begin(),shells.end(),[&](const Shell&a,const Shell&b){
            const int x=priority(a),y=priority(b);return x==y?a.index<b.index:x<y;});
    }
    std::vector<double> weights;
    if(p.manual) {
        std::vector<Shell> filtered;
        for(std::size_t i=0;i<shells.size()&&i<p.ratios.size();++i)if(p.ratios[i]>0.){
            filtered.push_back(shells[i]);weights.push_back(p.ratios[i]);}
        shells=std::move(filtered);
    } else {
        const auto he=[](const Shell&s){return upper(s.kind)=="HIGH_EXPLOSIVE";};
        if(p.class_tag=="SPG") shells.erase(std::remove_if(shells.begin(),shells.end(),[&](const Shell&s){return !he(s);}),shells.end());
        else if(!shells.empty()&&he(shells[0])) {
            std::vector<Shell> selected{shells[0]};
            auto backup=std::find_if(shells.begin()+1,shells.end(),[&](const Shell&s){return !he(s);});
            if(backup!=shells.end())selected.push_back(*backup);shells=std::move(selected);
        } else std::stable_partition(shells.begin(),shells.end(),[&](const Shell&s){return !he(s);});
        if(shells.size()==1)weights={100.};
        else if(shells.size()>1) {
            if(p.class_tag=="SPG")weights=p.clip_size>1?std::vector<double>{75.,25.}:std::vector<double>{80.,20.};
            else if(he(shells[0]))weights={80.,20.};
            else if(shells.size()==3)weights={60.,30.,10.};
            else weights=std::any_of(shells.begin(),shells.end(),he)?std::vector<double>{80.,20.}:std::vector<double>{60.,40.};
        }
    }
    for(std::size_t i=0;i<shells.size();++i) {
        const int index=shells[i].index;if(index<0||index>=count)throw std::invalid_argument("Invalid shell index");
        out.order.push_back(index);out.categories[index]=int(std::min<std::size_t>(i,2));
    }
    maximum=std::max(0,maximum);if(p.ammo_limit>=0)maximum=std::min(maximum,p.ammo_limit);
    const int size=std::max(1,p.clip_size),units=maximum/size,remainder=maximum%size;
    if(shells.empty()||weights.empty())return out;
    const double sum=std::accumulate(weights.begin(),weights.end(),0.);
    if(sum<=0.)throw std::invalid_argument("Invalid ammunition weights");
    std::vector<int> quantities(weights.size());for(std::size_t i=0;i<weights.size();++i)quantities[i]=int(i<std::size_t(units));
    int assigned=std::accumulate(quantities.begin(),quantities.end(),0);
    while(assigned<units) {
        std::size_t best=0;double deficit=units*weights[0]/sum-quantities[0];
        for(std::size_t i=1;i<weights.size();++i) {const double candidate=units*weights[i]/sum-quantities[i];
            if(candidate>deficit){best=i;deficit=candidate;}}
        ++quantities[best];++assigned;
    }
    for(std::size_t i=0;i<std::min(shells.size(),quantities.size());++i)out.remaining[shells[i].index]=quantities[i]*size;
    out.remaining[shells[0].index]+=remainder;return out;
}
}
