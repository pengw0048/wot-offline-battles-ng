#include "stock_native_bot_descriptor.hpp"
#include "stock_native_bot_python.hpp"
#include <algorithm>
#include <array>
#include <map>
#include <set>

namespace stock_native_bot_descriptor {
namespace {
namespace p=stock_native_bot_python;
using Point=wot::offline_bot_step::Point;
Point point(PyObject* v){p::Ref tuple(PySequence_Tuple(v));if(PyTuple_GET_SIZE(tuple.get())!=3)throw p::Error("Native descriptor vector width invalid");Point out;for(int i=0;i<3;++i)out[i]=p::scalar(PyTuple_GET_ITEM(tuple.get(),i));return out;}
Point boxPoint(PyObject* v){auto x=p::value(v,"x");if(x.get()!=Py_None){auto y=p::value(v,"y"),z=p::value(v,"z");return {p::scalar(x.get()),p::scalar(y.get()),p::scalar(z.get())};}Point out;for(int i=0;i<3;++i){auto value=p::item(v,i);out[i]=p::scalar(value.get());}return out;}
Bounds componentBox(PyObject*component){auto tester=p::value(component,"hitTester"),bbox=p::value(tester.get(),"bbox"),a=p::item(bbox.get(),0),b=p::item(bbox.get(),1);return {boxPoint(a.get()),boxPoint(b.get())};}
p::Ref invoke(PyObject* obj,const char* name,PyObject* a){p::Ref args(PyTuple_Pack(1,a));return p::call(obj,name,args.get());}
}
Bounds hullBounds(PyObject* descriptor){auto hull=p::value(descriptor,"hull");return componentBox(hull.get());}
Bounds bodyBounds(PyObject* descriptor){auto chassis=p::value(descriptor,"chassis");auto h=hullBounds(descriptor);if(chassis.get()==Py_None)return h;const auto c=componentBox(chassis.get());auto mount=p::value(chassis.get(),"hullPosition");const auto offset=boxPoint(mount.get());Bounds out;for(int i=0;i<3;++i){out.minimum[i]=std::min(c.minimum[i],h.minimum[i]+offset[i]);out.maximum[i]=std::max(c.maximum[i],h.maximum[i]+offset[i]);}return out;}
std::array<double,4> chassisShape(PyObject* descriptor){auto chassis=p::value(descriptor,"chassis"),mount=p::value(chassis.get(),"hullPosition");const auto c=componentBox(chassis.get()),h=hullBounds(descriptor);const auto offset=boxPoint(mount.get());return {std::max({std::abs(c.minimum[0]),std::abs(c.maximum[0]),.8}),std::max({std::abs(c.minimum[2]),std::abs(c.maximum[2]),1.}),c.minimum[1],std::max(c.maximum[1],offset[1]+h.maximum[1])};}
std::optional<std::array<Point,6>> checkpoints(PyObject* descriptor){try{std::array<Point,6> out;auto stored=p::value(descriptor,"visibilityCheckPoints");if(stored.get()!=Py_None&&PySequence_Size(stored.get())>=6){for(int i=0;i<6;++i){auto v=p::item(stored.get(),i);out[i]=boxPoint(v.get());}return out;}auto chassis=p::value(descriptor,"chassis"),hull=p::value(descriptor,"hull"),turret=p::value(descriptor,"turret"),hp=p::value(chassis.get(),"hullPosition"),positions=p::value(hull.get(),"turretPositions"),gp=p::value(turret.get(),"gunPosition"),first=p::item(positions.get(),0);const auto h=boxPoint(hp.get()),t=boxPoint(first.get()),g=boxPoint(gp.get());const auto box=componentBox(hull.get()),turret_box=componentBox(turret.get());const auto top=std::max(box.maximum[1],t[1]+turret_box.maximum[1]),cy=(box.minimum[1]+box.maximum[1])/2.,cz=(box.minimum[2]+box.maximum[2])/2.;Point gun{};for(int i=0;i<3;++i)gun[i]=t[i]+g[i];const std::array<Point,5> local{{gun,{{0.,cy,box.maximum[2]}},{{0.,cy,box.minimum[2]}},{{box.maximum[0],gun[1],cz}},{{box.minimum[0],gun[1],cz}}}};out[0]={{0.,h[1]+top,0.}};for(int n=0;n<5;++n)for(int i=0;i<3;++i)out[n+1][i]=h[i]+local[n][i];return out;}catch(const std::exception&){PyErr_Clear();return std::nullopt;}}
int crewLevelForRating(double rating){rating=std::max(0.,std::min(1.,rating));const double anchors[]={0.,.34,.67,1.},levels[]={75.,90.,100.,100.};double trained=100.;for(int i=0;i<3;++i)if(rating<=anchors[i+1]){const double f=(rating-anchors[i])/(anchors[i+1]-anchors[i]);trained=f<=0?levels[i]:f>=1?levels[i+1]:levels[i]+(levels[i+1]-levels[i])*f;break;}int result=75;for(int level:{90,100})if(std::abs(level-trained)<std::abs(result-trained))result=level;return result;}
double driveCap(PyObject* descriptor,double forward,double reverse,double speed,PyObject* travel_descriptor){const bool backwards=speed<0.;double cap=backwards?reverse:forward;auto travel=travel_descriptor?p::retain(travel_descriptor):p::value(descriptor,"defaultVehicleDescr");if(travel.get()!=Py_None&&cap>0.){auto physics=p::value(travel.get(),"physics"),limits=p::value(physics.get(),"speedLimits"),v=p::item(limits.get(),backwards?1:0);const double raw=p::scalar(v.get());if(raw<0.)throw p::Error("Native travel drive limit invalid");cap=std::max(cap,raw);}if(!std::isfinite(cap)||cap<0.)throw p::Error("Native drive cap invalid");return backwards?-cap:cap;}
DevicePool devicePool(PyObject* descriptor,const std::string& name){
    DevicePool out;auto pools=p::value(descriptor,"_offline_critical_device_pools");
    if(pools.get()!=Py_None){auto row=p::value(pools.get(),name.c_str());if(row.get()==Py_None)return out;
        auto a=p::item(row.get(),0),b=p::item(row.get(),1);if(a.get()!=Py_None)out.maximum=p::scalar(a.get());if(b.get()!=Py_None)out.regeneration=p::scalar(b.get());return out;}
    struct Spec{const char* component;const char* member;const char* factor;};
    static const std::map<std::string,Spec> specs={
        {"engineHealth",{"engine",nullptr,"engineHealthFactor"}},{"ammoBayHealth",{"hull","ammoBayHealth","ammoBayHealthFactor"}},
        {"fuelTankHealth",{"fuelTank",nullptr,"fuelTankHealthFactor"}},{"radioHealth",{"radio",nullptr,nullptr}},
        {"leftTrackHealth",{"chassis",nullptr,"chassisHealthFactor"}},{"rightTrackHealth",{"chassis",nullptr,"chassisHealthFactor"}},
        {"gunHealth",{"gun",nullptr,nullptr}},{"turretRotatorHealth",{"turret","turretRotatorHealth",nullptr}},
        {"surveyingDeviceHealth",{"turret","surveyingDeviceHealth",nullptr}}};
    const auto it=specs.find(name);if(it==specs.end()||descriptor==Py_None)return out;
    auto component=p::value(descriptor,it->second.component);auto value=it->second.member?p::value(component.get(),it->second.member):p::retain(component.get());
    auto maximum=p::value(value.get(),"maxHealth");if(maximum.get()==Py_None)return out;
    const double hp=p::scalar(maximum.get());double factor=1.;if(it->second.factor){auto misc=p::value(descriptor,"miscAttrs");factor=p::numeric(misc.get(),it->second.factor,1.);}
    auto regen=p::value(value.get(),"maxRegenHealth");const double recovery=p::flag(regen.get())?p::scalar(regen.get()):std::trunc(hp*.5);
    out.maximum=std::round(hp*factor);out.regeneration=std::round(recovery*factor);return out;
}
std::vector<std::string> crewRoster(PyObject* descriptor){
    std::vector<std::string> out;auto type=p::value(descriptor,"type"),roles=p::value(type.get(),"crewRoles");
    if(!PyList_Check(roles.get())&&!PyTuple_Check(roles.get()))return out;
    std::map<std::string,int> counters={{"gunner",1},{"loader",1},{"radioman",1}};
    const std::set<std::string> allowed={"commander","driver","gunner","loader","radioman"};std::set<std::string> used;
    for(Py_ssize_t i=0;i<p::size(roles.get());++i){auto row=p::item(roles.get(),i);if((!PyList_Check(row.get())&&!PyTuple_Check(row.get()))||!p::size(row.get()))return {};
        auto role=p::item(row.get(),0);const auto base=p::string(role.get());if(!allowed.count(base))return {};
        auto count=counters.find(base);const auto name=count==counters.end()?base:base+std::to_string(count->second++);if(!used.insert(name).second)return {};out.push_back(name);}
    return out;
}
bool aimSamples(PyObject* descriptor,std::vector<wot::offline_bot_step::AimSample>& output){
 try {
    using Sample=wot::offline_bot_step::AimSample;std::vector<Sample> centres,edges;auto chassis=p::value(descriptor,"chassis"),hull=p::value(descriptor,"hull"),turret=p::value(descriptor,"turret"),gun=p::value(descriptor,"gun");Point mount;
    try{auto v=p::value(chassis.get(),"hullPosition");mount=point(v.get());}catch(const std::exception&){PyErr_Clear();output.clear();return true;}
    for(int part=0;part<2;++part){try{Sample s;s.mount=mount;s.turret=part!=0;s.static_yaw=0.;
        if(part){auto positions=p::value(hull.get(),"turretPositions"),v=p::item(positions.get(),0);auto off=point(v.get());for(int axis=0;axis<3;++axis)s.mount[axis]+=off[axis];auto yaw=p::value(gun.get(),"staticTurretYaw");s.static_yaw=yaw.get()==Py_None?std::optional<double>{}:p::scalar(yaw.get());}
        auto tester=p::value(part?turret.get():hull.get(),"hitTester"),box=p::value(tester.get(),"bbox"),lo=p::item(box.get(),0),hi=p::item(box.get(),1);auto lower=point(lo.get()),upper=point(hi.get());bool valid=true;
        for(int axis=0;axis<3;++axis){if(upper[axis]<=lower[axis])valid=false;s.point[axis]=(lower[axis]+upper[axis])*.5;}if(!valid)continue;centres.push_back(s);
        for(const auto entry:{std::pair<int,double>{0,-1.},{0,1.},{1,1.}}){auto edge=s;edge.point[entry.first]+=(upper[entry.first]-lower[entry.first])*.25*entry.second;edges.push_back(edge);}
    }catch(const std::exception&){PyErr_Clear();}}
    centres.insert(centres.end(),edges.begin(),edges.end());output=std::move(centres);return true;
 }catch(const std::exception&e){if(!PyErr_Occurred())PyErr_SetString(PyExc_ValueError,e.what());return false;}
}
PyObject* defaultCrewFactors(PyObject* descriptor,std::optional<int> requested){
 try {
    p::Ref utils(PyImport_ImportModule("items.utils")),tankmen(PyImport_ImportModule("items.tankmen")),constants(PyImport_ImportModule("constants")),qualifier(PyImport_ImportModule("items.qualifiers"));
    p::Ref crew_module(PyImport_ImportModule("items.VehicleDescrCrew")),qualifiers_module(PyImport_ImportModule("VehicleQualifiersApplier"));
    auto aspects=p::value(constants.get(),"VEHICLE_TTC_ASPECTS"),normal=p::value(aspects.get(),"DEFAULT"),still=p::value(aspects.get(),"WHEN_STILL");
    auto qualifier_type=p::value(qualifier.get(),"QUALIFIER_TYPE"),main_skill=p::value(qualifier_type.get(),"MAIN_SKILL"),crew_class=p::value(crew_module.get(),"VehicleDescrCrew"),qualifiers_class=p::value(qualifiers_module.get(),"VehicleQualifiersApplier");
    const int maximum=int(p::integral(tankmen.get(),"MAX_SKILL_LEVEL"));auto type=p::value(descriptor,"type");
    auto produce=[&](int level){auto n=p::integer(level);p::Ref args(PyTuple_Pack(2,type.get(),n.get()));return p::call(utils.get(),"generateDefaultCrew",args.get());};
    auto crew=[&](){try{return produce(requested?std::max(50,std::min(maximum,*requested)):maximum);}catch(const std::exception&){if(!requested)throw;PyErr_Clear();return produce(maximum);}}();
    p::Ref compact(PySequence_List(crew.get())),flags(PyList_New(PyList_GET_SIZE(compact.get())));for(Py_ssize_t i=0;i<PyList_GET_SIZE(flags.get());++i){Py_INCREF(Py_True);PyList_SET_ITEM(flags.get(),i,Py_True);}
    p::Ref empty(PyTuple_New(0));auto factors=p::call(utils.get(),"makeDefaultVehicleAttributeFactors",empty.get());
    auto collect=[&](PyObject* target,PyObject* aspect){
        p::set(target,"crewLevelIncrease",p::integer(0));auto devices=p::value(descriptor,"optionalDevices");if(devices.get()!=Py_None)for(Py_ssize_t i=0;i<p::size(devices.get());++i){auto device=p::item(devices.get(),i);if(device.get()==Py_None)continue;p::Ref args(PyTuple_Pack(3,descriptor,target,aspect));p::call(device.get(),"updateVehicleAttrFactors",args.get());}
        auto mapping=p::dict();p::Ref qualifiers(PyObject_CallFunctionObjArgs(qualifiers_class.get(),mapping.get(),descriptor,nullptr));p::Ref bonuses(PyObject_GetItem(qualifiers.get(),main_skill.get()));
        p::Ref args(PyTuple_Pack(3,descriptor,compact.get(),bonuses.get()));auto kwargs=p::dict();p::set(kwargs.get(),"activityFlags",flags);p::set(kwargs.get(),"isFire",p::boolean(false));p::Ref c(PyObject_Call(crew_class.get(),args.get(),kwargs.get()));
        invoke(c.get(),"onCollectFactors",target);auto camouflage=p::value(c.get(),"camouflageFactor");p::set(target,"camouflage",camouflage);
        auto dispersion=p::list();p::append(dispersion.get(),p::number(1.));p::append(dispersion.get(),p::number(0.));invoke(c.get(),"onCollectShotDispersionFactors",dispersion.get());p::set(target,"shotDispersion",dispersion);
    };
    collect(factors.get(),normal.get());
    // Both aspects contain mutable stock factor lists. Match the stock deep
    // copy before the WHEN_STILL device/crew pass.
    p::Ref copy(PyImport_ImportModule("copy"));auto stationary=invoke(copy.get(),"deepcopy",factors.get());collect(stationary.get(),still.get());
    auto visibility=p::dict(),a=p::value(factors.get(),"invisibility"),b=p::value(stationary.get(),"invisibility");if(PyDict_SetItem(visibility.get(),normal.get(),a.get())<0||PyDict_SetItem(visibility.get(),still.get(),b.get())<0)throw p::Error("Native crew invisibility allocation failed");p::set(factors.get(),"invisibility",visibility);
    p::Ref pair(PyTuple_Pack(2,normal.get(),still.get()));p::set(factors.get(),"_aspects",pair);return factors.release();
 }catch(const std::exception&e){if(requested){PyErr_Clear();return defaultCrewFactors(descriptor,std::nullopt);}if(!PyErr_Occurred())PyErr_SetString(PyExc_ValueError,e.what());return nullptr;}
}
}
