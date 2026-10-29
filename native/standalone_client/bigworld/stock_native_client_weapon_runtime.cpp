#include "stock_native_client_weapon_runtime.hpp"
#include <algorithm>
#include <cmath>
#include <stdexcept>
namespace stock_native_client_weapon_runtime {
bool audioDue(std::optional<double> deadline,double now){return deadline&&*deadline<=now+1.e-9;}
Conditional conditional(const stock_native_client_state::Value&effective,
    const std::set<std::string>&casualties,bool fire,double health,double maximum,
    const wot::offline_bot_step::CriticalState&critical){
    const auto&dynamic=effective.get("crew").get("dynamic_spotting");const auto&crew=dynamic.get("crew").resolved();std::uint64_t mask=0;
    for(std::size_t i=0;i<crew.array.size();++i){if(i>=63)throw std::runtime_error("Native conditional crew mask exceeds the installed roster");if(casualties.count(crew.array[i].resolved().text))mask|=std::uint64_t(1)<<i;}
    const auto&row=dynamic.get("states").get(std::to_string(mask)+":"+(fire?"1":"0")).get("battle_factors");Conditional out;
    if(health>0.&&health<maximum*row.get("adrenaline_health_fraction").numeric())out.reload=row.get("adrenaline_reload_factor").numeric(1.);
    const auto found=critical.devices.find("gunHealth");double module=1.;
    // A destroyed gun has a None multiplier in #1513's reviewed device law;
    // firing is rejected separately. Armorer only reduces yellow gun bloom.
    if(found!=critical.devices.end()&&!found->second.destroyed){const auto&d=found->second;if(d.hp&&(d.critical||(*d.hp>0.&&d.maximum&&*d.hp<=*d.maximum*.5)))module=2.;}
    if(module>1.)out.dispersion=std::max(1.,module*row.get("damaged_gun_factor").numeric(1.))/module;return out;
}
int livingSkillCount(const stock_native_client_state::Value&effective,const std::string&wanted,const std::set<std::string>&casualties){
    std::string name=wanted;if(name.empty()||name.size()>64)throw std::runtime_error("Invalid crew skill name");
    for(char&c:name){if(c>='A'&&c<='Z')c=char(c-'A'+'a');if(!((c>='a'&&c<='z')||(c>='0'&&c<='9')||c=='_'))throw std::runtime_error("Invalid crew skill name");}
    const auto&members=effective.get("crew").get("members").resolved();std::set<std::string> roster;if(members.kind!=stock_native_client_state::Value::Kind::Array)throw std::runtime_error("Crew skill snapshot is invalid");for(const auto&member:members.array)roster.insert(member.get("instance").resolved().text);
    for(const auto&key:casualties)if(!roster.count(key))throw std::runtime_error("Critical crew state is outside its accepted snapshot");
    int count=0;for(const auto&member:members.array){if(casualties.count(member.get("instance").resolved().text))continue;
        for(const auto&skill:member.get("skills").resolved().array)if(skill.get("name").resolved().text==name&&skill.get("active").resolved().kind==stock_native_client_state::Value::Kind::Boolean&&skill.get("active").truth()&&skill.get("enabled").resolved().kind==stock_native_client_state::Value::Kind::Boolean&&skill.get("enabled").truth()&&skill.get("level").numeric()>=100.)++count;}
    return count;
}
Runtime::Runtime(Gun&gun,Metadata metadata):gun_(&gun),metadata_(std::move(metadata)){}
double Runtime::repairBonus()const{double result=0.;for(const auto&value:equipment_state_)if(value.repairkit&&value.uses_left!=0)result+=value.repair_bonus;return std::max(0.,result);}
double Runtime::enginePowerFactor()const {
    double result=1.;
    for(const auto&value:equipment_state_)
        if(value.fuel||(value.rpm_limiter&&value.active))
            result*=std::max(0.,value.engine_power_factor);
    return std::max(0.,std::isfinite(result)?result:1.);
}
void Runtime::ammo(Edges&out,double now,bool equipment_ready,bool force){
    const auto signature=std::make_pair(std::make_pair(gun_->shot,gun_->clip),gun_->ammunition);
    if(!force&&ammunition_&&*ammunition_==signature)return;
    if(metadata_.shells.size()!=gun_->ammunition.size())throw std::runtime_error("Native weapon shell metadata disagrees with ammunition");
    for(std::size_t i=0;i<metadata_.shells.size();++i){out.ammunition.push_back({metadata_.shells[i],std::max(0,std::min(gun_->ammunition[i],65535)),int(i)==gun_->shot?std::max(0,std::min(gun_->clip,255)):0});if(int(i)==gun_->shot)out.current_shell=metadata_.shells[i];}
    equipment(out,now,equipment_ready);ammunition_=signature;
}
void Runtime::equipment(Edges&out,double now,bool ready){
    if(!ready)return;std::map<std::int64_t,std::array<int,3>> signature;
    for(const auto&e:equipment_state_){int quantity=1,stage=metadata_.ready_stage,remaining=0;
        if(e.uses_left==0){quantity=0;stage=metadata_.exhausted_stage;}
        else if(e.rpm_limiter&&e.active)remaining=-1;
        else if(e.ready_at-now>0.){stage=metadata_.cooldown_stage;const double value=std::ceil(e.ready_at-now);if(!std::isfinite(value)||value>2147483647.)throw std::runtime_error("Native equipment cooldown is invalid");remaining=int(value);}
        const std::array<int,3> state{quantity,stage,remaining<0};signature[e.compact]=state;
        const auto previous=equipment_.find(e.compact);if(previous==equipment_.end()||previous->second!=state)out.equipment.push_back({e.compact,quantity,stage,remaining});
    }equipment_=std::move(signature);
}
void Runtime::reload(Edges&out,double remaining,double duration,bool force){
    const std::pair<double,double> event{std::max(0.,remaining),std::max(0.,duration)};
    if(!force&&reload_&&*reload_==event)return;
    const auto reason=event.first<=0.?"complete":!reload_||reload_->first<=0.?"start":"update";
    out.reload=ReloadEdge{event.first,event.second,reason};reload_=event;
}
void Runtime::targeting(Edges&out,const Factors&f){
    if(metadata_.raw_dispersion<=0.)throw std::runtime_error("#1513 gun descriptor has no dispersion angle");
    const auto&c=gun_->config;
    const std::array<double,7> signature{
        metadata_.turret_speed*metadata_.crew_factor*f.turret_speed,
        metadata_.gun_speed*metadata_.gun_rotation_factor*f.gun_stun,
        c.base_dispersion/metadata_.raw_dispersion*f.dispersion,
        metadata_.descriptor_factors[2]*f.bloom_turret,
        metadata_.descriptor_factors[0]*f.bloom_move,
        metadata_.descriptor_factors[1]*f.bloom_rotation,
        c.aim_time*f.aim_time};
    if(targeting_&&*targeting_==signature)return;targeting_=signature;out.targeting=signature;
}
Edges Runtime::advance(double now,bool live,double speed,double turn,const Factors&f,bool equipment_ready){
    if(!std::isfinite(now))throw std::runtime_error("Native local gun clock is invalid");
    if(!last_tick_)last_tick_=now;const double dt=std::max(0.,now-*last_tick_);last_tick_=now;
    const double previous=gun_->reload_time;
    gun_->tick(dt,live,speed*f.bloom_move,turn*f.bloom_rotation,0.,metadata_.descriptor_factors,f.dispersion,f.aim_time);
    // The elapsed interval belongs to the preceding cycle. A newly accepted
    // critical factor rescales the remaining progress only after that tick.
    const bool rescaled=gun_->rescale(f.reload);Edges out;
    ammo(out,now,equipment_ready);equipment(out,now,equipment_ready);
    if(!live)reload(out,0.,gun_->reload_duration);
    else if(rescaled)reload(out,gun_->reload_time,gun_->reload_duration,true);
    else if(!reload_)reload(out,gun_->reload_time,gun_->reload_duration);
    else if(previous>0.&&gun_->reload_time<=0.)reload(out,0.,gun_->reload_duration,true);
    targeting(out,f);return out;
}
Checkpoint Runtime::checkpoint()const{return {gun_->reload_time,gun_->reload_duration,gun_->dispersion,gun_->clip,gun_->config.clip_size,gun_->shot};}
}
