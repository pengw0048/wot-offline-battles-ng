// Real SDK mapping/camera bodies and production bank selector. Resource root
// children come from exact packed XML; Wwise I/O is the explicit host boundary.
#include <algorithm>
#include <cassert>
#include <fstream>
#include <map>
#include <memory>
#include <set>
#include <sstream>
#include <string>
#include <vector>
#include "stock_audio_space_path.hpp"
#define BW_GUARD
#define BW_GUARD_PROFILER(...)
#define MF_ASSERT(x) assert(x)
#define ERROR_MSG(...) (++errors)
#define INFO_MSG(...) ((void)0)
namespace BW {using string=std::string;}
unsigned errors=0,loads=0,callbacks=0;
std::vector<std::string> operations;
struct Section;
using DataSectionPtr=std::shared_ptr<Section>;
template<class T> using SmartPointer=std::shared_ptr<T>;
using DataSection=Section;
struct Section {
 std::set<std::string> children;
 DataSectionPtr openSection(const char*name){return children.count(name)?std::make_shared<Section>():nullptr;}
};
namespace BWResource {
std::map<std::string,DataSectionPtr> resources;
DataSectionPtr openSection(const std::string&path){operations.push_back("open:"+path);auto i=resources.find(path);return i==resources.end()?nullptr:i->second;}
}
namespace StockAudioDetail {
struct Manager {bool ready=true;std::set<BW::string> banks,permanent,current;} state;
Manager& manager(){return state;}
std::string failedBank;
bool bankNames(const BW::string&name,std::vector<BW::string>&output){
 static const std::map<std::string,std::vector<std::string>> lists={
  {"SFX_soundbanks_login",{"hangar_v2.bnk","permanent.bnk"}},
  {"SFX_soundbanks_hangar",{"hangar_v2.bnk","customization.bnk","permanent.bnk"}},
  {"SFX_soundbanks_arena",{"wpn.bnk","gun_reload.bnk","eng_gaz_aa.bnk","permanent.bnk"}}};
 auto i=lists.find(name);if(i==lists.end())return false;output=i->second;return true;
}
bool loadBank(const BW::string&name){
 if(name==failedBank)return false;
 if(state.banks.insert(name).second){operations.push_back("load:"+name);++loads;}
 return true;
}
bool unloadBank(const BW::string&name){operations.push_back("unload:"+name);state.banks.erase(name);return true;}
}
bool stockAudioReady(){return StockAudioDetail::state.ready;}
@BANK_DIFF@
@SPACE_SELECTOR@
struct TimeOfDay {void delUpdateNotifier(void*){}void addUpdateNotifier(void*){}};
struct EnviroMinder {
 DataSectionPtr data;TimeOfDay tod;
 DataSectionPtr pData(){return data;}
 void deactivate(){operations.push_back("deactivate");}
 void activate(){operations.push_back("activate");}
 TimeOfDay* timeOfDay(){return &tod;}
 void tick(float,bool){}
};
struct ClientSpace {virtual ~ClientSpace()=default;EnviroMinder environment;EnviroMinder& enviro(){return environment;}int id(){return 1;}};
using ClientSpacePtr=std::shared_ptr<ClientSpace>;
namespace DeprecatedSpaceHelpers {ClientSpacePtr current;ClientSpacePtr cameraSpace(){return current;}}
using GeometryMappingID=int;using uint32=unsigned;
struct Matrix {};
struct FileIOTaskManager {static int instance(){return 0;}};
void notifyStockAudioCameraSpace(ClientSpace*);
namespace CompiledSpace {
struct CompiledSpace;
struct CompiledSpaceMapping {
 std::string path_;DataSectionPtr pSettings_;
 @MAPPING_ACCESS@
 bool loadAsync(int){return true;}
 ~CompiledSpaceMapping(){operations.push_back("delete:"+path_);}
};
struct CompiledSpace:ClientSpace {
 struct Mapping {int mappingID_;CompiledSpaceMapping* pLoader_;};
 using Mappings=std::vector<Mapping>;Mappings mappings_;
 struct Environment {CompiledSpace*owner;void loadEnvironment(DataSectionPtr p){owner->environment.data=p;operations.push_back("environment");}} enviroMinderScene_{this};
 struct Lights {void clear(){}} dynamicLightScene_;
 void* pAssetClient_=nullptr;
 static CompiledSpaceMapping* create(const BW::string&p,Matrix&,DataSectionPtr s,CompiledSpace&,void*,uint32){return new CompiledSpaceMapping{p,s};}
 decltype(&create) mappingCreator_=&create;
 size_t mappingIDToIndex(int id){for(size_t i=0;i<mappings_.size();++i)if(mappings_[i].mappingID_==id)return i;return size_t(-1);}
 BW::string stockAudioGeometryPath();
 bool addMappingWithVisibilityMask(GeometryMappingID,Matrix&,const BW::string&,uint32,const DataSectionPtr& =DataSectionPtr());
 void doDelMapping(int);void doClear();
 ~CompiledSpace(){doClear();}
};
const char* SPACE_SETTING_FILE_NAME="space.settings";
template<class T> void bw_safe_delete(T*&p){delete p;p=nullptr;}
@SPACE_QUERY@
@ADD_MAPPING@
@DEL_MAPPING@
@CLEAR_MAPPING@
}
@NOTIFY@
struct Picker {void update(float){}};
struct CameraApp {static CameraApp& instance(){static CameraApp value;return value;}Picker picker;Picker& entityPicker(){return picker;}};
struct TimeGlobals {static TimeGlobals& instance(){static TimeGlobals value;return value;}void setupWatchersFirstTimeOnly(){}};
struct PyDataSection {explicit PyDataSection(DataSectionPtr){}};
struct ScriptErrorPrint {explicit ScriptErrorPrint(const char*){}};
// This is the native boundary before the actual initial music/ambient callback.
void musicProducer(){operations.push_back("music");auto*p=dynamic_cast<CompiledSpace::CompiledSpace*>(DeprecatedSpaceHelpers::current.get());
 const auto definition=BWResource::openSection(wot::stockAudioArenaResource(p->stockAudioGeometryPath()));
 if(stockAudioReady())assert(StockAudioDetail::state.banks.count(definition&&definition->openSection("gameplayTypes")?"wpn.bnk":"hangar_v2.bnk"));}
