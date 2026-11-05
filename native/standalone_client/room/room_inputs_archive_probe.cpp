// Compare the legacy full-actor bridge with direct canonical-map sampling.
#include "room_inputs.hpp"
#include "room_json.hpp"
#include <chrono>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <new>
#include <set>

#ifdef WOT_ROOM_ARCHIVE_BENCH
namespace { bool measuring=false;std::uint64_t allocation_count=0,allocation_bytes=0; }
void* operator new(std::size_t size){if(measuring){++allocation_count;allocation_bytes+=size;}if(auto* value=std::malloc(size?size:1))return value;throw std::bad_alloc();}
void* operator new[](std::size_t size){return ::operator new(size);}
void operator delete(void* value) noexcept{std::free(value);}
void operator delete[](void* value) noexcept{std::free(value);}
void operator delete(void* value,std::size_t) noexcept{std::free(value);}
void operator delete[](void* value,std::size_t) noexcept{std::free(value);}
#endif
namespace wot::room {
namespace {
void require(bool condition,const char* detail){if(!condition)throw std::logic_error(detail);}
Value actors(const std::map<std::int64_t,Value>& bots){auto out=array();for(const auto& entry:bots)out.array.push_back(entry.second);return out;}
std::map<std::int64_t,Value> fixture(const Value& resources,int count,int contacts){
 std::map<std::int64_t,Value> bots;
 for(int id=1;id<=count;++id){auto bot=resources.get("public_bot_defaults").resolved();bot.object["id"]=integer(id);bot.object["team"]=integer(id%2+1);bot.object["vehicle"]=text("ussr:R11_MS-1");bot.object["critical"]=object();auto& critical=bot.object["critical"];critical.object["devices"]=array();for(const auto* name:{"engineHealth","ammoBayHealth","fuelTankHealth","radioHealth","leftTrackHealth","rightTrackHealth","gunHealth","turretRotatorHealth","surveyingDeviceHealth"}){auto device=object();device.object["name"]=text(name);device.object["hp"]=number(80.);device.object["max_hp"]=number(100.);device.object["state"]=text("normal");critical.object["devices"].array.push_back(std::move(device));}critical.object["crew_roster"]=array();critical.object["crew_roster"].array={text("commander"),text("driver"),text("gunner1"),text("loader1")};critical.object["crew_ko"]=array();critical.object["destroyed"]=array();critical.object["events"]=array();critical.object["fire"]=boolean(false);critical.object["ammo_rack_death"]=boolean(false);bot.object["critical_profile"]=object();bot.object["critical_profile"].object["crew_roster"]=critical.get("crew_roster");bot.object["critical_profile"].object["devices"]=array();for(const auto& device:rows(critical.get("devices"))){auto profile=object();profile.object["name"]=device.get("name");profile.object["max_hp"]=device.get("max_hp");bot.object["critical_profile"].object["devices"].array.push_back(std::move(profile));}bot.object["terminal_critical"]=critical;
 bot.object["equipment_states"]=array();for(int equipment=0;equipment<3;++equipment){auto row=object(),contract=object();contract.object["id"]=integer(equipment+1);contract.object["compactDescr"]=integer(equipment+1);contract.object["name"]=text("smallRepairkit");contract.object["kind"]=text("repairkit");contract.object["tags"]=array();contract.object["tags"].array={text("repairkit"),text("equipment")};contract.object["reuseCount"]=integer(-1);contract.object["cooldownSeconds"]=number(90.);for(const auto* field:{"autoactivate","repairAll"})contract.object[field]=boolean(false);for(const auto* field:{"fireStartingChanceFactor","bonusValue","crewLevelIncrease","enginePowerFactor","turretRotationSpeedFactor","engineHpLossPerSecond","autoReactionSeconds"})contract.object[field]=number(1.);row.object["equipment"]=std::move(contract);row.object["usesLeft"]=integer(-1);row.object["cooldownTimeLeft"]=number(0.);row.object["active"]=boolean(false);row.object["autoPendingElapsed"]=Value();row.object["aiPendingElapsed"]=Value();bot.object["equipment_states"].array.push_back(std::move(row));}
 bot.object["ram_motion"]=array();for(int contact=0;contact<contacts;++contact){auto row=array();row.array={integer((id+contact)%count+1),integer(contact+1),integer(1000000)};for(int component=0;component<18;++component)row.array.push_back(number(double(component)/10.));if(contact%2==0){auto metadata=object();metadata.object["hydraulic_body"]=array();metadata.object["hydraulic_body"].array={number(.1),number(.2),number(.3),number(.4),number(.5),number(.6),number(.7),number(.8)};row.array.push_back(std::move(metadata));}bot.object["ram_motion"].array.push_back(std::move(row));}bots.emplace(id,std::move(bot));
 }return bots;
}
}
struct InputsArchiveProbe {
 static void equal(const Inputs& left,const Inputs& right){require(left.archive_.size()==right.archive_.size(),"Archive size changed");auto a=left.archive_.begin(),b=right.archive_.begin();for(;a!=left.archive_.end();++a,++b){require(a->first==b->first&&a->second.stamp==b->second.stamp&&a->second.bots==b->second.bots,"Archived values changed");}}
 static void test(const Value& resources){
  State left_state,right_state;Inputs left(left_state),right(right_state);auto bots=fixture(resources,3,4);bots[90]=number(1.);bots[91]=object();
  for(std::int64_t revision=0;revision<620;++revision){bots[1].object["x"]=number(double(revision));left.rememberBots(revision,1000000+revision,actors(bots));right.rememberBots(revision,1000000+revision,bots);if(revision==0||revision==511||revision==512||revision==619)equal(left,right);}
  require(right.archive_.size()==512&&right.archive_.begin()->first==108,"512-revision frontier changed");require(right.archive_.rbegin()->second.bots.size()==3,"Invalid actor admission changed");const auto frozen=right.archive_.rbegin()->second.bots;bots[1].object["x"]=number(-1.);bots[1].object["ram_motion"]=array();left.rememberBots(619,0,actors(bots));right.rememberBots(619,0,bots);equal(left,right);require(right.archive_.rbegin()->second.stamp==1000619&&right.archive_.rbegin()->second.bots==frozen,"Duplicate revision changed its immutable sample");
  const std::set<std::string> fields={"id","x","y","z","yaw","pitch","roll","aim_yaw","gun_pitch","alive","team","vehicle","ram_motion"};for(const auto& bot:frozen){std::set<std::string> actual;for(const auto& field:bot.second.object)actual.insert(field.first);require(actual==fields,"Archive selector fields changed");require(rows(bot.second.get("ram_motion")).size()==4&&rows(rows(bot.second.get("ram_motion"))[0]).size()==22,"Ram motion or hydraulic metadata was dropped");}
  std::cout<<"Inputs archive equivalence passed: all 13 fields, complete ram_motion, invalid rows, immutable revisions, 512-frame pruning\n";
 }
#ifdef WOT_ROOM_ARCHIVE_BENCH
 static void benchmark(const Value& resources){
  const auto bots=fixture(resources,29,1);constexpr int frames=80;
  auto run=[&](bool direct){State state;Inputs input(state);for(int revision=0;revision<512;++revision)input.rememberBots(revision,revision,bots);allocation_count=0;allocation_bytes=0;measuring=true;const auto began=std::chrono::steady_clock::now();for(int frame=0;frame<frames;++frame){if(direct)input.rememberBots(512+frame,frame,bots);else input.rememberBots(512+frame,frame,actors(bots));}const auto seconds=std::chrono::duration<double>(std::chrono::steady_clock::now()-began).count();measuring=false;auto out=object();out.object["us_per_batch"]=number(seconds*1000000./frames);out.object["allocations_per_batch"]=number(double(allocation_count)/frames);out.object["allocated_bytes_per_batch"]=number(double(allocation_bytes)/frames);return out;};
  auto out=object();out.object["bots"]=integer(29);out.object["ram_rows_per_bot"]=integer(1);out.object["history_frames"]=integer(512);out.object["measured_batches"]=integer(frames);out.object["legacy"]=run(false);out.object["direct"]=run(true);std::cout<<writeJson(out)<<'\n';
 }
#endif
};
}
int main(int argc,char** argv){using namespace wot::room;try{if(argc<2)return 2;std::ifstream file(argv[1]);const auto resources=parseJson(std::string((std::istreambuf_iterator<char>(file)),{}));InputsArchiveProbe::test(resources);
#ifdef WOT_ROOM_ARCHIVE_BENCH
if(argc>2)InputsArchiveProbe::benchmark(resources);
#endif
return 0;}catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}}
