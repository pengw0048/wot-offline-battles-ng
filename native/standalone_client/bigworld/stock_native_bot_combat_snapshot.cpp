#include "stock_native_bot_combat_snapshot.hpp"
#include "stock_native_bot_descriptor.hpp"
#include "stock_native_projectile_damage_python.hpp"
#include "stock_native_bot_value.hpp"
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
bool flag(PyObject* p){if(!p || p==Py_None)return false;const int v=PyObject_IsTrue(p);if(v<0)throw Error("invalid boolean field");return v!=0;}
Py_ssize_t size(PyObject* p){if(!p || !(PyTuple_Check(p)||PyList_Check(p)))throw Error("expected sequence");return PySequence_Size(p);}
Ref item(PyObject* p,Py_ssize_t n){return Ref(required(PySequence_GetItem(p,n),"sequence index"));}
Point point(PyObject* p){Ref tuple(required(PySequence_Tuple(p),"descriptor vector"));if(PyTuple_GET_SIZE(tuple.get())!=3)throw Error("descriptor vector width");return {scalar(PyTuple_GET_ITEM(tuple.get(),0)),scalar(PyTuple_GET_ITEM(tuple.get(),1)),scalar(PyTuple_GET_ITEM(tuple.get(),2))};}
// The round seed's state row (the pinned BotRuntime.states row) is an owned Value.
using SeedValue=stock_native_client_state::Value;
using SeedKind=SeedValue::Kind;
bool has(const SeedValue& v,const char* key){return v.resolved().kind==SeedKind::Object&&v.resolved().object.count(key)>0;}
double vscalar(const SeedValue& v){const auto& r=v.resolved();if(r.kind==SeedKind::Integer)return double(r.integer);
    if(r.kind!=SeedKind::Number||!std::isfinite(r.number))throw Error("invalid numeric field");return r.number;}
double vnum(const SeedValue& d,const char* key,double fallback=0.){const auto& v=d.resolved().get(key);return v.kind==SeedKind::Null?fallback:vscalar(v);}
bool vflag(const SeedValue& d,const char* key,bool fallback=false){return has(d,key)?d.resolved().get(key).truth(false):fallback;}
std::string vtext(const SeedValue& v){if(v.resolved().kind!=SeedKind::Text)throw Error("missing text");return v.resolved().text;}
std::vector<std::string> vstrings(const SeedValue& v){std::vector<std::string> out;const auto& r=v.resolved();if(r.kind==SeedKind::Null)return out;
    if(r.kind!=SeedKind::Array)throw Error("expected sequence");for(const auto& item:r.array)out.push_back(vtext(item));return out;}
