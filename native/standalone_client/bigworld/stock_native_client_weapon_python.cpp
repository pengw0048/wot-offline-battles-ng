#if !defined(WOT_CLIENT_WEAPON_HOST_BUILD)
#include "pch.hpp"
#endif
#include "stock_native_client_weapon_python.hpp"
#include "stock_native_bot_python.hpp"
#include "stock_native_projectile_random.hpp"
#include <algorithm>
#include <cstddef>
#include <map>
#include <memory>
#include <new>
#include <string>

namespace stock_native_client_weapon {
namespace p=stock_native_bot_python;
namespace {
struct Data {
    Gun state;
    std::unique_ptr<stock_native_projectile::Random> random_source;
    p::Ref shots,loadout,order;
    double dispersion_factor=1.,aim_factor=1.,reload_factor=1.;
    Data(p::Ref s,p::Ref l,p::Ref o):shots(std::move(s)),loadout(std::move(l)),order(std::move(o)){}
};
struct Object {PyObject_HEAD Data* data;PyObject* dictionary;};
PyTypeObject type{};
bool ready=false;
void require(bool valid,const char*message){if(!valid)throw p::Error(message);}
PyObject* fail(const std::exception&e){if(!PyErr_Occurred())PyErr_SetString(PyExc_RuntimeError,e.what());return nullptr;}
Data& data(PyObject*self){require(PyObject_TypeCheck(self,&type)&&reinterpret_cast<Object*>(self)->data,"Native player gun is unavailable");return *reinterpret_cast<Object*>(self)->data;}
double number(PyObject*v){p::Ref n(PyNumber_Float(v));return PyFloat_AS_DOUBLE(n.get());}
int index(PyObject*v){p::Ref n(PyNumber_Long(v));const long value=PyLong_AsLong(n.get());if(PyErr_Occurred())throw p::Error("Invalid player gun index");require(value>=-2147483647L&&value<=2147483647L,"Player gun index overflow");return int(value);}
double positive(PyObject*v,double fallback){try{const double n=number(v);return n>0.?n:fallback;}catch(const std::exception&){PyErr_Clear();return fallback;}}
double attrPositive(PyObject*v,const char*key,double fallback){auto n=p::value(v,key);return positive(n.get(),fallback);}
double attrNumber(PyObject*v,const char*key,double fallback=0.){auto n=p::value(v,key);return n.get()==Py_None?fallback:number(n.get());}
int attrIndex(PyObject*v,const char*key,int fallback){try{auto n=p::value(v,key);return n.get()==Py_None?fallback:index(n.get());}catch(const std::exception&){PyErr_Clear();return fallback;}}
p::Ref orderedShots(PyObject*descriptor,PyObject*order){
    auto g=p::value(descriptor,"gun"),raw=p::value(g.get(),"shots");p::Ref shots(PySequence_Tuple(raw.get()));
    std::map<int,int> positions;if(order!=Py_None)for(Py_ssize_t i=0;i<PySequence_Size(order);++i)try{auto v=p::item(order,i);positions.emplace(index(v.get()),int(i));}catch(const std::exception&){PyErr_Clear();}
    std::vector<std::pair<int,PyObject*>> rows;for(Py_ssize_t i=0;i<PyTuple_GET_SIZE(shots.get());++i){auto*v=PyTuple_GET_ITEM(shots.get(),i);auto shell=p::value(v,"shell"),compact=p::value(shell.get(),"compactDescr");int position=int(positions.size());try{auto found=positions.find(index(compact.get()));if(found!=positions.end())position=found->second;}catch(const std::exception&){PyErr_Clear();}rows.emplace_back(position,v);}
    std::stable_sort(rows.begin(),rows.end(),[](const auto&a,const auto&b){return a.first<b.first;});p::Ref result(PyTuple_New(Py_ssize_t(rows.size())));for(std::size_t i=0;i<rows.size();++i)PyTuple_SET_ITEM(result.get(),Py_ssize_t(i),p::retain(rows[i].second).release());return result;
}
std::vector<int> compactShots(PyObject*shots){std::vector<int> values;for(Py_ssize_t i=0;i<PySequence_Size(shots);++i){auto shot=p::item(shots,i),shell=p::value(shot.get(),"shell"),compact=p::value(shell.get(),"compactDescr");values.push_back(compact.get()==Py_None?0:index(compact.get()));}return values;}
void descriptorConfig(Data&d,PyObject*descriptor,bool initial){
    auto gun=p::value(descriptor,"gun"),factors=p::value(gun.get(),"shotDispersionFactors");auto&c=d.state.config;
    c.base_dispersion=attrPositive(gun.get(),"shotDispersionAngle",.1)*d.dispersion_factor;
    c.after_shot=attrPositive(factors.get(),"afterShot",1.5);
    auto burst_factor=p::value(factors.get(),"afterShotInBurst"),fallback=p::value(factors.get(),"afterShot");
    c.after_shot_in_burst=burst_factor.get()!=Py_None?number(burst_factor.get()):fallback.get()!=Py_None?number(fallback.get()):1.5;
    require(std::isfinite(c.after_shot_in_burst)&&c.after_shot_in_burst>=0.,"Installed gun burst dispersion is invalid");
    auto burst=p::value(gun.get(),"burst");c.burst_count=1;c.burst_interval=0.;
    if(p::flag(burst.get())){auto count=p::item(burst.get(),0);c.burst_count=index(count.get());if(PySequence_Size(burst.get())>1){auto interval=p::item(burst.get(),1);c.burst_interval=number(interval.get());}require(!PyBool_Check(count.get())&&number(count.get())==c.burst_count&&c.burst_count>=1&&c.burst_count<=64&&std::isfinite(c.burst_interval)&&c.burst_interval>=0.&&c.burst_interval<=10.&&(c.burst_count==1||c.burst_interval>0.),"Installed gun burst is invalid");if(c.burst_count==1)c.burst_interval=0.;}
    c.aim_time=attrPositive(gun.get(),"aimingTime",2.)*d.aim_factor;c.reload=attrPositive(gun.get(),"reloadTime",5.)*d.reload_factor;
    auto clip=p::value(gun.get(),"clip");if(initial)c.clip_size=1;c.clip_reload=2.;if(p::flag(clip.get())){if(initial)try{auto size=p::item(clip.get(),0);c.clip_size=std::max(1,index(size.get()));}catch(const std::exception&){PyErr_Clear();}try{auto reload=p::item(clip.get(),1);c.clip_reload=positive(reload.get(),2.);}catch(const std::exception&){PyErr_Clear();}}
    c.bloom_move=attrNumber(d.loadout.get(),"bloom_move_factor",1.);c.bloom_rotation=attrNumber(d.loadout.get(),"bloom_rotation_factor",1.);c.bloom_turret=attrNumber(d.loadout.get(),"bloom_turret_factor",1.);
}
double* scalar(Data&d,const std::string&key){
    auto&g=d.state;auto&c=g.config;
    if(key=="base_dispersion")return &c.base_dispersion;if(key=="after_shot")return &c.after_shot;if(key=="after_shot_in_burst")return &c.after_shot_in_burst;if(key=="aim_time")return &c.aim_time;if(key=="reload")return &c.reload;if(key=="clip_reload")return &c.clip_reload;if(key=="burst_interval")return &c.burst_interval;if(key=="reload_time")return &g.reload_time;if(key=="reload_duration")return &g.reload_duration;if(key=="dispersion")return &g.dispersion;if(key=="_dispersion_factor")return &d.dispersion_factor;if(key=="_aim_time_factor")return &d.aim_factor;if(key=="_reload_factor")return &d.reload_factor;return nullptr;
}
int* integral(Data&d,const std::string&key){auto&g=d.state;if(key=="shot_index")return &g.shot;if(key=="clip")return &g.clip;if(key=="clip_size")return &g.config.clip_size;if(key=="burst_count")return &g.config.burst_count;if(key=="_burst_remaining")return &g.burst_remaining;if(key=="_burst_total")return &g.burst_total;return nullptr;}
PyObject* get(PyObject*self,PyObject*name){try{auto&d=data(self);const auto key=p::string(name);if(auto*v=scalar(d,key))return PyFloat_FromDouble(*v);if(auto*v=integral(d,key))return p::integer(*v).release();if(key=="pending_index")return d.state.pending?p::integer(*d.state.pending).release():p::retain(Py_None).release();if(key=="load_started")return p::boolean(d.state.load_started).release();if(key=="ammo")return p::integers(d.state.ammunition).release();if(key=="shots")return p::retain(d.shots.get()).release();if(key=="shell_order")return p::retain(d.order.get()).release();if(key=="loadout")return p::retain(d.loadout.get()).release();return PyObject_GenericGetAttr(self,name);}catch(const std::exception&e){return fail(e);}}
int set(PyObject*self,PyObject*name,PyObject*value){try{auto&d=data(self);const auto key=p::string(name);if(auto*v=scalar(d,key)){require(value,"Cannot delete a native gun field");*v=number(value);return 0;}if(auto*v=integral(d,key)){require(value,"Cannot delete a native gun field");*v=index(value);return 0;}if(key=="pending_index"){require(value,"Cannot delete a native gun field");d.state.pending=value==Py_None?std::nullopt:std::optional<int>(index(value));return 0;}if(key=="load_started"){require(value,"Cannot delete a native gun field");d.state.load_started=p::flag(value);return 0;}if(key=="ammo"){require(value,"Cannot delete ammunition");std::vector<int> values;for(Py_ssize_t i=0;i<PySequence_Size(value);++i){auto v=p::item(value,i);values.push_back(index(v.get()));}d.state.ammunition=std::move(values);return 0;}return PyObject_GenericSetAttr(self,name,value);}catch(const std::exception&e){fail(e);return -1;}}
void destroy(PyObject*self){auto*value=reinterpret_cast<Object*>(self);PyObject_GC_UnTrack(self);delete value->data;Py_XDECREF(value->dictionary);Py_TYPE(self)->tp_free(self);}
int traverse(PyObject*self,visitproc visit,void*arg){auto*o=reinterpret_cast<Object*>(self);Py_VISIT(o->dictionary);if(o->data){Py_VISIT(o->data->shots.get());Py_VISIT(o->data->loadout.get());Py_VISIT(o->data->order.get());}return 0;}
int clear(PyObject*self){auto*o=reinterpret_cast<Object*>(self);Py_CLEAR(o->dictionary);delete o->data;o->data=nullptr;return 0;}
PyObject* canFire(PyObject*self,PyObject*args){try{PyObject*live=Py_True;if(!PyArg_ParseTuple(args,"|O",&live))return nullptr;return p::boolean(data(self).state.ready(p::flag(live))).release();}catch(const std::exception&e){return fail(e);}}
PyObject* begin(PyObject*self,PyObject*raw){try{return p::boolean(data(self).state.begin(index(raw))).release();}catch(const std::exception&){PyErr_Clear();Py_RETURN_FALSE;}}
PyObject* commit(PyObject*self,PyObject*args){try{PyObject*final=nullptr;double factor=1.;if(!PyArg_ParseTuple(args,"O|d",&final,&factor))return nullptr;return p::boolean(data(self).state.commit(p::flag(final),factor)).release();}catch(const std::exception&e){return fail(e);}}
PyObject* fire(PyObject*self,PyObject*args){try{double factor=1.;if(!PyArg_ParseTuple(args,"|d",&factor))return nullptr;auto&g=data(self).state;return p::boolean(g.begin(1)&&g.commit(true,factor)).release();}catch(const std::exception&e){return fail(e);}}
PyObject* cancel(PyObject*self,PyObject*args){try{double factor=1.;if(!PyArg_ParseTuple(args,"|d",&factor))return nullptr;return p::boolean(data(self).state.cancel(factor)).release();}catch(const std::exception&e){return fail(e);}}
PyObject* select(PyObject*self,PyObject*args,PyObject*kwargs){try{PyObject*raw=nullptr,*instant=Py_False;static const char*names[]={"index","instant",nullptr};if(!PyArg_ParseTupleAndKeywords(args,kwargs,"O|O",const_cast<char**>(names),&raw,&instant))return nullptr;return p::boolean(data(self).state.select(index(raw),p::flag(instant))).release();}catch(const std::exception&){PyErr_Clear();Py_RETURN_FALSE;}}
PyObject* request(PyObject*self,PyObject*raw){try{return p::boolean(data(self).state.request(index(raw))).release();}catch(const std::exception&){PyErr_Clear();Py_RETURN_FALSE;}}
PyObject* partial(PyObject*self,PyObject*){try{return p::boolean(data(self).state.partialReload()).release();}catch(const std::exception&e){return fail(e);}}
PyObject* tick(PyObject*self,PyObject*args,PyObject*kwargs){try{
    double dt,move,rotation,turret,dispersion=1.,aim=1.;PyObject*live=nullptr,*descriptor=nullptr;static const char*names[]={"dt","battle_live","move_speed","rotation_speed","turret_speed","descriptor","dispersion_factor","aim_time_factor",nullptr};
    if(!PyArg_ParseTupleAndKeywords(args,kwargs,"dOdddO|dd",const_cast<char**>(names),&dt,&live,&move,&rotation,&turret,&descriptor,&dispersion,&aim))return nullptr;
    auto chassis=p::value(descriptor,"chassis"),factors=p::value(chassis.get(),"shotDispersionFactors"),gun=p::value(descriptor,"gun"),gf=p::value(gun.get(),"shotDispersionFactors");require(factors.get()!=Py_None&&PySequence_Size(factors.get())==2,"#1513 chassis shot dispersion factors are unavailable");auto a=p::item(factors.get(),0),b=p::item(factors.get(),1);data(self).state.tick(dt,p::flag(live),move,rotation,turret,{number(a.get()),number(b.get()),attrNumber(gf.get(),"turretRotation")},dispersion,aim);Py_RETURN_NONE;
}catch(const std::exception&e){return fail(e);}}
PyObject* adopt(PyObject*self,PyObject*descriptor){try{
    auto&d=data(self);auto shots=orderedShots(descriptor,d.order.get());require(compactShots(shots.get())==compactShots(d.shots.get()),"#1513 siege-mode gun changed its ammunition contract");const auto before=d.state.config;Data next(p::retain(shots.get()),p::retain(d.loadout.get()),p::retain(d.order.get()));next.state=d.state;next.dispersion_factor=d.dispersion_factor;next.aim_factor=d.aim_factor;next.reload_factor=d.reload_factor;descriptorConfig(next,descriptor,false);d.state.config=next.state.config;auto*old=d.shots.release();d.shots.~Ref();new(&d.shots)p::Ref(std::move(shots));Py_DECREF(old);const auto&after=d.state.config;
    return p::boolean(before.base_dispersion!=after.base_dispersion||before.after_shot!=after.after_shot||before.after_shot_in_burst!=after.after_shot_in_burst||before.burst_count!=after.burst_count||before.burst_interval!=after.burst_interval||before.aim_time!=after.aim_time||before.reload!=after.reload||before.clip_reload!=after.clip_reload).release();
}catch(const std::exception&e){return fail(e);}}
PyObject* scatterMethod(PyObject*self,PyObject*args,PyObject*kwargs){try{
    PyObject*direction=nullptr,*perfect=Py_False,*gauss=Py_None,*angle=Py_None,*uniform=Py_None;static const char*names[]={"direction","perfect_accuracy","gauss","dispersion_angle","uniform",nullptr};
    if(!PyArg_ParseTupleAndKeywords(args,kwargs,"O|OOOO",const_cast<char**>(names),&direction,&perfect,&gauss,&angle,&uniform))return nullptr;
    const double dispersion=angle==Py_None?data(self).state.dispersion:number(angle);if(p::flag(perfect)||dispersion<=0.)return p::retain(direction).release();require(gauss==Py_None&&uniform==Py_None,"Native player scatter owns the installed random source");
    auto&random_source=data(self).random_source;
    if(!random_source){p::Ref module(PyImport_ImportModule("random"));auto instance=p::value(module.get(),"_inst");auto random=std::make_unique<stock_native_projectile::Random>();require(random->install(instance.get()),"Native player random source unavailable");random_source=std::move(random);}
    double radius=std::abs(random_source->gauss(0.,dispersion/2.));if(radius>dispersion)radius=dispersion*random_source->uniform();const double azimuth=random_source->uniform()*6.28318530717958647692;
    const Point original={attrNumber(direction,"x"),attrNumber(direction,"y"),attrNumber(direction,"z")};if(original==Point{})return p::retain(direction).release();const auto result=scatter(original,radius,azimuth);
    for(unsigned i=0;i<3;++i){auto value=p::number(result[i]);if(PyObject_SetAttrString(direction,i==0?"x":i==1?"y":"z",value.get())<0)throw p::Error("Native player scatter vector write failed");}
    // Math.Vector3 normalise applies float precision after each component write.
    auto method=p::value(direction,"normalise");p::Ref normalized(PyObject_CallObject(method.get(),nullptr));return p::retain(direction).release();
}catch(const std::exception&e){return fail(e);}}
PyCFunction keyword(PyCFunctionWithKeywords fn){return reinterpret_cast<PyCFunction>(reinterpret_cast<void(*)(void)>(fn));}
PyMethodDef gun_methods[]={
    {"can_fire",canFire,METH_VARARGS,"Read the committed gun's physical gate."},
    {"begin_burst",begin,METH_O,"Reserve the loaded physical burst."},
    {"commit_burst_round",commit,METH_VARARGS,"Debit one accepted physical shell."},
    {"commit_fire",fire,METH_VARARGS,"Debit one single-round trigger."},
    {"cancel_burst",cancel,METH_VARARGS,"Retire an unlaunched burst tail."},
    {"sync_shell_index",keyword(select),METH_VARARGS|METH_KEYWORDS,"Select a shell at a closed gun edge."},
    {"request_shell_index",request,METH_O,"Queue a shell for the next empty clip."},
    {"reload_partial_clip",partial,METH_NOARGS,"Begin one empty full reload."},
    {"tick",keyword(tick),METH_VARARGS|METH_KEYWORDS,"Advance the complete local gun law."},
    {"adopt_descriptor",adopt,METH_O,"Replace mode law without resetting ammunition."},
    {"scatter",keyword(scatterMethod),METH_VARARGS|METH_KEYWORDS,"Freeze one native aiming-cone sample."},
    {nullptr,nullptr,0,nullptr}};
}
Gun* gun(PyObject*object){return &data(object).state;}
PyObject* create(PyObject*,PyObject*args,PyObject*kwargs){try{
    PyObject*descriptor=nullptr,*modifiers=nullptr,*layout=Py_None,*order=Py_None;static const char*names[]={"descriptor","loadout_modifiers","ammo_layout","shell_order",nullptr};
    if(!PyArg_ParseTupleAndKeywords(args,kwargs,"OO|OO",const_cast<char**>(names),&descriptor,&modifiers,&layout,&order))return nullptr;
    require(PyDict_Check(modifiers),"Native player gun requires its accepted loadout");auto shots=orderedShots(descriptor,order);p::Ref loadout(PyDict_Copy(modifiers));auto shell_order=order==Py_None?p::Ref(PyTuple_New(0)):p::Ref(PySequence_Tuple(order));
    auto d=std::make_unique<Data>(std::move(shots),std::move(loadout),std::move(shell_order));const double crew=attrPositive(d->loadout.get(),"crew_multiplier",1.);d->dispersion_factor=attrPositive(d->loadout.get(),"dispersion_factor",crew);d->aim_factor=attrPositive(d->loadout.get(),"aim_time_factor",crew);d->reload_factor=attrPositive(d->loadout.get(),"reload_factor",crew);descriptorConfig(*d,descriptor,true);
    const auto compacts=compactShots(d->shots.get());auto&g=d->state;g.ammunition.resize(compacts.size());
    if(layout!=Py_None){p::Ref dictionary(PyObject_CallFunctionObjArgs(reinterpret_cast<PyObject*>(&PyDict_Type),layout,nullptr));std::map<int,int> wanted;Py_ssize_t n=0;PyObject*k=nullptr,*v=nullptr;while(PyDict_Next(dictionary.get(),&n,&k,&v))try{wanted[index(k)]=std::max(0,index(v));}catch(const std::exception&){PyErr_Clear();}bool matched=false;for(std::size_t i=0;i<compacts.size();++i){auto found=wanted.find(compacts[i]);if(found!=wanted.end()){matched=true;g.ammunition[i]=found->second;}}require(wanted.empty()||matched,"Client ammunition does not match the installed gun");}
    else {auto raw=p::value(descriptor,"maxAmmo"),gun_value=p::value(descriptor,"gun"),turret=p::value(descriptor,"turret");int maximum=45;if(raw.get()!=Py_None)maximum=attrIndex(descriptor,"maxAmmo",45);else {auto x=p::value(gun_value.get(),"maxAmmo");maximum=x.get()!=Py_None?attrIndex(gun_value.get(),"maxAmmo",45):attrIndex(turret.get(),"maxAmmo",45);}maximum=std::max(0,maximum);const double weights[]={.6,.3,.1};for(std::size_t i=0;i<compacts.size();++i){int count=int(maximum*weights[std::min<std::size_t>(i,2)]);if(count==0&&maximum>0)count=1;g.ammunition[i]=count;}}
    const int selected=PyTuple_GET_SIZE(d->order.get())>0?0:attrIndex(descriptor,"activeGunShotIndex",0);g.shot=std::max(0,std::min(selected,std::max(0,int(compacts.size())-1)));if(g.ammunition.empty()||g.ammunition[g.shot]<=0)for(std::size_t i=0;i<g.ammunition.size();++i)if(g.ammunition[i]>0){g.shot=int(i);break;}
    g.reload_time=g.reload_duration=g.config.reload;g.dispersion=g.config.base_dispersion;
    auto*object=reinterpret_cast<Object*>(type.tp_alloc(&type,0));if(!object)return nullptr;object->data=d.release();object->dictionary=nullptr;return reinterpret_cast<PyObject*>(object);
}catch(const std::exception&e){return fail(e);}}
PyMethodDef* methods(){static PyMethodDef table[]={
    {"create",keyword(create),METH_VARARGS|METH_KEYWORDS,"Create the visible player's native gun state."},
    {nullptr,nullptr,0,nullptr}};return table;}
bool initialize(PyObject*module){
    if(!ready){PyObject_INIT(reinterpret_cast<PyObject*>(&type),&PyType_Type);type.tp_name=const_cast<char*>("offline_client_weapon_native.GunState");type.tp_basicsize=sizeof(Object);type.tp_dealloc=destroy;type.tp_getattro=get;type.tp_setattro=set;type.tp_flags=Py_TPFLAGS_DEFAULT|Py_TPFLAGS_HAVE_GC;type.tp_traverse=traverse;type.tp_clear=clear;type.tp_methods=gun_methods;type.tp_dictoffset=offsetof(Object,dictionary);type.tp_alloc=PyType_GenericAlloc;type.tp_free=PyObject_GC_Del;if(PyType_Ready(&type)<0)return false;ready=true;}
    Py_INCREF(&type);return PyModule_AddObject(module,"GunState",reinterpret_cast<PyObject*>(&type))==0;
}
}
