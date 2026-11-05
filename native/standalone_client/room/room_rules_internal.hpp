#pragma once
#include "room_rules.hpp"
#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <stdexcept>
namespace wot::room::rule {
using Kind=Value::Kind;
inline const Value& resolved(const Value& v){return v.resolved();}
inline bool null(const Value& v){return resolved(v).kind==Kind::Null;}
inline bool isObject(const Value&v){return resolved(v).kind==Kind::Object;}
inline bool isArray(const Value&v){return resolved(v).kind==Kind::Array;}
inline std::string str(const Value& v){return resolved(v).kind==Kind::Text?resolved(v).text:std::string();}
inline std::string field(const Value& v,const std::string& k){return str(v.get(k));}
inline void require(bool b,const char* why){if(!b)throw std::invalid_argument(why);}
inline std::int64_t integer(const Value&v,std::int64_t lo=0,std::int64_t hi=INT64_MAX){const auto&r=v.resolved();require(r.kind==Kind::Integer&&!r.unsigned_integer,"integer required");require(r.integer>=lo&&r.integer<=hi,"integer outside bounds");return r.integer;}
inline double scalar(const Value&v,double lo=-std::numeric_limits<double>::infinity(),double hi=std::numeric_limits<double>::infinity()){
 const auto&r=resolved(v);require(r.kind==Kind::Integer||r.kind==Kind::Number,"number required");const double n=r.kind==Kind::Integer?r.numeric():r.number;require(std::isfinite(n)&&n>=lo&&n<=hi,"number outside bounds");return n;
}
inline bool finite(const Value&v){const auto&r=resolved(v);if(r.kind==Kind::Number)return std::isfinite(r.number);for(const auto&c:r.array)if(!finite(c))return false;for(const auto&c:r.object)if(!finite(c.second))return false;return true;}
// Integer-rational half-even rounding matches _projectile_wire_round across
// CPython 2.7/3 and MSVC (where long double has only binary64 precision).
inline double rounded(double n, int decimals = 6) {
    if (n == 0.) return n;
    std::uint64_t scale = 1;
    for (int i = 0; i < decimals; ++i) scale *= 10;

    int exponent = 0;
    const double fraction = std::frexp(std::abs(n), &exponent);
    const auto mantissa = static_cast<std::uint64_t>(std::ldexp(fraction, 53));
    const std::uint64_t low_product = (mantissa & std::uint64_t{0xffffffff}) * scale;
    const std::uint64_t high_product = (mantissa >> 32) * scale;
    const std::uint64_t low = low_product + (high_product << 32);
    const std::uint64_t high = (high_product >> 32) + (low < low_product ? 1 : 0);
    const int shift = 53 - exponent;
    if (shift <= 0) return n;

    std::uint64_t q = 0;
    bool above = false, tie = false;
    if (shift < 64) {
        q = (low >> shift) | (high << (64 - shift));
        const std::uint64_t mask = (std::uint64_t(1) << shift) - 1;
        const std::uint64_t rem = low & mask;
        const std::uint64_t half = std::uint64_t(1) << (shift - 1);
        above = rem > half;
        tie = rem == half;
    } else if (shift == 64) {
        q = high;
        const std::uint64_t half = std::uint64_t(1) << 63;
        above = low > half;
        tie = low == half;
    } else if (shift < 128) {
        const int bits = shift - 64;
        const std::uint64_t rem = high & ((std::uint64_t(1) << bits) - 1);
        const std::uint64_t half = std::uint64_t(1) << (bits - 1);
        q = high >> bits;
        above = rem > half || (rem == half && low != 0);
        tie = rem == half && low == 0;
    }
    if (above || (tie && (q & 1))) ++q;
    return std::copysign(double(q) / double(scale), n);
}
inline std::set<std::string> keys(const Value&v){std::set<std::string> o;for(const auto&kv:resolved(v).object)o.insert(kv.first);return o;}
inline void shape(const Value&v,std::initializer_list<const char*> required,std::initializer_list<const char*> optional={}){
 require(isObject(v),"object required");auto remaining=keys(v);for(const auto*k:required)require(remaining.erase(k)==1,"required field missing");for(const auto*k:optional)remaining.erase(k);require(remaining.empty(),"unexpected field");
}
inline Value select(const Value&v,std::initializer_list<const char*> names){Value o=object();for(auto*n:names)if(has(v,n))o.object[n]=v.get(n);return o;}
inline void merge(Value& target,const Value& source){for(const auto&kv:resolved(source).object)target.object[kv.first]=kv.second;}
inline Value point(const Value&v,bool velocity=false){require(isArray(v)&&rows(v).size()==3,"three coordinates required");Value out=array();const double low[3]={-5000.,-1000.,-5000.},high[3]={5000.,3000.,5000.};for(int i=0;i<3;++i)out.array.push_back(number(rounded(scalar(rows(v)[i],velocity?-3000.:low[i],velocity?3000.:high[i]))));return out;}
inline Value pose(const Value&v){Value out=array();for(const auto*k:{"x","y","z"})out.array.push_back(v.get(k));return point(out);}
inline double length(const Value&v){double s=0;for(const auto&c:rows(v)){const double n=c.numeric();s+=n*n;}return std::sqrt(s);}
inline std::string projectileId(std::int64_t round,const std::string&kind,std::int64_t id,std::int64_t seq){return std::to_string(round)+":"+(kind=="player"?"p":"b")+":"+std::to_string(id)+":"+std::to_string(seq);}
inline Rules::ActorKey key(const Value&v){return {field(v,"target_kind"),intField(v,"target_id")};}
inline bool alive(const Value&v){return v.get("alive").truth();}
inline std::set<std::string> strings(const Value&v){std::set<std::string> s;for(const auto&r:rows(v))s.insert(str(r));return s;}
inline Value strings(const std::set<std::string>&s){Value v=array();for(const auto&r:s)v.array.push_back(text(r));return v;}
inline std::string actorString(const Rules::ActorKey&k){return k.first+":"+std::to_string(k.second);}
inline Rules::ActorKey parseActor(const std::string&s){const auto colon=s.find(':');if(colon==std::string::npos)return {};try{return {s.substr(0,colon),std::stoll(s.substr(colon+1))};}catch(...){return {};}}
inline Value event(const std::string&kind){Value e=object();e.object["kind"]=text(kind);return e;}
inline void add(Value&v,const std::string&name,std::int64_t n){v.object[name]=wot::room::integer(intField(v,name)+n);}
Value botBurstEdges(const Value&,const Value&);
void validateBotInventory(const Value&,const Value&,bool unavailable=false);
inline Value combatCritical(Value value){if(rows(value.get("devices")).empty()&&rows(value.get("destroyed")).empty()&&rows(value.get("crew_ko")).empty()&&!value.get("fire").truth()&&!value.get("ammo_rack_death").truth())return {};value.object["events"]=array();auto&devices=value.object["devices"].array;std::sort(devices.begin(),devices.end(),[](const Value&a,const Value&b){return field(a,"name")<field(b,"name");});if(has(value,"crew_roster"))value.object["crew_roster"]=strings(strings(value.get("crew_roster")));return value;}
inline std::uint32_t criticalMask(const Value&before,const Value&after){
 const std::map<std::string,unsigned>devices={{"engineHealth",0},{"ammoBayHealth",1},{"fuelTankHealth",2},{"radioHealth",3},{"leftTrackHealth",4},{"rightTrackHealth",4},{"gunHealth",5},{"turretRotatorHealth",6},{"surveyingDeviceHealth",7}},crew={{"commander",0},{"driver",1},{"radioman1",2},{"radioman2",2},{"gunner1",3},{"gunner2",3},{"loader1",4},{"loader2",4}};
 std::map<std::string,std::string> old;for(const auto&r:rows(before.get("devices")))old[field(r,"name")]=field(r,"state");std::uint32_t mask=0;for(const auto&r:rows(after.get("devices"))){auto i=devices.find(field(r,"name"));if(i!=devices.end()&&field(r,"state")=="critical"&&old[i->first]!="critical")mask|=std::uint32_t(1)<<i->second;}auto destroyed=strings(before.get("destroyed"));for(const auto&r:rows(after.get("destroyed"))){auto i=devices.find(str(r));if(i!=devices.end()&&!destroyed.count(i->first))mask|=std::uint32_t(1)<<(12+i->second);}auto ko=strings(before.get("crew_ko"));for(const auto&r:rows(after.get("crew_ko"))){auto i=crew.find(str(r));if(i!=crew.end()&&!ko.count(i->first))mask|=std::uint32_t(1)<<(24+i->second);}return mask;
}
inline int popcount(std::uint32_t mask){int n=0;for(;mask;mask>>=1)n+=mask&1;return n;}
inline bool immobilized(const Value&critical){const auto red=strings(critical.get("destroyed"));return red.count("leftTrackHealth")||red.count("rightTrackHealth");}
inline Value distance(const Value*first,const Value*second){if(!first||!second)return {};double sum=0.;for(auto*k:{"x","y","z"}){if(!has(*first,k)||!has(*second,k))return {};const auto delta=first->get(k).numeric()-second->get(k).numeric();sum+=delta*delta;}return number(std::sqrt(sum));}
inline bool invisible(const Value*actor){return actor&&!actor->get("room_visible_to_enemy").truth();}
inline bool proof(const Value&message,const Value&record){
 const auto&p=message.get("launch_proof");if(field(record,"shooter_kind")=="bot")return null(p);
 auto wanted=select(record,{"projectile_id","shooter_kind","shooter_id","source_vehicle","source_shot","shot_seq","shell_index","burst_group_seq","burst_index","burst_count","origin","velocity","range_origin","gravity","max_distance","max_time_ms","is_he","splash_radius","penetration_factor","launch_server_time_ms","fire_intent_seq","fire_input_seq"});return p==wanted;
}
inline Value cursor(const Value&m,const Value&r,bool terminal){
 Value c=object();const auto base=integer(m.get("base_checked_ms"));const auto previous=intField(r,"checked_through_ms");if(terminal)require(base<=previous,"cursor compare-and-swap failed");
 const auto end=integer(m.get(terminal?"resolved_time_ms":"checked_through_ms"),terminal?previous:base,intField(r,"max_time_ms"));
 const double distance=rounded(scalar(m.get("checked_distance"),terminal?r.get("checked_distance").numeric():0.,r.get("max_distance").numeric()+.001));
 const double loss=rounded(scalar(m.get("piercing_loss"),terminal?r.get("piercing_loss").numeric():0.,100000.));const double factor=rounded(scalar(m.get("penetration_factor"),0.,100.));require(factor==r.get("penetration_factor").numeric(),"penetration factor changed");
 c.object["checked_through_ms"]=wot::room::integer(terminal?end:std::max(end,previous));c.object["checked_distance"]=number(terminal?distance:std::max(distance,r.get("checked_distance").numeric()));c.object["piercing_loss"]=number(terminal?loss:std::max(loss,r.get("piercing_loss").numeric()));return c;
}
}