double numeric(PyObject* p,const char* key,double fallback=0.){auto v=value(p,key);if(v.get()==Py_None)return fallback;try{return scalar(v.get());}catch(const std::exception&){PyErr_Clear();return fallback;}}
double anchor(double rating,const std::array<double,4>& points){const double anchors[]={0.,.34,.67,1.};for(int i=0;i<3;++i)if(rating<=anchors[i+1]){const double f=(rating-anchors[i])/(anchors[i+1]-anchors[i]);return f<=0?points[i]:f>=1?points[i+1]:points[i]+(points[i+1]-points[i])*f;}return points[3];}
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
void fresh_ammo(PyObject* descriptor,const SeedValue& profile,const SeedValue& state,CombatSnapshot& s){
    const int count=s.gun_config.shell_count;auto& ammo=s.ammo;ammo.categories.assign(count,0);ammo.remaining.assign(count,0);std::map<int,const SeedValue*> by_index;
    const auto& rows=profile.resolved().get("shells").resolved();if(rows.kind==SeedKind::Array)for(const auto& row:rows.array){if(row.resolved().kind!=SeedKind::Object)continue;const int n=int(row.resolved().get("index").numeric(-1));if(n>=0&&n<count)by_index[n]=&row;}
    int first=-1;double baseline=0.;for(int i=0;i<count;++i){auto it=by_index.find(i);std::string kind;if(it!=by_index.end()){const auto& raw=it->second->resolved().get("kind");if(raw.resolved().kind!=SeedKind::Null)kind=vtext(raw);}std::transform(kind.begin(),kind.end(),kind.begin(),[](unsigned char c){return char(std::tolower(c));});
        if(kind.find("high_explosive")!=std::string::npos||(kind.find("explosive")!=std::string::npos&&kind.find("armor_piercing")==std::string::npos))ammo.categories[i]=2;else if(first<0){first=i;baseline=it==by_index.end()?0.:std::max(0.,it->second->resolved().get("penetration").numeric(0.));}}
    for(int i=0;i<count;++i)if(ammo.categories[i]!=2&&i!=first&&baseline>0.){auto it=by_index.find(i);if(it!=by_index.end()&&std::max(0.,it->second->resolved().get("penetration").numeric(0.))>=baseline*1.03)ammo.categories[i]=1;}
    auto gun=value(descriptor,"gun"),turret=value(descriptor,"turret"),capacity=value(descriptor,"maxAmmo");if(capacity.get()==Py_None)capacity=value(gun.get(),"maxAmmo");if(capacity.get()==Py_None)capacity=value(turret.get(),"maxAmmo");int maximum=45;try{if(capacity.get()!=Py_None)maximum=int(scalar(capacity.get()));}catch(const std::exception&){PyErr_Clear();}maximum=std::max(0,std::min(1000,maximum));
    const auto& cls=profile.resolved().get("class_tag");const bool spg=cls.resolved().kind==SeedKind::Text&&cls.resolved().text=="SPG";const std::array<double,3> weights=spg?std::array<double,3>{1.,1.,4.}:std::array<double,3>{3.,2.,1.};std::vector<int> active;for(int category:{2,1,0})if(std::find(ammo.categories.begin(),ammo.categories.end(),category)!=ammo.categories.end())active.push_back(category);double total=0.;for(int c:active)total+=weights[c];std::map<int,int> quantities;int assigned=0;for(int c:active)assigned+=(quantities[c]=int(maximum*weights[c]/total));
    auto remainders=active;std::stable_sort(remainders.begin(),remainders.end(),[&](int a,int b){return maximum*weights[a]/total-quantities[a]>maximum*weights[b]/total-quantities[b];});for(int i=0;i<maximum-assigned;++i)++quantities[remainders[std::size_t(i)%remainders.size()]];
    for(int c:active){std::vector<int> indices;for(int i=0;i<count;++i)if(ammo.categories[i]==c)indices.push_back(i);for(int i=0;i<quantities[c];++i)++ammo.remaining[indices[std::size_t(i)%indices.size()]];}
    ammo.loaded=ammo.fallback();ammo.next=ammo.loaded;
    if(has(state,"ammo_remaining")||has(state,"next_shell_index")||has(state,"ammo_reload_pending")){
        for(const char* key:{"ammo_remaining","shell_index","next_shell_index","ammo_reload_pending"})if(!has(state,key))throw Error("incomplete ammunition snapshot");const auto& remaining=state.resolved().get("ammo_remaining").resolved();if(remaining.kind!=SeedKind::Array||int(remaining.array.size())!=count)throw Error("ammunition shape mismatch");ammo.remaining.clear();for(int i=0;i<count;++i){const auto& v=remaining.array[std::size_t(i)].resolved();if(v.kind!=SeedKind::Integer||v.integer<0||v.integer>1000)throw Error("ammunition quantity invalid");ammo.remaining.push_back(int(v.integer));}ammo.loaded=int(vnum(state,"shell_index"));ammo.next=int(vnum(state,"next_shell_index"));if(state.resolved().get("ammo_reload_pending").resolved().kind!=SeedKind::Boolean)throw Error("ammunition reload flag invalid");ammo.reload_pending=vflag(state,"ammo_reload_pending");ammo.plan_pending=false;
        const int sum=std::accumulate(ammo.remaining.begin(),ammo.remaining.end(),0);if(ammo.loaded<0||ammo.loaded>=count||ammo.next<0||ammo.next>=count||(sum>0&&(ammo.remaining[ammo.next]<=0||(!ammo.reload_pending&&ammo.remaining[ammo.loaded]<=0))))throw Error("ammunition selection invalid");}
    ammo.validate(count);
}
// Accepted server rows are owned Values, read with the former dictionary rules.
using WireValue=stock_native_client_state::Value;
const WireValue* wfield(const WireValue& d,const char* key){const auto& r=d.resolved();if(r.kind!=SeedKind::Object)throw Error("expected dictionary");auto i=r.object.find(key);return i==r.object.end()?nullptr:&i->second.resolved();}
const WireValue& wrequired(const WireValue* p,const char* label){if(!p)throw Error(label);return *p;}
double wscalar(const WireValue& v){if(v.kind!=SeedKind::Integer&&v.kind!=SeedKind::Number)throw Error(v.kind==SeedKind::Null||v.kind==SeedKind::Boolean?"missing numeric field":"invalid numeric field");
    const double n=v.kind==SeedKind::Number?v.number:v.numeric();if(!std::isfinite(n))throw Error("invalid numeric field");return n;}
