#include "stock_native_bot_combat_snapshot.hpp"
#include "stock_native_bot_descriptor.hpp"
#include "stock_native_projectile_damage_python.hpp"
#include <numeric>
#include <cctype>
#include <Python.h>
#include <algorithm>
#include <cmath>
#include <stdexcept>
#include <utility>

namespace wot::offline_bot_step {
namespace {
namespace w=offline_simulation::weapons;
struct Error:std::runtime_error {explicit Error(const char* message):std::runtime_error(message){}};
struct Ref {
    PyObject* p=nullptr;
    explicit Ref(PyObject* value=nullptr):p(value){}
    ~Ref(){Py_XDECREF(p);} Ref(const Ref&)=delete;Ref& operator=(const Ref&)=delete;
    Ref(Ref&& b) noexcept:p(b.p){b.p=nullptr;}
    Ref& operator=(Ref&& b) noexcept {if(this!=&b){Py_XDECREF(p);p=b.p;b.p=nullptr;}return *this;}
    PyObject* get() const{return p;} PyObject* release(){auto x=p;p=nullptr;return x;}
};
PyObject* required(PyObject* p,const char* label){if(!p)throw Error(label);return p;}
Ref attr(PyObject* p,const char* name){return Ref(required(PyObject_GetAttrString(p,name),name));}
PyObject* field(PyObject* p,const char* name){if(!PyDict_Check(p))throw Error("expected dictionary");return PyDict_GetItemString(p,name);}
Ref value(PyObject* object,const char* name){
    if(PyDict_Check(object)){auto p=PyDict_GetItemString(object,name);if(!p){Py_INCREF(Py_None);return Ref(Py_None);}Py_INCREF(p);return Ref(p);}
    auto p=PyObject_GetAttrString(object,name);if(!p && PyErr_ExceptionMatches(PyExc_AttributeError)){PyErr_Clear();Py_INCREF(Py_None);return Ref(Py_None);}return Ref(required(p,name));
}
std::string text(PyObject* p){
    if(!p || p==Py_None)throw Error("missing text");
#if PY_MAJOR_VERSION >= 3
    const char* s=PyUnicode_AsUTF8(p);if(!s)throw Error("text conversion failed");return s;
#else
    if(PyString_Check(p))return std::string(PyString_AS_STRING(p),PyString_GET_SIZE(p));
    Ref bytes(PyUnicode_AsUTF8String(p));if(!bytes.get())throw Error("text conversion failed");return std::string(PyString_AS_STRING(bytes.get()),PyString_GET_SIZE(bytes.get()));
#endif
}
double scalar(PyObject* p){if(!p || p==Py_None || PyBool_Check(p))throw Error("missing numeric field");
    double v=PyFloat_AsDouble(p);if(PyErr_Occurred() || !std::isfinite(v))throw Error("invalid numeric field");return v;}
std::int64_t integer(PyObject* p){const double d=scalar(p);const auto i=static_cast<std::int64_t>(d);if(double(i)!=d)throw Error("invalid integer field");return i;}
double getnum(PyObject* dict,const char* key,double fallback=0.){auto p=field(dict,key);return !p || p==Py_None?fallback:scalar(p);}
bool flag(PyObject* p){if(!p || p==Py_None)return false;const int v=PyObject_IsTrue(p);if(v<0)throw Error("invalid boolean field");return v!=0;}
bool getflag(PyObject* d,const char* key,bool fallback=false){auto p=field(d,key);return !p?fallback:flag(p);}
Py_ssize_t size(PyObject* p){if(!p || !(PyTuple_Check(p)||PyList_Check(p)))throw Error("expected sequence");return PySequence_Size(p);}
Ref item(PyObject* p,Py_ssize_t n){return Ref(required(PySequence_GetItem(p,n),"sequence index"));}
Point point(PyObject* p){Ref tuple(required(PySequence_Tuple(p),"descriptor vector"));if(PyTuple_GET_SIZE(tuple.get())!=3)throw Error("descriptor vector width");return {scalar(PyTuple_GET_ITEM(tuple.get(),0)),scalar(PyTuple_GET_ITEM(tuple.get(),1)),scalar(PyTuple_GET_ITEM(tuple.get(),2))};}
Ref number(double n){return Ref(required(PyFloat_FromDouble(n),"number allocation"));}
Ref pytext(const char* s){
#if PY_MAJOR_VERSION >= 3
    return Ref(required(PyUnicode_FromString(s),"text allocation"));
#else
    return Ref(required(PyString_FromString(s),"text allocation"));
#endif
}
Ref frommap(PyObject* runtime,const char* map_name,PyObject* id){auto map=attr(runtime,map_name);if(!PyDict_Check(map.get()))throw Error("actor map is not a dictionary");
    auto found=PyDict_GetItem(map.get(),id);if(!found){Py_INCREF(Py_None);return Ref(Py_None);}Py_INCREF(found);return Ref(found);}
std::vector<std::string> strings(PyObject* p){std::vector<std::string> result;if(!p || p==Py_None)return result;
    for(Py_ssize_t i=0;i<size(p);++i){auto v=item(p,i);result.push_back(text(v.get()));}return result;}
void setdict(PyObject* d,const char* key,PyObject* value){if(PyDict_SetItemString(d,key,required(value,key))<0)throw Error("dictionary write failed");}
void setnum(PyObject* d,const char* key,double value){auto v=number(value);setdict(d,key,v.get());}
void setbool(PyObject* d,const char* key,bool value){setdict(d,key,value?Py_True:Py_False);}
void settext(PyObject* d,const char* key,const std::string& v){auto x=pytext(v.c_str());setdict(d,key,x.get());}
void setint(PyObject* d,const char* key,std::int64_t n){Ref x(PyLong_FromLongLong(n));setdict(d,key,x.get());}
double numeric(PyObject* p,const char* key,double fallback=0.){auto v=value(p,key);if(v.get()==Py_None)return fallback;try{return scalar(v.get());}catch(const std::exception&){PyErr_Clear();return fallback;}}
double rating_value(PyObject* runtime,PyObject* id){auto raw=frommap(runtime,"_bot_ratings",id);return raw.get()==Py_None?.34:std::max(0.,std::min(1.,scalar(raw.get())));}
double anchor(double rating,const std::array<double,4>& points){const double anchors[]={0.,.34,.67,1.};for(int i=0;i<3;++i)if(rating<=anchors[i+1]){const double f=(rating-anchors[i])/(anchors[i+1]-anchors[i]);return f<=0?points[i]:f>=1?points[i+1]:points[i]+(points[i+1]-points[i])*f;}return points[3];}
int crew_level(PyObject* runtime,PyObject* id){return stock_native_bot_descriptor::botCrewLevel(runtime,id);}
void fresh_gun(PyObject* descriptor,std::optional<int> level,CombatSnapshot& s){
    Ref factors(required(stock_native_bot_descriptor::defaultCrewFactors(descriptor,level),"stock crew factors"));
    auto gun=value(descriptor,"gun"),chassis=value(descriptor,"chassis"),misc=value(descriptor,"miscAttrs"),bloom=value(gun.get(),"shotDispersionFactors");
    s.crew_factor=numeric(factors.get(),"turret/rotationSpeed",1.);s.gun_rotation_factor=numeric(factors.get(),"gun/rotationSpeed",1.);s.base_repair_factor=numeric(factors.get(),"repairSpeed",1.);
    double dispersion=1./std::max(s.crew_factor,1.e-6);try{auto pair=value(factors.get(),"shotDispersion"),v=item(pair.get(),0);dispersion=scalar(v.get());}catch(const std::exception&){PyErr_Clear();}
    auto& g=s.gun_config;auto raw=value(gun.get(),"shotDispersionAngle");g.fully_aimed_dispersion=scalar(raw.get())*dispersion;
    g.after_shot=std::max(0.,numeric(bloom.get(),"afterShot",1.5));auto intra=value(bloom.get(),"afterShotInBurst");g.after_shot_in_burst=intra.get()==Py_None?numeric(bloom.get(),"afterShot",1.5):scalar(intra.get());
    g.burst_count=1;g.burst_interval=0.;auto burst=value(gun.get(),"burst");if(flag(burst.get())){auto n=item(burst.get(),0);g.burst_count=int(integer(n.get()));if(size(burst.get())>1){auto v=item(burst.get(),1);g.burst_interval=scalar(v.get());}if(g.burst_count<1||g.burst_count>64||g.burst_interval<0||g.burst_interval>10.||(g.burst_count>1&&g.burst_interval<=0))throw Error("invalid installed burst");if(g.burst_count==1)g.burst_interval=0.;}
    g.turret_dispersion_factor=std::max(0.,numeric(bloom.get(),"turretRotation"));g.aiming_time=std::max(.1,numeric(gun.get(),"aimingTime",2.)*numeric(misc.get(),"gunAimingTimeFactor",1.)*numeric(factors.get(),"gun/aimingTime",1.));
    try{auto pair=value(chassis.get(),"shotDispersionFactors"),a=item(pair.get(),0),b=item(pair.get(),1);g.movement_dispersion_factor=std::max(0.,scalar(a.get()));g.rotation_dispersion_factor=std::max(0.,scalar(b.get()));}catch(const std::exception&){PyErr_Clear();g.movement_dispersion_factor=g.rotation_dispersion_factor=0.;}
    g.reload_full=std::max(.01,numeric(gun.get(),"reloadTime",3.)*numeric(misc.get(),"gunReloadTimeFactor",1.)*numeric(factors.get(),"gun/reloadTime",1.));g.clip_size=1;g.reload_intra=0.;
    try{auto clip=value(gun.get(),"clip");if(size(clip.get())==2){auto a=item(clip.get(),0),b=item(clip.get(),1);g.clip_size=std::max(1,int(scalar(a.get())));g.reload_intra=std::max(.01,scalar(b.get()));}}catch(const std::exception&){PyErr_Clear();}
    auto shots=value(gun.get(),"shots");g.shell_count=shots.get()==Py_None?1:std::max(1,int(size(shots.get())));g.validate();
}
void fresh_ammo(PyObject* descriptor,PyObject* profile,PyObject* state,CombatSnapshot& s){
    const int count=s.gun_config.shell_count;auto& ammo=s.ammo;ammo.categories.assign(count,0);ammo.remaining.assign(count,0);std::map<int,PyObject*> by_index;
    auto rows=field(profile,"shells");if(rows&&rows!=Py_None)for(Py_ssize_t i=0;i<size(rows);++i){auto row=item(rows,i);if(!PyDict_Check(row.get()))continue;const int n=int(numeric(row.get(),"index",-1));if(n>=0&&n<count)by_index[n]=PySequence_Fast_GET_ITEM(rows,i);}
    int first=-1;double baseline=0.;for(int i=0;i<count;++i){auto it=by_index.find(i);std::string kind;if(it!=by_index.end()){auto raw=value(it->second,"kind");if(raw.get()!=Py_None)kind=text(raw.get());}std::transform(kind.begin(),kind.end(),kind.begin(),[](unsigned char c){return char(std::tolower(c));});
        if(kind.find("high_explosive")!=std::string::npos||(kind.find("explosive")!=std::string::npos&&kind.find("armor_piercing")==std::string::npos))ammo.categories[i]=2;else if(first<0){first=i;baseline=it==by_index.end()?0.:std::max(0.,numeric(it->second,"penetration"));}}
    for(int i=0;i<count;++i)if(ammo.categories[i]!=2&&i!=first&&baseline>0.){auto it=by_index.find(i);if(it!=by_index.end()&&std::max(0.,numeric(it->second,"penetration"))>=baseline*1.03)ammo.categories[i]=1;}
    auto gun=value(descriptor,"gun"),turret=value(descriptor,"turret"),capacity=value(descriptor,"maxAmmo");if(capacity.get()==Py_None)capacity=value(gun.get(),"maxAmmo");if(capacity.get()==Py_None)capacity=value(turret.get(),"maxAmmo");int maximum=45;try{if(capacity.get()!=Py_None)maximum=int(scalar(capacity.get()));}catch(const std::exception&){PyErr_Clear();}maximum=std::max(0,std::min(1000,maximum));
    auto cls=value(profile,"class_tag");const bool spg=cls.get()!=Py_None&&text(cls.get())=="SPG";const std::array<double,3> weights=spg?std::array<double,3>{1.,1.,4.}:std::array<double,3>{3.,2.,1.};std::vector<int> active;for(int category:{2,1,0})if(std::find(ammo.categories.begin(),ammo.categories.end(),category)!=ammo.categories.end())active.push_back(category);double total=0.;for(int c:active)total+=weights[c];std::map<int,int> quantities;int assigned=0;for(int c:active)assigned+=(quantities[c]=int(maximum*weights[c]/total));
    auto remainders=active;std::stable_sort(remainders.begin(),remainders.end(),[&](int a,int b){return maximum*weights[a]/total-quantities[a]>maximum*weights[b]/total-quantities[b];});for(int i=0;i<maximum-assigned;++i)++quantities[remainders[std::size_t(i)%remainders.size()]];
    for(int c:active){std::vector<int> indices;for(int i=0;i<count;++i)if(ammo.categories[i]==c)indices.push_back(i);for(int i=0;i<quantities[c];++i)++ammo.remaining[indices[std::size_t(i)%indices.size()]];}
    ammo.loaded=ammo.fallback();ammo.next=ammo.loaded;
    if(field(state,"ammo_remaining")||field(state,"next_shell_index")||field(state,"ammo_reload_pending")){
        for(const char* key:{"ammo_remaining","shell_index","next_shell_index","ammo_reload_pending"})if(!field(state,key))throw Error("incomplete ammunition snapshot");auto remaining=field(state,"ammo_remaining");if(size(remaining)!=count)throw Error("ammunition shape mismatch");ammo.remaining.clear();for(int i=0;i<count;++i){auto v=item(remaining,i);const auto n=integer(v.get());if(n<0||n>1000)throw Error("ammunition quantity invalid");ammo.remaining.push_back(int(n));}ammo.loaded=int(getnum(state,"shell_index"));ammo.next=int(getnum(state,"next_shell_index"));if(!PyBool_Check(field(state,"ammo_reload_pending")))throw Error("ammunition reload flag invalid");ammo.reload_pending=getflag(state,"ammo_reload_pending");ammo.plan_pending=false;
        const int sum=std::accumulate(ammo.remaining.begin(),ammo.remaining.end(),0);if(ammo.loaded<0||ammo.loaded>=count||ammo.next<0||ammo.next>=count||(sum>0&&(ammo.remaining[ammo.next]<=0||(!ammo.reload_pending&&ammo.remaining[ammo.loaded]<=0))))throw Error("ammunition selection invalid");}
    ammo.validate(count);
}
CriticalState critical_from_wire(PyObject* payload,const CriticalState& metadata) {
    if(!payload || !PyDict_Check(payload))throw Error("critical record is not a dictionary");
    CriticalState result=metadata;
    for(auto& pair:result.devices){pair.second.hp.reset();pair.second.wire_maximum.reset();pair.second.destroyed=false;pair.second.critical=false;}
    result.crew_ko.clear();result.crew_roster.clear();result.fire=getflag(payload,"fire");result.ammo_rack_death=getflag(payload,"ammo_rack_death");
    for(const auto& name:strings(field(payload,"destroyed")))result.devices[name].destroyed=true;
    for(const auto& name:strings(field(payload,"crew_ko")))result.crew_ko.insert(name);
    for(const auto& name:strings(field(payload,"crew_roster")))result.crew_roster.insert(name);
    auto rows=field(payload,"devices");
    if(rows && rows!=Py_None) for(Py_ssize_t i=0;i<size(rows);++i){auto row=item(rows,i);
        const auto name=text(required(field(row.get(),"name"),"device name"));auto& d=result.devices[name];
        d.hp=scalar(required(field(row.get(),"hp"),"device hp"));d.wire_maximum=scalar(required(field(row.get(),"max_hp"),"device max"));
        const auto status=text(required(field(row.get(),"state"),"device state"));
        if(status!="normal" && status!="critical" && status!="destroyed")throw Error("invalid device state");
        d.critical=status=="critical";d.destroyed=d.destroyed || status=="destroyed";
    }
    return result;
}
CombatRecord record_from_wire(PyObject* raw,const CriticalState& metadata) {
    if(!raw || !PyDict_Check(raw))throw Error("combat record is not a dictionary");
    CombatRecord result;result.health=static_cast<int>(getnum(raw,"health"));result.alive=getflag(raw,"alive");
    result.critical=critical_from_wire(required(field(raw,"critical"),"combat critical"),metadata);
    result.critical.fire_elapsed=getnum(raw,"combat_fire_elapsed");result.critical.fire_timer=getnum(raw,"combat_fire_timer");
    result.stun_end_server_time_ms=static_cast<std::int64_t>(getnum(raw,"stun_end_server_time_ms"));return result;
}
std::vector<CombatReplayStep> replay_from_wire(PyObject* rows) {
    std::vector<CombatReplayStep> result;if(!rows || rows==Py_None)return result;
    for(Py_ssize_t i=0;i<size(rows);++i){auto row=item(rows,i);if(size(row.get())<3)throw Error("combat replay step shape");
        auto elapsed=item(row.get(),0),now=item(row.get(),1),fire=item(row.get(),2);
        CombatReplayStep step;step.elapsed=scalar(elapsed.get());step.now=scalar(now.get());step.advance_fire=flag(fire.get());
        if(size(row.get())>3){auto effects=item(row.get(),3);if(effects.get()!=Py_None)for(Py_ssize_t j=0;j<size(effects.get());++j){auto value=item(effects.get(),j);
            auto raw=field(value.get(),"action");if(!raw)continue;const auto action=text(raw);EquipmentEffect effect;
            if(action=="extinguish_fire")effect.action=EquipmentAction::extinguish_fire;
            else if(action=="repair_devices")effect.action=EquipmentAction::repair_devices;
            else if(action=="restore_crew")effect.action=EquipmentAction::restore_crew;else continue;
            auto selected=field(value.get(),"selected");if(selected && selected!=Py_None)effect.selected=text(selected);
            effect.all=getflag(value.get(),"repairAll");effect.clear_stun=getflag(value.get(),"clearStun");
            effect.stun_base_end_server_time_ms=static_cast<std::int64_t>(getnum(value.get(),"stunBaseEndServerTimeMs"));step.equipment.push_back(std::move(effect));}}
        result.push_back(std::move(step));}
    return result;
}
}

static bool load_snapshot(PyObject* runtime,PyObject* state,PyObject* descriptor,PyObject* profile,
                          CombatSnapshot& output,bool mode_only){
 try {
    if(!PyDict_Check(state) || !descriptor || !runtime)throw Error("missing combat bootstrap input");
    if(!profile || profile==Py_None){profile=PyDict_New();if(!profile)throw Error("profile allocation");}else Py_INCREF(profile);
    Ref profile_owner(profile);
    CombatSnapshot s;s.fire_sequence=static_cast<std::int64_t>(getnum(state,"fire_seq"));auto& c=s.state;
    c.health=static_cast<int>(getnum(state,"health"));c.maximum_health=std::max(1,static_cast<int>(getnum(state,"max_health",c.health?c.health:1)));
    c.display_health=static_cast<int>(getnum(state,"display_health",c.health));c.alive=getflag(state,"alive",true);
    c.death_reason=static_cast<int>(getnum(state,"death_reason"));
    c.position={getnum(state,"x"),getnum(state,"y"),getnum(state,"z")};c.pitch=getnum(state,"pitch");c.roll=getnum(state,"roll");
    c.speed=getnum(state,"speed");c.movement_direction=static_cast<int>(getnum(state,"movement_dir"));c.rotation_direction=static_cast<int>(getnum(state,"rotation_dir"));
    c.drown_check=getnum(state,"_drown_check");c.drown_time=getnum(state,"_drown_time");c.water_depth=getnum(state,"_water_depth",-1.);
    c.drowning=getflag(state,"_drowning");c.drowned=getflag(state,"_drowned");c.overturn_check=getnum(state,"_overturn_check");c.overturn_time=getnum(state,"_overturn_time");
    c.overturn_level=static_cast<int>(getnum(state,"_overturn_level"));c.overturned=getflag(state,"_overturned");
    c.critical.fire_elapsed=getnum(state,"combat_fire_elapsed");c.critical.fire_timer=getnum(state,"combat_fire_timer");
    c.stun_end_server_time_ms=static_cast<std::int64_t>(getnum(state,"stun_end_server_time_ms"));c.stun_until_equipment_time=getnum(state,"_stun_until_equipment_time");
    c.has_target=field(state,"target_id") && field(state,"target_id")!=Py_None;
    PyObject* critical=field(state,"critical");
    if(critical && critical!=Py_None){
        if(!PyDict_Check(critical))throw Error("critical payload is not a dictionary");
        c.has_critical_payload=PyDict_Size(critical)>0;
        c.critical.fire=getflag(critical,"fire");c.critical.ammo_rack_death=getflag(critical,"ammo_rack_death");
        for(const auto& n:strings(field(critical,"crew_ko")))c.critical.crew_ko.insert(n);
        for(const auto& n:strings(field(critical,"crew_roster")))c.critical.crew_roster.insert(n);
        for(const auto& n:strings(field(critical,"destroyed")))c.critical.devices[n].destroyed=true;
        auto rows=field(critical,"devices");
        if(rows && rows!=Py_None)for(Py_ssize_t i=0;i<size(rows);++i){auto row=item(rows,i);const auto name=text(required(field(row.get(),"name"),"device name"));
            auto& d=c.critical.devices[name];d.hp=scalar(required(field(row.get(),"hp"),"device hp"));
            auto maximum=field(row.get(),"max_hp");if(maximum && maximum!=Py_None)d.wire_maximum=scalar(maximum);
            auto p=field(row.get(),"state");if(p && p!=Py_None){const auto status=text(p);d.destroyed=status=="destroyed" || d.destroyed;d.critical=status=="critical";}}
    }
    for(const char* name:{"engineHealth","ammoBayHealth","fuelTankHealth","radioHealth","gunHealth","turretRotatorHealth","surveyingDeviceHealth","leftTrackHealth","rightTrackHealth"}){const auto pool=stock_native_bot_descriptor::devicePool(descriptor,name);auto& d=c.critical.devices[name];d.maximum=pool.maximum;d.regeneration=pool.regeneration;}
    for(const auto& n:stock_native_bot_descriptor::crewRoster(descriptor))c.descriptor_crew_roster.insert(n);
    auto id=required(field(state,"id"),"actor id");auto gun=frommap(runtime,"_gun_states",id),ammo=frommap(runtime,"_ammo_states",id),burst=frommap(runtime,"_burst_states",id);
    std::optional<int> installed_crew_level;if(mode_only&&gun.get()!=Py_None){auto level=value(gun.get(),"crew_level");if(level.get()!=Py_None)installed_crew_level=int(integer(level.get()));}
    if(mode_only){Py_INCREF(Py_None);gun=Ref(Py_None);}
    const auto get_attr_num=[&](PyObject* p,const char* name){auto v=attr(p,name);return scalar(v.get());};
    auto& g=s.gun_config;
    if(gun.get()==Py_None)fresh_gun(descriptor,installed_crew_level,s);
    else {
    g.fully_aimed_dispersion=get_attr_num(gun.get(),"fully_aimed_dispersion");g.after_shot=get_attr_num(gun.get(),"after_shot");g.after_shot_in_burst=get_attr_num(gun.get(),"after_shot_in_burst");
    g.turret_dispersion_factor=get_attr_num(gun.get(),"turret_dispersion_factor");g.aiming_time=get_attr_num(gun.get(),"aiming_time");
    g.movement_dispersion_factor=get_attr_num(gun.get(),"movement_dispersion_factor");g.rotation_dispersion_factor=get_attr_num(gun.get(),"rotation_dispersion_factor");
    g.reload_full=get_attr_num(gun.get(),"reload_full");g.reload_intra=get_attr_num(gun.get(),"reload_intra");
    g.clip_size=static_cast<int>(get_attr_num(gun.get(),"clip_size"));g.shell_count=static_cast<int>(get_attr_num(gun.get(),"shell_count"));
    g.burst_count=static_cast<int>(get_attr_num(gun.get(),"burst_count"));g.burst_interval=get_attr_num(gun.get(),"burst_interval");g.validate();
    }
    if(ammo.get()==Py_None)fresh_ammo(descriptor,profile,state,s);else {
    auto categories=attr(ammo.get(),"categories"),remaining=attr(ammo.get(),"remaining");
    if(size(remaining.get())!=g.shell_count)throw Error("ammunition count mismatch");
    for(int i=0;i<g.shell_count;++i){Ref key(PyLong_FromLong(i));Ref cat(required(PyObject_GetItem(categories.get(),key.get()),"ammo category"));const auto kind=text(cat.get());
        s.ammo.categories.push_back(kind=="standard"?0:kind=="premium"?1:kind=="he"?2:-1);auto quantity=item(remaining.get(),i);s.ammo.remaining.push_back(static_cast<int>(integer(quantity.get())));}
    s.ammo.loaded=static_cast<int>(get_attr_num(ammo.get(),"loaded"));s.ammo.next=static_cast<int>(get_attr_num(ammo.get(),"next"));
    {auto v=attr(ammo.get(),"reload_pending");s.ammo.reload_pending=flag(v.get());}{auto v=attr(ammo.get(),"plan_pending");s.ammo.plan_pending=flag(v.get());}s.ammo.validate(g.shell_count);
    }
    s.gun.emplace(g);auto& gs=*s.gun;if(gun.get()==Py_None)gs.restore(mode_only?0:s.fire_sequence,mode_only?1.:critical_factor(c.critical,CriticalStat::dispersion),false,0.,0.,1.,false,0,0);else {
gs.clip=static_cast<int>(get_attr_num(gun.get(),"clip"));gs.elapsed=get_attr_num(gun.get(),"elapsed");
    gs.reload_duration=get_attr_num(gun.get(),"reload_duration");{auto v=attr(gun.get(),"reload_kind");gs.intra=text(v.get())=="intra";}
    gs.reload_factor=get_attr_num(gun.get(),"reload_factor");gs.burst_remaining=static_cast<int>(get_attr_num(gun.get(),"_burst_remaining"));
    gs.current_dispersion_factor=get_attr_num(gun.get(),"current_dispersion_factor");gs.aiming_start_factor=get_attr_num(gun.get(),"aiming_start_factor");
    gs.aiming_elapsed=get_attr_num(gun.get(),"aiming_elapsed");gs.dispersion=get_attr_num(gun.get(),"dispersion");gs.motion_dispersion_squared=get_attr_num(gun.get(),"motion_dispersion_squared");
    {auto loadout=attr(gun.get(),"loadout");s.crew_factor=getnum(loadout.get(),"crew_factor",1.);
        s.gun_rotation_factor=getnum(loadout.get(),"gun_rotation_factor",1.);}
    }
    if(burst.get()!=Py_None){
    {auto v=attr(burst.get(),"active");s.burst.active=flag(v.get());}s.burst.group_seq=static_cast<std::int64_t>(get_attr_num(burst.get(),"group_seq"));
    s.burst.count=static_cast<int>(get_attr_num(burst.get(),"count"));s.burst.next_index=static_cast<int>(get_attr_num(burst.get(),"next_index"));
    s.burst.interval=get_attr_num(burst.get(),"interval");s.burst.time_left=get_attr_num(burst.get(),"time_left");s.burst.shell=static_cast<int>(get_attr_num(burst.get(),"shell_index"));
    }
    s.burst.validate(static_cast<std::int64_t>(getnum(state,"fire_seq")));
    s.aim.turret_yaw=getnum(state,"turret_yaw");s.aim.gun_pitch=getnum(state,"gun_pitch");s.aim.desired_gun_pitch=getnum(state,"desired_gun_pitch");s.aim.aligned=getflag(state,"gun_aligned");
    auto gun_desc=value(descriptor,"gun"),turret_desc=value(descriptor,"turret"),chassis_desc=value(descriptor,"chassis"),hull_desc=value(descriptor,"hull");auto limits=value(gun_desc.get(),"pitchLimits");
    try {
        if(PyDict_Check(limits.get())&&field(limits.get(),"minPitch")&&field(limits.get(),"maxPitch")){
            for(int side=0;side<2;++side){auto rows=field(limits.get(),side?"maxPitch":"minPitch");auto& curve=side?s.aim_config.maximum:s.aim_config.minimum;for(Py_ssize_t i=0;i<size(rows);++i){auto row=item(rows,i),x=item(row.get(),0),y=item(row.get(),1);curve.push_back({float(scalar(x.get())),float(scalar(y.get()))});}}
            w::pitch_limits(0.,s.aim_config);
        }else {auto raw=PyDict_Check(limits.get())?value(limits.get(),"absolute"):Ref((Py_INCREF(limits.get()),limits.get()));auto a=item(raw.get(),0),b=item(raw.get(),1);s.aim_config.fixed_pitch=true;s.aim_config.fixed_limits={scalar(a.get()),scalar(b.get())};}
        s.valid_pitch=true;
    }catch(const std::exception&){PyErr_Clear();s.aim_config.minimum.clear();s.aim_config.maximum.clear();s.aim_config.fixed_pitch=false;s.valid_pitch=false;}
    s.aim_config.turret_speed=std::max(0.,numeric(turret_desc.get(),"rotationSpeed",.5));s.aim_config.gun_speed=std::max(0.,numeric(gun_desc.get(),"rotationSpeed",.35));
    auto static_pitch=value(gun_desc.get(),"staticPitch");if(static_pitch.get()!=Py_None){s.aim_config.has_static_pitch=true;s.aim_config.static_pitch=scalar(static_pitch.get());}
    auto static_yaw=value(gun_desc.get(),"staticTurretYaw");if(static_yaw.get()!=Py_None)s.static_yaw=scalar(static_yaw.get());
    const double pi=3.14159265358979323846;s.minimum_yaw=-pi;s.maximum_yaw=pi;
    try{auto raw=value(gun_desc.get(),"turretYawLimits");if(raw.get()==Py_None)raw=value(turret_desc.get(),"yawLimits");auto a=item(raw.get(),0),b=item(raw.get(),1);s.minimum_yaw=scalar(a.get());s.maximum_yaw=scalar(b.get());if(std::abs(s.minimum_yaw)>pi+.1||std::abs(s.maximum_yaw)>pi+.1){s.minimum_yaw*=pi/180.;s.maximum_yaw*=pi/180.;}}
    catch(const std::exception&){PyErr_Clear();s.minimum_yaw=-pi;s.maximum_yaw=pi;}
    s.limited_yaw=!(s.minimum_yaw<=-pi+.1&&s.maximum_yaw>=pi-.1);s.aim_config.validate();
    try{auto hp=value(chassis_desc.get(),"hullPosition"),positions=value(hull_desc.get(),"turretPositions"),tp=item(positions.get(),0),gp=value(turret_desc.get(),"gunPosition");s.gun_mount=GunMount{point(hp.get()),point(tp.get()),point(gp.get())};}catch(const std::exception&){PyErr_Clear();}
    if(!load_aim_samples(descriptor,s.target_samples))throw Error("aim samples failed");
    try{auto hp=value(chassis_desc.get(),"hullPosition"),positions=value(hull_desc.get(),"turretPositions"),tp=item(positions.get(),0),carry=value(chassis_desc.get(),"topRightCarryingPoint");if(PySequence_Size(carry.get())!=2)throw Error("water carrying point width");for(int i=0;i<2;++i){auto v=item(carry.get(),i);scalar(v.get());}auto h=point(hp.get()),t=point(tp.get());for(int i=0;i<3;++i)h[i]+=t[i];s.water_sensor=h;}catch(const std::exception&){PyErr_Clear();}
    auto type=value(descriptor,"type"),hydraulics=value(type.get(),"hullAimingParams");if(hydraulics.get()==Py_None)hydraulics=value(descriptor,"hullAimingParams");
    try{auto pitch=value(hydraulics.get(),"pitch"),angles=value(pitch.get(),"wheelsCorrectionAngles");if(angles.get()!=Py_None){auto low=value(angles.get(),"pitchMin"),high=value(angles.get(),"pitchMax"),speed=value(pitch.get(),"wheelsCorrectionSpeed");s.hydraulic.minimum=scalar(low.get());s.hydraulic.maximum=scalar(high.get());s.hydraulic.speed=scalar(speed.get());auto center=value(pitch.get(),"wheelCorrectionCenterZ");if(center.get()!=Py_None)scalar(center.get());if(s.hydraulic.minimum>s.hydraulic.maximum||s.hydraulic.speed<0.)throw Error("invalid hydraulic descriptor");s.hydraulic.available=flag(value(pitch.get(),"isAvailable").get());s.hydraulic.enabled=flag(value(pitch.get(),"isEnabled").get());s.hydraulic.present=true;}}catch(const std::exception&){PyErr_Clear();s.hydraulic={};}
    const double rating=rating_value(runtime,id);auto behavior=frommap(runtime,"_bot_behavior",id);const auto override_number=[&](const char* key,double fallback){return behavior.get()==Py_None?fallback:getnum(behavior.get(),key,fallback);};
    s.gunner.reaction_seconds=override_number("reaction_seconds",anchor(rating,{1.2,.75,.45,.25}));s.gunner.patience_seconds=override_number("patience_seconds",anchor(rating,{.6,1.1,1.7,2.4}));
    s.gunner.converged_factor=override_number("converged_factor",anchor(rating,{2.6,1.8,1.35,1.12}));s.gunner.aim_bias_factor=override_number("aim_bias_factor",anchor(rating,{1.8,.85,.35,.1}));s.gunner.lead_error=override_number("lead_error",anchor(rating,{.45,.22,.09,.02}));
    auto shots=value(gun_desc.get(),"shots");
    for(int i=0;i<g.shell_count;++i){stock_native_bot_armor::Shell armor;armor.kind="ARMOR_PIERCING";armor.piercing_near=armor.piercing_far=100.;std::optional<DirectShot> physical;
        if(shots.get()!=Py_None&&i<size(shots.get())){auto raw=item(shots.get(),i);stock_native_projectile_damage::ShotDescriptor frozen;if(!stock_native_projectile_damage::readShotDescriptor(raw.get(),frozen))throw Error("native shell descriptor invalid");armor=frozen.shot.shell;
            const double speed=numeric(raw.get(),"speed",-1.),gravity=std::abs(numeric(raw.get(),"gravity",-1.)),maximum=numeric(raw.get(),"maxDistance",-1.);if(speed>1.&&gravity>.01&&maximum>1.){DirectShot shot;shot.speed=speed;shot.gravity=gravity;shot.maximum_distance=maximum;shot.splash_radius=frozen.shot.explosion_radius;physical=shot;}}
        s.armor_shells.push_back(std::move(armor));s.shells.push_back(physical);}
    for(const auto entry:{std::pair<const char*,CriticalStat>{"dispersion",CriticalStat::dispersion},{"aim_time",CriticalStat::aim_time},{"reload",CriticalStat::reload},{"turret_speed",CriticalStat::turret_speed},{"mobility",CriticalStat::mobility},{"vision",CriticalStat::vision}})s.initial_critical_factors[entry.first]=critical_factor(c.critical,entry.second);
    s.initial_critical_factors["traverse"]=1.;s.initial_critical_factors["engine_power"]=1.;
    auto misc=value(descriptor,"miscAttrs"),repair_speed=value(misc.get(),"repairSpeedFactor");if(repair_speed.get()!=Py_None)s.repair_speed_factor=scalar(repair_speed.get());
    s.siege.mode=static_cast<int>(getnum(state,"siege_state"));s.siege.remaining=getnum(state,"_siege_time_left");s.siege.total=getnum(state,"_siege_transition_total");s.siege.intent_elapsed=getnum(state,"_siege_intent_elapsed");
    auto intent=field(state,"_siege_intent");if(intent&&intent!=Py_None)s.siege.intent=flag(intent);
    auto name=value(type.get(),"name");if(name.get()!=Py_None){const auto n=text(name.get());if(n=="sweden:S10_Strv_103_0_Series"||n=="sweden:S11_Strv_103B"||n=="sweden:S22_Strv_S1")s.siege_config={true,2.,1.3,2.};else if(n=="sweden:S21_UDES_03")s.siege_config={true,2.,2.,2.};}
    auto equips=frommap(runtime,"_equipment_states",id);
    if(equips.get()!=Py_None)for(Py_ssize_t i=0;i<size(equips.get());++i){auto eq=item(equips.get(),i),contract=attr(eq.get(),"contract"),kind=value(contract.get(),"kind");
        EquipmentState e;e.contract_revision=static_cast<std::uint64_t>(reinterpret_cast<std::uintptr_t>(contract.get()));
        const auto name=text(kind.get());e.kind=name=="extinguisher"?EquipmentKind::extinguisher:name=="repairkit"?EquipmentKind::repairkit:name=="medkit"?EquipmentKind::medkit:
            name=="fuel"?EquipmentKind::fuel:name=="rpm_limiter"?EquipmentKind::rpm_limiter:name=="stimulator"?EquipmentKind::stimulator:EquipmentKind::other;
        e.uses_left=static_cast<int>(get_attr_num(eq.get(),"uses_left"));e.ready_at=get_attr_num(eq.get(),"ready_at");
        e.cooldown_seconds=getnum(contract.get(),"cooldownSeconds");e.reaction_seconds=getnum(contract.get(),"autoReactionSeconds");e.autoactivate=getflag(contract.get(),"autoactivate");e.repair_all=getflag(contract.get(),"repairAll");
        e.bonus_value=getnum(contract.get(),"bonusValue");e.crew_level_increase=getnum(contract.get(),"crewLevelIncrease");
        e.fire_starting_chance_factor=getnum(contract.get(),"fireStartingChanceFactor",1.);e.engine_power_factor=getnum(contract.get(),"enginePowerFactor",1.);
        e.turret_rotation_speed_factor=getnum(contract.get(),"turretRotationSpeedFactor",1.);e.engine_hp_loss_per_second=getnum(contract.get(),"engineHpLossPerSecond");
        {auto active=attr(eq.get(),"active");e.active=flag(active.get());}
        {auto pending=attr(eq.get(),"_auto_pending_since");if(pending.get()!=Py_None)e.auto_pending_since=scalar(pending.get());}
        {auto pending=attr(eq.get(),"_ai_pending_since");if(pending.get()!=Py_None)e.ai_pending_since=scalar(pending.get());}
        s.equipment.push_back(e);}
    const double repair_bonus=1.+std::max(0.,equipment_passives(s.equipment).repair_bonus);
    auto cached_repair=frommap(runtime,"_repair_factors",id);
    if(!mode_only&&cached_repair.get()!=Py_None){s.initial_repair_factor=scalar(cached_repair.get());s.base_repair_factor=s.initial_repair_factor/repair_bonus;}
    else {Ref factors(required(stock_native_bot_descriptor::defaultCrewFactors(descriptor,crew_level(runtime,id)),"repair crew factors"));s.base_repair_factor=numeric(factors.get(),"repairSpeed",1.);s.initial_repair_factor=s.base_repair_factor*repair_bonus;}
    auto sync=frommap(runtime,"_combat_sync",id);
    if(sync.get()==Py_None) s.ledger.initialize(c,static_cast<std::int64_t>(getnum(state,"combat_revision")),
        static_cast<std::int64_t>(getnum(state,"combat_base_revision")),static_cast<std::int64_t>(getnum(state,"combat_ack_seq")));
    else {
        auto& ledger=s.ledger;
        ledger.server_combat=record_from_wire(required(field(sync.get(),"server_combat"),"server combat"),c.critical);
        ledger.next_sequence=static_cast<std::int64_t>(getnum(sync.get(),"next_seq"));ledger.acknowledged_sequence=static_cast<std::int64_t>(getnum(sync.get(),"acked_seq"));
        ledger.revision=static_cast<std::int64_t>(getnum(sync.get(),"combat_revision"));ledger.base_revision=static_cast<std::int64_t>(getnum(sync.get(),"base_revision"));
        ledger.server_tick=static_cast<std::int64_t>(getnum(sync.get(),"server_tick",-1));ledger.authority_handoff_pending=getflag(sync.get(),"authority_handoff_pending");
        ledger.unpublished_steps=replay_from_wire(field(sync.get(),"unpublished_steps"));
        auto pending=field(sync.get(),"pending");if(pending && pending!=Py_None)for(Py_ssize_t i=0;i<size(pending);++i){auto row=item(pending,i);CombatPublication publication;
            publication.sequence=static_cast<std::int64_t>(getnum(row.get(),"seq"));publication.combat=record_from_wire(required(field(row.get(),"combat"),"pending combat"),c.critical);
            publication.steps=replay_from_wire(field(row.get(),"steps"));ledger.pending.push_back(std::move(publication));}
        ledger.published_combat=ledger.pending.empty()?ledger.server_combat:ledger.pending.back().combat;
    }
    output=std::move(s);return true;
 } catch(const std::exception& e){if(!PyErr_Occurred())PyErr_SetString(PyExc_ValueError,e.what());return false;}
}

bool load_combat_snapshot(PyObject* runtime,PyObject* state,PyObject* descriptor,PyObject* profile,CombatSnapshot& output){
    return load_snapshot(runtime,state,descriptor,profile,output,false);
}
bool load_aim_samples(PyObject* descriptor,std::vector<AimSample>& output){return stock_native_bot_descriptor::aimSamples(descriptor,output);}
bool readServerCombat(PyObject* raw,PyObject* server_tick,const CriticalState& metadata,ServerCombat& output){
    try {
        if(!raw || !PyDict_Check(raw))throw Error("server combat is not a dictionary");
        for(const char* key:{"critical","combat_revision","combat_base_revision","combat_ack_seq",
            "combat_fire_elapsed","combat_fire_timer","stun_end_server_time_ms"})
            if(!field(raw,key))throw Error("server combat contract is missing");
        ServerCombat result;result.record=record_from_wire(raw,metadata);
        result.record.stun_end_server_time_ms=integer(field(raw,"stun_end_server_time_ms"));
        result.revision=integer(field(raw,"combat_revision"));result.base_revision=integer(field(raw,"combat_base_revision"));
        result.acknowledged_sequence=integer(field(raw,"combat_ack_seq"));
        if(server_tick && server_tick!=Py_None)result.server_tick=integer(server_tick);
        output=std::move(result);return true;
    }catch(const std::exception& e){if(!PyErr_Occurred())PyErr_SetString(PyExc_ValueError,e.what());return false;}
}
bool load_combat_mode(PyObject* runtime,PyObject* state,PyObject* descriptor,PyObject* profile,CombatModeSnapshot& output){
    try {
        CombatSnapshot snapshot;if(!load_snapshot(runtime,state,descriptor,profile,snapshot,true))return false;
        CombatModeSnapshot mode;mode.gun_config=snapshot.gun_config;mode.aim_config=std::move(snapshot.aim_config);
        mode.valid_pitch=snapshot.valid_pitch;mode.gun_mount=snapshot.gun_mount;mode.water_sensor=snapshot.water_sensor;
        mode.limited_yaw=snapshot.limited_yaw;mode.minimum_yaw=snapshot.minimum_yaw;mode.maximum_yaw=snapshot.maximum_yaw;mode.static_yaw=snapshot.static_yaw;
        mode.hydraulic=snapshot.hydraulic;mode.shells=std::move(snapshot.shells);mode.base_repair_factor=snapshot.base_repair_factor;
        mode.armor_shells=std::move(snapshot.armor_shells);
        mode.target_samples=std::move(snapshot.target_samples);
        mode.repair_speed_factor=snapshot.repair_speed_factor;mode.crew_factor=snapshot.crew_factor;mode.gun_rotation_factor=snapshot.gun_rotation_factor;
        output=std::move(mode);return true;
    }catch(const std::exception& e){if(!PyErr_Occurred())PyErr_SetString(PyExc_ValueError,e.what());return false;}
}

PyObject* combat_snapshot_value(const CombatState& state,const SiegeState& siege,
    const w::ActorState& actor,const std::vector<CriticalEvent>& events) {
 try {
    Ref result(required(PyDict_New(),"snapshot allocation"));
    setint(result.get(),"health",state.health);setint(result.get(),"display_health",state.display_health);
    setbool(result.get(),"alive",state.alive);setint(result.get(),"death_reason",state.death_reason);
    setnum(result.get(),"_drown_check",state.drown_check);setnum(result.get(),"_drown_time",state.drown_time);
    setnum(result.get(),"_water_depth",state.water_depth);setbool(result.get(),"_drowning",state.drowning);
    setbool(result.get(),"_drowned",state.drowned);setnum(result.get(),"_overturn_check",state.overturn_check);
    setnum(result.get(),"_overturn_time",state.overturn_time);setint(result.get(),"_overturn_level",state.overturn_level);
    setbool(result.get(),"_overturned",state.overturned);setnum(result.get(),"combat_fire_elapsed",state.critical.fire_elapsed);
    setnum(result.get(),"combat_fire_timer",state.critical.fire_timer);setint(result.get(),"stun_end_server_time_ms",state.stun_end_server_time_ms);
    setint(result.get(),"siege_state",siege.mode);setnum(result.get(),"_siege_time_left",siege.remaining);
    setnum(result.get(),"_siege_transition_total",siege.total);setnum(result.get(),"_siege_intent_elapsed",siege.intent_elapsed);
    if(siege.intent)setbool(result.get(),"_siege_intent",*siege.intent);else setdict(result.get(),"_siege_intent",Py_None);
    setint(result.get(),"siege_time_left_ms",siege.remaining>0?static_cast<std::int64_t>(std::ceil(siege.remaining*1000.-1.e-9)):0);
    setint(result.get(),"siege_transition_total_ms",siege.total>0?static_cast<std::int64_t>(std::ceil(siege.total*1000.-1.e-9)):0);
    Ref critical(required(PyDict_New(),"critical allocation")),devices(required(PyList_New(0),"devices allocation"));
    Ref destroyed(required(PyList_New(0),"destroyed allocation")),crew(required(PyList_New(0),"crew allocation")),roster(required(PyList_New(0),"roster allocation"));
    for(const auto& entry:state.critical.devices){const auto& d=entry.second;
        if(d.destroyed){auto name=pytext(entry.first.c_str());if(PyList_Append(destroyed.get(),name.get())<0)throw Error("destroyed append");}
        if(!d.hp)continue;
        const double hp=std::max(0.,d.hp.value_or(0.)),maximum=std::max(1.,d.wire_maximum.value_or(d.maximum.value_or(std::max(1.,std::round(hp)))));
        const char* status=d.destroyed?"destroyed":d.critical?"critical":hp<=0.?"destroyed":hp<=maximum*.5?"critical":"normal";
        Ref row(required(PyDict_New(),"device allocation"));settext(row.get(),"name",entry.first);setnum(row.get(),"hp",hp);setnum(row.get(),"max_hp",maximum);settext(row.get(),"state",status);
        if(PyList_Append(devices.get(),row.get())<0)throw Error("device append");}
    for(const auto& name:state.critical.crew_ko){auto x=pytext(name.c_str());if(PyList_Append(crew.get(),x.get())<0)throw Error("crew append");}
    for(const auto& name:state.critical.crew_roster){auto x=pytext(name.c_str());if(PyList_Append(roster.get(),x.get())<0)throw Error("roster append");}
    Ref rows(required(PyList_New(0),"event allocation"));
    const auto status=[](CriticalStatus value){return value==CriticalStatus::destroyed?"destroyed":value==CriticalStatus::critical?"critical":"normal";};
    for(const auto& event:events){Ref row(required(PyDict_New(),"event row"));
        const char* kind=event.kind==CriticalEventKind::device?"device":event.kind==CriticalEventKind::crew?"crew":event.kind==CriticalEventKind::fire?"fire":"ammo_rack";
        settext(row.get(),"kind",kind);if(!event.name.empty())settext(row.get(),"name",event.name);
        if(event.kind==CriticalEventKind::device)settext(row.get(),"old_state",status(event.old_status));
        if(event.kind==CriticalEventKind::fire)setbool(row.get(),"state",event.status==CriticalStatus::fire_on);
        else settext(row.get(),"state",status(event.status));
        if(!event.cause.empty())settext(row.get(),"cause",event.cause);
        if(PyList_Append(rows.get(),row.get())<0)throw Error("event append");}
    setdict(critical.get(),"devices",devices.get());setdict(critical.get(),"destroyed",destroyed.get());setdict(critical.get(),"crew_ko",crew.get());
    if(!state.critical.crew_roster.empty())setdict(critical.get(),"crew_roster",roster.get());
    setbool(critical.get(),"fire",state.critical.fire);setbool(critical.get(),"ammo_rack_death",state.critical.ammo_rack_death);setdict(critical.get(),"events",rows.get());
    if(state.has_critical_payload)setdict(result.get(),"critical",critical.get());
    else {Ref empty(required(PyDict_New(),"empty critical allocation"));setdict(result.get(),"critical",empty.get());}
    setint(result.get(),"clip_size",actor.gun.config.clip_size);setint(result.get(),"clip",actor.gun.clip);
    setnum(result.get(),"reload_time",actor.gun.remaining(actor.gun.reload_factor));setnum(result.get(),"reload_duration",actor.gun.duration(actor.gun.reload_factor));
    setint(result.get(),"fire_seq",actor.fire_seq);setint(result.get(),"shell_index",actor.ammo.loaded);setint(result.get(),"next_shell_index",actor.ammo.next);
    setbool(result.get(),"ammo_reload_pending",actor.ammo.reload_pending);
    Ref ammo(required(PyList_New(0),"ammo allocation"));for(int n:actor.ammo.remaining){Ref x(PyLong_FromLong(n));if(!x.get() || PyList_Append(ammo.get(),x.get())<0)throw Error("ammo append");}
    setdict(result.get(),"ammo_remaining",ammo.get());
    setbool(result.get(),"burst_active",actor.burst.active);setint(result.get(),"burst_group_seq",actor.burst.group_seq);
    setint(result.get(),"burst_count",actor.burst.count);setint(result.get(),"burst_next_index",actor.burst.next_index);
    setnum(result.get(),"burst_interval",std::round(actor.burst.interval*1.e6)/1.e6);setnum(result.get(),"burst_time_left",std::round(std::max(0.,actor.burst.time_left)*1.e6)/1.e6);
    setint(result.get(),"burst_shell_index",actor.burst.shell);setnum(result.get(),"turret_yaw",actor.aim.turret_yaw);
    setnum(result.get(),"gun_pitch",actor.aim.gun_pitch);setnum(result.get(),"desired_gun_pitch",actor.aim.desired_gun_pitch);setbool(result.get(),"gun_aligned",actor.aim.aligned);
    return result.release();
 } catch(const std::exception& e){if(!PyErr_Occurred())PyErr_SetString(PyExc_ValueError,e.what());return nullptr;}
}
} // namespace wot::offline_bot_step
