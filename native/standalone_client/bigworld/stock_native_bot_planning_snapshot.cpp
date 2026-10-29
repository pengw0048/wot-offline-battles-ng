#include <Python.h>
#include "stock_native_bot_planning_snapshot.hpp"
#include "stock_native_bot_descriptor.hpp"
#include "stock_native_bot_motion_snapshot.hpp"
#include "stock_native_bot_parking_snapshot.hpp"
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
double waypointNumber(Read&r,PyObject*p){
    if(!p||p==Py_None)throw Invalid("Planning route coordinate is missing");
    if(PyString_Check(p)||PyUnicode_Check(p))return number(r.own(PyFloat_FromString(p,nullptr)));
    return number(p);
}
std::array<double,2> waypoint(Read&r,PyObject*p){
    // The authored/baked route schema is (x, z, hold), not a world XYZ pose.
    // Its exact director consumers read the first two coordinates only.
    if(!PyTuple_Check(p)&&!PyList_Check(p))throw Invalid("Planning route point requires a sequence");
    if(r.size(p)<2)throw Invalid("Planning route point width is invalid");
    return {waypointNumber(r,r.item(p,0)),waypointNumber(r,r.item(p,1))};
}
Point position(Read&r,PyObject*p){auto value=r.get(p,"position");return value==Py_None?point(r,p):point(r,value);}
nav::Cell cell(Read&r,PyObject*p){if(r.size(p)!=2)throw Invalid("Planning snapshot cell width is invalid");return {int(integer(r.item(p,0))),int(integer(r.item(p,1)))};}
nav::Edge edge(Read&r,PyObject*p){if(r.size(p)!=2)throw Invalid("Planning snapshot edge width is invalid");return {cell(r,r.item(p,0)),cell(r,r.item(p,1))};}
template<class T,class Parse>std::optional<T> optional(Read&r,PyObject*p,const char*name,Parse parse){auto*v=r.get(p,name);return v==Py_None?std::nullopt:std::optional<T>(parse(v));}
void optionalNumber(Read&r,PyObject*p,const char*name,plan::Optional<double>&out){auto*v=r.get(p,name);if(v!=Py_None)out.set(number(v));}
void optionalPoint(Read&r,PyObject*p,const char*name,plan::Optional<Point>&out,bool two=false){auto*v=r.get(p,name);if(v!=Py_None)out.set(two?xz(r,v):point(r,v));}
void optionalId(Read&r,PyObject*p,const char*name,plan::Optional<std::int64_t>&out){auto*v=r.get(p,name);if(v!=Py_None)out.set(integer(v));}
void empty(Read&r,PyObject*p,const char*name){auto*v=r.get(p,name);if(v==Py_None)return;const auto size=PyObject_Size(v);if(size<0)throw Invalid("Planning bootstrap state is invalid");if(size)throw Invalid(std::string("Planning bootstrap must precede legacy update: ")+name);}
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
strategy::Contact contact(Read&r,PyObject*p){strategy::Contact c;c.id=integer(r.get(p,"id",true));c.team=int(integer(r.get(p,"team",true)));c.position=point(r,r.get(p,"position",true));
    c.health=number(r.get(p,"health"));c.max_health=number(r.get(p,"max_health"),1.);c.armor=number(r.get(p,"armor"));c.speed=number(r.get(p,"speed"));c.last_seen=number(r.get(p,"last_seen"));c.class_tag=string(r.get(p,"class_tag"),"mediumTank");c.visible=boolean(r.get(p,"visible"));
    auto*ids=r.get(p,"shootable_by_ids");if(ids!=Py_None){c.shootable.emplace();for(Py_ssize_t i=0;i<r.size(ids);++i)c.shootable->insert(integer(r.item(ids,i)));}return c;}