std::int64_t winteger(const WireValue& v){const double d=wscalar(v);const auto i=static_cast<std::int64_t>(d);if(double(i)!=d)throw Error("invalid integer field");return i;}
double wgetnum(const WireValue& d,const char* key,double fallback=0.){auto p=wfield(d,key);return !p||p->kind==SeedKind::Null?fallback:wscalar(*p);}
bool wgetflag(const WireValue& d,const char* key,bool fallback=false){auto p=wfield(d,key);return !p?fallback:p->truth(false);}
std::string wtext(const WireValue& v){if(v.kind!=SeedKind::Text)throw Error(v.kind==SeedKind::Null?"missing text":"text conversion failed");return v.text;}
std::vector<std::string> wstrings(const WireValue* p){std::vector<std::string> result;if(!p||p->kind==SeedKind::Null)return result;
    if(p->kind!=SeedKind::Array)throw Error("expected sequence");for(const auto& v:p->array)result.push_back(wtext(v.resolved()));return result;}
CriticalState critical_from_wire(const WireValue& payload,const CriticalState& metadata) {
    if(payload.kind!=SeedKind::Object)throw Error("critical record is not a dictionary");
    CriticalState result=metadata;
    for(auto& pair:result.devices){pair.second.hp.reset();pair.second.wire_maximum.reset();pair.second.destroyed=false;pair.second.critical=false;}
    result.crew_ko.clear();result.crew_roster.clear();result.fire=wgetflag(payload,"fire");result.ammo_rack_death=wgetflag(payload,"ammo_rack_death");
    for(const auto& name:wstrings(wfield(payload,"destroyed")))result.devices[name].destroyed=true;
    for(const auto& name:wstrings(wfield(payload,"crew_ko")))result.crew_ko.insert(name);
    for(const auto& name:wstrings(wfield(payload,"crew_roster")))result.crew_roster.insert(name);
    auto rows=wfield(payload,"devices");
    if(rows && rows->kind!=SeedKind::Null){if(rows->kind!=SeedKind::Array)throw Error("expected sequence");for(const auto& entry:rows->array){const auto& row=entry.resolved();
        const auto name=wtext(wrequired(wfield(row,"name"),"device name"));auto& d=result.devices[name];
        d.hp=wscalar(wrequired(wfield(row,"hp"),"device hp"));d.wire_maximum=wscalar(wrequired(wfield(row,"max_hp"),"device max"));
        const auto status=wtext(wrequired(wfield(row,"state"),"device state"));
        if(status!="normal" && status!="critical" && status!="destroyed")throw Error("invalid device state");
        d.critical=status=="critical";d.destroyed=d.destroyed || status=="destroyed";
    }}
    return result;
}
CombatRecord record_from_wire(const WireValue& raw,const CriticalState& metadata) {
    if(raw.kind!=SeedKind::Object)throw Error("combat record is not a dictionary");
    CombatRecord result;result.health=static_cast<int>(wgetnum(raw,"health"));result.alive=wgetflag(raw,"alive");
    result.critical=critical_from_wire(wrequired(wfield(raw,"critical"),"combat critical"),metadata);
    result.critical.fire_elapsed=wgetnum(raw,"combat_fire_elapsed");result.critical.fire_timer=wgetnum(raw,"combat_fire_timer");
    result.stun_end_server_time_ms=static_cast<std::int64_t>(wgetnum(raw,"stun_end_server_time_ms"));return result;
}
}

