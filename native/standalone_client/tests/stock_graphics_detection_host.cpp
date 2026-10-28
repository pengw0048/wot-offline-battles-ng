#include <Python.h>
#include <algorithm>
#include <cassert>
#include <cstdint>
#include <cstdio>
#include <map>
#include <memory>
#include <string>
#include <vector>
#define BW_BEGIN_NAMESPACE
#define BW_END_NAMESPACE
using int64=long long;
using uint32=unsigned;
using uint=unsigned;
namespace BW {template<class T>using vector=std::vector<T>;}
@SDK_SUPPORT@
struct DataSection;
struct DataSectionPtr:std::shared_ptr<DataSection>{
    using std::shared_ptr<DataSection>::shared_ptr;
    DataSectionPtr()=default;DataSectionPtr(const std::shared_ptr<DataSection>& p):std::shared_ptr<DataSection>(p){}
    bool exists()const{return bool(*this);}
};
struct DataSection {
    std::string text;
    std::map<std::string,int64> integers;
    std::map<std::string,float> floats;
    std::vector<std::pair<std::string,DataSectionPtr>> children;
    std::string asString()const{return text;}
    void openSections(const char* name,std::vector<DataSectionPtr>& out){
        for(const auto& child:children)if(child.first==name)out.push_back(child.second);
    }
    DataSectionPtr openSection(const char* name,bool create=false){
        for(const auto& child:children)if(child.first==name)return child.second;
        if(!create)return {};auto child=std::make_shared<DataSection>();children.emplace_back(name,child);return child;
    }
    int readInt(const char* key,int fallback){auto i=integers.find(key);return i==integers.end()?fallback:int(i->second);}
    int64 readInt64(const char* key,int64 fallback){auto i=integers.find(key);return i==integers.end()?fallback:i->second;}
    float readFloat(const char* key,float fallback){auto i=floats.find(key);return i==floats.end()?fallback:i->second;}
    bool writeInt64(const char* key,int64 value){integers[key]=value;return true;}
};
DataSectionPtr presetResource,boundsResource;
bool resourceAvailable=true,deviceAvailable=true,initialized=true;
unsigned initCalls=0,measurementCalls=0,commitCalls=0; int appliedGroup=-1;
void configureStockGraphicsDetection(DataSectionPtr,DataSectionPtr);
namespace Moo {
struct GraphicsSetting {
    using SettingsVector = std::vector<std::pair<std::string,int>>;
    static void commitSettings(SettingsVector&) {++commitCalls;} static void commitPending() {}
};
struct GraphicsSettingsAutoDetector {bool init(){++initCalls;configureStockGraphicsDetection(presetResource,boundsResource);return resourceAvailable;}
    bool getSettings(GraphicsSetting::SettingsVector&, int selected){appliedGroup=selected;return true;}};
bool isInitialised(){return initialized;}
struct RenderContext {unsigned ps=0x300,vs=0x300;void* device(){return deviceAvailable?this:nullptr;}unsigned psVersion(){return ps;}unsigned vsVersion(){return vs;}};
RenderContext& rc(){assert(initialized);static RenderContext owner;return owner;}
}
void publishStockGraphicsPreferences(const Moo::GraphicsSetting::SettingsVector&) {}
bool stockMeasureGraphicsHardware(int64 (&scores)[5]) { ++measurementCalls; for(auto & score:scores) score=1000; return true; }
#include "stock_graphics_detection.cpp"

