#include <Python.h>
#include "stock_native_bot_planning_snapshot.hpp"
#include "stock_native_bot_descriptor.hpp"
#include "stock_native_bot_motion_snapshot.hpp"
#include "stock_native_bot_parking_snapshot.hpp"
#include "stock_native_bot_python.hpp"
#include <algorithm>
#include <cctype>
#include <cmath>
#include <limits>
#include <stdexcept>
#include <sstream>
#include <iomanip>

namespace stock_native_bot_planning {
namespace {
namespace nav=stock_native_bot_navigation;
namespace plan=stock_native_bot_planner;
namespace strategy=stock_native_bot_strategy;
namespace p=stock_native_bot_python;
using Point=nav::Point;
struct Invalid:std::runtime_error {explicit Invalid(const std::string&s):std::runtime_error(s){}};
// All references belong to this cold read and are released before it returns.
struct Read {
    std::vector<PyObject*> owned;
    ~Read(){for(auto*p:owned)Py_DECREF(p);}
    PyObject* own(PyObject*p){if(!p)throw Invalid("Planning snapshot Python projection failed");owned.push_back(p);return p;}
    PyObject* get(PyObject*p,const char*name,bool required=false){
        PyObject*v=nullptr;if(p&&p!=Py_None){if(PyDict_Check(p))v=PyDict_GetItemString(p,name);else{v=PyObject_GetAttrString(p,name);if(v)owned.push_back(v);else if(PyErr_ExceptionMatches(PyExc_AttributeError))PyErr_Clear();else if(PyErr_Occurred())throw Invalid("Planning snapshot attribute read failed");}}
        if(!v&&required)throw Invalid(std::string("Planning snapshot missing field: ")+name);return v?v:Py_None;
    }
    PyObject* item(PyObject*p,Py_ssize_t i){return own(PySequence_GetItem(p,i));}
    Py_ssize_t size(PyObject*p){if(!p||p==Py_None)return 0;auto n=PySequence_Size(p);if(n<0)throw Invalid("Planning snapshot requires a sequence");return n;}
};
std::int64_t integer(PyObject*p,std::int64_t fallback=0){if(!p||p==Py_None)return fallback;if(PyInt_Check(p))return PyInt_AS_LONG(p);if(!PyLong_Check(p))throw Invalid("Planning snapshot integer is invalid");const auto v=PyLong_AsLongLong(p);if(PyErr_Occurred())throw Invalid("Planning snapshot integer is outside its domain");return v;}
std::uint64_t unsignedInteger(PyObject*p){if(!p||p==Py_None)return 0;if(PyInt_Check(p)){const long v=PyInt_AS_LONG(p);if(v<0)throw Invalid("Planning snapshot counter is negative");return std::uint64_t(v);}if(!PyLong_Check(p))throw Invalid("Planning snapshot counter is invalid");const auto v=PyLong_AsUnsignedLongLong(p);if(PyErr_Occurred())throw Invalid("Planning snapshot counter is outside its domain");return v;}
double number(PyObject*p,double fallback=0.){if(!p||p==Py_None)return fallback;double v=PyFloat_Check(p)?PyFloat_AS_DOUBLE(p):double(integer(p));if(!std::isfinite(v))throw Invalid("Planning snapshot numeric value is nonfinite");return v;}
bool boolean(PyObject*p,bool fallback=false){if(!p||p==Py_None)return fallback;if(p==Py_True)return true;if(p==Py_False)return false;return integer(p)!=0;}
std::string string(PyObject*p,const std::string&fallback={}){if(!p||p==Py_None)return fallback;if(PyString_Check(p))return std::string(PyString_AS_STRING(p),std::size_t(PyString_GET_SIZE(p)));if(!PyUnicode_Check(p))throw Invalid("Planning snapshot string is invalid");auto*s=PyUnicode_AsUTF8String(p);if(!s)throw Invalid("Planning snapshot UTF-8 projection failed");std::string result(PyString_AS_STRING(s),std::size_t(PyString_GET_SIZE(s)));Py_DECREF(s);return result;}
template<class Function>void dictionary(PyObject*p,Function function){if(!p||p==Py_None)return;if(!PyDict_Check(p))throw Invalid("Planning snapshot mapping is invalid");Py_ssize_t pos=0;PyObject*k,*v;while(PyDict_Next(p,&pos,&k,&v))function(k,v);}
Point point(Read&r,PyObject*p){if(!p||p==Py_None)throw Invalid("Planning snapshot point is missing");if(PyDict_Check(p))return {number(r.get(p,"x")),number(r.get(p,"y")),number(r.get(p,"z"))};
    if(!PyTuple_Check(p)&&!PyList_Check(p)){auto x=r.get(p,"x"),y=r.get(p,"y"),z=r.get(p,"z");if(x!=Py_None&&y!=Py_None&&z!=Py_None)return {number(x),number(y),number(z)};}
    if(r.size(p)!=3)throw Invalid("Planning snapshot point width is invalid");return {number(r.item(p,0)),number(r.item(p,1)),number(r.item(p,2))};}
Point xz(Read&r,PyObject*p){if(r.size(p)==3)return point(r,p);if(r.size(p)!=2)throw Invalid("Planning snapshot XZ point width is invalid");return {number(r.item(p,0)),0.,number(r.item(p,1))};}
nav::Cell cell(Read&r,PyObject*p){if(r.size(p)!=2)throw Invalid("Planning snapshot cell width is invalid");return {int(integer(r.item(p,0))),int(integer(r.item(p,1)))};}
template<class T,class Parse>std::optional<T> optional(Read&r,PyObject*p,const char*name,Parse parse){auto*v=r.get(p,name);return v==Py_None?std::nullopt:std::optional<T>(parse(v));}
void optionalNumber(Read&r,PyObject*p,const char*name,plan::Optional<double>&out){auto*v=r.get(p,name);if(v!=Py_None)out.set(number(v));}
void optionalPoint(Read&r,PyObject*p,const char*name,plan::Optional<Point>&out,bool two=false){auto*v=r.get(p,name);if(v!=Py_None)out.set(two?xz(r,v):point(r,v));}
void optionalId(Read&r,PyObject*p,const char*name,plan::Optional<std::int64_t>&out){auto*v=r.get(p,name);if(v!=Py_None)out.set(integer(v));}
std::string signatureNumber(double value){std::ostringstream out;out<<std::hexfloat<<value;return "n"+out.str()+";";}
std::string signatureValue(Read&r,PyObject*p){
    if(!p||p==Py_None)return "-";
    if(PyFloat_Check(p)||PyInt_Check(p)||PyLong_Check(p))return signatureNumber(number(p));
    if(PyString_Check(p)||PyUnicode_Check(p)){const auto value=string(p);return "s"+std::to_string(value.size())+":"+value;}
    if(PyList_Check(p)||PyTuple_Check(p)){std::string out=PyList_Check(p)?"l":"t";out+=std::to_string(r.size(p))+":";
        for(Py_ssize_t i=0;i<r.size(p);++i)out+=signatureValue(r,r.item(p,i));return out;}
    if(PyDict_Check(p)){std::map<std::string,PyObject*> values;dictionary(p,[&](PyObject*k,PyObject*v){values[string(k)]=v;});
        std::string out="d"+std::to_string(values.size())+":";for(const auto&v:values)out+=std::to_string(v.first.size())+":"+v.first+signatureValue(r,v.second);return out;}
    throw Invalid("Server order contains a non-wire value");
}
std::string orderSignature(Read&r,PyObject*p){
    std::map<std::string,std::string> values;const bool target=r.get(p,"target_id")!=Py_None;
    dictionary(p,[&](PyObject*k,PyObject*v){const auto name=string(k);
        if(target&&(name=="aim_position"||(name=="face_position"&&!boolean(r.get(p,"stable_hull_face")))||(name=="move_position"&&string(r.get(p,"combat_mode"))=="advance_contact")))return;
        if((name=="aim_position"||name=="face_position"||name=="move_position"||name=="route_anchor")&&(PyDict_Check(v)||PyTuple_Check(v)||PyList_Check(v))){
            std::string value="t3:";for(double coordinate:point(r,v))value+=signatureNumber(coordinate);values[name]=value;}
        else values[name]=signatureValue(r,v);});
    std::string out;for(const auto&v:values)out+=std::to_string(v.first.size())+":"+v.first+v.second;return out;
}
StrategicOrder order(Read&r,PyObject*p){StrategicOrder out;out.signature=orderSignature(r,p);auto&d=out.drive;auto&n=out.navigation;
    optionalPoint(r,p,"aim_position",d.aim);optionalPoint(r,p,"move_position",d.move);optionalPoint(r,p,"face_position",d.face);
    optionalNumber(r,p,"throttle_override",d.throttle);optionalNumber(r,p,"arrival_radius",d.arrival_radius);optionalNumber(r,p,"parking_heading",d.parking_heading);optionalNumber(r,p,"hull_angle_degrees",d.hull_angle_degrees);optionalId(r,p,"target_id",d.target);
    d.target_kind=string(r.get(p,"target_kind"));d.stable_hull_face=boolean(r.get(p,"stable_hull_face"));d.mode=string(r.get(p,"combat_mode"),"route");d.parking_phase=string(r.get(p,"parking_phase"));d.fire_allowed=boolean(r.get(p,"fire_allowed"));d.shell_index=int(integer(r.get(p,"shell_index")));d.fire_range=number(r.get(p,"fire_range"));
    n.mode=d.mode;n.parking_phase=d.parking_phase;n.route_id=string(r.get(p,"route_id"),"direct");n.defense_base=string(r.get(p,"defense_base_id"),"own_base");n.team_command=string(r.get(p,"team_command_id"));
    if(d.throttle.present)n.throttle=d.throttle.value;if(d.target.present)n.target=d.target.value;
    n.route_index=optional<int>(r,p,"route_index",[](PyObject*v){return int(integer(v));});n.route_join=optional<bool>(r,p,"route_join",[](PyObject*v){return boolean(v);});n.route_anchor=optional<Point>(r,p,"route_anchor",[&](PyObject*v){return point(r,v);});
    auto*b=r.get(p,"move_area_bounds");if(b!=Py_None){if(r.size(b)!=4)throw Invalid("Planning move area width is invalid");std::array<double,4> bounds;for(int i=0;i<4;++i)bounds[i]=number(r.item(b,i));n.move_area=bounds;}return out;
}
// The round seed's rows are owned Values (the pinned BotRuntime rows the native round start built).
using SeedValue=stock_native_client_state::Value;
using Kind=SeedValue::Kind;
const SeedValue& field(const SeedValue&v,const char*name){return v.resolved().get(name);}
bool present(const SeedValue&v){return v.resolved().kind!=Kind::Null;}
double number(const SeedValue&v,double fallback=0.){const auto&r=v.resolved();if(r.kind==Kind::Null)return fallback;
    if(r.kind==Kind::Integer)return double(r.integer);if(r.kind==Kind::Boolean)return r.boolean?1.:0.;
    if(r.kind!=Kind::Number||!std::isfinite(r.number))throw Invalid("Planning seed numeric value is invalid");return r.number;}
std::int64_t integer(const SeedValue&v,std::int64_t fallback=0){const auto&r=v.resolved();if(r.kind==Kind::Null)return fallback;
    if(r.kind==Kind::Integer)return r.integer;if(r.kind==Kind::Boolean)return r.boolean?1:0;throw Invalid("Planning seed integer is invalid");}
bool boolean(const SeedValue&v,bool fallback=false){const auto&r=v.resolved();if(r.kind==Kind::Null)return fallback;if(r.kind==Kind::Boolean)return r.boolean;return integer(r)!=0;}
std::string string(const SeedValue&v,const std::string&fallback={}){const auto&r=v.resolved();if(r.kind==Kind::Null)return fallback;if(r.kind!=Kind::Text)throw Invalid("Planning seed string is invalid");return r.text;}
const std::vector<SeedValue>& rows(const SeedValue&v){static const std::vector<SeedValue> none;const auto&r=v.resolved();if(r.kind==Kind::Null)return none;
    if(r.kind!=Kind::Array)throw Invalid("Planning seed requires a sequence");return r.array;}
Point point(const SeedValue&v){const auto&r=v.resolved();if(r.kind==Kind::Object)return {number(r.get("x")),number(r.get("y")),number(r.get("z"))};
    const auto&values=rows(r);if(values.size()!=3)throw Invalid("Planning seed point width is invalid");return {number(values[0]),number(values[1]),number(values[2])};}
std::array<double,2> waypoint(const SeedValue&v){
    // The authored/baked route schema is (x, z, hold); the director consumers read the first two.
    const auto&values=rows(v);if(values.size()<2)throw Invalid("Planning route point width is invalid");return {number(values[0]),number(values[1])};}
Point position(const SeedValue&state){return {number(field(state,"x")),number(field(state,"y")),number(field(state,"z"))};}
strategy::Agent agent(const SeedValue&p){strategy::Agent a;a.id=integer(field(p,"id"));a.team=int(integer(field(p,"team")));a.seed=std::uint64_t(integer(field(p,"seed")));
    const auto&profile=field(p,"profile");auto&v=a.profile;v.class_tag=string(field(profile,"class_tag"));v.dominant_role=string(field(profile,"dominant_role"));
    v.desired_range=number(field(profile,"desired_range"));v.fire_range=number(field(profile,"fire_range"));v.armor=number(field(profile,"armor"));
    for(const auto&role:field(profile,"roles").resolved().object)v.roles[role.first]=number(role.second);
    for(const auto&s:rows(field(profile,"shells")))v.shells.push_back({int(integer(field(s,"index"))),string(field(s,"kind")),number(field(s,"penetration")),number(field(s,"damage"))});
    const auto&personality=field(p,"personality");a.personality={number(field(personality,"aggression")),number(field(personality,"caution")),number(field(personality,"initiative")),number(field(personality,"teamwork"))};
    const auto&route=field(p,"route");a.has_route=present(route);if(a.has_route){a.route_id=string(field(route,"id"));for(const auto&w:rows(field(route,"waypoints")))a.waypoints.push_back(waypoint(w));}
    a.route_started=boolean(field(p,"route_started"));a.waypoint_index=int(integer(field(p,"waypoint_index")));a.route_join_index=int(integer(field(p,"route_join_index"),-1));
    if(present(field(p,"position")))a.position=point(field(p,"position"));if(present(field(p,"route_join_anchor")))a.route_join_anchor=point(field(p,"route_join_anchor"));
    if(present(field(p,"target_id")))a.target=integer(field(p,"target_id"));a.health_fraction=number(field(p,"health_fraction"),1.);return a;
}
nav::NavigationActor navigationActor(std::int64_t id,const SeedValue&p,const strategy::Agent*local){nav::NavigationActor a;a.id=id;a.team=int(integer(field(p,"team")));a.slot=int(integer(field(p,"slot")));
    a.alive=boolean(field(p,"alive"),true);a.position=position(p);a.yaw=number(field(p,"yaw"));a.speed=number(field(p,"speed"));a.half_length=number(field(p,"half_length"));a.half_width=number(field(p,"half_width"));
    const auto&shape=rows(field(p,"collision_shape"));if(shape.size()!=4)throw Invalid("Planning actor collision shape is invalid");a.collision_half_width=number(shape[0]);a.collision_half_length=number(shape[1]);
    a.traffic_waiting=boolean(field(p,"traffic_waiting"));if(local){a.local_route_id=local->has_route?local->route_id:"";a.local_route=local->waypoints;a.local_route_index=local->waypoint_index;a.local_route_join=local->route_join_index==local->waypoint_index;}
    const auto&spg=field(p,"_spg_initial");if(present(spg)){nav::InitialSPG v;v.catalog=string(field(spg,"catalog"));v.map=string(field(spg,"map"));v.zone=string(field(spg,"zone"));v.cell=string(field(spg,"cell"));v.point=point(field(spg,"point"));v.clearance=number(field(spg,"clearance"));a.spg=v;}return a;
}
nav::ExactDescriptor descriptor(Read&r,PyObject*d,const SeedValue&source){nav::ExactDescriptor out;out.candidate.present=d!=Py_None;const auto&shape=rows(field(source,"collision_shape"));if(shape.empty())throw Invalid("Planning actor collision shape is invalid");out.candidate.collision_half_width=number(shape[0]);if(d==Py_None)return out;
    const auto bbox=stock_native_bot_descriptor::hullBounds(d);out.hull_minimum=bbox.minimum;out.hull_maximum=bbox.maximum;
    auto*params=r.own(stock_native_bot::readUnmodifiedDrive(d));out.mass=number(r.get(params,"mass",true));out.speed_forward=number(r.get(params,"speedFwd",true));out.speed_reverse=number(r.get(params,"speedBwd",true));out.valid=true;return out;
}
stock_native_bot_parking::Plan parkingPlan(const SeedValue&p){stock_native_bot_parking::Plan out;
    out.catalog=string(field(p,"catalog"));out.map=string(field(p,"map"));out.zone=string(field(p,"zone"));out.cell=string(field(p,"cell"));out.source=string(field(p,"source"));out.vehicle=string(field(p,"vehicle"));out.side=string(field(p,"side"));
    out.point=point(field(p,"point"));out.face=point(field(p,"face"));out.radius=number(field(p,"radius"));out.clearance=number(field(p,"clearance"));return out;
}
stock_native_bot_perception::Actor perception(Read&r,std::int64_t id,const SeedValue&state,PyObject*d,int crew_level){stock_native_bot_perception::Actor out;auto&config=out.config;config.key={offline_simulation::ActorKind::Bot,id};out.sample.key=config.key;out.identity=std::uint64_t(id);out.target_available=true;
    auto*factors=r.own(stock_native_bot_descriptor::defaultCrewFactors(d,crew_level));
    const auto numeric=[&](PyObject*source,const char*name,double fallback){try{return number(r.get(source,name),fallback);}catch(const std::exception&){PyErr_Clear();return fallback;}};
    auto*misc=r.get(d,"miscAttrs"),*type=r.get(d,"type"),*turret=r.get(d,"turret"),*radio=r.get(d,"radio"),*devices=r.get(d,"optionalDevices");
    double optics=numeric(misc,"circularVisionRadiusFactor",1.);if(optics==0.)optics=1.;double binocular=1.,binocular_delay=3.,net_delay=3.;bool has_binocular=false,has_net=false;
    for(Py_ssize_t i=0;i<r.size(devices);++i){auto*device=r.item(devices,i);if(device==Py_None)continue;auto*name=r.get(device,"name");if(name==Py_None||PyObject_IsTrue(name)==0)name=r.get(r.get(device,"descriptor"),"name");if(name==Py_None||PyObject_IsTrue(name)==0)name=device;auto text=string(r.own(PyObject_Str(name)));std::transform(text.begin(),text.end(),text.begin(),[](unsigned char c){return char(std::tolower(c));});
        if(text.find("stereoscope")!=std::string::npos){const double factor=numeric(device,"circularVisionRadiusFactor",0.);if(factor>0.)binocular=std::max(binocular,factor/std::max(optics,1.e-6));has_binocular=true;binocular_delay=numeric(device,"activateWhenStillSec",3.);}
        else if(text.find("camouflagenet")!=std::string::npos){has_net=true;net_delay=numeric(device,"activateWhenStillSec",3.);}}
    const double vision=numeric(factors,"circularVisionRadius",1.)/std::max(has_binocular?binocular:1.,1.e-6),camouflage=numeric(factors,"camouflage",1.);
    std::array<double,2> base{};bool native_base=false;auto*compute=r.get(d,"computeBaseInvisibility");if(compute!=Py_None&&PyCallable_Check(compute)){auto*factor=r.own(PyFloat_FromDouble(camouflage));auto*result=PyObject_CallFunctionObjArgs(compute,factor,Py_None,nullptr);if(result){r.own(result);try{if((PyList_Check(result)||PyTuple_Check(result))&&r.size(result)>=2){base={number(r.item(result,0)),number(r.item(result,1))};native_base=true;}}catch(const std::exception&){PyErr_Clear();}}else PyErr_Clear();}
    if(!native_base){auto*values=r.get(type,"invisibility");if((PyList_Check(values)||PyTuple_Check(values))&&r.size(values)>=2){const double factor=std::max(0.,camouflage)*std::max(0.,numeric(misc,"invisibilityFactor",1.));base={std::max(0.,number(r.item(values,0)))*factor,std::max(0.,number(r.item(values,1)))*factor};}}
    std::array<double,2> moving={0.,1.},still={0.,1.};auto*aspects=r.get(factors,"_aspects"),*invisibility=r.get(factors,"invisibility");for(int side=0;side<2;++side){try{auto*aspect=r.item(aspects,side),*values=r.own(PyObject_GetItem(invisibility,aspect));auto pair=std::array<double,2>{number(r.item(values,0)),number(r.item(values,1))};(side?still:moving)=pair;}catch(const std::exception&){PyErr_Clear();}}
    config.team=int(integer(field(state,"team")));config.slot=int(integer(field(state,"slot")));config.half_length=3.5;config.half_width=1.7;
    const double view=std::max(50.,numeric(turret,"circularVisionRadius",330.))*std::max(0.,numeric(misc,"circularVisionRadiusFactor",1.))*std::max(0.,vision);
    config.view_moving=std::max(50.,view);config.view_still=has_binocular?std::max(50.,view*std::max(1.,binocular)):config.view_moving;config.view_delay=has_binocular?binocular_delay:-1.;
    config.camo_still=base[1];config.camo_moving=base[0];config.camo_add=moving[0];config.camo_net_add=still[0]-moving[0];config.camo_multiplier=moving[1];config.camo_net_multiplier=still[1];
    config.camo_shot=std::max(0.,std::min(1.,numeric(r.get(d,"gun"),"invisibilityFactorAtShot",1.)));config.camo_net_delay=has_net?net_delay:-1.;config.radio=std::max(0.,numeric(factors,"radio/distance",1.)*numeric(radio,"distance",0.));config.relay=0.;
    auto*wire=r.own(Py_BuildValue("(sL)","bot",static_cast<PY_LONG_LONG>(id)));const auto hash=PyObject_Hash(wire);if(hash==-1&&PyErr_Occurred())throw Invalid("Planning visibility key hash failed");config.python_hash=hash;
    auto&sample=out.sample;sample.alive=boolean(field(state,"alive"),true);sample.last_effort=false;sample.designated=false;sample.fire_sequence=integer(field(state,"fire_seq"),-1);sample.pose.position=position(state);sample.pose.mask=1|2|4|8|(1u<<9);
    const char*names[]={"x","y","z","yaw","pitch","roll","aim_yaw","turret_yaw","gun_pitch","speed"};for(int i=0;i<10;++i){const auto&v=field(state,names[i]);sample.pose.values[i]=number(v);if(present(v))sample.pose.mask|=1u<<i;}
    const auto&velocity=field(state,"velocity");if(present(velocity)){const auto v=point(velocity);for(int i=0;i<3;++i)sample.pose.values[10+i]=v[i];sample.pose.mask|=1u<<10;}
    auto&pose=out.observer_pose;pose.position=sample.pose.position;pose.yaw=sample.pose.values[3];pose.pitch=sample.pose.values[4];pose.roll=sample.pose.values[5];const auto&relative=field(state,"turret_yaw");pose.relative_turret_yaw=present(relative)?number(relative):number(field(state,"aim_yaw"),pose.yaw)-pose.yaw;out.target_pose=pose;
    auto points=stock_native_bot_descriptor::checkpoints(d);if(points){out.checkpoints.available=true;out.checkpoints.points=*points;}
    auto*chassis=r.get(d,"chassis"),*hull=r.get(d,"hull"),*gun=r.get(d,"gun");auto*hp=r.get(chassis,"hullPosition"),*turrets=r.get(hull,"turretPositions");if(hp!=Py_None&&turrets!=Py_None&&r.size(turrets)){auto a=point(r,hp),b=point(r,r.item(turrets,0));out.checkpoints.turret_mount_available=true;for(int i=0;i<3;++i)out.checkpoints.turret_mount[i]=a[i]+b[i];}
    auto*yaw=r.get(gun,"staticTurretYaw");if(yaw!=Py_None){out.checkpoints.static_turret_yaw_present=true;out.checkpoints.static_turret_yaw=number(yaw);}return out;
}
}
namespace {
offline_navigation::MapInput mapInput(const stock_native_bot::RoundSeed&seed){
    // TerrainGrid._install_baked_graph over the round's validated graph and its shore-classified hazards.
    Read r;PyObject*graph=seed.graph.get();if(!graph||!PyDict_Check(graph))throw Invalid("Native planning requires the installed reviewed baked graph");
    if(string(r.get(graph,"format"))!="offline-lan-0922-navgraph"||integer(r.get(graph,"version"),-1)!=2)throw Invalid("unsupported baked navigation graph");
    offline_navigation::MapInput m;auto*origin=r.get(graph,"origin",true);if(r.size(origin)!=2)throw Invalid("Planning grid origin is invalid");m.origin_x=number(r.item(origin,0));m.origin_z=number(r.item(origin,1));
    m.cell_size=std::max(1.,number(r.get(graph,"cell_size")));m.width=int(integer(r.get(graph,"width")));m.height=int(integer(r.get(graph,"height")));
    if(m.width<=0||m.height<=0||std::uint64_t(m.width)*std::uint64_t(m.height)>100000000ULL)throw Invalid("Planning grid dimensions are invalid");const auto count=std::size_t(m.width)*std::size_t(m.height);
    auto*heights=r.get(graph,"heights_mm",true),*links=r.get(graph,"links",true);if(std::size_t(r.size(heights))!=count||std::size_t(r.size(links))!=count||seed.hazards.size()!=count)throw Invalid("Planning grid column widths differ");
    m.heights.reserve(count);m.links.reserve(count);
    p::Ref fast_heights(PySequence_Fast(heights,"Planning grid heights")),fast_links(PySequence_Fast(links,"Planning grid links"));
    for(std::size_t i=0;i<count;++i){auto*h=PySequence_Fast_GET_ITEM(fast_heights.get(),Py_ssize_t(i));m.heights.push_back(h==Py_None?std::numeric_limits<double>::quiet_NaN():number(h)/1000.);m.links.push_back(unsigned(integer(PySequence_Fast_GET_ITEM(fast_links.get(),Py_ssize_t(i)))));}
    m.hazards=seed.hazards;
    auto*bounds=r.get(graph,"bounds");if(r.size(bounds)){if(r.size(bounds)!=4)throw Invalid("Planning grid bounds width is invalid");m.has_bounds=true;for(int i=0;i<4;++i)m.bounds[i]=number(r.item(bounds,i));}
    auto*bake=r.get(graph,"bake");m.max_grade=std::max(.05,PyDict_Check(bake)?number(r.get(bake,"max_grade"),.30):.30);m.heuristic_weight=1.70;return m;
}
}
bool readMapInput(const stock_native_bot::RoundSeed&seed,offline_navigation::MapInput&result){try{result=mapInput(seed);return true;}catch(const std::exception&e){if(!PyErr_Occurred())PyErr_SetString(PyExc_ValueError,e.what());return false;}}
bool readNavigationMode(const stock_native_client_state::Value&state,PyObject*value,nav::ExactDescriptor&result){
    try{Read r;result=descriptor(r,value,state);return true;
    }catch(const std::exception&e){if(!PyErr_Occurred())PyErr_SetString(PyExc_ValueError,e.what());return false;}}
bool readPerceptionMode(const stock_native_bot::BotSeed&bot,PyObject*descriptor,stock_native_bot_perception::Actor&result){try{Read r;const auto id=integer(field(bot.state,"id"));auto value=perception(r,id,bot.state,descriptor,bot.crew_level);result=std::move(value);return true;}catch(const std::exception&e){if(!PyErr_Occurred())PyErr_SetString(PyExc_ValueError,e.what());return false;}}
bool readPlanningOrders(PyObject*source,std::map<std::int64_t,StrategicOrder>&result){try{Read r;std::map<std::int64_t,StrategicOrder> next;dictionary(source,[&](PyObject*k,PyObject*v){next[integer(k)]=order(r,v);});result=std::move(next);return true;}catch(const std::exception&e){if(!PyErr_Occurred())PyErr_SetString(PyExc_ValueError,e.what());return false;}}
bool readPlanningSnapshot(const stock_native_bot::RoundSeed&seed,const stock_native_bot::ServerOrders&orders,PlanningSnapshot&result){try{Read r;PlanningSnapshot out;out.order_revision=orders.revision;
    out.map=mapInput(seed);out.grade_up=out.grade_down=std::tan(25.*3.14159265358979323846/180.);
    auto*bake=r.get(seed.graph.get(),"bake");if(PyDict_Check(bake)){out.bake.present=true;out.bake.vehicle_half_width=number(r.get(bake,"vehicle_half_width"));auto*radii=r.get(bake,"edge_clearance_radii");for(Py_ssize_t i=0;i<r.size(radii);++i)out.bake.maximum_clearance_radius=std::max(out.bake.maximum_clearance_radius,number(r.item(radii,i)));}
    auto*directions=r.get(seed.graph.get(),"directions",true);if(r.size(directions)!=8)throw Invalid("Planning parking directions width is invalid");std::set<nav::Cell> unique;
    for(int i=0;i<8;++i){auto d=cell(r,r.item(directions,i));if((d[0]==0&&d[1]==0)||std::abs(d[0])>1||std::abs(d[1])>1||!unique.insert(d).second)throw Invalid("Planning parking direction is invalid");out.directions[i]=d;}
    for(const auto&team:seed.routes){auto&routes=out.routes[team.first];for(const auto&route:team.second){std::vector<std::array<double,2>> path;for(const auto&w:rows(field(route,"waypoints")))path.push_back(waypoint(w));routes[string(field(route,"id"))]=std::move(path);}}
    for(const auto&row:seed.agents){auto a=agent(row.second);if(a.id!=row.first)throw Invalid("Planning director actor identity mismatch");out.director.agents[a.id]=std::move(a);}
    out.director.restoreContacts({});
    // The accepted orders, parsed exactly as a later snapshot's order replacement is.
    p::Ref mapping(PyDict_New());for(const auto&row:orders.orders){p::Ref key(PyLong_FromLongLong(row.first));p::Ref value(stock_native_client_state::write(row.second));if(PyDict_SetItem(mapping.get(),key.get(),value.get())<0)throw Invalid("Planning order allocation failed");}
    dictionary(mapping.get(),[&](PyObject*k,PyObject*v){out.orders[integer(k)]=order(r,v);});out.order_tokens=orders.tokens;
    for(const auto&row:seed.bots){const auto id=row.first;const auto&bot=row.second;auto local=out.director.agents.find(id);if(local==out.director.agents.end())throw Invalid("Planning actor has no registered strategy profile");
        PyObject*d=stock_native_bot::activeDescriptor(bot);out.actors[id]=navigationActor(id,bot.state,&local->second);const auto&initial=field(bot.state,"_spg_initial");if(present(initial))out.initial_parking[id]=parkingPlan(initial);
        const auto&origin=field(bot.state,"_spg_selection_origin");if(present(origin))out.parking_origins[id]=point(origin);out.descriptors[id]=descriptor(r,d,bot.state);out.perception.push_back(perception(r,id,bot.state,d,bot.crew_level));}
    if(!out.initial_parking.empty()&&!stock_native_bot_parking::readCatalog(seed,out.map,out.directions,out.parking))throw Invalid("Native parking catalog installation failed");
    result=std::move(out);return true;
}catch(const std::exception&e){if(!PyErr_Occurred())PyErr_SetString(PyExc_ValueError,e.what());return false;}}
}