// One Bot's combat, weapons and ledgers at the round start: the seed row is the BotRuntime state and
// the gun, ammunition and burst clocks are the fresh ones battle_start installs from the descriptor.
static bool load_snapshot(const stock_native_bot::BotSeed& bot,PyObject* descriptor,CombatSnapshot& output,bool mode_only){
 try {
    const SeedValue& state=bot.state;
    if(state.resolved().kind!=SeedKind::Object || !descriptor)throw Error("missing combat bootstrap input");
    const SeedValue& profile=state.resolved().get("profile");
    CombatSnapshot s;s.fire_sequence=static_cast<std::int64_t>(vnum(state,"fire_seq"));auto& c=s.state;
    c.health=static_cast<int>(vnum(state,"health"));c.maximum_health=std::max(1,static_cast<int>(vnum(state,"max_health",c.health?c.health:1)));
    c.display_health=static_cast<int>(vnum(state,"display_health",c.health));c.alive=vflag(state,"alive",true);
    c.death_reason=static_cast<int>(vnum(state,"death_reason"));
    c.position={vnum(state,"x"),vnum(state,"y"),vnum(state,"z")};c.pitch=vnum(state,"pitch");c.roll=vnum(state,"roll");
    c.speed=vnum(state,"speed");c.movement_direction=static_cast<int>(vnum(state,"movement_dir"));c.rotation_direction=static_cast<int>(vnum(state,"rotation_dir"));
    c.drown_check=vnum(state,"_drown_check");c.drown_time=vnum(state,"_drown_time");c.water_depth=vnum(state,"_water_depth",-1.);
    c.drowning=vflag(state,"_drowning");c.drowned=vflag(state,"_drowned");c.overturn_check=vnum(state,"_overturn_check");c.overturn_time=vnum(state,"_overturn_time");
    c.overturn_level=static_cast<int>(vnum(state,"_overturn_level"));c.overturned=vflag(state,"_overturned");
    c.critical.fire_elapsed=vnum(state,"combat_fire_elapsed");c.critical.fire_timer=vnum(state,"combat_fire_timer");
    c.stun_end_server_time_ms=static_cast<std::int64_t>(vnum(state,"stun_end_server_time_ms"));c.stun_until_equipment_time=vnum(state,"_stun_until_equipment_time");
    c.has_target=state.resolved().get("target_id").resolved().kind!=SeedKind::Null;
    const SeedValue& critical=state.resolved().get("critical");
    if(critical.resolved().kind!=SeedKind::Null){
        if(critical.resolved().kind!=SeedKind::Object)throw Error("critical payload is not a dictionary");
        c.has_critical_payload=!critical.resolved().object.empty();
        c.critical.fire=vflag(critical,"fire");c.critical.ammo_rack_death=vflag(critical,"ammo_rack_death");
        for(const auto& n:vstrings(critical.resolved().get("crew_ko")))c.critical.crew_ko.insert(n);
        for(const auto& n:vstrings(critical.resolved().get("crew_roster")))c.critical.crew_roster.insert(n);
        for(const auto& n:vstrings(critical.resolved().get("destroyed")))c.critical.devices[n].destroyed=true;
        const auto& rows=critical.resolved().get("devices").resolved();
        if(rows.kind!=SeedKind::Null){if(rows.kind!=SeedKind::Array)throw Error("expected sequence");for(const auto& row:rows.array){const auto name=vtext(row.resolved().get("name"));
            auto& d=c.critical.devices[name];d.hp=vscalar(row.resolved().get("hp"));
            const auto& maximum=row.resolved().get("max_hp");if(maximum.resolved().kind!=SeedKind::Null)d.wire_maximum=vscalar(maximum);
            const auto& p=row.resolved().get("state");if(p.resolved().kind!=SeedKind::Null){const auto status=vtext(p);d.destroyed=status=="destroyed" || d.destroyed;d.critical=status=="critical";}}}
    }
    for(const char* name:{"engineHealth","ammoBayHealth","fuelTankHealth","radioHealth","gunHealth","turretRotatorHealth","surveyingDeviceHealth","leftTrackHealth","rightTrackHealth"}){const auto pool=stock_native_bot_descriptor::devicePool(descriptor,name);auto& d=c.critical.devices[name];d.maximum=pool.maximum;d.regeneration=pool.regeneration;}
    for(const auto& n:stock_native_bot_descriptor::crewRoster(descriptor))c.descriptor_crew_roster.insert(n);
    auto& g=s.gun_config;
    fresh_gun(descriptor,bot.crew_level,s);
    fresh_ammo(descriptor,profile,state,s);
    s.gun.emplace(g);s.gun->restore(mode_only?0:s.fire_sequence,mode_only?1.:critical_factor(c.critical,CriticalStat::dispersion),false,0.,0.,1.,false,0,0);
    s.burst.validate(static_cast<std::int64_t>(vnum(state,"fire_seq")));
    s.aim.turret_yaw=vnum(state,"turret_yaw");s.aim.gun_pitch=vnum(state,"gun_pitch");s.aim.desired_gun_pitch=vnum(state,"desired_gun_pitch");s.aim.aligned=vflag(state,"gun_aligned");
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
    // The round start installs no tactics overrides: the gunner bundle is the rating's anchors.
    const double rating=std::max(0.,std::min(1.,bot.rating));const auto override_number=[](const char*,double fallback){return fallback;};
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
    s.siege.mode=static_cast<int>(vnum(state,"siege_state"));s.siege.remaining=vnum(state,"_siege_time_left");s.siege.total=vnum(state,"_siege_transition_total");s.siege.intent_elapsed=vnum(state,"_siege_intent_elapsed");
    const auto& intent=state.resolved().get("_siege_intent");if(intent.resolved().kind!=SeedKind::Null)s.siege.intent=intent.truth(false);
    auto name=value(type.get(),"name");if(name.get()!=Py_None){const auto n=text(name.get());if(n=="sweden:S10_Strv_103_0_Series"||n=="sweden:S11_Strv_103B"||n=="sweden:S22_Strv_S1")s.siege_config={true,2.,1.3,2.};else if(n=="sweden:S21_UDES_03")s.siege_config={true,2.,2.,2.};}
    std::uint64_t contract=0;
    for(const auto& row:bot.equipment){EquipmentState e;e.contract_revision=++contract;
        const auto& name=row.kind;e.kind=name=="extinguisher"?EquipmentKind::extinguisher:name=="repairkit"?EquipmentKind::repairkit:name=="medkit"?EquipmentKind::medkit:
            name=="fuel"?EquipmentKind::fuel:name=="rpm_limiter"?EquipmentKind::rpm_limiter:name=="stimulator"?EquipmentKind::stimulator:EquipmentKind::other;
        e.uses_left=static_cast<int>(row.uses_left);e.ready_at=row.ready_at;
        e.cooldown_seconds=row.cooldown_seconds;e.reaction_seconds=row.auto_reaction_seconds;e.autoactivate=row.autoactivate;e.repair_all=row.repair_all;
        e.bonus_value=row.bonus_value;e.crew_level_increase=row.crew_level_increase;
        e.fire_starting_chance_factor=row.fire_starting_chance_factor;e.engine_power_factor=row.engine_power_factor;
        e.turret_rotation_speed_factor=row.turret_rotation_speed_factor;e.engine_hp_loss_per_second=row.engine_hp_loss_per_second;
        e.active=row.active;s.equipment.push_back(e);}
    const double repair_bonus=1.+std::max(0.,equipment_passives(s.equipment).repair_bonus);
    {Ref factors(required(stock_native_bot_descriptor::defaultCrewFactors(descriptor,bot.crew_level),"repair crew factors"));s.base_repair_factor=numeric(factors.get(),"repairSpeed",1.);s.initial_repair_factor=s.base_repair_factor*repair_bonus;}
    s.ledger.initialize(c,static_cast<std::int64_t>(vnum(state,"combat_revision")),
        static_cast<std::int64_t>(vnum(state,"combat_base_revision")),static_cast<std::int64_t>(vnum(state,"combat_ack_seq")));
    output=std::move(s);return true;
 } catch(const std::exception& e){if(!PyErr_Occurred())PyErr_SetString(PyExc_ValueError,e.what());return false;}
}

