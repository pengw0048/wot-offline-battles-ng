#include "stock_native_bot_human_snapshot.hpp"
#include "stock_native_bot_combat_snapshot.hpp"
#include "stock_native_bot_descriptor.hpp"
#include <Python.h>
#ifdef hypot
#undef hypot
#endif
#include <algorithm>
#include <cmath>
#include <limits>
#include <set>
#include <stdexcept>
namespace stock_native_bot {
namespace {
struct Error:std::runtime_error{using std::runtime_error::runtime_error;};
struct Ref {PyObject*p=nullptr;explicit Ref(PyObject*v=nullptr):p(v){}~Ref(){Py_XDECREF(p);}Ref(const Ref&)=delete;Ref&operator=(const Ref&)=delete;Ref(Ref&&r):p(r.p){r.p=nullptr;}PyObject*get()const{return p;}};
PyObject* need(PyObject*p,const char*label){if(!p)throw Error(label);return p;}
PyObject* field(PyObject*p,const char*name){if(!p||!PyDict_Check(p))throw Error("human input is not a dictionary");return PyDict_GetItemString(p,name);}
Ref attr(PyObject*p,const char*name){return Ref(need(PyObject_GetAttrString(p,name),name));}
Ref value(PyObject*p,const char*name){if(PyDict_Check(p)){auto v=field(p,name);if(!v)v=Py_None;Py_INCREF(v);return Ref(v);}auto v=PyObject_GetAttrString(p,name);if(!v&&PyErr_ExceptionMatches(PyExc_AttributeError)){PyErr_Clear();Py_INCREF(Py_None);return Ref(Py_None);}return Ref(need(v,name));}
// Canonical snapshot static arrays are read-only list subclasses.
Py_ssize_t size(PyObject*p){if(!p || !(PyTuple_Check(p)||PyList_Check(p)))throw Error("human sequence shape");return PySequence_Size(p);}
Ref item(PyObject*p,Py_ssize_t i){return Ref(need(PySequence_GetItem(p,i),"human sequence index"));}
double number(PyObject*p){if(!p||p==Py_None||PyBool_Check(p))throw Error("human numeric field missing");
#if PY_MAJOR_VERSION>=3
 if(!PyFloat_CheckExact(p)&&!PyLong_CheckExact(p))throw Error("human number must be plain data");
#else
 if(!PyFloat_CheckExact(p)&&!PyInt_CheckExact(p)&&!PyLong_CheckExact(p))throw Error("human number must be plain data");
#endif
 const double v=PyFloat_AsDouble(p);if(PyErr_Occurred()||!std::isfinite(v))throw Error("human numeric field invalid");return v;}
double num(PyObject*p,const char*n,double def=0.){auto v=field(p,n);return !v||v==Py_None?def:number(v);}
ID integer(PyObject*p){const double v=number(p);if(v<double(std::numeric_limits<ID>::min())||v>=double(std::numeric_limits<ID>::max())||std::trunc(v)!=v)throw Error("human integer field invalid");return static_cast<ID>(v);}
ID idfield(PyObject*p,const char*n,ID def=0){auto v=field(p,n);return !v||v==Py_None?def:integer(v);}
bool flag(PyObject*p,const char*n,bool def=false){auto v=field(p,n);if(!v)return def;
 if(v==Py_True)return true;if(v==Py_False||v==Py_None)return false;
#if PY_MAJOR_VERSION>=3
 if(!PyLong_CheckExact(v)&&!PyFloat_CheckExact(v))throw Error("human flag must be plain data");
#else
 if(!PyInt_CheckExact(v)&&!PyLong_CheckExact(v)&&!PyFloat_CheckExact(v))throw Error("human flag must be plain data");
#endif
 const int f=PyObject_IsTrue(v);if(f<0)throw Error("human flag invalid");return f!=0;}
std::string text(PyObject*p){if(!p||p==Py_None)return {};
#if PY_MAJOR_VERSION>=3
 auto v=PyUnicode_AsUTF8(p);if(!v)throw Error("human string invalid");return v;
#else
 if(PyString_Check(p))return std::string(PyString_AS_STRING(p),PyString_GET_SIZE(p));Ref v(PyUnicode_AsUTF8String(p));need(v.get(),"human unicode");return std::string(PyString_AS_STRING(v.get()),PyString_GET_SIZE(v.get()));
#endif
}
Ref str(const char*p){
#if PY_MAJOR_VERSION>=3
 return Ref(need(PyUnicode_FromString(p),"human string allocation"));
#else
 return Ref(need(PyString_FromString(p),"human string allocation"));
#endif
}
Point point(PyObject*p){if(size(p)<3)throw Error("human point shape");auto x=item(p,0),y=item(p,1),z=item(p,2);return {number(x.get()),number(y.get()),number(z.get())};}
// Descriptor vectors are BigWorld Math.Vector3, a native indexable type,
// not necessarily a Python tuple/list. This is used only at installation.
Point descriptorPoint(PyObject* p){if(!p||p==Py_None)throw Error("human descriptor point missing");Point out{};for(int i=0;i<3;++i){Ref v(need(PySequence_GetItem(p,i),"human descriptor vector index"));out[i]=number(v.get());}return out;}
Point position(PyObject*p){auto v=field(p,"position");return v&&v!=Py_None&&size(v)>0?point(v):Point{num(p,"x"),num(p,"y"),num(p,"z")};}
double moduleConstant(PyObject*module,const char*name){auto p=attr(module,name);return number(p.get());}
contacts::Profile profile(PyObject* p){contacts::Profile v;if(!p||p==Py_None)return v;
 v.spall=num(p,"spall_coefficient",1.);v.bonus=num(p,"ramming_bonus");auto a=field(p,"contact_armor");if(a&&a!=Py_None)v.contact_armor=number(a);return v;}
void readCheckpoints(PyObject*raw,contacts::Human& h){auto rows=field(raw,"tank_pushes");h.checkpoint_field_present=rows!=nullptr;if(!rows)return;
 try{const auto n=size(rows);if(n>30)throw Error("checkpoint count");std::set<ID> ids;for(Py_ssize_t i=0;i<n;++i){auto row=item(rows,i);if(size(row.get())!=7)throw Error("checkpoint shape");
  auto a=item(row.get(),0),s=item(row.get(),1);contacts::Checkpoint c;c.actor=integer(a.get());c.sequence=integer(s.get());
  if(c.actor<1||c.sequence<1||c.actor>2147483647||c.sequence>2147483647||!ids.insert(c.actor).second)throw Error("checkpoint identity");
  for(int j=0;j<5;++j){auto v=item(row.get(),j+2);c.totals[j]=number(v.get());if(std::abs(c.totals[j])>1.e12)throw Error("checkpoint total");}h.checkpoints.push_back(c);}}
 catch(...){PyErr_Clear();h.checkpoints.clear();h.checkpoints_valid=false;}}
void readReceipt(PyObject* raw,PyObject* row,contacts::Human& h){if(!row||!PyDict_Check(row))return;
 contacts::Receipt r;try{r.sequence=idfield(row,"seq");r.bot=idfield(row,"bot_id");if(r.sequence<=0||r.bot<=0)return;
  auto history=field(row,"_ram_contact_bot_state");if(!history)history=field(raw,"_ram_contact_bot_state");r.has_history=history&&PyDict_Check(history);
  if(r.has_history){r.history.id=idfield(history,"id");r.history.team=idfield(history,"team");r.history.x=num(history,"x");r.history.y=num(history,"y");r.history.z=num(history,"z");
   r.history.yaw=num(history,"yaw");r.history.pitch=num(history,"pitch");r.history.roll=num(history,"roll");r.history.mass=num(history,"mass",25000.);
   auto shape=field(history,"collision_shape");if(size(shape)!=4)throw Error("history shape");for(int j=0;j<4;++j){auto v=item(shape,j);r.history.shape[j]=number(v.get());}
   r.history_profile=profile(field(history,"ram_profile"));}
  const char* player_pos[]={"x","y","z"},*player_vel[]={"vx","vy","vz"},*bot_vel[]={"bot_vx","bot_vy","bot_vz"},*hit[]={"contact_x","contact_y","contact_z"};
  for(int j=0;j<3;++j){r.player_position[j]=number(need(field(row,player_pos[j]),"receipt position"));r.player_velocity[j]=number(need(field(row,player_vel[j]),"receipt velocity"));
   r.bot_velocity[j]=number(need(field(row,bot_vel[j]),"receipt bot velocity"));r.hit[j]=number(need(field(row,hit[j]),"receipt hit"));}
  r.player_angles={number(need(field(row,"yaw"),"receipt yaw")),num(row,"pitch"),num(row,"roll")};
  number(need(field(row,"presentation_time_us"),"receipt time"));r.armor_player=number(need(field(row,"contact_armor_player"),"receipt armor"));
  r.armor_bot=number(need(field(row,"contact_armor_bot"),"receipt armor"));r.spall_player=num(row,"contact_spall_player");r.bonus_player=num(row,"contact_bonus_player",-1.);
  r.player_normal={number(need(field(row,"contact_normal_x"),"receipt normal")),number(need(field(row,"contact_normal_z"),"receipt normal"))};
  r.player_screened=flag(row,"contact_screened_player");r.bot_screened=flag(row,"contact_screened_bot");
  for(const auto& pair:{std::make_pair("track_contacts_bot",&r.bot_tracks),std::make_pair("track_contacts_player",&r.player_tracks)}){auto list=field(row,pair.first);
   if(list&&list!=Py_None)for(Py_ssize_t i=0;i<size(list);++i){auto v=item(list,i);pair.second->push_back(text(v.get()));}}
  r.valid=true;
 }catch(...){PyErr_Clear();r.valid=false;}h.receipts.push_back(std::move(r));(void)raw;}
}
namespace {
double requiredNumber(PyObject*object,const char*name){return number(need(field(object,name),name));}
bool requiredFlag(PyObject*object,const char*name){auto value=need(field(object,name),name);if(!PyBool_Check(value))throw Error("human projection flag is not boolean");return value==Py_True;}
double descriptorNumber(PyObject*object,const char*name,double fallback){if(!object||object==Py_None)return fallback;if(PyDict_Check(object)){auto p=field(object,name);return !p?fallback:p==Py_None?0.:number(p);}Ref p(PyObject_GetAttrString(object,name));if(!p.get()){if(PyErr_ExceptionMatches(PyExc_AttributeError)){PyErr_Clear();return fallback;}throw Error("human descriptor projection read failed");}return p.get()==Py_None?0.:number(p.get());}
double boundedNumber(PyObject*object,const char*name,double low,double high){const auto result=requiredNumber(object,name);if(result<low||result>high)throw Error("human projection number is outside range");return result;}
}
bool readHumanProjections(PyObject*raw,PyObject*snapshot,PyObject*descriptor,HumanDescriptor&output){
 try{
  auto*crew=need(field(snapshot,"crew"),"human crew snapshot missing"),*dynamic=need(field(crew,"dynamic_spotting"),"human dynamic spotting missing"),*roster=need(field(dynamic,"crew"),"human crew roster missing"),*states=need(field(dynamic,"states"),"human dynamic states missing"),*members=need(field(crew,"members"),"human crew members missing");
  std::vector<std::string> names;for(Py_ssize_t i=0;i<size(roster);++i){auto name=item(roster,i);const auto parsed=text(name.get());if(parsed.empty()||std::find(names.begin(),names.end(),parsed)!=names.end())throw Error("human crew roster is invalid");names.push_back(parsed);}if(names.empty()||names.size()>6||size(members)!=Py_ssize_t(names.size()))throw Error("human crew projection shape is invalid");
  const std::size_t masks=std::size_t(1)<<names.size();if(!PyDict_Check(states)||PyDict_Size(states)!=Py_ssize_t(masks*2))throw Error("human dynamic state count is invalid");
  struct Skill {std::size_t carrier;std::string name;double level;};std::vector<Skill> skills;std::set<std::string> physical;
  for(Py_ssize_t i=0;i<size(members);++i){auto member=item(members,i);const auto name=text(field(member.get(),"instance"));const auto carrier=std::find(names.begin(),names.end(),name);if(carrier==names.end()||!physical.insert(name).second)throw Error("human crew member identity is invalid");auto*rows=need(field(member.get(),"skills"),"human crew skills missing");for(Py_ssize_t n=0;n<size(rows);++n){auto skill=item(rows,n);if(requiredFlag(skill.get(),"active")&&requiredFlag(skill.get(),"enabled"))skills.push_back({std::size_t(carrier-names.begin()),text(field(skill.get(),"name")),boundedNumber(skill.get(),"level",0.,100.)});}}
  auto*profile=need(field(snapshot,"spotting"),"human spotting profile missing"),*camouflage=need(field(snapshot,"camouflage"),"human camouflage missing"),*loadout=need(field(snapshot,"loadout"),"human loadout missing");
  const auto crew_vision=boundedNumber(profile,"vision_factor",0.,100.),binocular_factor=boundedNumber(profile,"binocular_factor",0.,100.),binocular_delay=boundedNumber(profile,"binocular_delay",0.,100.),net_delay=boundedNumber(profile,"camouflage_net_delay",0.,100.),shot=boundedNumber(camouflage,"shot_factor",0.,1.),radio_factor=boundedNumber(loadout,"radio_factor",0.,100.);
  const bool binoculars=requiredFlag(profile,"has_binoculars"),net=requiredFlag(profile,"has_camouflage_net");auto turret=value(descriptor,"turret"),misc=value(descriptor,"miscAttrs"),radio=value(descriptor,"radio");const auto vision_base=descriptorNumber(turret.get(),"circularVisionRadius",330.),vision_misc=descriptorNumber(misc.get(),"circularVisionRadiusFactor",1.),radio_base=descriptorNumber(radio.get(),"distance",0.);
  std::vector<HumanProjection> rows;rows.reserve(masks*6);const ID id=output.id>0?output.id:idfield(raw,"network_id",idfield(raw,"id"));if(id<=0)throw Error("human projection actor identity is invalid");
  for(std::size_t mask=0;mask<masks;++mask){double relay=0.;bool last_effort=false,designated=false;for(const auto&skill:skills){if(mask&(std::size_t(1)<<skill.carrier))continue;if(skill.name=="radioman_retransmitter")relay=std::max(relay,skill.level);if(skill.level>=100.){last_effort=last_effort||skill.name=="radioman_lasteffort";designated=designated||skill.name=="gunner_rancorous";}}
   for(int fire=0;fire<2;++fire){const auto key=std::to_string(mask)+":"+std::to_string(fire);auto*state=need(PyDict_GetItemString(states,key.c_str()),"human dynamic spotting row missing");const auto vision=boundedNumber(state,"vision",0.,10.),signal=boundedNumber(state,"signal",0.,10.),moving=boundedNumber(state,"base_moving",0.,100.),still=boundedNumber(state,"base_still",0.,100.);auto*invis_moving=need(field(state,"invisibility_moving"),"human moving invisibility missing"),*invis_still=need(field(state,"invisibility_still"),"human still invisibility missing");if(size(invis_moving)!=2||size(invis_still)!=2)throw Error("human invisibility shape is invalid");auto ma=item(invis_moving,0),mm=item(invis_moving,1),sa=item(invis_still,0),sm=item(invis_still,1);const auto moving_add=number(ma.get()),moving_multiplier=number(mm.get()),still_add=number(sa.get()),still_multiplier=number(sm.get());if(std::abs(moving_add)>100.||std::abs(still_add)>100.||moving_multiplier<0.||moving_multiplier>100.||still_multiplier<0.||still_multiplier>100.)throw Error("human invisibility values are outside range");
    for(int damage=0;damage<3;++damage){const auto module=damage==2?.25:damage==1?.5:1.;const auto factor=std::clamp(vision*module,.5,1.);HumanProjection row;auto&c=row.config;c.key={offline_simulation::ActorKind::Human,id};c.team=int(idfield(raw,"team"));c.slot=int(idfield(raw,"slot"));c.half_length=3.5;c.half_width=1.7;double view=std::max(50.,vision_base);view*=std::max(0.,vision_misc*factor);view*=std::max(0.,crew_vision);c.view_moving=std::max(50.,view);c.view_still=std::max(50.,binoculars?view*std::max(1.,binocular_factor):view);c.view_delay=binoculars?binocular_delay:-1.;c.camo_still=still;c.camo_moving=moving;c.camo_add=moving_add;c.camo_net_add=still_add-moving_add;c.camo_multiplier=moving_multiplier;c.camo_net_multiplier=still_multiplier;c.camo_shot=shot;c.camo_net_delay=net?net_delay:-1.;c.radio=std::max(0.,radio_base*(radio_factor*signal));c.relay=relay*.001;row.last_effort=last_effort;row.designated=designated;rows.push_back(std::move(row));}
   }
  }
  output.crew=std::move(names);output.projections=std::move(rows);return true;
 }catch(const std::exception&e){if(!PyErr_Occurred())PyErr_SetString(PyExc_ValueError,e.what());return false;}
}
namespace {
void pivotGeometry(PyObject*descriptor,HumanDescriptor&out){try{auto chassis=value(descriptor,"chassis"),physics=value(descriptor,"physics"),around=value(chassis.get(),"rotationIsAroundCenter"),gauge=value(physics.get(),"trackCenterOffset");if(!PyBool_Check(around.get())||gauge.get()==Py_None)return;const double half=std::abs(number(gauge.get()));if(half<=0.||half>100.)return;out.pivot_available=true;out.rotation_around_center=around.get()==Py_True;out.track_center=half;}catch(const std::exception&){PyErr_Clear();}}
double primaryArmor(PyObject*component){auto armor=value(component,"primaryArmor");const auto scalar=[](PyObject*p){try{return number(p);}catch(const std::exception&){PyErr_Clear();return 0.;}};if(armor.get()==Py_None)return 0.;if(PyList_Check(armor.get())||PyTuple_Check(armor.get())){double result=0.;bool first=true;for(Py_ssize_t i=0;i<size(armor.get());++i){auto v=item(armor.get(),i);const auto n=scalar(v.get());result=first?n:std::max(result,n);first=false;}return result;}return scalar(armor.get());}
void tacticalMetadata(PyObject*descriptor,HumanDescriptor&out){auto type=value(descriptor,"type"),tags=value(type.get(),"tags"),hull=value(descriptor,"hull"),turret=value(descriptor,"turret");out.class_tag="mediumTank";if(tags.get()!=Py_None){Ref list(PySequence_Fast(tags.get(),"human vehicle tags are not iterable"));if(list.get()){const auto n=PySequence_Fast_GET_SIZE(list.get());for(const char*candidate:{"heavyTank","mediumTank","lightTank","AT-SPG","SPG"}){bool found=false;for(Py_ssize_t i=0;i<n;++i)if(text(PySequence_Fast_GET_ITEM(list.get(),i))==candidate){found=true;break;}if(found){out.class_tag=candidate;break;}}}else PyErr_Clear();}out.armor=std::max(0.,std::max(primaryArmor(hull.get()),primaryArmor(turret.get())));}
void driveCaps(PyObject*descriptor,PyObject*effective,HumanDescriptor&out){auto physics=value(descriptor,"physics"),limits=value(physics.get(),"speedLimits");double spawn_forward=32./3.6;if(limits.get()!=Py_None){auto v=item(limits.get(),0);spawn_forward=number(v.get());}const auto forward=boundedNumber(effective,"speedFwd",0.,1000.),reverse=boundedNumber(effective,"speedBwd",0.,1000.);out.spawn_drive_cap=stock_native_bot_descriptor::driveCap(descriptor,spawn_forward,0.,0.);out.contact_drive_forward=stock_native_bot_descriptor::driveCap(descriptor,forward,reverse,1.);out.contact_drive_reverse=stock_native_bot_descriptor::driveCap(descriptor,forward,reverse,-1.);}
}
bool readHumanDescriptor(PyObject* runtime,PyObject* raw,PyObject* descriptor,HumanDescriptor& output){
 try {
  (void)runtime;HumanDescriptor out;
  auto descriptor_physics=value(descriptor,"physics"),descriptor_weight=value(descriptor_physics.get(),"weight");out.contact_mass=number(descriptor_weight.get());if(out.contact_mass<=0.)throw Error("human contact mass unavailable");
  const auto body=stock_native_bot_descriptor::bodyBounds(descriptor);out.body_minimum=body.minimum;out.body_maximum=body.maximum;out.shape=stock_native_bot_descriptor::chassisShape(descriptor);pivotGeometry(descriptor,out);tacticalMetadata(descriptor,out);
  out.id=idfield(raw,"network_id",idfield(raw,"id"));if(out.id<=0)throw Error("human identity invalid");out.vehicle=text(field(raw,"vehicle"));if(out.vehicle.empty()){out.class_tag="unknown";out.armor=0.;}
  auto*snapshot=need(field(raw,"effective_params"),"human effective parameters missing"),*physics=need(field(snapshot,"physics"),"human effective physics missing");
  if(idfield(snapshot,"version")!=1)throw Error("human effective schema version is invalid");out.mass=boundedNumber(physics,"mass",1.,1000000.);auto*ramming=need(field(snapshot,"ramming"),"human ramming profile missing");if(!PyDict_Check(ramming)||PyDict_Size(ramming)!=2)throw Error("human ramming profile shape is invalid");out.ram.spall=boundedNumber(ramming,"spall_coefficient",1.,100.);out.ram.bonus=boundedNumber(ramming,"ramming_bonus",0.,.15);out.contact_rotation_speed=std::abs(boundedNumber(physics,"rotSpd",0.,100.));driveCaps(descriptor,physics,out);
  Ref physics_module(need(PyImport_ImportModule("gui.mods.offline_lan_0922.vehicle_physics"),"physics constants import"));auto*terrain=need(field(physics,"terrainResist"),"human terrain resistance missing");if(size(terrain)!=3)throw Error("human terrain resistance shape is invalid");for(unsigned i=0;i<3;++i){auto v=item(terrain,i);const auto n=number(v.get());if(n<.000001||n>1000000.)throw Error("human terrain resistance is outside range");}auto resistance=item(terrain,0);
  out.gravity=moduleConstant(physics_module.get(),"GRAVITY");out.slide_hold=moduleConstant(physics_module.get(),"SLIDE_HOLD_TAN");out.side_min_y=moduleConstant(physics_module.get(),"SLOPE_GRIP_SDW_MIN_Y");out.side_full_y=moduleConstant(physics_module.get(),"SLOPE_GRIP_SDW_FULL_Y");out.side_min=moduleConstant(physics_module.get(),"SLOPE_GRIP_SDW_MIN");out.side_full=moduleConstant(physics_module.get(),"SLOPE_GRIP_SDW_FULL");out.rolling_decel=out.mass*boundedNumber(physics,"specificFriction",0.,1000.)*moduleConstant(physics_module.get(),"GRAVITY_FACTOR")*number(resistance.get())/std::max(out.mass,1.);
  out.vision_maximum=stock_native_bot_descriptor::devicePool(descriptor,"surveyingDeviceHealth").maximum;out.radio_maximum=stock_native_bot_descriptor::devicePool(descriptor,"radioHealth").maximum;
  auto&base=out.template_actor;const auto checkpoints=stock_native_bot_descriptor::checkpoints(descriptor);if(checkpoints){base.checkpoints.available=true;base.checkpoints.points=*checkpoints;}auto chassis=value(descriptor,"chassis"),hull=value(descriptor,"hull"),gun=value(descriptor,"gun"),mount=value(chassis.get(),"hullPosition"),turrets=value(hull.get(),"turretPositions");if(mount.get()!=Py_None&&turrets.get()!=Py_None&&size(turrets.get())>0){const auto h=descriptorPoint(mount.get());auto t=item(turrets.get(),0);const auto v=descriptorPoint(t.get());base.checkpoints.turret_mount_available=true;for(int i=0;i<3;++i)base.checkpoints.turret_mount[i]=h[i]+v[i];}auto yaw=value(gun.get(),"staticTurretYaw");if(yaw.get()!=Py_None){base.checkpoints.static_turret_yaw_present=true;base.checkpoints.static_turret_yaw=number(yaw.get());}
  if(!step::load_aim_samples(descriptor,out.aim_samples)||!readHumanProjections(raw,snapshot,descriptor,out))return false;
  auto wire_kind=str("human");Ref id(need(PyLong_FromLongLong(out.id),"human hash id")),wire(need(PyTuple_Pack(2,wire_kind.get(),id.get()),"human hash key"));const auto hash=PyObject_Hash(wire.get());if(hash==-1&&PyErr_Occurred())throw Error("human hash failed");for(auto&row:out.projections)row.config.python_hash=hash;
  base.config=out.projections.front().config;base.sample.key=base.config.key;base.identity=std::uint64_t(out.id);base.target_available=true;output=std::move(out);return true;
 }catch(const std::exception&e){if(!PyErr_Occurred())PyErr_SetString(PyExc_ValueError,e.what());return false;}
}

namespace {
int module_state(PyObject* critical,const char* name,const std::optional<double>& maximum){
 if(!critical||!PyDict_Check(critical))return 0;auto destroyed=field(critical,"destroyed");
 if(destroyed&&destroyed!=Py_None)for(Py_ssize_t i=0;i<size(destroyed);++i){auto row=item(destroyed,i);if(text(row.get())==name)return 2;}
 auto devices=field(critical,"devices");if(!devices||devices==Py_None)return 0;
 for(Py_ssize_t i=0;i<size(devices);++i){auto row=item(devices,i);if(!PyDict_Check(row.get())||text(field(row.get(),"name"))!=name)continue;
  auto hp=field(row.get(),"hp");if(!hp||hp==Py_None)return 0;if(text(field(row.get(),"state"))=="critical")return 1;
  const double value=number(hp);return maximum&&value>0.&&value<=*maximum*.5?1:0;}
 return 0;
}
}
bool readHumanInput(PyObject* raw,const HumanDescriptor& descriptor,HumanIngress& output){
 try {
  if(!raw||!PyDict_Check(raw))throw Error("human input is not plain data");const ID id=idfield(raw,"id");
  if(id<=0||id!=descriptor.id)throw Error("human identity mismatch");const int team=int(idfield(raw,"team")),slot=int(idfield(raw,"slot"));
  const bool alive=flag(raw,"alive",true);const double yaw=num(raw,"yaw"),pitch=num(raw,"pitch"),roll=num(raw,"roll");
  const double speed=alive?num(raw,"speed"):0.;const Point p=position(raw);auto critical=field(raw,"critical");
  if(critical&&critical!=Py_None&&!PyDict_Check(critical))critical=nullptr;std::set<std::string> knocked;
  if(critical){auto list=field(critical,"crew_ko");if(list&&list!=Py_None)for(Py_ssize_t i=0;i<size(list);++i){auto v=item(list,i);knocked.insert(text(v.get()));}}
  std::size_t mask=0;for(const auto& name:knocked){const auto it=std::find(descriptor.crew.begin(),descriptor.crew.end(),name);
   if(it==descriptor.crew.end())throw Error("human crew outside projection");mask|=std::size_t(1)<<std::distance(descriptor.crew.begin(),it);}
  const bool fire=critical&&flag(critical,"fire");const int vision=module_state(critical,"surveyingDeviceHealth",descriptor.vision_maximum);
  const auto index=(mask*2+std::size_t(fire))*3+std::size_t(vision);if(index>=descriptor.projections.size())throw Error("human projection missing");
  const auto& projection=descriptor.projections[index];HumanIngress out;
  out.human.contact_mass=descriptor.contact_mass;
  out.human.body_minimum=descriptor.body_minimum;out.human.body_maximum=descriptor.body_maximum;
  out.human.spawn_anchor=descriptor.spawn_anchor;out.human.spawn_drive_cap=descriptor.spawn_drive_cap;
  out.human.contact_drive_forward=descriptor.contact_drive_forward;out.human.contact_drive_reverse=descriptor.contact_drive_reverse;
  out.human.contact_rotation_speed=descriptor.contact_rotation_speed;out.human.pivot_available=descriptor.pivot_available;
  out.human.rotation_around_center=descriptor.rotation_around_center;out.human.track_center=descriptor.track_center;out.human.perception=descriptor.template_actor;
  auto& perceive=out.human.perception;perceive.config=projection.config;perceive.config.team=team;perceive.config.slot=slot;
  perceive.identity=descriptor.record_revision;perceive.target_available=descriptor.target_available;
  const int radio=module_state(critical,"radioHealth",descriptor.radio_maximum);perceive.config.radio*=radio==2?.25:radio==1?.5:1.;
  auto& sample=perceive.sample;sample.key={offline_simulation::ActorKind::Human,id};sample.alive=alive;
  sample.last_effort=projection.last_effort;sample.designated=projection.designated;sample.decision_due=true;sample.has_selected=false;
  auto seq=field(raw,"fire_seq");sample.fire_sequence=seq&&seq!=Py_None?std::max<ID>(0,integer(seq)):-1;
  sample.pose.position=p;sample.pose.mask=1u|2u|4u|8u|(1u<<9);
  const char* names[]={"x","y","z","yaw","pitch","roll","aim_yaw","turret_yaw","gun_pitch","speed"};
  for(int i=0;i<10;++i){sample.pose.values[i]=num(raw,names[i]);if(field(raw,names[i]))sample.pose.mask|=1u<<i;}
  Point velocity{};auto raw_velocity=field(raw,"velocity");if(raw_velocity&&raw_velocity!=Py_None){velocity=point(raw_velocity);sample.pose.mask|=1u<<10;}
  for(int i=0;i<3;++i)sample.pose.values[10+i]=velocity[i];
  auto& observer=perceive.observer_pose;observer.position=p;observer.yaw=yaw;observer.pitch=pitch;observer.roll=roll;
  auto turret_value=field(raw,"turret_yaw");observer.relative_turret_yaw=turret_value&&turret_value!=Py_None?num(raw,"turret_yaw"):num(raw,"aim_yaw",yaw)-yaw;perceive.target_pose=observer;
  auto& human=out.human.contacts;human.network_id=id;human.acknowledged_ram_sequence=std::max<ID>(0,idfield(raw,"ram_contact_resolved_seq"));human.profile=descriptor.ram;
  auto& body=human.body;body.id=id;body.team=team;body.flags=offline_contact::HasY|offline_contact::HasGrip;
  if(alive)body.flags|=offline_contact::Alive;else body.flags|=offline_contact::Immovable;
  human.checkpoint_field_present=field(raw,"tank_pushes")!=nullptr;
  if(!human.checkpoint_field_present)body.flags|=offline_contact::Impulse;else body.flags|=offline_contact::PositionFixed;
  if(!alive)body.flags|=offline_contact::PositionFixed;
  body.x=num(raw,"x");body.y=num(raw,"y");body.z=num(raw,"z");body.yaw=yaw;body.pitch=pitch;body.roll=roll;body.mass=descriptor.mass;body.shape=descriptor.shape;
  body.vx=std::sin(yaw)*speed;body.vy=0.;body.vz=std::cos(yaw)*speed;
  const double normal=std::cos(pitch)*std::cos(roll),ny=normal>.1?normal:.1;
  const bool rolling=bool(speed)||flag(raw,"forward");const double hold=descriptor.slide_hold*descriptor.gravity*ny;
  const double n=std::max(0.,std::min(1.,normal));double side;
  if(n>=descriptor.side_full_y)side=descriptor.side_full;else if(n<=descriptor.side_min_y)side=descriptor.side_min;
  else side=descriptor.side_min+(n-descriptor.side_min_y)/(descriptor.side_full_y-descriptor.side_min_y)*(descriptor.side_full-descriptor.side_min);
  body.grip[0]=rolling?descriptor.rolling_decel:std::max(hold,descriptor.rolling_decel);
  body.grip[1]=std::max(descriptor.rolling_decel,descriptor.slide_hold*side*descriptor.gravity*ny);
  readCheckpoints(raw,human);auto rows=field(raw,"ram_contacts");if(rows&&PyList_CheckExact(rows))for(Py_ssize_t i=0;i<size(rows);++i){auto row=item(rows,i);readReceipt(raw,row.get(),human);}
  else readReceipt(raw,field(raw,"ram_contact"),human);
  auto& neighbour=out.human.neighbour;neighbour.id=1000000+id;neighbour.team=team;neighbour.alive=alive;neighbour.position=p;
  neighbour.velocity={body.vx,body.vy,body.vz};neighbour.yaw=yaw;neighbour.pitch=pitch;neighbour.roll=roll;
  neighbour.half_width=descriptor.shape[0];neighbour.half_length=descriptor.shape[1];neighbour.lower_y=descriptor.shape[2];neighbour.upper_y=descriptor.shape[3];
  auto& nav=out.human.navigation;nav.position=p;nav.collision_half_width=descriptor.shape[0];nav.collision_half_length=descriptor.shape[1];
  nav.world_pose=field(raw,"world_pose")!=Py_False;nav.participating=field(raw,"participating")!=Py_False;
  auto& observation=out.human.observation;observation.key={offline_simulation::ActorKind::Human,id};observation.target.id=1000000+id;
  observation.target.identity={"human",id};observation.target.team=team;observation.target.alive=alive;observation.target.position=p;
  observation.target.health=int(num(raw,"health",alive?1.:0.));observation.target.max_health=std::max(1,int(num(raw,"max_health",1.)));
  observation.target.class_tag=descriptor.class_tag;observation.target.armor=descriptor.armor;observation.target.speed=speed;
  observation.pose.position=p;observation.pose.yaw=yaw;observation.pose.pitch=pitch;observation.pose.roll=roll;
  observation.pose.aim_yaw=num(raw,"aim_yaw",yaw);observation.pose.turret_yaw=observer.relative_turret_yaw;observation.pose.gun_pitch=num(raw,"gun_pitch");observation.speed=speed;
  out.target.target.kind="human";out.target.target.id=id;out.target.target.position=p;
  out.target.target.velocity=raw_velocity&&raw_velocity!=Py_None?velocity:Point{std::sin(yaw)*speed,0.,std::cos(yaw)*speed};
  out.target.pose=observation.pose;out.target.alive=alive;out.target.available=descriptor.target_available&&nav.world_pose;out.target.record_revision=descriptor.record_revision;
  out.target.descriptor_revision=descriptor.descriptor_revision;out.target.samples=descriptor.aim_samples;
  out.radius=std::hypot(descriptor.shape[0],descriptor.shape[1]);output=std::move(out);return true;
 }catch(const std::exception&e){if(!PyErr_Occurred())PyErr_SetString(PyExc_ValueError,e.what());return false;}
}
}
