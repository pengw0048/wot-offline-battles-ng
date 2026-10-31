#include "stock_native_player_contacts_python.hpp"
#include "stock_native_bot_value.hpp"
#include <algorithm>
#include <limits>

namespace stock_native_player_contacts {
namespace {
namespace v=stock_native_bot_value;
using stock_native_client_state::Value;
using Kind=Value::Kind;
const std::vector<Value>& rows(const Value* value){if(!value||value->kind!=Kind::Array)throw v::Error("Native player contact sequence invalid");return value->array;}
bool number(const Value* value){return value&&(value->kind==Kind::Integer||value->kind==Kind::Number);}
double scalar(const Value* value){if(!number(value))throw v::Error("Native player contact number must be plain data");return v::scalar(*value);}
ID integer(const Value* value){if(!number(value))throw v::Error("Native player contact integer must be plain data");return v::wide(*value);}
double numeric(const Value& value,const char* key,double fallback=0.,bool required=false){auto raw=v::field(value,key);if(!raw){if(required)throw v::Error("Native player contact field missing");return fallback;}return scalar(raw);}
std::uint32_t nativeID(const Value& value){const auto id=integer(&value.resolved());if(id<0||std::uint64_t(id)>std::numeric_limits<std::uint32_t>::max())throw v::Error("Native destructible identity out of range");return std::uint32_t(id);}
Token token(const Value* value){const auto& list=rows(value);if(list.empty()||list.size()>64)throw v::Error("Native player contact token count invalid");Token out;
    for(const auto& entry:list){const auto& row=rows(&entry.resolved());if(row.size()!=3)throw v::Error("Native player contact token width invalid");
        const auto chunk=nativeID(row[0]),index=nativeID(row[1]);const auto& material=row[2].resolved();
        const auto kind=material.kind==Kind::Null?-1:integer(&material);out.emplace(chunk,index,kind);}
    return out;
}
bool criticalValid(const Value& raw){try{
    auto critical=v::field(raw,"critical");if(!critical||critical->kind==Kind::Null)return true;
    if(critical->kind!=Kind::Object)return false;
    auto devices=v::field(*critical,"devices");if(!devices||devices->kind==Kind::Null)return true;
    for(const auto& entry:rows(devices)){const auto& row=entry.resolved();if(row.kind!=Kind::Object)return false;scalar(v::field(row,"hp"));}
    return true;
}catch(const std::exception&){return false;}}
Descriptor descriptor(const stock_native_bot::HumanDescriptor& human,const Value& raw){Descriptor out;
    out.body_minimum=human.body_minimum;out.body_maximum=human.body_maximum;out.mass=human.contact_mass;
    out.drive_forward=human.contact_drive_forward;out.drive_reverse=human.contact_drive_reverse;out.rotation_speed=human.contact_rotation_speed;
    out.pivot_available=human.pivot_available;out.rotation_around_center=human.rotation_around_center;out.track_center=human.track_center;
    // The fixed source has no traverse crew branch or module stat entry;
    // its factor is one after validating the critical device payload.
    out.traverse_valid=criticalValid(raw);return out;
}
}
bool readPlayerContacts(const Value& source,const std::map<ID,stock_native_bot::HumanDescriptor>& descriptors,
    std::vector<Request>& output,std::uint64_t& rejected){
    try{
        std::vector<Request> result;const auto& players=source.resolved();if(players.kind==Kind::Null){output.clear();return true;}
        for(const auto& entry:rows(&players)){const auto& player=entry.resolved();if(player.kind!=Kind::Object)continue;
            auto participating=v::field(player,"participating"),pose=v::field(player,"world_pose");
            if(!participating||participating->kind!=Kind::Boolean||!participating->boolean||!pose||(pose->kind!=Kind::Boolean&&!number(pose))||!v::flag(*pose))continue;
            auto contacts=v::field(player,"destructible_contacts");if(!contacts||contacts->kind!=Kind::Array)continue;
            for(std::size_t j=0;j<std::min<std::size_t>(16,contacts->array.size());++j){const auto& raw=contacts->array[j].resolved();if(raw.kind!=Kind::Object)continue;
                try{Request request;auto&r=request.receipt;r.player=integer(v::field(player,"id"));if(r.player<=0)continue;r.sequence=integer(v::field(raw,"seq"));r.token=token(v::field(raw,"token"));
                    const char* starts[]={"x","y","z"},*ends[]={"end_x","end_y","end_z"};
                    for(unsigned axis=0;axis<3;++axis){r.start[axis]=numeric(raw,starts[axis],0.,true);r.end[axis]=numeric(raw,ends[axis],0.,true);}
                    r.yaw=numeric(raw,"yaw",0.,true);r.end_yaw=numeric(raw,"end_yaw",0.,true);r.speed=numeric(raw,"speed",0.,true);r.dt=numeric(raw,"dt",0.,true);
                    r.pitch=numeric(raw,"pitch");r.roll=numeric(raw,"roll");const auto frozen=descriptors.find(r.player);
                    if(frozen!=descriptors.end())request.descriptor=descriptor(frozen->second,player);
                    result.push_back(std::move(request));
                }catch(const std::exception&){++rejected;}
            }
        }
        output=std::move(result);return true;
    }catch(const std::exception& e){if(!PyErr_Occurred())PyErr_SetString(PyExc_ValueError,e.what());return false;}
}
Value packPlayerVerdicts(const std::vector<Verdict>& values){
    auto result=v::array();for(const auto& value:values){auto row=v::object(),keys=v::array();auto& r=row.object;
        r["serial"]=v::integer(std::int64_t(value.serial));r["player_id"]=v::integer(value.player);r["seq"]=v::integer(value.sequence);r["accepted"]=v::boolean(value.accepted);
        for(const auto& identity:value.token){auto key=v::array();key.array.push_back(v::integer(std::get<0>(identity)));key.array.push_back(v::integer(std::get<1>(identity)));
            key.array.push_back(std::get<2>(identity)==-1?v::null():v::integer(std::get<2>(identity)));keys.array.push_back(std::move(key));}
        r["token"]=std::move(keys);result.array.push_back(std::move(row));
    }return result;
}
}
