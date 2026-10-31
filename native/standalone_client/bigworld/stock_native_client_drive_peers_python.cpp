#include "stock_native_client_drive_peers_python.hpp"
#include "stock_native_client_drive_peers.hpp"
#include "stock_native_client_drive_install.hpp"
#include "stock_native_client_query.hpp"
#include "stock_native_bot_descriptor.hpp"
#include "stock_native_bot_python.hpp"
#include "stock_native_battle_local.hpp"
namespace native_battle {namespace bots {
// Provided by the Bot subsystem: {"tactics","mode","pins":[[team,slot,tier]],"round"} of the round's Bot skills.
stock_native_client_state::Value peerSkillContext(const Battle&);
}}
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
// The Bot skill context the visible client's Bot runtime owns: tactics, skill mode, pinned tiers and round.
Context context(const native_battle::Battle&b){const Value raw=native_battle::bots::peerSkillContext(b);Context out;out.tactics=raw.get("tactics");out.mode=raw.get("mode").resolved().text;if(out.mode!="easy"&&out.mode!="relaxed"&&out.mode!="mixed"&&out.mode!="hard"&&out.mode!="brutal")out.mode="mixed";out.round=raw.get("round").resolved().text;
 for(const auto&pin:raw.get("pins").resolved().array){const auto&row=pin.resolved().array;if(row.size()==3)out.pins[{int(row[0].numeric()),int(row[1].numeric())}]=row[2].resolved().text;}return out;}
