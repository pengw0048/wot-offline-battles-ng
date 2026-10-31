#include "stock_native_human_ram.hpp"
#include "stock_native_bot_python.hpp"
#include "stock_native_bot_resource_lifecycle.hpp"
#include "stock_native_battle_bots.hpp"
#include "stock_native_bot_value.hpp"
#include <cctype>
#include <cstdlib>
#include <cmath>
#include <set>
#include <algorithm>

namespace stock_native_human_ram {
namespace {
namespace p=stock_native_bot_python;
namespace v=stock_native_bot_value;
using native_battle::Value;
using Kind=Value::Kind;
void fields(const Value&object,std::initializer_list<const char*>names){if(object.kind!=Kind::Object||object.object.size()!=names.size())throw p::Error("Worker human ram probe fields are invalid");for(const auto*name:names)if(!object.object.count(name))throw p::Error("Worker human ram probe field is missing");}
const Value& field(const Value&object,const char*name){return object.get(name).resolved();}
bool space(char c){return c==' '||c=='\t'||c=='\n'||c=='\r'||c=='\f'||c=='\v';}
std::string stripped(const std::string&text){std::size_t begin=0,end=text.size();while(begin<end&&space(text[begin]))++begin;while(end>begin&&space(text[end-1]))--end;return text.substr(begin,end-begin);}
bool digits(const std::string&s,std::size_t&i){const auto start=i;while(i<s.size()&&s[i]>='0'&&s[i]<='9')++i;return i>start;}
// Python float(text): decimal digits, an optional fraction and exponent; inf/nan are rejected as nonfinite.
double textNumber(const std::string&raw){const auto s=stripped(raw);std::size_t i=0;if(i<s.size()&&(s[i]=='+'||s[i]=='-'))++i;
    std::string word=s.substr(i);for(auto&c:word)c=char(std::tolower(static_cast<unsigned char>(c)));
    if(word=="inf"||word=="infinity"||word=="nan")throw p::Error("Worker human ram probe number is nonfinite");
    const bool whole=digits(s,i);bool fraction=false;if(i<s.size()&&s[i]=='.'){++i;fraction=digits(s,i);}if(!whole&&!fraction)throw p::Error("Worker human ram probe number is invalid");
    if(i<s.size()&&(s[i]=='e'||s[i]=='E')){++i;if(i<s.size()&&(s[i]=='+'||s[i]=='-'))++i;if(!digits(s,i))throw p::Error("Worker human ram probe number is invalid");}
    if(i!=s.size())throw p::Error("Worker human ram probe number is invalid");const double result=std::strtod(s.c_str(),nullptr);if(!std::isfinite(result))throw p::Error("Worker human ram probe number is nonfinite");return result;}
double number(const Value&object){if(object.kind==Kind::Null)throw p::Error("Worker human ram probe number is missing");if(object.kind==Kind::Text)return textNumber(object.text);
    if(object.kind!=Kind::Number&&object.kind!=Kind::Integer&&object.kind!=Kind::Boolean)throw p::Error("Worker human ram probe number is not plain data");
    const auto result=object.numeric(NAN);if(!std::isfinite(result))throw p::Error("Worker human ram probe number is invalid");return result;
}
ID exactID(const Value&object){if(object.kind==Kind::Null||object.kind==Kind::Boolean)throw p::Error("Worker human ram probe integer is invalid");ID result;
    if(object.kind==Kind::Text){const auto s=stripped(object.text);std::size_t i=0;if(i<s.size()&&(s[i]=='+'||s[i]=='-'))++i;if(!digits(s,i)||i!=s.size())throw p::Error("Worker human ram probe integer is invalid");
        std::size_t used=0;try{result=std::stoll(s,&used);}catch(const std::exception&){throw p::Error("Native Bot integer overflow");}if(number(object)!=double(result))throw p::Error("Worker human ram probe integer is inexact");}
    else{const auto value=number(object);if(value!=std::floor(value)||value<1.||value>2147483647.)throw p::Error("Worker human ram probe integer is outside range");result=ID(value);}
    if(result<1||result>2147483647)throw p::Error("Worker human ram probe integer is outside range");return result;
}
double scalar(const Value&object,const char*name){return number(field(object,name));}
// Characters of decoded wire text (UTF-8 code points).
std::size_t characters(const std::string&text){std::size_t count=0;for(unsigned char c:text)if((c&0xc0)!=0x80)++count;return count;}
struct InputBody {Body body;std::string vehicle;};
InputBody body(const Value&raw){fields(raw,{"id","vehicle","x","y","z","yaw","pitch","roll","shape"});InputBody out;out.body.id=exactID(field(raw,"id"));const auto&vehicle=field(raw,"vehicle"),&shape=field(raw,"shape");if(vehicle.kind!=Kind::Text||characters(vehicle.text)<1||characters(vehicle.text)>80||v::rows(shape).size()!=4)throw p::Error("Worker human ram probe body is invalid");out.vehicle=vehicle.text;auto&b=out.body;b.x=scalar(raw,"x");b.y=scalar(raw,"y");b.z=scalar(raw,"z");b.yaw=scalar(raw,"yaw");b.pitch=scalar(raw,"pitch");b.roll=scalar(raw,"roll");for(unsigned i=0;i<4;++i)b.shape[i]=number(shape.array[i].resolved());if(std::abs(b.x)>5000.||std::abs(b.y)>5000.||std::abs(b.z)>5000.||b.shape[0]<.5||b.shape[0]>20.||b.shape[1]<.75||b.shape[1]>30.||b.shape[2]<-20.||b.shape[2]>=b.shape[3]||b.shape[3]>30.)throw p::Error("Worker human ram probe body is outside range");b.flags=offline_contact::Alive|offline_contact::Player|offline_contact::HasY;return out;}
XZ normal(const Value&raw){const auto&values=v::rows(raw);if(values.size()<2)throw p::Error("Worker human ram normal width is invalid");const auto x=number(values[0].resolved()),z=number(values[1].resolved()),length=std::sqrt(x*x+z*z);if(!std::isfinite(length)||length<=.000001)throw p::Error("Worker human ram normal is invalid");return {{x/length,z/length}};}
bool ready(native_battle::Battle&battle,const InputBody&input){auto*record=battle.vehicle("player:"+std::to_string(input.body.id));if(!record||!record->ready||record->tombstone)return false;if(record->kind!="player"||record->id!=input.body.id)throw p::Error("Worker human ram probe identity is invalid");const auto&vehicle=record->state.get("vehicle");const auto state_vehicle=vehicle.kind==native_battle::Value::Kind::Text?vehicle.text:vehicle.kind==native_battle::Value::Kind::Null?std::string{}:throw p::Error("Worker human ram probe vehicle is invalid");if(state_vehicle!=input.vehicle)throw p::Error("Worker human ram probe vehicle is invalid");auto entity=p::Ref(stock_native_bot::readNativeEntity(nullptr,record->engine_id));if(entity.get()==Py_None)return false;auto started=p::Ref(stock_native_bot::readNativeEntityField(nullptr,entity.get(),"isStarted")),descriptor=p::Ref(stock_native_bot::readNativeEntityField(nullptr,entity.get(),"typeDescriptor"));return p::flag(started.get())&&descriptor.get()!=Py_None;}
bool geometric(const Body&a,const Body&b){const auto interval=[](const Body&v){const auto center=v.y+(v.shape[2]+v.shape[3])*.5,extent=(v.shape[3]-v.shape[2])*.5;return std::array<double,2>{{center-extent,center+extent}};};const auto av=interval(a),bv=interval(b);if(std::min(av[1],bv[1])-std::max(av[0],bv[0])<=.02)return false;const double low=std::max(a.y+a.shape[2],b.y+b.shape[2]),high=std::min(a.y+a.shape[3],b.y+b.shape[3]);if(high<=low)return false;return !stock_native_bot_armor::overlap_polygon(a,b).empty();}
}
Value results(native_battle::Battle&battle,World&world,std::vector<std::string>*errors){auto out=v::array();try{if(!battle.worker||!battle.bots)return out;const auto&requests=battle.bots->last_snapshot.resolved().get("human_ram_probes").resolved();if(requests.kind==Kind::Null)return out;if(requests.kind!=Kind::Array||requests.array.size()>64)throw p::Error("Worker human ram probe batch is invalid");std::set<ID> seen;
    for(const auto&entry:requests.array)try{const auto&raw=entry.resolved();fields(raw,{"seq","contact_normal","first","second"});const auto sequence=exactID(field(raw,"seq"));if(!seen.insert(sequence).second)throw p::Error("Worker human ram probe sequence is repeated");const auto n=normal(field(raw,"contact_normal"));const auto first=body(field(raw,"first")),second=body(field(raw,"second"));if(first.body.id>=second.body.id)throw p::Error("Worker human ram probe order is invalid");if(n[0]*(first.body.x-second.body.x)+n[1]*(first.body.z-second.body.z)<=1.e-6)throw p::Error("Worker human ram probe orientation is invalid");if(!ready(battle,first)||!ready(battle,second)||!world.ready(first.body.id)||!world.ready(second.body.id))continue;if(!geometric(first.body,second.body))throw p::Error("Worker human ram probe geometry is invalid");const auto armor=world.armor(first.body,second.body,n);auto result=v::object();auto&r=result.object;r["seq"]=v::integer(sequence);r["first_id"]=v::integer(first.body.id);r["second_id"]=v::integer(second.body.id);r["available"]=v::boolean(bool(armor));if(armor){for(auto value:armor->armor)if(!std::isfinite(value)||value<=0.)throw p::Error("Worker human ram native armor is invalid");r["armor_first"]=v::number(armor->armor[0]);r["armor_second"]=v::number(armor->armor[1]);if(!armor->tracks[0].empty()||!armor->tracks[1].empty()){r["tracks_first"]=v::strings(armor->tracks[0]);r["tracks_second"]=v::strings(armor->tracks[1]);}}out.array.push_back(std::move(result));
    }catch(const std::exception&e){if(errors)errors->push_back(e.what());PyErr_Clear();}
    return out;}catch(const std::exception&e){if(errors){errors->push_back(e.what());PyErr_Clear();return v::array();}throw;}}
}
