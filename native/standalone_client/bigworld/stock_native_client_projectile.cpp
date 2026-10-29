#include "stock_native_client_projectile.hpp"
#include <algorithm>
#include <cmath>
#include <limits>
#include <sstream>
#include <stdexcept>
namespace stock_native_client_projectile {
namespace {
double radius(damage::Random&r,double dispersion){double value=std::abs(r.gauss(0.,dispersion/2.));if(value>dispersion)value=dispersion*r.uniform();return value;}
Point normalize(Point value){float square=0.f;for(double v:value)square+=float(v)*float(v);const float length=std::sqrt(square);if(!(length>0.f))return {};for(auto&v:value)v=float(float(v)/length);return value;}
void roundSource(damage::LaunchDescriptor&d){d.speed=wireRound(d.speed);d.gravity=wireRound(d.gravity);d.maximum=wireRound(d.maximum);d.raw_radius=wireRound(d.raw_radius);auto&s=d.shot;s.shell.caliber=wireRound(s.shell.caliber);s.shell.damage=wireRound(s.shell.damage);s.shell.piercing_near=wireRound(s.shell.piercing_near);s.shell.piercing_far=wireRound(s.shell.piercing_far);s.shell.maximum_distance=d.maximum;if(s.device_damage_mean)s.device_damage_mean=wireRound(*s.device_damage_mean);s.shell.explosion_damage_factor=wireRound(s.shell.explosion_damage_factor);s.shell.explosion_absorption_factor=wireRound(s.shell.explosion_absorption_factor);s.edge_damage_factor=wireRound(s.edge_damage_factor);s.explosion_radius=d.raw_radius>0.?d.raw_radius:s.shell.caliber*s.shell.caliber/5555.;if(s.stun){auto&v=*s.stun;v.radius=wireRound(v.radius);v.duration=wireRound(v.duration);v.factor=wireRound(v.factor);v.guaranteed_duration=wireRound(v.guaranteed_duration);v.damage_duration=wireRound(v.damage_duration);v.guaranteed_effect=wireRound(v.guaranteed_effect);v.damage_effect=wireRound(v.damage_effect);}}
std::string signature(const damage::LaunchDescriptor&raw){auto d=raw;roundSource(d);std::ostringstream out;out<<std::hexfloat<<d.speed<<','<<d.gravity<<','<<d.maximum<<','<<d.raw_radius<<','<<d.explosion_factors;const auto&s=d.shot;out<<','<<s.deadeye<<','<<s.shell.kind<<','<<s.shell.caliber<<','<<s.shell.damage<<','<<s.shell.piercing_near<<','<<s.shell.piercing_far<<','<<s.device_damage_mean.value_or(0.);if(d.explosion_factors)out<<','<<s.shell.explosion_damage_factor<<','<<s.shell.explosion_absorption_factor<<','<<s.edge_damage_factor;if(s.stun){const auto&v=*s.stun;out<<','<<v.radius<<','<<v.duration<<','<<v.factor<<','<<v.guaranteed_duration<<','<<v.damage_duration<<','<<v.guaranteed_effect<<','<<v.damage_effect;}auto result=out.str();const std::string negative="-0x0p+0";for(auto at=result.find(negative);at!=std::string::npos;at=result.find(negative,at+6))result.erase(at,1);return result;}
}
double wireRound(double value){
    if(!std::isfinite(value))return value;if(value==0.)return value;
    const bool negative=std::signbit(value);int exponent=0;const double fraction=std::frexp(std::abs(value),&exponent);const std::uint64_t significand=std::uint64_t(std::ldexp(fraction,53));
    const std::uint64_t lower=(significand&0xffffffffu)*15625u,upper=(significand>>32)*15625u,low=lower+(upper<<32),high=(upper>>32)+(low<lower?1u:0u);const int shift=47-exponent;std::uint64_t rounded=0;
    if(shift>0&&shift<64){rounded=(low>>shift)|(high<<(64-shift));const auto remainder=low&((std::uint64_t(1)<<shift)-1),half=std::uint64_t(1)<<(shift-1);if(remainder>half||(remainder==half&&(rounded&1)))++rounded;}
    else if(shift==64){rounded=high;const auto half=std::uint64_t(1)<<63;if(low>half||(low==half&&(rounded&1)))++rounded;}
    else if(shift>64&&shift<128){const int bits=shift-64;rounded=high>>bits;const auto remainder=high&((std::uint64_t(1)<<bits)-1),half=std::uint64_t(1)<<(bits-1);if(remainder>half||(remainder==half&&(low||(rounded&1))))++rounded;}
    else if(shift<=0)return value;
    const double result=double(rounded)/1.e6;return negative?-result:result;
}
Point barrelPoint(const Barrel&mount,const Point&position,const Point&attitude,double turret_yaw,double gun_pitch){
    const auto add=[](const Point&a,const Point&b){return Point{a[0]+b[0],a[1]+b[1],a[2]+b[2]};};
    const auto rx=[](Point v,double a){return Point{v[0],std::cos(a)*v[1]-std::sin(a)*v[2],std::sin(a)*v[1]+std::cos(a)*v[2]};};
    const auto ry=[](Point v,double a){return Point{std::cos(a)*v[0]+std::sin(a)*v[2],v[1],-std::sin(a)*v[0]+std::cos(a)*v[2]};};
    const auto rz=[](Point v,double a){return Point{std::cos(a)*v[0]-std::sin(a)*v[1],std::sin(a)*v[0]+std::cos(a)*v[1],v[2]};};
    auto local=add(add(ry(add(rx({0.,0.,mount.length},gun_pitch),mount.gun),turret_yaw),mount.turret),mount.hull);
    return add(ry(rx(rz(local,attitude[2]),attitude[1]),attitude[0]),position);
}
void normalize(Frozen&frozen){frozen.flight.source_signature=signature(frozen.source);}
const Frozen* Lifecycle::begin(const Trigger&input,stock_native_client_weapon::Gun&gun,damage::Random&random,const Normalizer&normalizer){
    ++metrics_.triggers;if(pending_||input.round<=0||input.player<=0||input.input_sequence<=0||input.shell!=gun.shot||input.shell<0||input.shell>=int(gun.ammunition.size())||input.mounted.speed<=0.||input.mounted.gravity<=0.||input.mounted.maximum<=0.){++metrics_.rejected;return nullptr;}
    const bool continuing=burst_&&burst_->committed>0&&gun.burst_remaining>0&&gun.reload_time<=0.&&gun.clip>0&&gun.shot==burst_->shell&&gun.ammunition[gun.shot]>0;
    if((burst_&&!continuing)||(!continuing&&!gun.ready(true))){++metrics_.rejected;return nullptr;}
    Frozen frozen;frozen.round=input.round;frozen.player=input.player;frozen.input_sequence=input.input_sequence;frozen.team=input.team;frozen.reload_factor=input.reload_factor;frozen.local_time=input.now;for(int n:gun.ammunition)frozen.shells_before+=n;frozen.source=input.mounted;frozen.stickers=input.stickers;frozen.source.shot.deadeye=input.deadeye;roundSource(frozen.source);
    const auto unit=[&](Point value){return normalizer?normalizer(value):normalize(value);};Point direction=unit(input.direction);if(direction==Point{}){++metrics_.rejected;return nullptr;}
    if(!input.perfect&&input.dispersion>0.){const double deviation=radius(random,input.dispersion),angle=random.uniform()*(2.*3.1415926535897932384626433832795);direction=stock_native_client_weapon::scatter(direction,deviation,angle);for(auto&v:direction)v=float(v);direction=unit(direction);}
    auto&l=frozen.flight;l.id=std::to_string(input.round)+":p:"+std::to_string(input.player)+":"+std::to_string(++next_sequence_);l.vehicle=input.vehicle;l.shooter=input.player;l.sequence=next_sequence_;l.shell_index=input.shell;l.maximum_ms=20000;l.launch_ms=input.server_ms;l.gravity=wireRound(input.mounted.gravity);l.maximum_distance=wireRound(input.mounted.maximum);l.he=frozen.source.shot.shell.kind=="HIGH_EXPLOSIVE";l.splash_radius=l.he?wireRound(frozen.source.shot.explosion_radius):0.;l.penetration_factor=wireRound(damage::sampleShellValue(1.,random));l.shot=frozen.source.shot;
    for(unsigned i=0;i<3;++i){l.origin[i]=l.segment_origin[i]=wireRound(input.origin[i]);l.velocity[i]=l.segment_velocity[i]=wireRound(direction[i]*input.mounted.speed);l.range_origin[i]=wireRound(input.position[i]);}
    if(!burst_){const int count=std::max(0,std::min({std::max(1,gun.config.burst_count),gun.ammunition[gun.shot],gun.clip}));if(count>1)burst_=Burst{count,0,gun.shot,l.sequence,input.now,gun.config.burst_interval};}
    l.group_sequence=burst_?burst_->group:l.sequence;l.burst_index=burst_?burst_->committed:0;l.burst_count=burst_?burst_->count:1;l.source_signature=signature(frozen.source);
    auto transaction=gun;const bool final=l.burst_index+1==l.burst_count;
    if((l.burst_index==0&&!transaction.begin(int(l.burst_count)))||!transaction.commit(final,frozen.reload_factor)){if(burst_&&burst_->committed==0)burst_.reset();++metrics_.rejected;return nullptr;}
    launches_[l.id]=frozen;
    if(!owner_.launch(l,input.now,input.now)){launches_.erase(l.id);if(burst_&&burst_->committed==0)burst_.reset();++metrics_.rejected;return nullptr;}
    gun_before_=gun;pending_=std::move(frozen);return &*pending_;
}
bool Lifecycle::complete(bool sent,stock_native_client_weapon::Gun&gun){if(!pending_)return false;const auto frozen=*pending_;pending_.reset();auto transaction=gun_before_.value_or(gun);gun_before_.reset();transaction.pending=gun.pending;if(!sent){owner_.discard(frozen.flight.id);launches_.erase(frozen.flight.id);if(burst_&&burst_->committed==0)burst_.reset();++metrics_.send_failures;return false;}
    const auto&launch=frozen.flight;const bool final=launch.burst_index+1==launch.burst_count;
    // Validate the complete transaction before admission. Once enqueue returns
    // true, only frozen launch fields decide its debit, even after re-entry.
    const bool ready=transaction.shot==launch.shell_index&&(launch.burst_index!=0||transaction.begin(int(launch.burst_count)));
    if(!ready||!transaction.commit(final,frozen.reload_factor)){++metrics_.commit_failures;throw std::logic_error("Admitted native player shot lost its frozen gun transaction");}
    if(burst_&&burst_->group==launch.group_sequence){++burst_->committed;if(final)burst_.reset();}
    else if(!final)transaction.cancel(frozen.reload_factor);
    transaction.rescale(frozen.reload_factor);gun=std::move(transaction);++metrics_.launched;return true;
}
bool Lifecycle::canonical(const Frozen&incoming,double now,bool admit){auto frozen=incoming;normalize(frozen);if(frozen.player<=0||frozen.player!=frozen.flight.shooter||frozen.flight.id!=std::to_string(frozen.round)+":p:"+std::to_string(frozen.player)+":"+std::to_string(frozen.flight.sequence))return false;const auto found=launches_.find(frozen.flight.id);if(found!=launches_.end()){if(!projectile::same_launch(found->second.flight,frozen.flight)||found->second.input_sequence!=frozen.input_sequence)return false;}else launches_.emplace(frozen.flight.id,frozen);sequence(frozen.flight.sequence);return !admit||owner_.acceptCanonical(frozen.flight,now);}
bool Lifecycle::cancelBurst(stock_native_client_weapon::Gun&gun,double factor){if(!burst_)return false;burst_.reset();return gun.cancel(factor);}
bool Lifecycle::burstDue(double now)const{return burst_&&!pending_&&now+1.e-9>=burst_->start+burst_->committed*burst_->interval;}
void Lifecycle::authorityChanged(){launches_.clear();if(pending_)launches_[pending_->flight.id]=*pending_;}
void Lifecycle::prune(){for(auto at=launches_.begin();at!=launches_.end();)if(!owner_.flights().count(at->first)&&(!pending_||pending_->flight.id!=at->first))at=launches_.erase(at);else ++at;}
void Lifecycle::reset(bool preserve_sequence){launches_.clear();pending_.reset();gun_before_.reset();burst_.reset();if(!preserve_sequence)next_sequence_=0;}
}