// The first 32 bits of SHA-1(text), as int(hashlib.sha1(text).hexdigest()[:8], 16).
std::uint32_t sha1Prefix(const std::string&text){
 std::uint32_t h[5]={0x67452301u,0xefcdab89u,0x98badcfeu,0x10325476u,0xc3d2e1f0u};
 std::vector<unsigned char>m(text.begin(),text.end());const std::uint64_t bits=std::uint64_t(text.size())*8u;m.push_back(0x80);while(m.size()%64!=56)m.push_back(0);for(int i=7;i>=0;--i)m.push_back(static_cast<unsigned char>(bits>>(8*i)));
 const auto rotate=[](std::uint32_t v,int n){return (v<<n)|(v>>(32-n));};
 for(std::size_t chunk=0;chunk<m.size();chunk+=64){std::uint32_t w[80];for(int i=0;i<16;++i)w[i]=std::uint32_t(m[chunk+4*i])<<24|std::uint32_t(m[chunk+4*i+1])<<16|std::uint32_t(m[chunk+4*i+2])<<8|std::uint32_t(m[chunk+4*i+3]);for(int i=16;i<80;++i)w[i]=rotate(w[i-3]^w[i-8]^w[i-14]^w[i-16],1);
  std::uint32_t a=h[0],b=h[1],c=h[2],d=h[3],e=h[4];
  for(int i=0;i<80;++i){const std::uint32_t f=i<20?((b&c)|(~b&d)):i<40?(b^c^d):i<60?((b&c)|(b&d)|(c&d)):(b^c^d),k=i<20?0x5a827999u:i<40?0x6ed9eba1u:i<60?0x8f1bbcdcu:0xca62c1d6u;const std::uint32_t t=rotate(a,5)+f+e+k+w[i];e=d;d=c;c=rotate(b,30);b=a;a=t;}
  h[0]+=a;h[1]+=b;h[2]+=c;h[3]+=d;h[4]+=e;}
 return h[0];
}
// CPython 2.7 _random.Random(seed).random() for a seed below 2**32: init_by_array over one key word, then genrand_res53.
double pythonRandom(std::uint32_t seed){
 constexpr int n=624,m=397;std::uint32_t mt[n];mt[0]=19650218u;for(int i=1;i<n;++i)mt[i]=1812433253u*(mt[i-1]^(mt[i-1]>>30))+std::uint32_t(i);
 int i=1;for(int k=n;k;--k){mt[i]=(mt[i]^((mt[i-1]^(mt[i-1]>>30))*1664525u))+seed;if(++i>=n){mt[0]=mt[n-1];i=1;}}
 for(int k=n-1;k;--k){mt[i]=(mt[i]^((mt[i-1]^(mt[i-1]>>30))*1566083941u))-std::uint32_t(i);if(++i>=n){mt[0]=mt[n-1];i=1;}}
 mt[0]=0x80000000u;
 // The first two outputs need only the first regeneration of the state.
 const auto twist=[&](int k){const std::uint32_t y=(mt[k]&0x80000000u)|(mt[(k+1)%n]&0x7fffffffu);return mt[(k+m)%n]^(y>>1)^((y&1u)?0x9908b0dfu:0u);};
 const auto temper=[](std::uint32_t y){y^=y>>11;y^=(y<<7)&0x9d2c5680u;y^=(y<<15)&0xefc60000u;return y^(y>>18);};
 const std::uint32_t a=temper(twist(0))>>5,b=temper(twist(1))>>6;
 return (a*67108864.0+b)*(1.0/9007199254740992.0);
}
double draw(const Context&context,int team,int slot){
 const auto payload="bot-skill-rating-v1|"+context.round+"|"+context.mode+"|"+std::to_string(team)+"|"+std::to_string(slot);
 const auto seed=sha1Prefix(payload)&0x7fffffffu;const double roll=pythonRandom(seed);
 const std::map<std::string,std::array<double,5>>points{{"easy",{0.,.03,.08,.14,.30}},{"relaxed",{.05,.16,.28,.40,.67}},{"mixed",{.12,.42,.62,.80,1.}},{"hard",{.52,.70,.82,.92,1.}},{"brutal",{1.,1.,1.,1.,1.}}};const auto&values=points.at(context.mode);const double scaled=std::clamp(roll,0.,1.)*4.;const unsigned index=unsigned(scaled);if(index>=4)return values.back();return values[index]+(values[index+1]-values[index])*(scaled-index);
}
int crewLevel(const Context&context,const Value&state,PyObject*descriptor){const int team=int(state.get("team").numeric(1.)),slot=int(state.get("slot").numeric());std::string tag=state.get("profile").get("class_tag").resolved().text;if(tag.empty()){auto type=p::value(descriptor,"type"),tags=p::value(type.get(),"tags");tag="unknown";for(const char*name:{"lightTank","mediumTank","heavyTank","AT-SPG","SPG"}){auto value=p::text(name);const int found=PySequence_Contains(tags.get(),value.get());if(found<0)throw p::Error("Native peer class tags are unavailable");if(found){tag=name;break;}}}
 double rating=.34;const auto&fields=state.resolved().object;if(fields.count("skill_rating"))rating=std::clamp(state.get("skill_rating").numeric(.34),0.,1.);else if(const auto published=tier(state.get("skill").resolved().text))rating=*published;else{const auto pinned=context.pins.find({team,slot});const auto value=pinned==context.pins.end()?std::optional<double>{}:tier(pinned->second);rating=value?*value:draw(context,team,slot);}
 int result=trained(rating);for(const auto&rule:context.tactics.get("behavior").resolved().array){const int selector_team=int(rule.get("team").numeric()),selector_slot=int(rule.get("slot").numeric(-1.));const auto&selector_tag=rule.get("class_tag").resolved().text;if((selector_team==0||selector_team==team)&&(selector_slot==-1||selector_slot==slot)&&(selector_tag=="all"||selector_tag==tag)){const auto&level=rule.get("values").get("crew_level").resolved();if(level.kind!=Value::Kind::Null)result=int(level.numeric());}}return result;}
