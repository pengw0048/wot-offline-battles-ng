// Real CPython 2.7 and unchanged SDK PY_AUTO/converter/Event bodies.
// Only resource IO and the final Wwise SDK acceptor are fixture boundaries.
#ifdef __clang__
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wdeprecated-register"
#endif
#include <Python.h>
#ifdef __clang__
#pragma clang diagnostic pop
#endif
#include <algorithm>
#include <cassert>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <fstream>
#include <map>
#include <memory>
#include <string>
#include <vector>
#define BW_BEGIN_NAMESPACE namespace BW {
#define BW_END_NAMESPACE }
#define MF_ASSERT assert
#define BW_STATIC_ASSERT(x,m) static_assert(x,#m)
#define BW_GUARD
#define WOT_STOCK_SOUND_ZONES_PORTABLE
namespace BW {
using string=std::string;
using wstring=std::wstring;
template<class T>using vector=std::vector<T>;
struct Vector2 {float x=0,y=0;Vector2()=default;Vector2(float a,float b):x(a),y(b){}float& operator[](int n){return n?y:x;}};
struct Vector3 {float x=0,y=0,z=0;};
struct Matrix {Vector3 point;Vector3 applyToOrigin()const{return point;}};
uint64_t clockValue=0;uint64_t timestamp(){return clockValue;}double stampsPerSecondD(){return 1000.;}
@SDK_EVENT@
class ClientSpace{};
class SpaceManager {public:Event<SpaceManager,ClientSpace*> destroyed;static SpaceManager& instance(){static SpaceManager v;return v;}Event<SpaceManager,ClientSpace*>::EventDelegateList& spaceDestroyed(){return destroyed.delegates();}};
struct DataSection {std::map<std::string,std::shared_ptr<DataSection>> children;std::string name;float value=0;std::shared_ptr<DataSection> openSection(const char* n){return children[n];}std::string readString(const char*){return name;}float readFloat(const char*,float){return value;}};
using DataSectionPtr=std::shared_ptr<DataSection>;
struct BWResource {static std::map<std::string,DataSectionPtr> resources;static DataSectionPtr openSection(const std::string& name){return resources[name];}};std::map<std::string,DataSectionPtr> BWResource::resources;
using AkUniqueID=unsigned;using AkGameObjectID=uint64_t;
struct AkAuxSendValue {AkGameObjectID listenerID;unsigned auxBusID;float fControlValue;};
struct AudioCall {bool event;uint64_t id;unsigned bus;float gain;};std::vector<AudioCall> calls;
namespace StockAudioDetail {struct Source {uint64_t id;bool auxSend=true,valid=true;};using SourcePtr=std::shared_ptr<Source>;struct Manager {bool ready=true;unsigned auxiliaryBus=0;float auxiliaryGain=0;uint64_t global=900,listener=901;std::map<uint64_t,std::weak_ptr<Source>> sources;};Manager& manager(){static Manager v;return v;}bool sourceValid(const SourcePtr& s){return s->valid;}bool result(bool accepted,const char*){return accepted;}}
namespace AK {namespace SoundEngine {unsigned GetIDFromString(const char* n){unsigned hash=2166136261u;for(;*n;++n){unsigned char c=static_cast<unsigned char>(*n);if(c>='A'&&c<='Z')c+=32;hash=(hash*16777619u)^c;}return hash;}bool PostEvent(unsigned id,uint64_t object){assert(object==900);calls.push_back({true,object,id,0});return true;}bool SetGameObjectAuxSendValues(uint64_t object,const AkAuxSendValue* value,unsigned count){assert(value->listenerID==901&&count==(value->auxBusID?1u:0u));calls.push_back({false,object,value->auxBusID,value->fControlValue});return true;}}}
@AUDIO_BRIDGES@
struct HostLink {const char* name;PyCFunction function;};std::vector<HostLink>& links(){static std::vector<HostLink> v;return v;}
struct PyModuleMethodLink {PyModuleMethodLink(const char* module,const char* name,PyCFunction f,const char*){assert(std::string(module)=="BigWorld");links().push_back({name,f});}};
struct PyObjectPtr {enum{STEAL_REFERENCE};PyObject* value=nullptr;PyObjectPtr()=default;PyObjectPtr(PyObject* p,int):value(p){}PyObjectPtr(const PyObjectPtr& p):value(p.value){Py_XINCREF(value);}PyObjectPtr& operator=(const PyObjectPtr& p){Py_XINCREF(p.value);Py_XDECREF(value);value=p.value;return *this;}~PyObjectPtr(){Py_XDECREF(value);}PyObject* get()const{return value;}};
namespace Script {int setData(PyObject*,double&,const char* = "");int setData(PyObject*,float&,const char* = "");int setData(PyObject*,BW::string&,const char* = "");int setData(PyObject*,Vector2&,const char* = "");PyObject* argCountError(const char*,int,int,...);}
template<class T>struct PyVector {static bool Check(PyObject*){return false;}T getVector(){return {};}};
@SCRIPT_BODIES@
@PYAUTO_MACROS@
}
#include "stock_sound_zones.cpp"
using namespace BW;
std::vector<StockAudioDetail::SourcePtr> sources;
PyObject* reset(PyObject*,PyObject*) {finiStockSoundZones();calls.clear();clockValue=0;sources.clear();auto& audio=StockAudioDetail::manager();audio.ready=true;audio.sources.clear();for(unsigned i=1;i!=4;++i){auto s=std::make_shared<StockAudioDetail::Source>();s->id=i;s->auxSend=i!=2;s->valid=i!=3;sources.push_back(s);audio.sources[i]=s;}Py_RETURN_NONE;}
PyObject* tick(PyObject*,PyObject* args){float x,y,z;double now;if(!PyArg_ParseTuple(args,"fffd",&x,&y,&z,&now))return nullptr;Matrix m;m.point={x,y,z};clockValue=uint64_t(now*1000);tickStockSoundZones(m,now);Py_RETURN_NONE;}
PyObject* trace(PyObject*,PyObject*){PyObject* result=PyList_New(0);for(auto& c:calls){auto value=c.event?Py_BuildValue("(sI)","event",c.bus):Py_BuildValue("(sKIf)","aux",c.id,c.bus,c.gain);PyList_Append(result,value);Py_DECREF(value);}calls.clear();return result;}
PyObject* clearSpace(PyObject*,PyObject*){SpaceManager::instance().destroyed.invoke(&SpaceManager::instance(),nullptr);Py_RETURN_NONE;}
PyObject* mapping(PyObject*,PyObject* args){const char *path,*bus;float gain;if(!PyArg_ParseTuple(args,"ssf",&path,&bus,&gain))return nullptr;auto root=std::make_shared<DataSection>();auto child=std::make_shared<DataSection>();child->name=bus;child->value=gain;root->children["wwDefaultReverb"]=child;auto slash=std::string(path).find('/');if(slash!=std::string::npos){std::string component=std::string(path).substr(slash+1);component=component.substr(0,component.find('/'));BWResource::resources["scripts/arena_defs/"+component+".xml"]=root;}stockSoundZonesMappingLoaded(path);Py_RETURN_NONE;}
PyObject* geometry(PyObject*,PyObject* args){const char* path;if(!PyArg_ParseTuple(args,"s",&path))return nullptr;std::ifstream in(path,std::ios::binary);unsigned n;in.read(reinterpret_cast<char*>(&n),4);assert(in);for(unsigned i=0;i<n;++i){Zone zone;Vector3 camera;int expected;static_assert(sizeof(Zone)==44,"Original zone ABI");in.read(reinterpret_cast<char*>(&zone),44);in.read(reinterpret_cast<char*>(&camera),12);in.read(reinterpret_cast<char*>(&expected),4);assert(in&&zone.contains(camera)==bool(expected));}return PyLong_FromUnsignedLong(n);}
int main(int argc,char** argv){assert(argc==3);Py_Initialize();std::vector<PyMethodDef> methods;for(auto& l:links())methods.push_back({const_cast<char*>(l.name),l.function,METH_VARARGS,nullptr});methods.push_back({nullptr,nullptr,0,nullptr});assert(Py_InitModule("BigWorld",methods.data()));static PyMethodDef checks[]={{const_cast<char*>("reset"),reset,METH_NOARGS,nullptr},{const_cast<char*>("tick"),tick,METH_VARARGS,nullptr},{const_cast<char*>("trace"),trace,METH_NOARGS,nullptr},{const_cast<char*>("clearSpace"),clearSpace,METH_NOARGS,nullptr},{const_cast<char*>("mapping"),mapping,METH_VARARGS,nullptr},{const_cast<char*>("geometry"),geometry,METH_VARARGS,nullptr},{nullptr,nullptr,0,nullptr}};assert(Py_InitModule("_zones",checks));PySys_SetArgv(argc-1,argv+1);FILE* f=fopen(argv[1],"rb");assert(f);int result=PyRun_SimpleFile(f,argv[1]);fclose(f);finiStockSoundZones();Py_Finalize();return result?1:0;}