strategy::Agent agent(Read&r,PyObject*p){strategy::Agent a;a.id=integer(r.get(p,"id",true));a.team=int(integer(r.get(p,"team",true)));a.seed=unsignedInteger(r.get(p,"seed",true));
    auto*profile=r.get(p,"profile",true);auto&v=a.profile;v.class_tag=string(r.get(profile,"class_tag",true));v.dominant_role=string(r.get(profile,"dominant_role",true));v.desired_range=number(r.get(profile,"desired_range",true));v.fire_range=number(r.get(profile,"fire_range",true));v.armor=number(r.get(profile,"armor"));
    dictionary(r.get(profile,"roles"),[&](PyObject*k,PyObject*x){v.roles[string(k)]=number(x);});auto*shells=r.get(profile,"shells");for(Py_ssize_t i=0;i<r.size(shells);++i){auto*s=r.item(shells,i);v.shells.push_back({int(integer(r.get(s,"index"))),string(r.get(s,"kind")),number(r.get(s,"penetration")),number(r.get(s,"damage"))});}
    auto*personality=r.get(p,"personality",true);a.personality={number(r.get(personality,"aggression",true)),number(r.get(personality,"caution",true)),number(r.get(personality,"initiative",true)),number(r.get(personality,"teamwork",true))};
    auto*route=r.get(p,"route");a.has_route=route!=Py_None;if(a.has_route){a.route_id=string(r.get(route,"id"));auto*points=r.get(route,"waypoints");for(Py_ssize_t i=0;i<r.size(points);++i){a.waypoints.push_back(waypoint(r,r.item(points,i)));}}
    a.route_started=boolean(r.get(p,"route_started"));a.waypoint_index=int(integer(r.get(p,"waypoint_index")));a.route_join_index=int(integer(r.get(p,"route_join_index"),-1));
    a.position=optional<Point>(r,p,"position",[&](PyObject*v){return point(r,v);});a.route_join_anchor=optional<Point>(r,p,"route_join_anchor",[&](PyObject*v){return point(r,v);});a.target=optional<std::int64_t>(r,p,"target_id",[](PyObject*v){return integer(v);});a.health_fraction=number(r.get(p,"health_fraction"),1.);return a;
}
plan::DriverState driver(Read&r,PyObject*p){plan::DriverState s;s.slot=int(integer(r.get(p,"team_slot")));s.last_position=xz(r,r.get(p,"last_position",true));
#define NUMBER(member,key) s.member=number(r.get(p,key),s.member)
    NUMBER(stuck_time,"stuck_time");NUMBER(recovery_time,"recovery_time");NUMBER(recovery_side,"recovery_side");NUMBER(clock,"clock");NUMBER(steering_age,"steering_age");NUMBER(plan_age,"plan_age");NUMBER(escape_side,"escape_side");NUMBER(escape_until,"escape_side_until");NUMBER(traffic_wait_time,"traffic_wait_time");NUMBER(last_step,"last_step");
#undef NUMBER
    s.recovery_count=int(integer(r.get(p,"recovery_count")));s.traffic_waiting=boolean(r.get(p,"traffic_waiting"));s.avoiding=string(r.get(p,"steering_reason"),"route")!="route";s.native_translation_stalled=boolean(r.get(p,"native_translation_stalled"));
    optionalNumber(r,p,"steering_yaw",s.steering_yaw);optionalNumber(r,p,"last_desired_yaw",s.desired_yaw);optionalNumber(r,p,"heading_progress_yaw",s.progress_yaw);optionalNumber(r,p,"best_heading_error",s.best_heading_error);optionalNumber(r,p,"last_clear_yaw",s.last_clear_yaw);optionalNumber(r,p,"translation_progress_at",s.translation_progress_at);
    optionalPoint(r,p,"alignment_target",s.alignment_target);optionalPoint(r,p,"braking_target",s.braking_target,true);
    dictionary(r.get(p,"failed_yaws"),[&](PyObject*k,PyObject*v){s.failed_yaws[int(integer(k))]=number(v);});dictionary(r.get(p,"real_failed_yaws"),[&](PyObject*k,PyObject*v){s.real_failed_yaws[int(integer(k))]=number(v);});
    for(const auto&pair:{std::make_pair("short_translation_escape",&s.short_escape),std::make_pair("translation_escape",&s.translation_escape)}){auto*v=r.get(p,pair.first);if(v!=Py_None){plan::Escape e;e.start=point(r,r.get(v,"start",true));e.yaw=number(r.get(v,"yaw"));e.sign=number(r.get(v,"sign"),1.);e.distance=number(r.get(v,"distance"));e.until=number(r.get(v,"until"));pair.second->set(e);}}
    auto*o=r.get(p,"objective_progress");if(o!=Py_None){plan::Objective v;v.goal=point(r,r.get(o,"goal",true));v.best=number(r.get(o,"best"));v.at=number(r.get(o,"at"));optionalNumber(r,o,"alignment_yaw",v.alignment_yaw);optionalNumber(r,o,"alignment_best",v.alignment_best);s.objective.set(v);}
    for(const auto&pair:{std::make_pair("recovery_pocket",&s.pocket),std::make_pair("recovery_replan_episode",&s.replan)}){auto*v=r.get(p,pair.first);if(v!=Py_None){plan::Episode e;e.origin=point(r,r.get(v,"origin",true));auto*g=r.get(v,"goal");if(g!=Py_None)e.goal=point(r,g);e.at=number(r.get(v,"at"));e.issued_at=number(r.get(v,"issued_at"));e.count=int(integer(r.get(v,"count")));e.issued_count=int(integer(r.get(v,"issued_count")));e.issued=boolean(r.get(v,"issued"));pair.second->set(e);}}
    auto*w=r.get(p,"navigation_wait");if(w!=Py_None){plan::NavigationWait v;v.age=number(r.get(w,"age"));v.elapsed=number(r.get(w,"elapsed"));v.active=boolean(r.get(w,"active"));v.completed=boolean(r.get(w,"completed"));auto*o=r.get(w,"origin");if(o!=Py_None)v.origin=point(r,o);s.navigation_wait.set(v);}return s;
}
void grid(Read&r,PyObject*p,PyObject*baked,PlanningSnapshot&out){if(!boolean(r.get(p,"prebaked")))throw Invalid("Native planning requires the installed reviewed baked graph");
    auto&m=out.map;auto*origin=r.get(p,"_baked_origin",true);if(r.size(origin)!=2)throw Invalid("Planning grid origin is invalid");m.origin_x=number(r.item(origin,0));m.origin_z=number(r.item(origin,1));m.cell_size=number(r.get(p,"cell_size",true));m.width=int(integer(r.get(p,"_baked_width",true)));m.height=int(integer(r.get(p,"_baked_height",true)));
    if(m.width<=0||m.height<=0||m.cell_size<1.||std::uint64_t(m.width)*std::uint64_t(m.height)>100000000ULL)throw Invalid("Planning grid dimensions are invalid");const auto count=std::size_t(m.width)*std::size_t(m.height);
    auto*heights=r.get(p,"_baked_heights",true),*links=r.get(p,"_baked_links",true),*hazards=r.get(p,"_baked_hazards",true);if(std::size_t(r.size(heights))!=count||std::size_t(r.size(links))!=count||std::size_t(r.size(hazards))!=count)throw Invalid("Planning grid column widths differ");
    for(std::size_t i=0;i<count;++i){auto*h=r.item(heights,Py_ssize_t(i));m.heights.push_back(h==Py_None?std::numeric_limits<double>::quiet_NaN():number(h)/1000.);m.links.push_back(unsigned(integer(r.item(links,Py_ssize_t(i)))));m.hazards.push_back(unsigned(integer(r.item(hazards,Py_ssize_t(i)))));}
    auto*bounds=r.get(p,"bounds");if(bounds!=Py_None){if(r.size(bounds)!=4)throw Invalid("Planning grid bounds width is invalid");m.has_bounds=true;for(int i=0;i<4;++i)m.bounds[i]=number(r.item(bounds,i));}
    m.max_grade=number(r.get(p,"_baked_max_grade",true));m.heuristic_weight=number(r.get(p,"heuristic_weight",true));out.grade_up=number(r.get(p,"max_grade_up",true));out.grade_down=number(r.get(p,"max_grade_down",true));
    out.hull_revision=unsignedInteger(r.get(p,"static_hull_revision"));out.proof_revision=unsignedInteger(r.get(p,"_native_proof_revision"));
    dictionary(r.get(p,"_failed_edges"),[&](PyObject*k,PyObject*v){if(r.size(v)!=2)throw Invalid("Planning failed edge state is invalid");out.failed_edges[edge(r,k)]={number(r.item(v,0)),number(r.item(v,1))};});
    dictionary(r.get(p,"_static_hull_edges"),[&](PyObject*k,PyObject*v){out.static_hull_edges[edge(r,k)]=number(v);});
    auto*review=r.get(p,"_native_review_cells");if(review!=Py_None){auto*values=r.own(PySequence_List(review));for(Py_ssize_t i=0;i<r.size(values);++i)out.review_cells.insert(cell(r,r.item(values,i)));}
    auto*bake=r.get(baked,"bake");if(bake!=Py_None){out.bake.present=true;out.bake.vehicle_half_width=number(r.get(bake,"vehicle_half_width"));auto*radii=r.get(bake,"edge_clearance_radii");for(Py_ssize_t i=0;i<r.size(radii);++i)out.bake.maximum_clearance_radius=std::max(out.bake.maximum_clearance_radius,number(r.item(radii,i)));}
}
nav::NavigationActor navigationActor(Read&r,std::int64_t id,PyObject*p,const strategy::Agent*local){nav::NavigationActor a;a.id=id;a.team=int(integer(r.get(p,"team")));a.slot=int(integer(r.get(p,"slot")));a.alive=boolean(r.get(p,"alive"),true);a.position=position(r,p);a.yaw=number(r.get(p,"yaw"));a.speed=number(r.get(p,"speed"));a.half_length=number(r.get(p,"half_length",true));a.half_width=number(r.get(p,"half_width",true));
    auto*shape=r.get(p,"collision_shape",true);if(r.size(shape)!=4)throw Invalid("Planning actor collision shape is invalid");a.collision_half_width=number(r.item(shape,0));a.collision_half_length=number(r.item(shape,1));
    a.traffic_waiting=boolean(r.get(p,"traffic_waiting"));if(local){a.local_route_id=local->has_route?local->route_id:"";a.local_route=local->waypoints;a.local_route_index=local->waypoint_index;a.local_route_join=local->route_join_index==local->waypoint_index;}
    auto*spg=r.get(p,"_spg_initial");if(spg!=Py_None){nav::InitialSPG v;v.catalog=string(r.get(spg,"catalog"));v.map=string(r.get(spg,"map"));v.zone=string(r.get(spg,"zone"));v.cell=string(r.get(spg,"cell"));v.point=point(r,r.get(spg,"point",true));v.clearance=number(r.get(spg,"clearance"));a.spg=v;}return a;
}
nav::ExactDescriptor descriptor(Read&r,PyObject*d,PyObject*source){nav::ExactDescriptor out;out.candidate.present=d!=Py_None;out.candidate.collision_half_width=number(r.item(r.get(source,"collision_shape",true),0));if(d==Py_None)return out;
    const auto bbox=stock_native_bot_descriptor::hullBounds(d);out.hull_minimum=bbox.minimum;out.hull_maximum=bbox.maximum;
    auto*params=r.own(stock_native_bot::readUnmodifiedDrive(d));out.mass=number(r.get(params,"mass",true));out.speed_forward=number(r.get(params,"speedFwd",true));out.speed_reverse=number(r.get(params,"speedBwd",true));out.valid=true;return out;
}
stock_native_bot_parking::Plan parkingPlan(Read&r,PyObject*p){stock_native_bot_parking::Plan out;
    out.catalog=string(r.get(p,"catalog",true));out.map=string(r.get(p,"map",true));out.zone=string(r.get(p,"zone",true));out.cell=string(r.get(p,"cell",true));out.source=string(r.get(p,"source",true));out.vehicle=string(r.get(p,"vehicle",true));out.side=string(r.get(p,"side",true));
    out.point=point(r,r.get(p,"point",true));out.face=point(r,r.get(p,"face",true));out.radius=number(r.get(p,"radius",true));out.clearance=number(r.get(p,"clearance",true));return out;
}
stock_native_bot_perception::Actor perception(Read&r,std::int64_t id,PyObject*state,PyObject*d,PyObject*runtime){stock_native_bot_perception::Actor out;auto&config=out.config;config.key={offline_simulation::ActorKind::Bot,id};out.sample.key=config.key;out.identity=std::uint64_t(id);out.target_available=true;
    auto*key=r.own(PyLong_FromLongLong(id));auto*factors=r.own(stock_native_bot_descriptor::defaultCrewFactors(d,stock_native_bot_descriptor::botCrewLevel(runtime,key)));
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
    config.team=int(integer(r.get(state,"team")));config.slot=int(integer(r.get(state,"slot")));config.half_length=3.5;config.half_width=1.7;
    const double view=std::max(50.,numeric(turret,"circularVisionRadius",330.))*std::max(0.,numeric(misc,"circularVisionRadiusFactor",1.))*std::max(0.,vision);
    config.view_moving=std::max(50.,view);config.view_still=has_binocular?std::max(50.,view*std::max(1.,binocular)):config.view_moving;config.view_delay=has_binocular?binocular_delay:-1.;
    config.camo_still=base[1];config.camo_moving=base[0];config.camo_add=moving[0];config.camo_net_add=still[0]-moving[0];config.camo_multiplier=moving[1];config.camo_net_multiplier=still[1];
    config.camo_shot=std::max(0.,std::min(1.,numeric(r.get(d,"gun"),"invisibilityFactorAtShot",1.)));config.camo_net_delay=has_net?net_delay:-1.;config.radio=std::max(0.,numeric(factors,"radio/distance",1.)*numeric(radio,"distance",0.));config.relay=0.;
    auto*wire=r.own(Py_BuildValue("(sL)","bot",static_cast<PY_LONG_LONG>(id)));const auto hash=PyObject_Hash(wire);if(hash==-1&&PyErr_Occurred())throw Invalid("Planning visibility key hash failed");config.python_hash=hash;
    auto&sample=out.sample;sample.alive=boolean(r.get(state,"alive"),true);sample.last_effort=false;sample.designated=false;sample.fire_sequence=integer(r.get(state,"fire_seq"),-1);sample.pose.position=position(r,state);sample.pose.mask=1|2|4|8|(1u<<9);
    const char*names[]={"x","y","z","yaw","pitch","roll","aim_yaw","turret_yaw","gun_pitch","speed"};for(int i=0;i<10;++i){auto*v=r.get(state,names[i]);sample.pose.values[i]=number(v);if(v!=Py_None)sample.pose.mask|=1u<<i;}
    auto*velocity=r.get(state,"velocity");if(velocity!=Py_None){const auto v=point(r,velocity);for(int i=0;i<3;++i)sample.pose.values[10+i]=v[i];sample.pose.mask|=1u<<10;}
    auto&pose=out.observer_pose;pose.position=sample.pose.position;pose.yaw=sample.pose.values[3];pose.pitch=sample.pose.values[4];pose.roll=sample.pose.values[5];auto*relative=r.get(state,"turret_yaw");pose.relative_turret_yaw=relative!=Py_None?number(relative):number(r.get(state,"aim_yaw"),pose.yaw)-pose.yaw;out.target_pose=pose;
    auto points=stock_native_bot_descriptor::checkpoints(d);if(points){out.checkpoints.available=true;out.checkpoints.points=*points;}
    auto*chassis=r.get(d,"chassis"),*hull=r.get(d,"hull"),*gun=r.get(d,"gun");auto*hp=r.get(chassis,"hullPosition"),*turrets=r.get(hull,"turretPositions");if(hp!=Py_None&&turrets!=Py_None&&r.size(turrets)){auto a=point(r,hp),b=point(r,r.item(turrets,0));out.checkpoints.turret_mount_available=true;for(int i=0;i<3;++i)out.checkpoints.turret_mount[i]=a[i]+b[i];}
    auto*yaw=r.get(gun,"staticTurretYaw");if(yaw!=Py_None){out.checkpoints.static_turret_yaw_present=true;out.checkpoints.static_turret_yaw=number(yaw);}return out;
}
}
bool readNavigationMode(PyObject*runtime,PyObject*state,PyObject*value,nav::ExactDescriptor&result){
    (void)runtime;try{Read r;result=descriptor(r,value,state);return true;
    }catch(const std::exception&e){if(!PyErr_Occurred())PyErr_SetString(PyExc_ValueError,e.what());return false;}}
