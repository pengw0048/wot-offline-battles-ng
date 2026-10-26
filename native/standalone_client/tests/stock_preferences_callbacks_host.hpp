#pragma once
#include <Python.h>
#include <map>
#include <memory>
#include <string>
#include <vector>
#include <algorithm>
#define BW_BEGIN_NAMESPACE namespace BW {
#define BW_END_NAMESPACE }
#define BW_GUARD
#define ERROR_MSG(...) ((void)0)
#define PY_MODULE_FUNCTION(name,module)
namespace BW {
using string=std::string;using uint32=unsigned;
struct DataSection;using DataSectionPtr=std::shared_ptr<DataSection>;
struct DataSection {
 std::map<string,DataSectionPtr> children;PyObject*values;
 DataSection():values(PyDict_New()){}~DataSection(){Py_DECREF(values);}
 int countChildren(){return static_cast<int>(children.size()+PyDict_Size(values));}
 DataSectionPtr openSection(const string&name,bool create=false){auto f=children.find(name);if(f!=children.end())return f->second;if(!create)return {};return children[name]=std::make_shared<DataSection>();}
 void delChildren(){children.clear();PyDict_Clear(values);}
 void put(const char*key,PyObject*value){PyDict_SetItemString(values,key,value);Py_DECREF(value);}
 void writeInt(const char*key,int value){put(key,PyInt_FromLong(value));}
 void writeBool(const char*key,bool value){put(key,PyBool_FromLong(value));}
 void writeFloat(const char*key,float value){put(key,PyFloat_FromDouble(value));}
 void writeWideStrings(const char*key,const std::vector<std::wstring>&v){writeInt(key,static_cast<int>(v.size()));}
 void copy(DataSectionPtr p){PyDict_Update(values,p->values);children=p->children;}
};
class PyObjectPtr {PyObject*p_;public:static const bool STEAL_REFERENCE=true;explicit PyObjectPtr(PyObject*p=nullptr,bool steal=false):p_(p){if(!steal)Py_XINCREF(p_);}PyObjectPtr(const PyObjectPtr&o):PyObjectPtr(o.p_){}~PyObjectPtr(){Py_XDECREF(p_);}PyObject*get()const{return p_;}};
namespace Script {
struct FiniTimeJob {virtual~FiniTimeJob(){}virtual void fini()=0;};
inline bool call(PyObject*f,PyObject*a,const char*){PyObject*r=PyObject_CallObject(f,a);Py_DECREF(f);Py_DECREF(a);if(!r){PyErr_Print();return false;}Py_DECREF(r);return true;}
}
extern PyTypeObject dataType;
struct PyDataSection:PyObject {
 DataSectionPtr section;
 explicit PyDataSection(DataSectionPtr p):section(p){PyObject_Init(this,&dataType);}
 static void*operator new(std::size_t n){return PyObject_Malloc(n);}
 static void operator delete(void*p){PyObject_Free(p);}
};
struct PathedFilename {string path="preferences.xml";string resolveName()const{return path;}};
struct DeviceApp {PathedFilename preferencesFilename_;bool savePreferences();};
struct DataHandle {enum{DHE_NoError=0,DHE_Error=1};};
struct DataResource {
 static bool fail;static DataSectionPtr lastSaved;static int writes;
 DataSectionPtr root=std::make_shared<DataSection>();DataResource(const string&,int,bool){}
 DataSectionPtr getRootSection(){return root;}int save(){++writes;lastSaved=root;return fail?DataHandle::DHE_Error:DataHandle::DHE_NoError;}
};
const int RESOURCE_TYPE_XML=1;
struct BWResource {static bool watched;static void ensureAbsolutePathExists(const string&){}
 struct WatchAccessFromCallingThreadHolder {bool old;explicit WatchAccessFromCallingThreadHolder(bool value):old(watched){watched=value;}~WatchAccessFromCallingThreadHolder(){watched=old;}};
};
extern DeviceApp fixtureDevice;
struct App {static App&instance(){static App app;return app;}bool savePreferences(){return fixtureDevice.savePreferences();}};
struct Vector2 {float x,y;};struct D3DDISPLAYMODE {int Width,Height;};
namespace Moo {
struct DeviceInfo {std::vector<D3DDISPLAYMODE>displayModes_{{1920,1080}};};
struct GraphicsSetting {static void write(DataSectionPtr p){p->writeInt("nativeGraphics",9);}};
struct RenderContext {bool windowed(){return true;}bool waitForVBL(){return true;}bool tripleBuffering(){return false;}float fullScreenAspectRatio(){return 1.5f;}Vector2 windowedModeSize(){return {800,600};}const DeviceInfo&deviceInfo(int){static DeviceInfo s;return s;}unsigned modeIndex(){return 0;}};
inline RenderContext&rc(){static RenderContext s;return s;}
}
struct CanvasApp {using StringVector=std::vector<std::wstring>;static CanvasApp instance;StringVector pythonConsoleHistory(){return {L"history"};}};
extern DataSectionPtr s_scriptsPreferences;
}
