#include "stock_native_client_drive_peers_python.hpp"
#include "stock_native_client_drive_peers.hpp"
#include "stock_native_client_drive_install.hpp"
#include "stock_native_client_query.hpp"
#include "stock_native_bot_motion_snapshot.hpp"
#include "stock_native_bot_descriptor.hpp"
#include "stock_native_bot_python.hpp"
#include <algorithm>
#include <cmath>
#include <map>
#include <set>
#include <stdexcept>
namespace stock_native_client_drive {
namespace {
namespace cs=stock_native_client_state;namespace p=stock_native_bot_python;
using Value=cs::Value;
const std::array<const char*,4>tiers{"rookie","regular","veteran","elite"};
const std::array<double,4>anchors{0.,.34,.67,1.};
std::optional<double>tier(const std::string&name){for(unsigned i=0;i<tiers.size();++i)if(name==tiers[i])return anchors[i];return {};}
int trained(double rating){double level=100.;const double levels[]={75.,90.,100.,100.};for(unsigned i=0;i<3;++i)if(rating<=anchors[i+1]){const double fraction=(rating-anchors[i])/(anchors[i+1]-anchors[i]);level=fraction<=0.?levels[i]:fraction>=1.?levels[i+1]:levels[i]+(levels[i+1]-levels[i])*fraction;break;}int out=75;for(int n:{90,100})if(std::abs(n-level)<std::abs(out-level))out=n;return out;}
struct Context {
 Value tactics;
 std::map<std::pair<int,int>,std::string>pins;
 std::string mode="mixed",round;
};
Context context(PyObject*battle){Context out;auto bots=p::value(battle,"_bots");if(bots.get()==Py_None)throw p::Error("Native peer Bot context is unavailable");auto tactics=p::value(bots.get(),"_bot_tactics"),mode=p::value(bots.get(),"_bot_skill_mode"),pins=p::value(bots.get(),"_bot_skill_pins"),round=p::value(bots.get(),"round_id");out.tactics=cs::read(tactics.get());out.mode=p::string(mode.get());if(out.mode!="easy"&&out.mode!="relaxed"&&out.mode!="mixed"&&out.mode!="hard"&&out.mode!="brutal")out.mode="mixed";p::Ref round_text(PyObject_Str(round.get()));out.round=p::string(round_text.get());if(PyDict_Check(pins.get())){Py_ssize_t index=0;PyObject*key,*value;while(PyDict_Next(pins.get(),&index,&key,&value)){const auto pair=p::numbers<2>(key);out.pins[{int(pair[0]),int(pair[1])}]=p::string(value);}}return out;}
double draw(const Context&context,int team,int slot){
 const auto payload="bot-skill-rating-v1|"+context.round+"|"+context.mode+"|"+std::to_string(team)+"|"+std::to_string(slot);
 p::Ref hashlib(PyImport_ImportModule("hashlib"));auto producer=p::value(hashlib.get(),"sha1");if(!PyCFunction_Check(producer.get()))throw p::Error("Native peer seed producer is not the reviewed builtin");auto bytes=p::text(payload);p::Ref digest(PyObject_CallFunctionObjArgs(producer.get(),bytes.get(),nullptr));auto hex=p::value(digest.get(),"hexdigest");if(!PyCFunction_Check(hex.get()))throw p::Error("Native peer seed digest is not the reviewed builtin");p::Ref empty(PyTuple_New(0)),text(PyObject_CallObject(hex.get(),empty.get()));const auto seed=std::stoul(p::string(text.get()).substr(0,8),nullptr,16)&0x7ffffffful;
 p::Ref module(PyImport_ImportModule("_random"));auto type=p::value(module.get(),"Random");if(!PyType_Check(type.get()))throw p::Error("Native peer random producer is not a native type");p::Ref random(PyObject_CallFunction(type.get(),const_cast<char*>("k"),seed));auto method=p::value(random.get(),"random");if(!PyCFunction_Check(method.get()))throw p::Error("Native peer random draw is not a native method");p::Ref raw(PyObject_CallObject(method.get(),empty.get()));const double roll=p::scalar(raw.get());
 const std::map<std::string,std::array<double,5>>points{{"easy",{0.,.03,.08,.14,.30}},{"relaxed",{.05,.16,.28,.40,.67}},{"mixed",{.12,.42,.62,.80,1.}},{"hard",{.52,.70,.82,.92,1.}},{"brutal",{1.,1.,1.,1.,1.}}};const auto&values=points.at(context.mode);const double scaled=std::clamp(roll,0.,1.)*4.;const unsigned index=unsigned(scaled);if(index>=4)return values.back();return values[index]+(values[index+1]-values[index])*(scaled-index);
}
int crewLevel(const Context&context,const Value&state,PyObject*descriptor){const int team=int(state.get("team").numeric(1.)),slot=int(state.get("slot").numeric());std::string tag=state.get("profile").get("class_tag").resolved().text;if(tag.empty()){auto type=p::value(descriptor,"type"),tags=p::value(type.get(),"tags");tag="unknown";for(const char*name:{"lightTank","mediumTank","heavyTank","AT-SPG","SPG"}){auto value=p::text(name);const int found=PySequence_Contains(tags.get(),value.get());if(found<0)throw p::Error("Native peer class tags are unavailable");if(found){tag=name;break;}}}
 double rating=.34;const auto&fields=state.resolved().object;if(fields.count("skill_rating"))rating=std::clamp(state.get("skill_rating").numeric(.34),0.,1.);else if(const auto published=tier(state.get("skill").resolved().text))rating=*published;else{const auto pinned=context.pins.find({team,slot});const auto value=pinned==context.pins.end()?std::optional<double>{}:tier(pinned->second);rating=value?*value:draw(context,team,slot);}
 int result=trained(rating);for(const auto&rule:context.tactics.get("behavior").resolved().array){const int selector_team=int(rule.get("team").numeric()),selector_slot=int(rule.get("slot").numeric(-1.));const auto&selector_tag=rule.get("class_tag").resolved().text;if((selector_team==0||selector_team==team)&&(selector_slot==-1||selector_slot==slot)&&(selector_tag=="all"||selector_tag==tag)){const auto&level=rule.get("values").get("crew_level").resolved();if(level.kind!=Value::Kind::Null)result=int(level.numeric());}}return result;}
p::Ref botPhysics(PyObject*descriptor,int level){p::Ref params(stock_native_bot::readUnmodifiedDrive(descriptor)),factors(stock_native_bot_descriptor::defaultCrewFactors(descriptor,level));
 std::array<double,3>crew{1.,1.,1.};try{auto values=p::value(factors.get(),"chassis/terrainResistance");if(values.get()!=Py_None)for(unsigned i=0;i<3;++i)crew[i]=p::scalar(p::item(values.get(),i).get());}catch(const std::exception&){PyErr_Clear();crew={1.,1.,1.};}
 const auto factor=[&](const char*name){try{return std::max(0.,p::numeric(factors.get(),name,1.));}catch(const std::exception&){PyErr_Clear();return 1.;}};
 auto terrain=p::value(params.get(),"terrainResist");auto values=p::numbers<3>(terrain.get());for(unsigned i=0;i<3;++i)values[i]*=crew[i];p::set(params.get(),"terrainResist",p::tupleNumbers(values));p::set(params.get(),"rotSpd",p::number(p::numeric(params.get(),"rotSpd")*factor("vehicle/rotationSpeed")));p::set(params.get(),"powerW",p::number(p::numeric(params.get(),"powerW")*factor("engine/power")));auto chassis=p::value(descriptor,"chassis");p::set(params.get(),"rotationIsAroundCenter",p::boolean(p::truth(chassis.get(),"rotationIsAroundCenter",true)));return params;}
Value signature(const cs::Actor&actor){Value out;out.kind=Value::Kind::Object;if(actor.kind=="player"){out.object["physics"]=actor.state.get("effective_params").get("physics");}else{for(const char*key:{"skill_rating","skill","team","slot"})out.object[key]=actor.state.get(key);out.object["class_tag"]=actor.state.get("profile").get("class_tag");}return out;}
}
struct PeerInstaller::Impl {
 struct Entry {std::shared_ptr<cs::Actor>actor;std::uint64_t descriptor_revision=0;PyObject*descriptor=nullptr;Value input;bool installed=false;std::string failure;};
 std::map<std::string,Entry>entries;std::uint64_t epoch=0;std::optional<Context>context;
};
PeerInstaller::PeerInstaller():impl_(new Impl){}
PeerInstaller::~PeerInstaller()=default;
void PeerInstaller::synchronize(PyObject*battle,cs::State&state,NativePeers&peers){if(state.retired){impl_->entries.clear();peers.forget({});return;}if(impl_->epoch!=state.epoch){impl_->entries.clear();impl_->context.reset();impl_->epoch=state.epoch;peers.forget({});}std::set<std::string>live;const std::vector<std::pair<std::string,std::shared_ptr<cs::Actor>>>actors(state.actors.begin(),state.actors.end());
 for(const auto&row:actors){const auto&actor=row.second;if(!actor||actor->local||!actor->ready||actor->tombstone||actor->missing)continue;auto*descriptor=stock_native_client_query::descriptor(*actor);if(!descriptor||descriptor==Py_None||!stock_native_client_query::geometry(*actor))continue;auto input=signature(*actor);auto&entry=impl_->entries[row.first];const bool changed=entry.actor!=actor||entry.descriptor_revision!=actor->descriptor_revision||entry.descriptor!=descriptor||entry.input!=input;if(!changed){if(entry.installed)live.insert(row.first);continue;}
  p::Ref retained_descriptor(p::retain(descriptor));const bool retain=entry.installed&&entry.actor==actor&&entry.descriptor_revision==actor->descriptor_revision&&entry.descriptor==descriptor;entry.actor=actor;entry.descriptor_revision=actor->descriptor_revision;entry.descriptor=descriptor;entry.input=std::move(input);entry.installed=false;
  try{auto params=[&](){if(actor->kind=="player"){const auto&physics=actor->state.get("effective_params").get("physics").resolved();if(physics.kind!=Value::Kind::Object)throw p::Error("Native human peer effective physics are unavailable");return p::Ref(cs::write(physics));}if(actor->kind!="bot")throw p::Error("Native peer actor kind is invalid");if(!impl_->context)impl_->context=context(battle);return botPhysics(descriptor,crewLevel(*impl_->context,actor->state,descriptor));}();auto entity=p::dict();p::set(entity.get(),"typeDescriptor",p::retain(descriptor));auto installed=readPlayerDescriptor(battle,entity.get(),params.get());installed.team=int(actor->state.get("team").numeric());if(state.retired||state.epoch!=impl_->epoch||state.actors.find(row.first)==state.actors.end()||state.actors.at(row.first)!=actor||actor->tombstone||actor->descriptor_revision!=entry.descriptor_revision)continue;peers.install(actor,std::move(installed));entry.installed=true;entry.failure.clear();live.insert(row.first);
  }catch(const std::exception&error){PyErr_Clear();
   const auto current=state.actors.find(row.first);const bool owned=!state.retired&&state.epoch==impl_->epoch&&current!=state.actors.end()&&current->second==actor&&!actor->tombstone&&actor->descriptor_revision==entry.descriptor_revision;if(!owned)continue;
   if(retain){entry.installed=true;live.insert(row.first);}
   else try{PlayerDescriptor fallback;fallback.motion.collision_shape=stock_native_bot_descriptor::chassisShape(descriptor);auto physics=p::value(descriptor,"physics");fallback.motion.drive.mass=p::numeric(physics.get(),"weight");if(!std::isfinite(fallback.motion.drive.mass)||fallback.motion.drive.mass<=0.)throw p::Error("Native peer mounted mass is unavailable");fallback.team=int(actor->state.get("team").numeric());peers.installGeometry(actor,std::move(fallback));entry.installed=true;live.insert(row.first);}catch(const std::exception&){PyErr_Clear();}
   const std::string detail=error.what();if(detail!=entry.failure){entry.failure=detail;PySys_WriteStdout("[Offline LAN 0.9.22] Native contact peer unavailable: %s (%s)\n",row.first.c_str(),detail.c_str());if(PyErr_Occurred())PyErr_Clear();}}
 }
 if(state.retired||state.epoch!=impl_->epoch){peers.forget({});impl_->entries.clear();return;}peers.forget(live);for(auto at=impl_->entries.begin();at!=impl_->entries.end();)if(!state.actors.count(at->first))at=impl_->entries.erase(at);else ++at;
}
}