PyObject* hostConfigure(PyObject*,PyObject* args){
    PyObject* profiles;if(!PyArg_ParseTuple(args,"O",&profiles))return nullptr;
    presetResource=std::make_shared<DataSection>();boundsResource=std::make_shared<DataSection>();
    for(Py_ssize_t i=0;i<PySequence_Size(profiles);++i){
        PyObjectPtr entry(PySequence_GetItem(profiles,i),true);
        PyObject* name=PyDict_GetItemString(entry.get(),"name");
        auto profile=std::make_shared<DataSection>();profile->text=PyString_AsString(name);
        auto bounds=std::make_shared<DataSection>();bounds->text=profile->text;
        for(const char* key:thresholdFields){
            PyObject* value=PyDict_GetItemString(entry.get(),key);if(value)bounds->integers[key]=PyLong_AsLongLong(value);
        }
        for(const char* key:{"PixelShaderVersion","VertexShaderVersion"}){
            PyObject* value=PyDict_GetItemString(entry.get(),key);if(value)bounds->floats[key]=float(PyFloat_AsDouble(value));
        }
        presetResource->children.emplace_back("graphicsPreferences",profile);
        boundsResource->children.emplace_back("graphicsPreferences",bounds);
    }
    if(PyErr_Occurred())return nullptr;
    resourceAvailable=true;configured=false;Py_RETURN_NONE;
}
PyObject* hostImport(PyObject*,PyObject* args){
    PyObject* source;if(!PyArg_ParseTuple(args,"O",&source))return nullptr;
    auto preferences=std::make_shared<DataSection>();
    if(source!=Py_None){
        auto saved=preferences->openSection("detector",true);
        PyObject *key,*value;Py_ssize_t index=0;
        while(PyDict_Next(source,&index,&key,&value))saved->integers[PyString_AsString(key)]=PyLong_AsLongLong(value);
    }
    readStockGraphicsDetectionPreferences(preferences);Py_RETURN_NONE;
}
PyObject* hostExport(PyObject*,PyObject*){
    auto preferences=std::make_shared<DataSection>();writeStockGraphicsDetectionPreferences(preferences);
    auto saved=preferences->openSection("detector");PyObject* result=PyDict_New();
    for(const auto& field:saved->integers){PyObject* value=PyLong_FromLongLong(field.second);PyDict_SetItemString(result,field.first.c_str(),value);Py_DECREF(value);}
    return result;
}
PyObject* hostState(PyObject*,PyObject*){return Py_BuildValue("(II)",initCalls,measurementCalls);}
PyObject* hostDevice(PyObject*,PyObject* args){int ready;unsigned ps,vs;if(!PyArg_ParseTuple(args,"iII",&ready,&ps,&vs))return nullptr;deviceAvailable=ready!=0;Moo::rc().ps=ps;Moo::rc().vs=vs;Py_RETURN_NONE;}
PyObject* hostSchedule(PyObject*,PyObject* args){int apply, group;if(!PyArg_ParseTuple(args,"ii",&apply,&group))return nullptr;scheduleStockGraphicsDetection(apply!=0,group);Py_RETURN_NONE;}
PyObject* hostFinish(PyObject*,PyObject*){finishStockGraphicsDetectionStartup();Py_RETURN_NONE;}
PyObject* hostMeasured(PyObject*,PyObject*){return Py_BuildValue("(Ii)",commitCalls,appliedGroup);}
PyObject* hostMeasure(PyObject*,PyObject*){return PyInt_FromLong(measureStockGraphicsDetection());}
PyObject* hostMissing(PyObject*,PyObject*){presetResource.reset();configured=false;resourceAvailable=false;Py_RETURN_NONE;}
PyMethodDef methods[]={{"configure",hostConfigure,METH_VARARGS,nullptr},{"importScores",hostImport,METH_VARARGS,nullptr},
    {"exportScores",hostExport,METH_NOARGS,nullptr},{"state",hostState,METH_NOARGS,nullptr},
    {"device",hostDevice,METH_VARARGS,nullptr},{"schedule",hostSchedule,METH_VARARGS,nullptr},{"finish",hostFinish,METH_NOARGS,nullptr},{"applied",hostMeasured,METH_NOARGS,nullptr},{"measure",hostMeasure,METH_NOARGS,nullptr},{"missingResource",hostMissing,METH_NOARGS,nullptr},{nullptr,nullptr,0,nullptr}};
int main(int argc,char** argv){
    assert(argc>=2); resourceAvailable=false; assert(measureStockGraphicsDetection()==2); assert(!Py_IsInitialized()); initCalls=0; resourceAvailable=true; Py_SetProgramName(argv[0]);Py_Initialize();PySys_SetArgv(argc-1,argv+1);
    assert(Py_InitModule("_graphics_detection_host",methods));assert(hostMethodLinks().size()==3);
    for(auto& link:hostMethodLinks()){
        PyObject* module=PyImport_AddModule(link.module.c_str());PyObject* fn=PyCFunction_NewEx(&link.def,nullptr,nullptr);
        assert(fn&&PyObject_SetAttrString(module,link.def.ml_name,fn)==0);Py_DECREF(fn);
    }
    FILE* source=fopen(argv[1],"rb");assert(source);int status=PyRun_SimpleFileEx(source,argv[1],1);
    if(status)PyErr_Print();Py_Finalize();return status?1:0;
}
