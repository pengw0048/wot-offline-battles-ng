#include "stock_native_client_drive_publication.hpp"
#include <algorithm>
#include <cmath>
#include <stdexcept>
namespace stock_native_client_drive {
namespace {
using V=stock_native_client_state::Value;
V array(){V v;v.kind=V::Kind::Array;return v;}
V object(){V v;v.kind=V::Kind::Object;return v;}
V number(double n){V v;v.kind=V::Kind::Number;v.number=n;return v;}
V integer(ID n){V v;v.kind=V::Kind::Integer;v.integer=n;return v;}
double rounded(double value,unsigned digits){
    // CPython 2.7 rounds the exact binary input to decimal, with halfway
    // cases away from zero. A rounded double multiplication by 10**digits
    // loses that distinction (for example round(2.675, 2) is 2.67).
    if(value==0.)return value;int exponent=0;const auto mantissa=std::uint64_t(std::ldexp(std::frexp(std::abs(value),&exponent),53));
    std::uint64_t factor=1;double scale=1.;for(unsigned i=0;i<digits;++i){factor*=5;scale*=10.;}
    const std::uint64_t upper=(mantissa>>32)*factor,lower=(mantissa&0xffffffffULL)*factor;
    const std::uint64_t low=lower+(upper<<32),high=(upper>>32)+(low<lower);const int shift=53-exponent-int(digits);
    std::uint64_t quotient=0;bool half=false;
    if(shift<=0)throw std::invalid_argument("Native receipt decimal rounding overflow");
    if(shift<64){quotient=(low>>shift)|(high<<(64-shift));half=((low>>(shift-1))&1)!=0;}
    else if(shift==64){quotient=high;half=(low>>63)!=0;}
    else if(shift<128){quotient=high>>(shift-64);half=((high>>(shift-65))&1)!=0;}
    else if(shift==128)half=(high>>63)!=0;
    if(half)++quotient;return std::copysign(double(quotient)/scale,value);
}
V token(const std::set<stock_native_world::MaterialKey>&keys){V out=array();for(const auto&key:keys){auto row=array();row.array.push_back(integer(std::get<0>(key)));row.array.push_back(integer(std::get<1>(key)));row.array.push_back(std::get<2>(key)==-1?V{}:integer(std::get<2>(key)));out.array.push_back(std::move(row));}return out;}
}
PublicationLedger::PublicationLedger():ram_contacts(array()),destructible_contacts(array()),tank_pushes(array()),turret_pushes(array()),landing_impacts(array()){}
void PublicationLedger::refresh(){ram_contacts.array.clear();destructible_contacts.array.clear();for(const auto&row:ram_)if(row.first>ram_admitted_)ram_contacts.array.push_back(row.second);for(const auto&row:destructible_)if(row.first>destructible_admitted_){destructible_contacts.array.push_back(row.second);if(destructible_contacts.array.size()>=destructible_window_)break;}}
bool PublicationLedger::queueDestructible(const stock_native_world::ContactResult&detail,const Point&start,double yaw,const Point&end,double end_yaw,double speed,double dt){
    if(!detail.requires_commit||detail.contacts.empty()||detail.contacts.size()>token_limit_||!std::isfinite(speed)||!std::isfinite(dt)||dt<=0.||dt>.1)return false;
    for(double v:start)if(!std::isfinite(v))return false;for(double v:end)if(!std::isfinite(v))return false;if(!std::isfinite(yaw)||!std::isfinite(end_yaw))return false;
    const auto identities=token(detail.contacts);for(const auto&row:destructible_)if(row.second.get("token")==identities)return true;
    auto receipt=object();auto&fields=receipt.object;fields["seq"]=integer(++destructible_sequence_);const char*names[]={"x","y","z"},*ends[]={"end_x","end_y","end_z"};for(unsigned i=0;i<3;++i){fields[names[i]]=number(start[i]);fields[ends[i]]=number(end[i]);}
    fields["yaw"]=number(yaw);fields["end_yaw"]=number(end_yaw);fields["speed"]=number(rounded(std::max(-200.,std::min(200.,speed)),4));fields["dt"]=number(rounded(dt,6));fields["token"]=identities;
    destructible_[destructible_sequence_]=std::move(receipt);refresh();return true;
}
ID PublicationLedger::queueRam(V receipt){if(receipt.resolved().kind!=V::Kind::Object)throw std::invalid_argument("Native ram receipt is not an object");if(ram_.size()>=16)return 0;const ID sequence=++ram_sequence_;receipt.object["seq"]=integer(sequence);ram_[sequence]=std::move(receipt);refresh();return sequence;}
void PublicationLedger::replaceTankPushes(const std::map<ID,PushCheckpoint>&rows){tank_pushes.array.clear();for(const auto&row:rows){const auto&p=row.second;auto value=array();value.array.push_back(integer(p.actor));value.array.push_back(integer(p.sequence));for(double total:p.totals)value.array.push_back(number(total));tank_pushes.array.push_back(std::move(value));}}
void PublicationLedger::replaceTurretPushes(V rows){if(rows.resolved().kind!=V::Kind::Array)throw std::invalid_argument("Native turret pushes require an array");turret_pushes=std::move(rows);}
bool PublicationLedger::acknowledgeRam(ID admitted,ID resolved){if(admitted<0||resolved<0||resolved>admitted)return false;bool changed=admitted>ram_admitted_;ram_admitted_=std::max(ram_admitted_,admitted);for(auto at=ram_.begin();at!=ram_.end();)if(at->first<=resolved){at=ram_.erase(at);changed=true;}else ++at;refresh();return changed;}
std::vector<V>PublicationLedger::acknowledgeDestructible(ID admitted,ID resolved,const std::vector<ID>&exact,const std::vector<ID>&rejected){
    if(resolved<0||resolved>admitted||admitted>destructible_sequence_||exact.size()>inflight_limit_||rejected.size()>16)return {};
    std::set<ID>done;for(ID value:exact)if(value<=resolved||value>admitted||!done.insert(value).second)return {};
    destructible_admitted_=std::max(destructible_admitted_,admitted);std::set<ID>reject(rejected.begin(),rejected.end());std::vector<V>out;
    for(auto at=destructible_.begin();at!=destructible_.end();)if(at->first<=resolved||done.count(at->first)){if(reject.count(at->first))out.push_back(at->second);at=destructible_.erase(at);}else ++at;
    refresh();return out;
}
void PublicationLedger::queueLanding(V observation,std::size_t maximum){if(landing_impacts.array.size()>=maximum)throw std::runtime_error("Native landing observation queue exceeded limit");landing_impacts.array.push_back(std::move(observation));}
bool PublicationLedger::landingAdmitted(){if(landing_impacts.array.empty())return false;landing_impacts.array.erase(landing_impacts.array.begin());return true;}
void PublicationLedger::barrier(const PlayerState&state,const Controls&controls,const Point&position,double yaw){FrozenInput out;out.sequence=++barrier_sequence_;out.state=state;out.state.position=position;out.state.yaw=yaw;out.controls=controls;out.ram_contacts=ram_contacts;out.destructible_contacts=destructible_contacts;out.tank_pushes=tank_pushes;out.turret_pushes=turret_pushes;barriers_.push_back(std::move(out));}
bool PublicationLedger::barrierAdmitted(std::uint64_t sequence){if(barriers_.empty()||barriers_.front().sequence!=sequence)return false;barriers_.pop_front();return true;}
}