// The Bot's drive parameters: derive_params under its trained crew's default attribute factors.
native_battle::local::DriveParams botPhysics(PyObject*descriptor,int level){p::Ref factors(stock_native_bot_descriptor::defaultCrewFactors(descriptor,level));return native_battle::local::deriveParams(descriptor,factors.get());}
Value signature(const cs::Actor&actor){Value out;out.kind=Value::Kind::Object;if(actor.kind=="player"){out.object["physics"]=actor.state.get("effective_params").get("physics");}else{for(const char*key:{"skill_rating","skill","team","slot"})out.object[key]=actor.state.get(key);out.object["class_tag"]=actor.state.get("profile").get("class_tag");}return out;}
}
struct PeerInstaller::Impl {
 struct Entry {std::shared_ptr<cs::Actor>actor;std::uint64_t descriptor_revision=0;PyObject*descriptor=nullptr;Value input;bool installed=false;std::string failure;};
 std::map<std::string,Entry>entries;std::uint64_t epoch=0;std::optional<Context>context;
};
PeerInstaller::PeerInstaller():impl_(new Impl){}
PeerInstaller::~PeerInstaller()=default;
void PeerInstaller::synchronize(const native_battle::Battle&battle,cs::State&state,NativePeers&peers){if(state.retired){impl_->entries.clear();peers.forget({});return;}if(impl_->epoch!=state.epoch){impl_->entries.clear();impl_->context.reset();impl_->epoch=state.epoch;peers.forget({});}std::set<std::string>live;const std::vector<std::pair<std::string,std::shared_ptr<cs::Actor>>>actors(state.actors.begin(),state.actors.end());
 for(const auto&row:actors){const auto&actor=row.second;if(!actor||actor->local||!actor->ready||actor->tombstone||actor->missing)continue;auto*descriptor=stock_native_client_query::descriptor(*actor);if(!descriptor||descriptor==Py_None||!stock_native_client_query::geometry(*actor))continue;auto input=signature(*actor);auto&entry=impl_->entries[row.first];const bool changed=entry.actor!=actor||entry.descriptor_revision!=actor->descriptor_revision||entry.descriptor!=descriptor||entry.input!=input;if(!changed){if(entry.installed)live.insert(row.first);continue;}
  p::Ref retained_descriptor(p::retain(descriptor));const bool retain=entry.installed&&entry.actor==actor&&entry.descriptor_revision==actor->descriptor_revision&&entry.descriptor==descriptor;entry.actor=actor;entry.descriptor_revision=actor->descriptor_revision;entry.descriptor=descriptor;entry.input=std::move(input);entry.installed=false;
  try{auto params=[&](){if(actor->kind=="player"){const auto&physics=actor->state.get("effective_params").get("physics").resolved();if(physics.kind!=Value::Kind::Object)throw p::Error("Native human peer effective physics are unavailable");return native_battle::local::physicsFromValue(physics);}if(actor->kind!="bot")throw p::Error("Native peer actor kind is invalid");if(!impl_->context)impl_->context=context(battle);return botPhysics(descriptor,crewLevel(*impl_->context,actor->state,descriptor));}();auto installed=readPlayerDescriptor(battle,descriptor,params);installed.team=int(actor->state.get("team").numeric());if(state.retired||state.epoch!=impl_->epoch||state.actors.find(row.first)==state.actors.end()||state.actors.at(row.first)!=actor||actor->tombstone||actor->descriptor_revision!=entry.descriptor_revision)continue;peers.install(actor,std::move(installed));entry.installed=true;entry.failure.clear();live.insert(row.first);
  }catch(const std::exception&error){PyErr_Clear();
   const auto current=state.actors.find(row.first);const bool owned=!state.retired&&state.epoch==impl_->epoch&&current!=state.actors.end()&&current->second==actor&&!actor->tombstone&&actor->descriptor_revision==entry.descriptor_revision;if(!owned)continue;
   if(retain){entry.installed=true;live.insert(row.first);}
   else try{PlayerDescriptor fallback;fallback.motion.collision_shape=stock_native_bot_descriptor::chassisShape(descriptor);auto physics=p::value(descriptor,"physics");fallback.motion.drive.mass=p::numeric(physics.get(),"weight");if(!std::isfinite(fallback.motion.drive.mass)||fallback.motion.drive.mass<=0.)throw p::Error("Native peer mounted mass is unavailable");fallback.team=int(actor->state.get("team").numeric());peers.installGeometry(actor,std::move(fallback));entry.installed=true;live.insert(row.first);}catch(const std::exception&){PyErr_Clear();}
   const std::string detail=error.what();if(detail!=entry.failure){entry.failure=detail;PySys_WriteStdout("[Offline LAN 0.9.22] Native contact peer unavailable: %s (%s)\n",row.first.c_str(),detail.c_str());if(PyErr_Occurred())PyErr_Clear();}}
 }
 if(state.retired||state.epoch!=impl_->epoch){peers.forget({});impl_->entries.clear();return;}peers.forget(live);for(auto at=impl_->entries.begin();at!=impl_->entries.end();)if(!state.actors.count(at->first))at=impl_->entries.erase(at);else ++at;
}
}
