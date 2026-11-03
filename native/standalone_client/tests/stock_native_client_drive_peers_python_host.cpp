// Exercise the production cold installer with explicit descriptor/crew providers.
#include "stock_native_client_drive_peers_python.cpp"
#include "stock_native_battle_test_lifetime.hpp"
namespace {
namespace d=stock_native_client_drive;
namespace p=stock_native_bot_python;
namespace cs=stock_native_client_state;
namespace b=native_battle;
struct Resource {p::Ref descriptor;explicit Resource(PyObject*v):descriptor(p::retain(v)){}};
struct Fixture {
    d::PlayerContacts contacts;
    d::NativePeers peers{contacts,.25,1./60.};
    d::PeerInstaller installer;
    cs::State state;
    p::Ref descriptors;
    std::map<std::string,p::Ref> params;
    std::map<std::string,bool> dynamics;
    unsigned producers=0,parses=0;
    explicit Fixture(PyObject*raw):descriptors(p::retain(raw)){}
};
Fixture* active=nullptr;
cs::Value lastParams;
void destroy(PyObject*c){delete static_cast<Fixture*>(PyCapsule_GetPointer(c,"peer.installer.test"));}
Fixture& fixture(PyObject*c){auto*v=static_cast<Fixture*>(PyCapsule_GetPointer(c,"peer.installer.test"));if(!v)throw p::Error("Invalid test owner");return *v;}
PyObject*fail(const std::exception&e){if(!PyErr_Occurred())PyErr_SetString(PyExc_RuntimeError,e.what());return nullptr;}
PyObject* create(PyObject*,PyObject*args){PyObject*raw;if(!PyArg_ParseTuple(args,"O",&raw))return nullptr;try{return PyCapsule_New(new Fixture(raw),"peer.installer.test",destroy);}catch(const std::exception&e){return fail(e);}}
void install(Fixture&owner,const std::string&key,bool dynamics,const d::PlayerDescriptor&descriptor){
    auto row=dynamics?p::Ref(cs::write(lastParams)):p::dict();
    if(!dynamics){p::set(row.get(),"mass",p::number(descriptor.motion.drive.mass));p::set(row.get(),"shape",p::tupleNumbers(descriptor.motion.collision_shape));}
    owner.params.erase(key);owner.params.emplace(key,std::move(row));owner.dynamics[key]=dynamics;
}
PyObject*synchronize(PyObject*,PyObject*args){PyObject*c,*raw,*rows;int epoch=0;if(!PyArg_ParseTuple(args,"OOO|i",&c,&raw,&rows,&epoch))return nullptr;try{
    auto&owner=fixture(c);active=&owner;b::Battle battle;battle.engine.shell=b::borrow(raw);if(epoch)++owner.state.epoch;
    std::set<std::string>live;
    for(Py_ssize_t i=0;i<p::size(rows);++i){auto row=p::item(rows,i);auto value=cs::read(row.get());const auto key=value.get("key").text;live.insert(key);auto&a=owner.state.actors[key];if(!a)a=std::make_shared<cs::Actor>();a->key=key;a->kind=value.get("kind").text;a->id=std::int64_t(value.get("id").numeric());a->ready=true;a->state=value;a->descriptor_revision=std::uint64_t(value.get("descriptor_revision").numeric());
        auto id=p::integer(std::int64_t(value.get("descriptor").numeric()));p::Ref descriptor(PyObject_GetItem(owner.descriptors.get(),id.get()));
        if(value.get("no_geometry").truth())a->geometry.reset();else if(!a->geometry||std::static_pointer_cast<const Resource>(a->geometry)->descriptor.get()!=descriptor.get())a->geometry=std::make_shared<Resource>(descriptor.get());
    }
    for(auto at=owner.state.actors.begin();at!=owner.state.actors.end();)if(!live.count(at->first))at=owner.state.actors.erase(at);else ++at;
    owner.installer.synchronize(battle,owner.state,owner.peers);auto result=p::dict(),params=p::dict(),dynamics=p::dict();for(const auto&row:owner.params)p::set(params.get(),row.first.c_str(),row.second);for(const auto&row:owner.dynamics)p::set(dynamics.get(),row.first.c_str(),p::boolean(row.second));p::set(result.get(),"params",params);p::set(result.get(),"dynamics",dynamics);p::set(result.get(),"producers",p::integer(owner.producers));p::set(result.get(),"parses",p::integer(owner.parses));return result.release();
}catch(const std::exception&e){return fail(e);}}
PyObject*crew(PyObject*,PyObject*args){PyObject*raw,*row,*descriptor;if(!PyArg_ParseTuple(args,"OOO",&raw,&row,&descriptor))return nullptr;try{b::Battle battle;battle.engine.shell=b::borrow(raw);return PyInt_FromLong(d::crewLevel(d::context(battle),cs::read(row),descriptor));}catch(const std::exception&e){return fail(e);}}
PyMethodDef methods[]={{"create",create,METH_VARARGS,""},{"synchronize",synchronize,METH_VARARGS,""},{"crew_level",crew,METH_VARARGS,""},{nullptr,nullptr,0,nullptr}};
}
namespace native_battle {namespace bots {
cs::Value peerSkillContext(const Battle&battle){auto bots=p::value(battle.engine.shell.get(),"_bots");auto result=p::dict();p::set(result.get(),"tactics",p::value(bots.get(),"_bot_tactics"));p::set(result.get(),"mode",p::value(bots.get(),"_bot_skill_mode"));auto round=p::value(bots.get(),"round_id");p::Ref text(PyObject_Str(round.get()));p::set(result.get(),"round",text);auto pins=p::list();auto raw=p::value(bots.get(),"_bot_skill_pins");PyObject*k,*v;Py_ssize_t pos=0;while(PyDict_Next(raw.get(),&pos,&k,&v)){p::Ref pin(Py_BuildValue("(OOO)",PyTuple_GET_ITEM(k,0),PyTuple_GET_ITEM(k,1),v));p::append(pins.get(),pin);}p::set(result.get(),"pins",pins);return cs::read(result.get());}
}}
namespace stock_native_bot_descriptor {
PyObject*defaultCrewFactors(PyObject*descriptor,std::optional<int>level){++active->producers;if(p::truth(descriptor,"_fail")){PyErr_SetString(PyExc_ValueError,"Fixture crew unavailable");return nullptr;}auto raw=p::value(descriptor,"_factors");auto key=p::integer(level.value_or(100));return PyObject_GetItem(raw.get(),key.get());}
std::array<double,4>chassisShape(PyObject*descriptor){return p::numbers<4>(p::value(descriptor,"_shape").get());}
}
namespace native_battle {namespace local {
DriveParams physicsFromValue(const cs::Value&v){DriveParams out;out.mass=v.get("mass").numeric();out.power=v.get("powerW").numeric();out.native_power_ratio=v.get("nativePowerRatio").numeric();out.forward=v.get("speedFwd").numeric();out.reverse=v.get("speedBwd").numeric();out.friction=v.get("specificFriction").numeric();out.brake=v.get("brakeDecel").numeric();out.rotation=v.get("rotSpd").numeric();out.track_center=v.get("trackCenter").numeric();out.rotation_around_center=v.get("rotationIsAroundCenter").truth(true);for(unsigned i=0;i<3;++i)out.terrain[i]=v.get("terrainResist").resolved().array.at(i).numeric();lastParams=v;return out;}
DriveParams deriveParams(PyObject*descriptor,PyObject*factors){p::Ref module(PyImport_ImportModule("gui.mods.offline_lan_0922.vehicle_physics"));p::Ref args(PyTuple_Pack(2,descriptor,factors));auto values=p::call(module.get(),"derive_params",args.get());return physicsFromValue(cs::read(values.get()));}
}}
namespace stock_native_client_query {
PyObject*descriptor(const cs::Actor&a){return a.geometry?std::static_pointer_cast<const Resource>(a.geometry)->descriptor.get():nullptr;}
const stock_native_bot_geometry::Vehicle*geometry(const cs::Actor&a){return a.geometry?reinterpret_cast<const stock_native_bot_geometry::Vehicle*>(a.geometry.get()):nullptr;}
}
namespace stock_native_client_drive {
PlayerDescriptor readPlayerDescriptor(const b::Battle&,PyObject*,const b::local::DriveParams&params){++active->parses;PlayerDescriptor out;out.motion.drive.mass=params.mass;return out;}
NativePeers::NativePeers(PlayerContacts&contacts,double padding,double fixed):contacts_(contacts),broadphase_padding_(padding),fixed_step_(fixed){}
void NativePeers::install(std::shared_ptr<cs::Actor>a,PlayerDescriptor descriptor){::install(*active,a->key,true,descriptor);}
void NativePeers::installGeometry(std::shared_ptr<cs::Actor>a,PlayerDescriptor descriptor){::install(*active,a->key,false,descriptor);}
void NativePeers::forget(const std::set<std::string>&live){for(auto at=active->params.begin();at!=active->params.end();)if(!live.count(at->first)){active->dynamics.erase(at->first);at=active->params.erase(at);}else ++at;}
}
PyMODINIT_FUNC initoffline_peer_installer_test(){Py_InitModule("offline_peer_installer_test",methods);}