bool readPerceptionMode(PyObject*runtime,PyObject*state,PyObject*descriptor,stock_native_bot_perception::Actor&result){try{Read r;const auto id=integer(r.get(state,"id",true));auto value=perception(r,id,state,descriptor,runtime);result=std::move(value);return true;}catch(const std::exception&e){if(!PyErr_Occurred())PyErr_SetString(PyExc_ValueError,e.what());return false;}}
bool readPlanningOrders(PyObject*source,std::map<std::int64_t,StrategicOrder>&result){try{Read r;std::map<std::int64_t,StrategicOrder> next;dictionary(source,[&](PyObject*k,PyObject*v){next[integer(k)]=order(r,v);});result=std::move(next);return true;}catch(const std::exception&e){if(!PyErr_Occurred())PyErr_SetString(PyExc_ValueError,e.what());return false;}}
bool readPlanningSnapshot(PyObject*runtime,PlanningSnapshot&result){try{Read r;PlanningSnapshot out;out.order_revision=integer(r.get(runtime,"_order_revision"),-1);auto*adapter=r.get(runtime,"adapter",true),*director=r.get(adapter,"director",true),*navigator=r.get(runtime,"navigator",true),*raw_grid=r.get(navigator,"grid",true),*raw_driver=r.get(adapter,"driver",true),*traffic=r.get(runtime,"_traffic_coordinator",true);
    for(const char*name:{"searches","bot_states","paths","bot_direct_progress","bot_failed_edges","bot_macro_edges"})empty(r,navigator,name);
    if(boolean(r.get(navigator,"search_frame_open")))throw Invalid("Planning bootstrap cannot run inside a legacy navigation frame");
    for(const char*name:{"_motion_probe_cache","_world_receipt_waiting","_decision_cache"})empty(r,runtime,name);
    for(const char*name:{"_contact_peers","_contact_attempts","_contact_escapes","_wreck_attempts","_wreck_history","_withdrawal_attempts"})empty(r,adapter,name);
    for(const char*name:{"_pairs","_held","_orders","_jams","_parked"})empty(r,traffic,name);
    grid(r,raw_grid,r.get(runtime,"baked_graph",true),out);
    auto*directions=r.get(r.get(runtime,"baked_graph",true),"directions",true);if(r.size(directions)!=8)throw Invalid("Planning parking directions width is invalid");std::set<nav::Cell> unique;
    for(int i=0;i<8;++i){auto d=cell(r,r.item(directions,i));if((d[0]==0&&d[1]==0)||std::abs(d[0])>1||std::abs(d[1])>1||!unique.insert(d).second)throw Invalid("Planning parking direction is invalid");out.directions[i]=d;}
    dictionary(r.get(r.get(director,"map_data"),"routes"),[&](PyObject*k,PyObject*values){auto&routes=out.routes[int(integer(k))];for(Py_ssize_t i=0;i<r.size(values);++i){auto*route=r.item(values,i),*points=r.get(route,"waypoints");std::vector<std::array<double,2>> path;for(Py_ssize_t j=0;j<r.size(points);++j){path.push_back(waypoint(r,r.item(points,j)));}routes[string(r.get(route,"id"))]=std::move(path);}});
    dictionary(r.get(director,"agents",true),[&](PyObject*k,PyObject*v){auto a=agent(r,v);if(a.id!=integer(k))throw Invalid("Planning director actor identity mismatch");out.director.agents[a.id]=std::move(a);});
    dictionary(r.get(director,"bases"),[&](PyObject*k,PyObject*v){const auto p=xz(r,v);out.director.bases[int(integer(k))]={p[0],p[2]};});
    std::map<int,std::map<std::int64_t,strategy::Contact>> contacts;dictionary(r.get(director,"contacts"),[&](PyObject*team,PyObject*values){dictionary(values,[&](PyObject*k,PyObject*v){auto c=contact(r,v);if(c.id!=integer(k))throw Invalid("Planning contact identity mismatch");contacts[int(integer(team))][c.id]=std::move(c);});});out.director.restoreContacts(std::move(contacts));
    dictionary(r.get(raw_driver,"states",true),[&](PyObject*k,PyObject*v){out.planner[integer(k)].driver=driver(r,v);});
    dictionary(r.get(runtime,"_server_orders"),[&](PyObject*k,PyObject*v){out.orders[integer(k)]=order(r,v);});dictionary(r.get(runtime,"_server_order_tokens"),[&](PyObject*k,PyObject*v){out.order_tokens[integer(k)]=unsignedInteger(v);});
    auto*descriptors=r.get(runtime,"_descriptors",true);
    auto*native=r.get(runtime,"_native_simulation",true),*control=r.get(native,"control",true);if(boolean(r.get(control,"_open")))throw Invalid("Planning bootstrap cannot run inside a legacy control frame");
    dictionary(r.get(runtime,"states",true),[&](PyObject*k,PyObject*v){const auto id=integer(k);auto local=out.director.agents.find(id);if(local==out.director.agents.end())throw Invalid("Planning actor has no registered strategy profile");
        auto*d=PyDict_GetItem(descriptors,k);if(!d)throw Invalid("Planning actor has no installed descriptor");out.actors[id]=navigationActor(r,id,v,&local->second);auto*initial=r.get(v,"_spg_initial");if(initial!=Py_None)out.initial_parking[id]=parkingPlan(r,initial);auto*origin=r.get(v,"_spg_selection_origin");if(origin!=Py_None)out.parking_origins[id]=point(r,origin);out.descriptors[id]=descriptor(r,d,v);out.perception.push_back(perception(r,id,v,d,runtime));});
    if(!out.initial_parking.empty()&&!stock_native_bot_parking::readCatalog(runtime,out.map,out.directions,out.parking))throw Invalid("Native parking catalog installation failed");
    result=std::move(out);return true;
}catch(const std::exception&e){if(!PyErr_Occurred())PyErr_SetString(PyExc_ValueError,e.what());return false;}}
}
