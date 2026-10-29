#include <Python.h>
#include "stock_native_client_drive_peers_python.cpp"
namespace {
namespace d=stock_native_client_drive;namespace p=stock_native_bot_python;namespace cs=stock_native_client_state;
struct Test {d::PlayerContacts contacts;d::NativePeers peers{contacts,.1,.1};d::PeerInstaller installer;cs::State state;p::Ref descriptors;std::map<std::string,p::Ref>params;std::map<std::string,bool>dynamics;int producers=0,parses=0;explicit Test(PyObject*raw):descriptors(p::retain(raw)){};};
Test*active=nullptr;
const char*tag="peer.installer.test";
Test&get(PyObject*c){auto*t=static_cast<Test*>(PyCapsule_GetPointer(c,tag));if(!t)throw p::Error("Peer test owner unavailable");return *t;}
void destroy(PyObject*c){delete static_cast<Test*>(PyCapsule_GetPointer(c,tag));}
PyObject*fail(const std::exception&e){if(!PyErr_Occurred())PyErr_SetString(PyExc_RuntimeError,e.what());return nullptr;}
PyObject*create(PyObject*,PyObject*raw){try{auto t=std::make_unique<Test>(raw);p::Ref out(PyCapsule_New(t.get(),tag,destroy));t.release();return out.release();}catch(const std::exception&e){return fail(e);}}
PyObject*synchronize(PyObject*,PyObject*args){PyObject*c,*battle,*rows;int reset=0;if(!PyArg_ParseTuple(args,"OOO|i",&c,&battle,&rows,&reset))return nullptr;try{auto&t=get(c);active=&t;if(reset)t.state.reset(cs::Value{});++t.state.revision;std::set<std::string>seen;
 for(Py_ssize_t i=0;i<p::size(rows);++i){auto raw=p::item(rows,i);auto value=cs::read(raw.get());const auto key=value.get("key").resolved().text;seen.insert(key);auto&a=t.state.actors[key];if(!a){a=std::make_shared<cs::Actor>();a->key=key;}a->kind=value.get("kind").resolved().text;a->id=std::int64_t(value.get("id").numeric());a->engine_id=std::int64_t(value.get("engine_id").numeric(a->id+100));a->descriptor_revision=std::uint64_t(value.get("descriptor_revision").numeric());a->ready=value.get("ready").truth(true);a->local=value.get("local").truth();a->tombstone=value.get("tombstone").truth();a->missing=value.get("missing").truth();a->state=std::move(value);++a->revision;}
 for(auto at=t.state.actors.begin();at!=t.state.actors.end();)if(!seen.count(at->first))at=t.state.actors.erase(at);else ++at;
 t.installer.synchronize(battle,t.state,t.peers);auto out=p::dict(),values=p::dict(),kinds=p::dict();for(const auto&key:seen){if(t.peers.descriptor(key)){const auto it=t.params.find(key);if(it!=t.params.end())p::set(values.get(),key.c_str(),p::retain(it->second.get()));p::set(kinds.get(),key.c_str(),p::boolean(t.dynamics[key]));}}p::set(out.get(),"params",values);p::set(out.get(),"dynamics",kinds);p::set(out.get(),"producers",p::integer(t.producers));p::set(out.get(),"parses",p::integer(t.parses));return out.release();
 }catch(const std::exception&e){return fail(e);}}
PyObject*level(PyObject*,PyObject*args){PyObject*battle,*raw,*descriptor;if(!PyArg_ParseTuple(args,"OOO",&battle,&raw,&descriptor))return nullptr;try{return p::integer(d::crewLevel(d::context(battle),cs::read(raw),descriptor)).release();}catch(const std::exception&e){return fail(e);}}
PyMethodDef methods[]={{"create",create,METH_O,""},{"synchronize",synchronize,METH_VARARGS,""},{"crew_level",level,METH_VARARGS,""},{nullptr,nullptr,0,nullptr}};
}
namespace stock_native_client_query {
PyObject*descriptor(const cs::Actor&a){auto id=p::integer(std::int64_t(a.state.get("descriptor").numeric()));auto*raw=PyDict_GetItem(active->descriptors.get(),id.get());return raw?raw:Py_None;}
const stock_native_bot_geometry::Vehicle*geometry(const cs::Actor&a){return a.state.get("no_geometry").truth()?nullptr:reinterpret_cast<const stock_native_bot_geometry::Vehicle*>(std::uintptr_t(1));}
}
namespace stock_native_bot {PyObject*readUnmodifiedDrive(PyObject*descriptor){auto raw=p::value(descriptor,"_base");return PyDict_Copy(raw.get());}}
namespace stock_native_bot_descriptor {
PyObject*defaultCrewFactors(PyObject*descriptor,std::optional<int>level){++active->producers;if(p::truth(descriptor,"_fail"))throw p::Error("Injected native stock crew producer failure");auto values=p::value(descriptor,"_factors");auto key=p::integer(level.value_or(100));auto*raw=PyDict_GetItem(values.get(),key.get());if(!raw)throw p::Error("Missing generated crew level fixture");return p::retain(raw).release();}
std::array<double,4>chassisShape(PyObject*descriptor){return p::numbers<4>(p::value(descriptor,"_shape").get());}
}
namespace stock_native_client_drive {
PlayerDescriptor readPlayerDescriptor(PyObject*,PyObject*entity,PyObject*raw){++active->parses;auto descriptor=p::value(entity,"typeDescriptor");auto key=p::value(descriptor.get(),"_key");const auto name=p::string(key.get());active->params.erase(name);active->params.emplace(name,p::Ref(PyDict_Copy(raw)));PlayerDescriptor out;out.motion.drive.mass=p::numeric(raw,"mass");out.motion.collision_shape=stock_native_bot_descriptor::chassisShape(descriptor.get());return out;}
NativePeers::NativePeers(PlayerContacts&c,double padding,double fixed):contacts_(c),broadphase_padding_(padding),fixed_step_(fixed){}
void NativePeers::install(std::shared_ptr<cs::Actor>a,PlayerDescriptor d){const auto key=a->key;entries_[key]={a,std::move(d),true};active->dynamics[key]=true;}
void NativePeers::installGeometry(std::shared_ptr<cs::Actor>a,PlayerDescriptor d){const auto key=a->key;entries_[key]={a,std::move(d),false};active->dynamics[key]=false;auto raw=p::dict();p::set(raw.get(),"mass",p::number(entries_[key].descriptor.motion.drive.mass));p::set(raw.get(),"shape",p::tupleNumbers(entries_[key].descriptor.motion.collision_shape));active->params.erase(key);active->params.emplace(key,std::move(raw));}
void NativePeers::forget(const std::set<std::string>&keys){for(auto at=entries_.begin();at!=entries_.end();)if(!keys.count(at->first))at=entries_.erase(at);else ++at;}
const PlayerDescriptor*NativePeers::descriptor(const std::string&key)const{const auto at=entries_.find(key);return at==entries_.end()?nullptr:&at->second.descriptor;}
}
extern "C" void initoffline_peer_installer_test(){Py_InitModule("offline_peer_installer_test",methods);}
