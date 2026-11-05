#include "room_strategy_data.hpp"
#include <cctype>

namespace wot::room::strategy {
namespace {
bool fields(const Value& value,std::initializer_list<const char*> names) {
    if(!dict(value)||value.resolved().object.size()!=names.size())return false;for(const auto* name:names)if(!has(value,name))return false;return true;
}
bool numeric(const Value& v,bool strings) {
    const auto& value=v.resolved();if(value.kind==Value::Kind::Boolean)return false;
    return (value.kind==Value::Kind::Integer||value.kind==Value::Kind::Number||(strings&&value.kind==Value::Kind::Text))&&std::isfinite(value.numeric(std::numeric_limits<double>::quiet_NaN()));
}
std::size_t characterCount(const std::string& value) {std::size_t count=0;for(unsigned char c:value)if((c&0xc0)!=0x80)++count;return count;}
}
Value canonicalSpgPlan(const Value& raw,const Value& resources,const std::string& map,const Value& vehicle,int side,bool enforce_identity) {
    if(!fields(raw,{"schema","catalog","map","mode","side","zone","cell","source","vehicle","point","face","radius","clearance","geometry","fire_validation"}))return {};
    if((raw.get("schema").resolved().kind!=Value::Kind::Integer&&raw.get("schema").resolved().kind!=Value::Kind::Number)||raw.get("schema").numeric()!=1.||s(raw,"mode")!="regular"||s(raw,"geometry")!="baked_checked"||s(raw,"fire_validation")!="runtime_required"||(enforce_identity&&s(raw,"map")!=map)||(!null(vehicle)&&raw.get("vehicle")!=vehicle))return {};
    const auto name=s(raw,"map"),vehicle_name=s(raw,"vehicle");if(vehicle_name.empty()||characterCount(vehicle_name)>80)return {};
    const bool manual=s(raw,"source")=="launcher_manual_v1";
    const auto& catalog=resources.get("spg_catalog");const auto& entry=catalog.get("maps").get(name);const auto& tactics=resources.get("tactics");const auto& settings=tactics.get("maps").get(name);
    Value zone;Value bounds;
    if(manual) {
        if(!has(resources,"tactics_digest"))throw std::logic_error("Missing cold manual SPG tactics identity");
        if(raw.get("catalog")!=resources.get("tactics_digest")||s(raw,"cell")!="manual"||!numeric(raw.get("radius"),false)||raw.get("radius").numeric()!=2.)return {};
        for(const auto& row:rows(settings.get("positions")))if(row.get("id")==raw.get("zone")){zone=row;break;}
        if(null(zone)||(enforce_identity&&team(zone)!=side)||s(raw,"side")!="team"+std::to_string(team(zone)))return {};
        if(std::all_of(vehicle_name.begin(),vehicle_name.end(),[](unsigned char c){return std::isspace(c)!=0;})||std::any_of(vehicle_name.begin(),vehicle_name.end(),[](unsigned char c){return c<32;}))return {};
        bounds=resources.get("arena_bounds");if(null(bounds))bounds=resources.get("tactical_map").get("bounds");if(null(bounds))bounds=entry.get("bounds");
    } else {
        if(!has(resources,"spg_catalog"))throw std::logic_error("Missing cold SPG provenance catalog");
        if(raw.get("catalog")!=catalog.get("revision")||null(entry)||(enforce_identity&&(side!=1&&side!=2))||(enforce_identity&&entry.get("spawn_sides").get(std::to_string(side))!=raw.get("side")))return {};
        for(const auto& row:rows(entry.get("zones")))if(row.get("id")==raw.get("zone")){zone=row;break;}
        if(null(zone)||zone.get("source")!=raw.get("source")||zone.get("side")!=raw.get("side")||!contains(zone.get("cells"),raw.get("cell")))return {};bounds=entry.get("bounds");
    }
    const auto& edges=rows(bounds);if(edges.size()!=4)return {};auto result=raw.resolved();
    for(const char* field:{"point","face"}) {
        const auto& p=raw.get(field);if(!fields(p,{"x","y","z"}))return {};auto normalized=object();
        for(const char* axis:{"x","y","z"}){if(!numeric(p.get(axis),!manual))return {};normalized.object[axis]=number(p.get(axis).numeric());}
        if(n(normalized,"x")<edges[0].numeric()||n(normalized,"x")>edges[2].numeric()||n(normalized,"z")<edges[1].numeric()||n(normalized,"z")>edges[3].numeric()||n(normalized,"y")<-1000.||n(normalized,"y")>1000.)return {};
        if(!manual)result.object[field]=normalized;
    }
    if(!numeric(raw.get("radius"),!manual)||!numeric(raw.get("clearance"),!manual))return {};const double radius=raw.get("radius").numeric(),clearance=raw.get("clearance").numeric();if(radius!=2.||clearance<radius||clearance>50.)return {};
    if(manual) {
        const auto p=point(zone.get("point"));if(distance(raw.get("point"),p)+2.>n(zone,"radius")+1.e-5)return {};return result;
    }
    const auto cell=s(raw,"cell");const std::string rows_key="ABCDEFGHJK",columns="1234567890";if(cell.size()!=2||rows_key.find(cell[0])==std::string::npos||columns.find(cell[1])==std::string::npos)return {};
    const double dx=(edges[2].numeric()-edges[0].numeric())/10.,dz=(edges[3].numeric()-edges[1].numeric())/10.;if(dx<=0.||dz<=0.)return {};
    const double left=edges[0].numeric()+double(columns.find(cell[1]))*dx,top=edges[3].numeric()-double(rows_key.find(cell[0]))*dz;const auto& p=result.get("point");
    if(n(p,"x")<left+radius||n(p,"x")>left+dx-radius||n(p,"z")<top-dz+radius||n(p,"z")>top-radius)return {};
    result.object["radius"]=number(radius);result.object["clearance"]=number(clearance);return result;
}
}
