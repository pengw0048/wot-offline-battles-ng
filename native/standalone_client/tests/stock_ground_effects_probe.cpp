#include "stock_ground_effects_data.hpp"
#include <array>
#include <cassert>
#include <cstdlib>
#include <cstdio>
#include <deque>
#include <map>
#include <memory>
#include <sstream>
#include <string>
#include <vector>
namespace BW {using string=std::string;}
struct Vector2 {float x,y;Vector2(float a=0,float b=0):x(a),y(b){}};
struct Vector3 {
float x,y,z;Vector3(float a=0,float b=0,float c=0):x(a),y(b),z(c){}
Vector3 operator+(const Vector3& v)const{return {x+v.x,y+v.y,z+v.z};}
Vector3 operator-(const Vector3& v)const{return {x-v.x,y-v.y,z-v.z};}
float length()const{return sqrtf(x*x+y*y+z*z);}
};
void D3DXVec3Normalize(Vector3* out,const Vector3* v){const float n=v->length();*out={v->x/n,v->y/n,v->z/n};}
struct Section;using DataSectionPtr=std::shared_ptr<Section>;
struct Section {
std::map<std::string,DataSectionPtr> children;std::string value;
DataSectionPtr openSection(const char* key){auto it=children.find(key);return it==children.end()?nullptr:it->second;}
std::string asString()const{return value;}
std::string readString(const char* key){auto s=openSection(key);return s?s->value:"";}
float readFloat(const char* key,float fallback){auto s=openSection(key);return s?std::stof(s->value):fallback;}
int readInt(const char* key,int fallback){auto s=openSection(key);return s?std::stoi(s->value):fallback;}
bool readBool(const char* key,bool fallback){auto s=openSection(key);return s?s->value=="true":fallback;}
Vector2 readVector2(const char* key,Vector2 fallback){auto s=openSection(key);if(!s)return fallback;std::istringstream i(s->value);i>>fallback.x>>fallback.y;return fallback;}
};
DataSectionPtr section(){return std::make_shared<Section>();}
void set(const DataSectionPtr& p,const std::string& key,const std::string& value){auto c=section();c->value=value;p->children[key]=c;}
struct Source {Vector3 position;std::vector<std::string> events;unsigned stops=0;};
using StockAudioSource=std::shared_ptr<Source>;
std::vector<StockAudioSource> sources;
StockAudioSource createStockAudioSource(const std::string& name){assert(name.find("strike")!=0);auto p=std::make_shared<Source>();sources.push_back(p);return p;}
void stopStockAudioSource(const StockAudioSource& s){++s->stops;}
bool setStockAudioSourceOffset(const StockAudioSource& s,const Vector3& v){s->position=v;return true;}
bool playStockAudioSource(const StockAudioSource& s,const std::string& event){s->events.push_back(event);return true;}
#define ERROR_MSG(...) ((void)0)
struct IRendererPipeline{enum {TYPE_FORWARD_SHADING=1};};
struct Renderer {int type=0;static Renderer& instance(){static Renderer r;return r;}int pipelineType()const{return type;}};
bool sniper=false;bool stockBinocularsEnabled(){return sniper;}
bool decalReady=true,tpsReady=true;unsigned decals=0;std::vector<std::string> particles;
float stockDynamicDecalGroupLifetime(const std::string& name){assert(name=="fast");return 2.f;}
bool addStockDynamicDecal(float lifetime,const Vector3& start,const Vector3& end,const Vector2& size,
 float yaw,const std::string& diffuse,const std::string& normal,const std::string& specular,bool simple,std::string& error){
assert(lifetime==2.f&&start.y>end.y&&size.x>0&&size.y>0&&yaw>=0&&simple);
assert(diffuse=="maps/fx/decal_ground_1_AM.dds"&&normal=="maps/fx/decal_ground_1_NM.dds"&&specular=="maps/fx/decal_ground_1_GMM.dds");
++decals;if(!decalReady)error="decal failed";return decalReady;
}
bool playStockTPSOneShot(const std::string& name,const Vector3&,std::string& error){if(!tpsReady){error="tPS failed";return false;}particles.push_back(name);return true;}
namespace Terrain {struct BaseTerrainBlock {static constexpr float NO_TERRAIN=-1000000.f;};}
constexpr float Terrain::BaseTerrainBlock::NO_TERRAIN;
struct ClientSpace {bool dead=false;float height=Terrain::BaseTerrainBlock::NO_TERRAIN;bool cleared()const{return dead;}float findTerrainHeight(const Vector3&){return height;}};
using ClientSpacePtr=std::shared_ptr<ClientSpace>;ClientSpacePtr currentSpace;
namespace DeprecatedSpaceHelpers {ClientSpacePtr cameraSpace(){return currentSpace;}}
size_t surface=0;
Vector3 groundPoint(ClientSpace&,const Vector3& p,size_t& ground){ground=surface;return p;}
using GroundMath=wot::StockGroundEffectMath;
Vector3 vector3(const GroundMath::Vector& v){return {v[0],v[1],v[2]};}
GroundMath::Vector vector3(const Vector3& v){return {{v.x,v.y,v.z}};}
const char* groundNames[]={"ground","stone","wood","metal","snow","sand","water"};
const char* effectNames[]={"effect_explosion","effect_explosion_add","effect_explosion_snp","effect_explosion_fwd","effect_smoke"};
@PRODUCTION@
DataSectionPtr definition(){
auto p=section();
for(const char* ground:groundNames){auto s=section();p->children[ground]=s;for(const char* effect:effectNames)set(s,effect,std::string(ground)+"/"+effect);}
auto d=section();p->children["decal"]=d;set(d,"size","5 5");set(d,"groupName","fast");set(d,"variation",".25");set(d,"randomYaw","true");
set(d,"diffuseName","maps/fx/decal_ground_1_AM.dds");set(d,"bumpName","maps/fx/decal_ground_1_NM.dds");set(d,"smTexName","maps/fx/decal_ground_1_GMM.dds");
for(const char* name:{"wwexplosion_close","wwexplosion_far","wwtracer"}){auto s=section();p->children[name]=s;set(s,"main",std::string(name)+"/main");set(s,"keyoff",std::string(name)+"/keyoff");}
return p;
}
int main(){
std::string error;
auto def=definition();
Artillery artillery;assert(artillery.load(def,error));assert(artillery.settings.optimisation==8&&artillery.settings.smokeVariation==.5f);
auto limits=section();def->children["optimization"]=limits;set(limits,"default","10");set(limits,"sniper","20");set(def,"smoke_variation",".1");set(def,"add_variation",".4");
assert(artillery.load(def,error));assert(artillery.settings.optimisation==10&&artillery.settings.sniperOptimisation==20&&artillery.settings.smokeVariation==.1f);
for(size_t g=0;g<7;++g)for(size_t e=0;e<5;++e)assert(artillery.settings.effects[g][e]==std::string(groundNames[g])+"/"+effectNames[e]);
Settings invalid;auto broken=definition();broken->children.erase("water");assert(!invalid.load(broken,true,error));broken=definition();broken->children.erase("decal");assert(!invalid.load(broken,false,error));
Sound simple("Artillery");auto simpleDef=section();simpleDef->value="event";simple.load(simpleDef);assert(simple.main=="event"&&simple.keyoff.empty());simple.play({1,2,3});assert(simple.source->events.back()=="event");simple.play({4,5,6});assert(simple.source->stops==1&&simple.source->position.y==5);simple.stop();assert(simple.source->stops==2);
currentSpace=std::make_shared<ClientSpace>();currentSpace->height=6.f;
def=definition();set(def,"optimization","2");set(def,"add_variation",".2");set(def,"step_offset",".6");
Strike strike;assert(strike.load(def,error));assert(strike.launch({1,2,3},{0,0,10},4,8,2,3,error));assert(strike.impacts.size()==7&&strike.closeSound.source&&strike.farSound.source);for(const auto& i:strike.impacts)assert(i.position[1]==6.f);
// The original consumer processes against OLD elapsed, then adds delta.
strike.impacts={{{{1,2,3}},0.f,0},{{{4,5,6}},.5f,1},{{{7,8,9}},.25f,2}};strike.cursor=0;
assert(strike.tick(1.f,error));assert(strike.cursor==1&&decals==0&&particles.empty());
assert(strike.tick(0.f,error));assert(!strike.alive()&&decals==1&&particles.size()==2);assert(particles[0]=="ground/effect_explosion_add"&&particles[1]=="ground/effect_smoke");assert(strike.closeSound.source->events.back()=="wwexplosion_close/keyoff");
strike.impacts={{{{1,2,3}},0.f,0}};strike.cursor=0;strike.elapsed=0;strike.counter=1;sniper=true;
assert(strike.tick(0,error));assert(particles.back()=="ground/effect_explosion_snp");sniper=false;
Renderer::instance().type=1;strike.impacts={{{{1,2,3}},0.f,0}};strike.cursor=0;strike.counter=1;unsigned count=decals;
assert(strike.tick(0,error));assert(decals==count&&particles.back()=="ground/effect_explosion_fwd");Renderer::instance().type=0;
strike.impacts={{{{1,2,3}},0.f,0}};strike.cursor=0;strike.counter=1;decalReady=false;
count=static_cast<unsigned>(particles.size());assert(!strike.tick(0,error)&&error=="decal failed"&&!strike.alive()&&particles.size()==count);decalReady=true;
strike.impacts={{{{1,2,3}},0.f,2}};strike.cursor=0;tpsReady=false;assert(!strike.tick(0,error)&&error=="tPS failed"&&!strike.alive());tpsReady=true;
strike.impacts={{{{1,2,3}},0.f,2}};strike.cursor=0;currentSpace=std::make_shared<ClientSpace>();count=static_cast<unsigned>(particles.size());assert(strike.tick(0,error)&&!strike.alive()&&particles.size()==count);
Manager owner;owner.artillery.emplace_back(new Artillery);owner.airstrikes.push_back(def);auto active=std::unique_ptr<Strike>(new Strike);assert(active->load(def,error));assert(active->launch({0,0,0},{0,0,1},1,1,1,1,error));auto source=active->closeSound.source;owner.active.push_back(std::move(active));count=static_cast<unsigned>(particles.size());owner.stopAll();owner.stopAll();assert(owner.active.empty()&&owner.airstrikes.size()==1&&owner.artillery.size()==1&&source->stops==1&&particles.size()==count);
currentSpace.reset();Strike absent;assert(absent.load(def,error)&&!absent.launch({0,0,0},{0,0,1},1,1,1,1,error));
for(auto args:{std::array<int,2>{{0,1}},std::array<int,2>{{1,-1}}}){bool threw=false;try{GroundMath::airstrike({{0,0,0}},{{0,0,1}},1,1,1,args[0],args[1],.2f,.6f,[]{return 0;});}catch(const std::invalid_argument&){threw=true;}assert(threw);}
}
