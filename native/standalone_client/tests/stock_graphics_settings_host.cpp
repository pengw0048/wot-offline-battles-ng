#include <Python.h>
#include "wot/stock_graphics_presets.hpp"
#include "wot/stock_graphics_settings.hpp"
#include "wot/stock_graphics_detection.hpp"
#include <algorithm>
#include <cassert>
#include <cstdint>
#include <map>
#include <memory>
#include <string>
#include <vector>
#define BW_BEGIN_NAMESPACE
#define BW_END_NAMESPACE
#define BW_GUARD
#define MF_ASSERT(value) assert(value)
#define ERROR_MSG(...) ((void)0)
#define PY_MODULE_FUNCTION(name,module) static PyObject* wrap_py_##name(PyObject*,PyObject* args){return py_##name(args);}
using uint32=std::uint32_t;
namespace BW{using string=std::string;template<class T>using vector=std::vector<T>;}
template<class T>struct Ptr:std::shared_ptr<T>{
using std::shared_ptr<T>::shared_ptr;Ptr(T* p):std::shared_ptr<T>(p){}Ptr()=default;
Ptr(const std::shared_ptr<T>& p):std::shared_ptr<T>(p){}
T* getObject()const{return this->get();}bool exists()const{return bool(*this);}};
struct DataSection;using DataSectionPtr=Ptr<DataSection>;
struct DataSection{
std::string text,label;int option=0;std::map<std::string,int> integers;
std::vector<std::pair<std::string,DataSectionPtr>> children;
void openSections(const char* name,std::vector<DataSectionPtr>& output){for(const auto& p:children)if(p.first==name)output.push_back(p.second);}
std::string asString()const{return text;}
std::string readString(const char* name,const char*)const{assert(std::string(name)=="label");return label;}
int readInt(const char* name,int fallback)const{if(std::string(name)=="activeOption")return option;auto i=integers.find(name);return i==integers.end()?fallback:i->second;}
void writeInt(const char* name,int value){integers[name]=value;}
DataSectionPtr openSection(const char* name,bool create=false){
for(const auto& p:children)if(p.first==name)return p.second;
if(!create)return {};auto p=std::make_shared<DataSection>();children.emplace_back(name,p);return p;}
std::int64_t readInt64(const char* name,std::int64_t fallback)const{auto i=integers.find(name);return i==integers.end()?fallback:i->second;}
void writeInt64(const char* name,std::int64_t value){integers[name]=int(value);}
};
DataSectionPtr fixture(){@FIXTURE@}
struct BWResource{
static int content;static BWResource& instance(){static BWResource r;return r;}
int stockActiveContentType()const{return content;}
static DataSectionPtr openSection(const char* name){assert(std::string(name)=="system/data/graphics_settings_presets.xml");return fixture();}
};int BWResource::content=0;
struct PyObjectPtr{
enum ReferenceType{STEAL_REFERENCE};PyObject* value;
PyObjectPtr(PyObject* p,ReferenceType):value(p){}
PyObjectPtr(const PyObjectPtr& p):value(p.value){Py_XINCREF(value);}
PyObjectPtr& operator=(const PyObjectPtr& p){Py_XINCREF(p.value);Py_XDECREF(value);value=p.value;return *this;}
~PyObjectPtr(){Py_XDECREF(value);}explicit operator bool()const{return value!=nullptr;}PyObject* get()const{return value;}
};
namespace Script{int setData(PyObject* value,std::string& output){
char* data;Py_ssize_t size;if(PyString_AsStringAndSize(value,&data,&size)<0)return -1;output.assign(data,size);return 0;}}
namespace Moo{
struct FakeRC {void* device(){return this;}}; FakeRC& rc(){static FakeRC x;return x;} bool isInitialised(){return true;}
struct GraphicsSetting{
using GraphicsSettingPtr=Ptr<GraphicsSetting>;using GraphicsSettingVector=std::vector<GraphicsSettingPtr>;
using Setting=std::pair<std::string,uint32>;using SettingsVector=std::vector<Setting>;using StringIntMap=std::map<std::string,int>;
struct Option{std::string m_label,m_desc;bool m_supported,m_advanced;};
std::string label_,desc_;int activeOption_;bool delayed_,needsRestart_,advanced_;
bool stockImportant_=true,stockArrayOptions_=false;std::vector<Option> options_;GraphicsSettingVector slaves_;
GraphicsSetting(const char* label,const char* desc,int selected,bool delayed,bool restart,bool advanced=false):
label_(label),desc_(desc),activeOption_(selected),delayed_(delayed),needsRestart_(restart),advanced_(advanced){}
virtual ~GraphicsSetting(){}virtual void onOptionSelected(int)=0;
void addOption(const char* label,const char* desc,bool supported,bool advanced=false){options_.push_back({label,desc,supported,advanced});}
const std::string& label()const{return label_;}const std::string& desc()const{return desc_;}
int activeOption()const{return activeOption_;}bool requiresRestart()const{return needsRestart_;}
bool delayed()const{return delayed_;}bool advanced()const{return advanced_;}
bool stockImportant()const{return stockImportant_;}void stockImportant(bool value){stockImportant_=value;}
bool stockArrayOptions()const{return stockArrayOptions_;}void stockArrayOptions(bool value){stockArrayOptions_=value;}
const std::vector<Option>& options()const{return options_;}void addSlave(GraphicsSettingPtr p){slaves_.push_back(p);}
struct Data{GraphicsSettingVector s_settings;StringIntMap s_latentSettings;SettingsVector s_oldSettings;bool s_needsRestart=false,s_advanced=true;};
static Data& getSettingsData(){static Data d;return d;}
static const GraphicsSettingVector& settings(){return getSettingsData().s_settings;}
static GraphicsSettingPtr getFromLabel(const std::string& label){for(const auto& p:settings())if(p->label()==label)return p;return {};}
static bool isPending(GraphicsSettingPtr,int&){return false;}static bool addPending(GraphicsSetting*,int){assert(false);return false;}
static void add(GraphicsSettingPtr);bool selectOption(int,bool=false);bool selectOptionPriv(int,bool=false);
static SettingsVector stockCurrentSettings();static void applyStockPreset(const SettingsVector&);
static void captureStockPresetBaseline();static bool needsRestart();
private:
static void commitSettings(SettingsVector&);
public:
static void commitPending(){}
};
struct GraphicsSettingsAutoDetector{
static int invalidations,queries;static bool valid;static GraphicsSetting::SettingsVector detected;
static void invalidateStockDetection(){++invalidations;}
bool init(){return valid;}
bool getSettings(GraphicsSetting::SettingsVector& out,int){++queries;out=detected;return true;}
};int GraphicsSettingsAutoDetector::invalidations=0;int GraphicsSettingsAutoDetector::queries=0;
bool GraphicsSettingsAutoDetector::valid=true;GraphicsSetting::SettingsVector GraphicsSettingsAutoDetector::detected;
}
@INTERFACES@
namespace Moo{@REGISTRY@}
static std::vector<std::pair<std::string,int>> calls;
struct ProbeSetting:Moo::GraphicsSetting{
ProbeSetting(const char* label,int selected,bool delayed):GraphicsSetting(label,"description",selected,delayed,false){
for(int i=0;i<5;++i)addOption("option","option description",true);}
void onOptionSelected(int option){calls.emplace_back(label(),option);}
};
@PRODUCTION@
static PyObject* seed(PyObject*,PyObject* args){
PyObject* sequence;if(!PyArg_ParseTuple(args,"O",&sequence))return NULL;
Moo::GraphicsSetting::SettingsVector input;
for(Py_ssize_t i=0;i<PySequence_Size(sequence);++i){PyObjectPtr p(PySequence_GetItem(sequence,i),PyObjectPtr::STEAL_REFERENCE);
const char* label;int selected;if(!p||!PyArg_ParseTuple(p.get(),"si",&label,&selected))return NULL;
input.emplace_back(label,uint32(selected));Moo::GraphicsSetting::getSettingsData().s_latentSettings[label]=selected;}
publishStockGraphicsPreferences(input);Py_RETURN_NONE;}
static PyObject* add(PyObject*,PyObject* args){const char* label;int selected,delayed;
if(!PyArg_ParseTuple(args,"sii",&label,&selected,&delayed))return NULL;
Moo::GraphicsSetting::add(new ProbeSetting(label,selected,delayed!=0));Py_RETURN_NONE;}
static PyObject* flags(PyObject*,PyObject* args){const char* label;int restart,advanced,important;
if(!PyArg_ParseTuple(args,"siii",&label,&restart,&advanced,&important))return NULL;
auto p=Moo::GraphicsSetting::getFromLabel(label);assert(p);p->needsRestart_=restart!=0;p->advanced_=advanced!=0;p->stockImportant(important!=0);Py_RETURN_NONE;}
static PyObject* supported(PyObject*,PyObject* args){const char* label;int index,value;
if(!PyArg_ParseTuple(args,"sii",&label,&index,&value))return NULL;
auto p=Moo::GraphicsSetting::getFromLabel(label);assert(p);p->options_.at(index).m_supported=value!=0;Py_RETURN_NONE;}
static PyObject* initialise(PyObject*,PyObject*){if(!initialiseStockGraphicsPresets())return NULL;Py_RETURN_NONE;}
static PyObject* getCalls(PyObject*,PyObject*){PyObject* result=PyList_New(calls.size());for(std::size_t i=0;i<calls.size();++i)
PyList_SET_ITEM(result,i,Py_BuildValue("si",calls[i].first.c_str(),calls[i].second));calls.clear();return result;}
static PyObject* prepare(PyObject*,PyObject* args){PyObject *fields,*sequence;int content;
if(!PyArg_ParseTuple(args,"OiO",&fields,&content,&sequence))return NULL;
DataSectionPtr section;if(fields!=Py_None){section=new DataSection;PyObject *key,*value;Py_ssize_t pos=0;
while(PyDict_Next(fields,&pos,&key,&value)){char* label=PyString_AsString(key);if(!label)return NULL;
long n=PyInt_AsLong(value);if(PyErr_Occurred())return NULL;section->integers[label]=int(n);}}
Moo::GraphicsSetting::SettingsVector entries;
for(Py_ssize_t i=0;i<PySequence_Size(sequence);++i){PyObjectPtr p(PySequence_GetItem(sequence,i),PyObjectPtr::STEAL_REFERENCE);
const char* label;int value;if(!p||!PyArg_ParseTuple(p.get(),"si",&label,&value))return NULL;entries.emplace_back(label,uint32(value));}
BWResource::content=content;bool result=prepareStockGraphicsPreferences(section,entries,-1);
if(!result){PyErr_SetString(PyExc_RuntimeError,"Detector initialization failed");return NULL;}
return dictionary(toStock(entries));}
static PyObject* deferred(PyObject*,PyObject* args){const char* name;int value;if(!PyArg_ParseTuple(args,"si",&name,&value))return NULL;
Moo::GraphicsSettingsAutoDetector::detected={{name,uint32(value)}};pendingApply=true;finishStockGraphicsDetectionStartup();Py_RETURN_NONE;}
static PyObject* metadata(PyObject*,PyObject*){DataSectionPtr section=new DataSection;writeStockGraphicsPreferencesMetadata(section);
PyObject* result=PyDict_New();for(const auto& pair:section->integers){PyObjectPtr value(PyInt_FromLong(pair.second),PyObjectPtr::STEAL_REFERENCE);
if(PyDict_SetItemString(result,pair.first.c_str(),value.get())<0){Py_DECREF(result);return NULL;}}return result;}
static PyObject* detector(PyObject*,PyObject* args){int valid;if(!PyArg_ParseTuple(args,"i",&valid))return NULL;
Moo::GraphicsSettingsAutoDetector::valid=valid!=0;Py_RETURN_NONE;}
static PyObject* detectorCalls(PyObject*,PyObject*){return Py_BuildValue("ii",Moo::GraphicsSettingsAutoDetector::invalidations,Moo::GraphicsSettingsAutoDetector::queries);}
static PyObject* settingList(PyObject*,PyObject*){return graphicsSettings();}
static PyObject* probeSet(PyObject*,PyObject* args){const char* label;int index;if(!PyArg_ParseTuple(args,"si",&label,&index))return NULL;
bool ok=setGraphicsSetting(label,index);if(!ok||PyErr_Occurred())return NULL;Py_RETURN_NONE;}
static PyObject* probeGet(PyObject*,PyObject* args){const char* label;if(!PyArg_ParseTuple(args,"s",&label))return NULL;
int result=getGraphicsSetting(label);if(PyErr_Occurred())return NULL;return PyInt_FromLong(result);}
PyMODINIT_FUNC initgraphics_settings_probe(){
static PyMethodDef methods[]={
@METHODS@
{"_seed",seed,METH_VARARGS,NULL},{"_register",add,METH_VARARGS,NULL},{"_flags",flags,METH_VARARGS,NULL},
{"_supported",supported,METH_VARARGS,NULL},{"_initialise",initialise,METH_NOARGS,NULL},{"_calls",getCalls,METH_NOARGS,NULL},
{"_deferred",deferred,METH_VARARGS,NULL},{"_prepare",prepare,METH_VARARGS,NULL},{"_metadata",metadata,METH_NOARGS,NULL},{"_detector",detector,METH_VARARGS,NULL},
{"_detectorCalls",detectorCalls,METH_NOARGS,NULL},{"graphicsSettings",settingList,METH_NOARGS,NULL},
{"setGraphicsSetting",probeSet,METH_VARARGS,NULL},{"getGraphicsSetting",probeGet,METH_VARARGS,NULL},{NULL,NULL,0,NULL}};
Py_InitModule("graphics_settings_probe",methods);}
