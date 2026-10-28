// Execute the SDK completion body after composition with actual retirement seams.
#include <cassert>
#include <string>
#include <vector>
#define PROFILER_SCOPED(...)
#define BW_GUARD
#define ENABLE_WATCHERS 0
#define MF_ASSERT assert
#define TRACE_MSG(...)
#define ASSET_MSG(...)
using uint64=unsigned long long;
uint64 timestamp(){return 1;}
double stampsPerSecondD(){return 1.;}
std::vector<std::string> logEntries;
void stockSoundZonesMappingLoaded(const std::string& path){assert(path=="spaces/map");logEntries.push_back("default-aux");}
struct Space {bool clear=false,revoked=false;bool cleared(){return clear;}bool stockLoadPublicationRevoked(){return revoked;}void mappingLoaded(void*){logEntries.push_back("mapping");}};
struct ILoader {bool fail=false,must=true;bool bind(){logEntries.push_back("bind");return !fail;}bool loadMustSucceed(){return must;}};
struct CompiledSpaceMapping {enum {STATE_LOADING,STATE_LOADED,STATE_FAILED_TO_LOAD};using Loaders=std::vector<ILoader*>;int state_=STATE_LOADING;void* pTask_=reinterpret_cast<void*>(1);unsigned numFailures_=0;bool stockPublicationRevoked_=false;Space space_;Loaders loaders_;std::string path_="spaces/map";uint64 loadStartTimestamp_=0;void spawnEntities(){logEntries.push_back("entities");}void spawnUserDataObjects(){logEntries.push_back("udo-register-zone");}void loadBGFinished();};
@BODY@
struct DeviceApp {static DeviceApp instance;void deleteGUI(){logEntries.push_back("delete-gui");}};DeviceApp DeviceApp::instance;
struct SimpleGUI {static SimpleGUI& instance(){static SimpleGUI v;return v;}void clearSimpleComponents(){logEntries.push_back("gui-clear");}static void fini(){logEntries.push_back("gui-fini");}};
struct BgTaskManager {static BgTaskManager& instance(){static BgTaskManager v;return v;}void stopAll(){logEntries.push_back("bg-stop");}};
void finiStockVehicleAudio(){logEntries.push_back("vehicle-audio-fini");}void finiStockAudio(){logEntries.push_back("audio-fini");}void finiStockSpaceSounds(){logEntries.push_back("space-sounds-fini");}void finiStockSoundZones(){logEntries.push_back("zones-fini");}
struct BigWorldClientScript {static void fini(){logEntries.push_back("python-fini");}};struct ScriptApp {void fini();};
@FINI@
int main(){for(unsigned reason=0;reason!=5;++reason){logEntries.clear();CompiledSpaceMapping value;ILoader loader;value.loaders_.push_back(&loader);value.stockPublicationRevoked_=reason==1;value.space_.clear=reason==2;value.space_.revoked=reason==3;loader.fail=reason==4;value.loadBGFinished();assert(!value.pTask_);if(reason==0)assert((logEntries==std::vector<std::string>{"bind","default-aux","mapping","entities","udo-register-zone"}));else assert((logEntries==std::vector<std::string>{"bind"}));}logEntries.clear();ScriptApp app;app.fini();assert((logEntries==std::vector<std::string>{"delete-gui","gui-clear","bg-stop","vehicle-audio-fini","audio-fini","space-sounds-fini","zones-fini","python-fini","gui-fini"}));}