struct ScriptObject {
 enum {FROM_NEW_REFERENCE};std::shared_ptr<PyDataSection> data;
 ScriptObject()=default;ScriptObject(PyDataSection*value,int):data(value){}
 explicit operator bool()const{return true;}static ScriptObject none(){return {};}
 void callMethod(const char*,int,ScriptErrorPrint,bool){++callbacks;operations.push_back("callback");musicProducer();}
};
struct Personality {static ScriptObject instance(){return {};}};
struct ScriptArgs {static int create(int,ScriptObject){return 0;}};
struct FacadeApp {
 float dGameTime_=0;ClientSpacePtr lastCameraSpace_;void* todUpdateNotifier_=nullptr;
 bool isCameraOutside(){return true;}void tick(float,float);
};
@FACADE_TICK@
int main(int argc,char**argv){
 assert(argc==2);std::ifstream fixture(argv[1]);std::string line;
 while(std::getline(fixture,line)){std::istringstream row(line);std::string path,key;row>>path;auto section=std::make_shared<Section>();while(row>>key)section->children.insert(key);BWResource::resources[path]=section;}
 assert(BWResource::resources.count("spaces/33_fjord/space.settings"));
 assert(!BWResource::resources.at("spaces/33_fjord/space.settings")->openSection("gameplayTypes"));
 assert(BWResource::resources.at("scripts/arena_defs/33_fjord.xml")->openSection("wwmusicSetup"));
 using namespace StockAudioDetail;
 state.permanent.insert("permanent.bnk");assert(selectStockAudioBankSet("SFX_soundbanks_login"));
 auto arena=std::make_shared<CompiledSpace::CompiledSpace>();DeprecatedSpaceHelpers::current=arena;
 FacadeApp app;app.tick(.01f,.01f);assert(callbacks==1&&state.banks.count("hangar_v2.bnk"));
 Matrix transform;operations.clear();assert(arena->addMappingWithVisibilityMask(1,transform,"spaces/33_fjord",1));
 // Same pointer, later mapping publication must switch banks before its loaded
 // entities/UDOs and their initial music/ambient producers can execute.
 musicProducer();assert(state.banks.count("wpn.bnk"));
 assert(std::find(operations.begin(),operations.end(),"load:wpn.bnk")<std::find(operations.begin(),operations.end(),"music"));
 const auto admitted=loads;app.tick(.01f,.01f);assert(callbacks==1&&loads==admitted);
 auto garage=std::make_shared<CompiledSpace::CompiledSpace>();
 assert(garage->addMappingWithVisibilityMask(2,transform,"spaces/hangar_v2",1));
 assert(state.banks.count("wpn.bnk")); // Inactive preload cannot replace the active arena.
 operations.clear();DeprecatedSpaceHelpers::current=garage;app.tick(.01f,.01f);
 const auto where=[](const char*name){return std::find(operations.begin(),operations.end(),name)-operations.begin();};
 assert(where("deactivate")<where("unload:wpn.bnk"));
 assert(where("load:hangar_v2.bnk")<where("activate")&&where("activate")<where("callback"));
 assert(state.banks.count("permanent.bnk"));
 auto second=std::make_shared<CompiledSpace::CompiledSpace>();
 assert(second->addMappingWithVisibilityMask(3,transform,"spaces/01_karelia",1));
 assert(!state.banks.count("wpn.bnk"));
 DeprecatedSpaceHelpers::current=second;app.tick(.01f,.01f);assert(state.banks.count("wpn.bnk"));
 second->doDelMapping(3);assert(second->mappings_.empty()&&state.banks.count("hangar_v2.bnk"));
 assert(second->addMappingWithVisibilityMask(4,transform,"spaces/33_fjord",1));musicProducer();
 second->doClear();assert(second->mappings_.empty()&&state.banks.count("hangar_v2.bnk"));
 DeprecatedSpaceHelpers::current=arena;app.tick(.01f,.01f);assert(state.banks.count("wpn.bnk"));
 DeprecatedSpaceHelpers::current=nullptr;app.tick(.01f,.01f);
 state.ready=false;const auto saved=state.banks;setStockAudioSpaceBankSet("");assert(state.banks==saved&&errors==0);state.ready=true;
 setStockAudioSpaceBankSet("");failedBank="wpn.bnk";setStockAudioSpaceBankSet("spaces/33_fjord");
 assert(errors==1&&!state.banks.count("wpn.bnk"));failedBank.clear();setStockAudioSpaceBankSet("spaces/33_fjord");
 assert(state.banks.count("wpn.bnk")&&state.banks.count("permanent.bnk"));
 app.lastCameraSpace_.reset();arena.reset();garage.reset();second.reset();
}
