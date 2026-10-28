// Actual production bank diff/selector and SDK camera-space callback bodies.
// Wwise bank operations, DataSection storage and unrelated scene work are
// explicit boundaries; the fixture does not claim speaker output.
#include <algorithm>
#include <cassert>
#include <map>
#include <memory>
#include <set>
#include <string>
#include <vector>
#define BW_GUARD
#define BW_GUARD_PROFILER(...)
#define ERROR_MSG(...) (++errors)
namespace BW {using string=std::string;}
unsigned errors=0,loads=0,callbacks=0;
std::vector<std::string> operations;
struct Section;
using DataSectionPtr=std::shared_ptr<Section>;
struct Section {
 std::map<std::string,DataSectionPtr> children;
 DataSectionPtr openSection(const char*name){auto i=children.find(name);return i==children.end()?nullptr:i->second;}
};
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
struct ClientSpace {EnviroMinder environment;EnviroMinder& enviro(){return environment;}int id(){return 1;}};
using ClientSpacePtr=std::shared_ptr<ClientSpace>;
namespace DeprecatedSpaceHelpers {ClientSpacePtr current;ClientSpacePtr cameraSpace(){return current;}}
struct Picker {void update(float){}};
struct CameraApp {static CameraApp& instance(){static CameraApp value;return value;}Picker picker;Picker& entityPicker(){return picker;}};
struct TimeGlobals {static TimeGlobals& instance(){static TimeGlobals value;return value;}void setupWatchersFirstTimeOnly(){}};
struct PyDataSection {explicit PyDataSection(DataSectionPtr){}};
struct ScriptErrorPrint {explicit ScriptErrorPrint(const char*){}};
struct ScriptObject {
 enum {FROM_NEW_REFERENCE};std::shared_ptr<PyDataSection> data;
 ScriptObject()=default;
 ScriptObject(PyDataSection*value,int):data(value){}
 explicit operator bool()const{return true;}
 static ScriptObject none(){return {};}
 void callMethod(const char*,int,ScriptErrorPrint,bool){
  ++callbacks;operations.push_back("callback");
  if(!stockAudioReady())return;
  const auto settings=DeprecatedSpaceHelpers::current->environment.data;
  assert(StockAudioDetail::state.banks.count(settings&&settings->openSection("gameplayTypes")?"wpn.bnk":"hangar_v2.bnk"));
 }
};
struct Personality {static ScriptObject instance(){return {};}};
struct ScriptArgs {static int create(int,ScriptObject){return 0;}};
struct FacadeApp {
 float dGameTime_=0;ClientSpacePtr lastCameraSpace_;void* todUpdateNotifier_=nullptr;
 bool isCameraOutside(){return true;}
 void tick(float,float);
};
@FACADE_TICK@
int main(){
 using namespace StockAudioDetail;
 state.permanent.insert("permanent.bnk");assert(selectStockAudioBankSet("SFX_soundbanks_login"));
 auto arena=std::make_shared<ClientSpace>();arena->environment.data=std::make_shared<Section>();
 // Even an empty gameplayTypes section means arena; it is not a bool value.
 arena->environment.data->children["gameplayTypes"]=std::make_shared<Section>();
 DeprecatedSpaceHelpers::current=arena;FacadeApp app;app.tick(.01f,.01f);
 assert(callbacks==1&&state.banks.count("wpn.bnk")&&!state.banks.count("hangar_v2.bnk"));
 const auto admittedLoads=loads;app.tick(.01f,.01f);assert(callbacks==1&&loads==admittedLoads);
 auto garage=std::make_shared<ClientSpace>();garage->environment.data=std::make_shared<Section>();
 operations.clear();DeprecatedSpaceHelpers::current=garage;app.tick(.01f,.01f);
 assert(callbacks==2&&state.banks.count("hangar_v2.bnk")&&!state.banks.count("wpn.bnk"));
 assert(state.banks.count("permanent.bnk"));
 const auto where=[](const char*name){return std::find(operations.begin(),operations.end(),name)-operations.begin();};
 assert(where("deactivate")<where("unload:wpn.bnk"));
 assert(where("load:hangar_v2.bnk")<where("activate")&&where("activate")<where("callback"));
 DeprecatedSpaceHelpers::current=arena;app.tick(.01f,.01f);
 assert(callbacks==3&&state.banks.count("wpn.bnk"));
 DeprecatedSpaceHelpers::current=nullptr;app.tick(.01f,.01f);assert(callbacks==3);
 // Device-disabled startup is a native guard, not a bank-load error.
 state.ready=false;const auto saved=state.banks;setStockAudioSpaceBankSet(nullptr);
 assert(state.banks==saved&&errors==0);state.ready=true;
 setStockAudioSpaceBankSet(nullptr);assert(state.banks.count("hangar_v2.bnk"));
 failedBank="wpn.bnk";setStockAudioSpaceBankSet(arena->environment.data);
 assert(errors==1&&!state.banks.count("wpn.bnk"));
 failedBank.clear();setStockAudioSpaceBankSet(arena->environment.data);
 assert(state.banks.count("wpn.bnk")&&state.banks.count("permanent.bnk"));
}
