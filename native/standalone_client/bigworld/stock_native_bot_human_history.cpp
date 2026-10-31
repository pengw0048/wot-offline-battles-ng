#include "stock_native_bot_human_history.hpp"
#include "stock_native_battle_world.hpp"
#include <cmath>
#include <string>

namespace stock_native_bot {
namespace {
using stock_native_client_state::Value;
using Kind=Value::Kind;
const Value* field(const Value& d,const char* k){const auto& r=d.resolved();if(r.kind!=Kind::Object)return nullptr;auto i=r.object.find(k);return i==r.object.end()?nullptr:&i->second.resolved();}
// Only plain numbers are accepted on the trusted wire.
bool integer(const Value* p,long long& out){if(!p)return false;
 if(p->kind==Kind::Integer){if(p->unsigned_integer)return false;out=p->integer;return true;}
 if(p->kind==Kind::Number){const double d=p->number;if(std::isfinite(d)&&d>=-9007199254740991.&&d<=9007199254740991.){out=static_cast<long long>(d);return true;}}return false;}
// Decoded wire text is unicode, which this truth test never counted.
bool truth(const Value* p){if(!p)return false;switch(p->kind){case Kind::Boolean:return p->boolean;case Kind::Integer:return p->integer!=0;
 case Kind::Number:return p->number!=0.;case Kind::Array:return !p->array.empty();case Kind::Object:return !p->object.empty();default:return false;}}
}
Value prepareHumanInput(const Value& raw,const native_battle::Battle& battle,const std::map<std::int64_t,Value>& profiles){
 Value output;output.kind=Kind::Array;const auto& list=raw.resolved();if(list.kind!=Kind::Array)return output;
 for(const auto& entry:list.array){const auto& player=entry.resolved();if(player.kind!=Kind::Object)continue;const auto* participating=field(player,"participating");
  if(!participating||participating->kind!=Kind::Boolean||!participating->boolean||!truth(field(player,"world_pose")))continue;long long id;if(!integer(field(player,"id"),id)||id<=0)continue;
  Value copy=player;Value decorated;decorated.kind=Kind::Array;const Value* contacts=field(player,"ram_contacts");Value single;
  if(!contacts||contacts->kind!=Kind::Array){const auto* legacy=field(player,"ram_contact");contacts=nullptr;if(legacy&&legacy->kind==Kind::Object){single.kind=Kind::Array;single.array.push_back(*legacy);contacts=&single;}}
  if(contacts)for(const auto& item:contacts->array){const auto& rawreceipt=item.resolved();if(rawreceipt.kind!=Kind::Object)continue;Value receipt=rawreceipt;long long bid=0,revision=0,stamp=0;
   const bool valid=integer(field(receipt,"bot_id"),bid)&&integer(field(receipt,"bot_state_revision"),revision)&&integer(field(receipt,"presentation_time_us"),stamp);const auto* pinned=field(receipt,"ram_bot_state");long long pid=0;Value botstate;
   if(valid&&pinned&&pinned->kind==Kind::Object&&integer(field(*pinned,"id"),pid)&&pid==bid){botstate.kind=Kind::Object;const auto profile=profiles.find(bid);
    if(profile!=profiles.end())for(const char*k:{"mass","collision_shape","ram_profile","vehicle","team"}){if(const auto* v=field(profile->second,k))botstate.object[k]=*v;}
    for(const auto& v:pinned->object)botstate.object[v.first]=v.second;if(!botstate.object.count("mass")||!botstate.object.count("collision_shape")||!botstate.object.count("ram_profile"))botstate=Value();}
   else if(valid){if(auto body=native_battle::world::ramBotState(battle,bid,revision,stamp))botstate=std::move(*body);}
   if(botstate.kind!=Kind::Null)receipt.object["_ram_contact_bot_state"]=std::move(botstate);decorated.array.push_back(std::move(receipt));}
  copy.object["ram_contacts"]=std::move(decorated);output.array.push_back(std::move(copy));}
 return output;
}
}