bool load_combat_snapshot(const stock_native_bot::BotSeed& bot,PyObject* descriptor,CombatSnapshot& output){
    return load_snapshot(bot,descriptor,output,false);
}
bool load_aim_samples(PyObject* descriptor,std::vector<AimSample>& output){return stock_native_bot_descriptor::aimSamples(descriptor,output);}
bool readServerCombat(const stock_native_client_state::Value& source,const stock_native_client_state::Value& server_tick,const CriticalState& metadata,ServerCombat& output){
    try {
        const auto& raw=source.resolved();
        if(raw.kind!=SeedKind::Object)throw Error("server combat is not a dictionary");
        for(const char* key:{"critical","combat_revision","combat_base_revision","combat_ack_seq",
            "combat_fire_elapsed","combat_fire_timer","stun_end_server_time_ms"})
            if(!wfield(raw,key))throw Error("server combat contract is missing");
        ServerCombat result;result.record=record_from_wire(raw,metadata);
        result.record.stun_end_server_time_ms=winteger(*wfield(raw,"stun_end_server_time_ms"));
        result.revision=winteger(*wfield(raw,"combat_revision"));result.base_revision=winteger(*wfield(raw,"combat_base_revision"));
        result.acknowledged_sequence=winteger(*wfield(raw,"combat_ack_seq"));
        const auto& tick=server_tick.resolved();if(tick.kind!=SeedKind::Null)result.server_tick=winteger(tick);
        output=std::move(result);return true;
    }catch(const std::exception& e){if(!PyErr_Occurred())PyErr_SetString(PyExc_ValueError,e.what());return false;}
}
bool load_combat_mode(const stock_native_bot::BotSeed& bot,PyObject* descriptor,CombatModeSnapshot& output){
    try {
        CombatSnapshot snapshot;if(!load_snapshot(bot,descriptor,snapshot,true))return false;
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

stock_native_client_state::Value combat_snapshot_value(const CombatState& state,const SiegeState& siege,
    const w::ActorState& actor,const std::vector<CriticalEvent>& events) {
    namespace v=stock_native_bot_value;
    auto out=v::object();auto& r=out.object;
    r["health"]=v::integer(state.health);r["display_health"]=v::integer(state.display_health);
    r["alive"]=v::boolean(state.alive);r["death_reason"]=v::integer(state.death_reason);
    r["_drown_check"]=v::real(state.drown_check);r["_drown_time"]=v::real(state.drown_time);
    r["_water_depth"]=v::real(state.water_depth);r["_drowning"]=v::boolean(state.drowning);
    r["_drowned"]=v::boolean(state.drowned);r["_overturn_check"]=v::real(state.overturn_check);
    r["_overturn_time"]=v::real(state.overturn_time);r["_overturn_level"]=v::integer(state.overturn_level);
    r["_overturned"]=v::boolean(state.overturned);r["combat_fire_elapsed"]=v::real(state.critical.fire_elapsed);
    r["combat_fire_timer"]=v::real(state.critical.fire_timer);r["stun_end_server_time_ms"]=v::integer(state.stun_end_server_time_ms);
    r["siege_state"]=v::integer(siege.mode);r["_siege_time_left"]=v::real(siege.remaining);
    r["_siege_transition_total"]=v::real(siege.total);r["_siege_intent_elapsed"]=v::real(siege.intent_elapsed);
    r["_siege_intent"]=siege.intent?v::boolean(*siege.intent):v::null();
    r["siege_time_left_ms"]=v::integer(siege.remaining>0?static_cast<std::int64_t>(std::ceil(siege.remaining*1000.-1.e-9)):0);
    r["siege_transition_total_ms"]=v::integer(siege.total>0?static_cast<std::int64_t>(std::ceil(siege.total*1000.-1.e-9)):0);
    auto critical=v::object(),devices=v::array(),destroyed=v::array(),crew=v::array(),roster=v::array();
    for(const auto& entry:state.critical.devices){const auto& d=entry.second;
        if(d.destroyed)destroyed.array.push_back(v::text(entry.first));
        if(!d.hp)continue;
        const double hp=std::max(0.,d.hp.value_or(0.)),maximum=std::max(1.,d.wire_maximum.value_or(d.maximum.value_or(std::max(1.,std::round(hp)))));
        const char* status=d.destroyed?"destroyed":d.critical?"critical":hp<=0.?"destroyed":hp<=maximum*.5?"critical":"normal";
        auto row=v::object();row.object["name"]=v::text(entry.first);row.object["hp"]=v::real(hp);row.object["max_hp"]=v::real(maximum);row.object["state"]=v::text(status);
        devices.array.push_back(std::move(row));}
    for(const auto& name:state.critical.crew_ko)crew.array.push_back(v::text(name));
    for(const auto& name:state.critical.crew_roster)roster.array.push_back(v::text(name));
    auto rows=v::array();
    const auto status=[](CriticalStatus value){return value==CriticalStatus::destroyed?"destroyed":value==CriticalStatus::critical?"critical":"normal";};
    for(const auto& event:events){auto row=v::object();auto& e=row.object;
        const char* kind=event.kind==CriticalEventKind::device?"device":event.kind==CriticalEventKind::crew?"crew":event.kind==CriticalEventKind::fire?"fire":"ammo_rack";
        e["kind"]=v::text(kind);if(!event.name.empty())e["name"]=v::text(event.name);
        if(event.kind==CriticalEventKind::device)e["old_state"]=v::text(status(event.old_status));
        if(event.kind==CriticalEventKind::fire)e["state"]=v::boolean(event.status==CriticalStatus::fire_on);
        else e["state"]=v::text(status(event.status));
        if(!event.cause.empty())e["cause"]=v::text(event.cause);
        rows.array.push_back(std::move(row));}
    critical.object["devices"]=std::move(devices);critical.object["destroyed"]=std::move(destroyed);critical.object["crew_ko"]=std::move(crew);
    if(!state.critical.crew_roster.empty())critical.object["crew_roster"]=std::move(roster);
    critical.object["fire"]=v::boolean(state.critical.fire);critical.object["ammo_rack_death"]=v::boolean(state.critical.ammo_rack_death);critical.object["events"]=std::move(rows);
    r["critical"]=state.has_critical_payload?std::move(critical):v::object();
    r["clip_size"]=v::integer(actor.gun.config.clip_size);r["clip"]=v::integer(actor.gun.clip);
    r["reload_time"]=v::real(actor.gun.remaining(actor.gun.reload_factor));r["reload_duration"]=v::real(actor.gun.duration(actor.gun.reload_factor));
    r["fire_seq"]=v::integer(actor.fire_seq);r["shell_index"]=v::integer(actor.ammo.loaded);r["next_shell_index"]=v::integer(actor.ammo.next);
    r["ammo_reload_pending"]=v::boolean(actor.ammo.reload_pending);
    r["ammo_remaining"]=v::integers(actor.ammo.remaining);
    r["burst_active"]=v::boolean(actor.burst.active);r["burst_group_seq"]=v::integer(actor.burst.group_seq);
    r["burst_count"]=v::integer(actor.burst.count);r["burst_next_index"]=v::integer(actor.burst.next_index);
    r["burst_interval"]=v::real(std::round(actor.burst.interval*1.e6)/1.e6);r["burst_time_left"]=v::real(std::round(std::max(0.,actor.burst.time_left)*1.e6)/1.e6);
    r["burst_shell_index"]=v::integer(actor.burst.shell);r["turret_yaw"]=v::real(actor.aim.turret_yaw);
    r["gun_pitch"]=v::real(actor.aim.gun_pitch);r["desired_gun_pitch"]=v::real(actor.aim.desired_gun_pitch);r["gun_aligned"]=v::boolean(actor.aim.aligned);
    return out;
}
} // namespace wot::offline_bot_step
