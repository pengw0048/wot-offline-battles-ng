// SDK boundary doubles for the actual compound resource init/close functions.
#ifdef NDEBUG
#undef NDEBUG
#endif
#include <algorithm>
#include <cassert>
#include <cstdint>
#include <cstring>
#include <map>
#include <memory>
#include <string>
#include <vector>
#undef NULL
#define NULL nullptr
#define SUCCEEDED(value) ((value)>=0)
#define ERROR_MSG(...) (++errors)
using DWORD=unsigned;
using D3DXHANDLE=const char*;
const DWORD D3DVERTEXTEXTURESAMPLER0=257;
unsigned errors=0,registrations=0,unregistrations=0,textureLoads=0,effectLoads=0;
int failedEffect=-1;
bool failedTexture=false;
std::string configuredTexture="system/maps/stipple.dds";
namespace BW {using string=std::string;using StringRef=std::string;}
template<class T> struct Ptr:std::shared_ptr<T> {
 using std::shared_ptr<T>::shared_ptr;using std::shared_ptr<T>::operator=;
 Ptr(){};Ptr(T*value):std::shared_ptr<T>(value){}
 Ptr& operator=(T*value){this->reset(value);return *this;}
 Ptr& operator=(std::nullptr_t){this->reset();return *this;}
};
struct IDirect3DBaseTexture9{};
struct ID3DXEffect;
std::vector<ID3DXEffect*> effects;
struct IDirect3DDevice9 {
 std::map<unsigned,IDirect3DBaseTexture9*> samplers;
 int GetTexture(unsigned slot,IDirect3DBaseTexture9**value){*value=samplers[slot];return 0;}
 int SetTexture(unsigned slot,IDirect3DBaseTexture9*value){samplers[slot]=value;return 0;}
} testDevice;
template<class T> struct ComObjectWrap {
 T* value=nullptr;T** operator&(){return &value;}T* pComObject(){return value;}
};
struct ID3DXEffect {
 IDirect3DBaseTexture9* texture=nullptr;
 ID3DXEffect(){effects.push_back(this);}
 ~ID3DXEffect(){effects.erase(std::find(effects.begin(),effects.end(),this));}
 D3DXHANDLE GetParameterBySemantic(void*,const char*semantic){assert(!std::strcmp(semantic,"StippleMap"));return "map";}
 int SetTexture(D3DXHANDLE handle,IDirect3DBaseTexture9*value){assert(handle);texture=value;return 0;}
};
namespace wot {bool is_stock_effect(ID3DXEffect*){return false;}}
namespace Moo {
enum ERenderingPassType {RENDERING_PASS_COLOR,RENDERING_PASS_REFLECTION,RENDERING_PASS_SHADOWS,RENDERING_PASS_DEPTH,RENDERING_PASS_COUNT};
struct BaseTexture {
 IDirect3DBaseTexture9 data;
 IDirect3DBaseTexture9* pTexture(){return &data;}
 ~BaseTexture(){for(const auto&sampler:testDevice.samplers)assert(sampler.second!=&data);
  for(auto*effect:effects)assert(effect->texture!=&data);}
};
using BaseTexturePtr=Ptr<BaseTexture>;
struct EffectConstantValue {virtual ~EffectConstantValue(){};virtual bool operator()(ID3DXEffect*,D3DXHANDLE)=0;};
using EffectConstantValuePtr=Ptr<EffectConstantValue>;
struct EffectVisualContext {
 enum {AUTO_CONSTANT_TYPE_PER_OBJECT};
 EffectConstantValuePtr mapping,registered;
 void registerAutoConstant(int,const BW::string&name,const EffectConstantValuePtr&value){
  assert(name=="StippleMap"&&value);registered=value;++registrations;
 }
 void unregisterAutoConstant(int,const BW::string&name){assert(name=="StippleMap");registered=nullptr;++unregistrations;}
 EffectConstantValuePtr* getMapping(const BW::StringRef&name,bool=true){assert(name=="StippleMap");return &mapping;}
};
EffectVisualContext constants;
struct Context {
 unsigned invalidations=0;
 EffectVisualContext& effectVisualContext(){return constants;}
 IDirect3DDevice9* device(){return &::testDevice;}
 void invalidateStateCache(){++invalidations;}
};
Context& rc(){static Context value;return value;}
struct ManagedEffect {ID3DXEffect data;ID3DXEffect* pEffect(){return &data;}};
struct Properties {ID3DXEffect binding;ID3DXEffect* existingStockBinding(){return &binding;}};
struct EffectMaterial {
 std::shared_ptr<ManagedEffect> managed=std::make_shared<ManagedEffect>();Properties values;
 std::shared_ptr<ManagedEffect> pEffect(){return managed;}
 Properties& properties(){return values;}
};
using EffectMaterialPtr=Ptr<EffectMaterial>;
struct ComplexEffectMaterial {
 EffectMaterialPtr base{new EffectMaterial};EffectMaterialPtr passes[RENDERING_PASS_COUNT][2];
 ComplexEffectMaterial(){for(auto&pass:passes)for(auto&value:pass)value=new EffectMaterial;}
 EffectMaterialPtr baseMaterial(){return base;}
 EffectMaterialPtr pass(ERenderingPassType which,bool instanced){return passes[which][instanced];}
};
struct DrawContext {
 struct OverrideBlock {
  std::shared_ptr<ComplexEffectMaterial> materials[2];
  OverrideBlock(const BW::StringRef&name,bool doubleSided){
   assert(name=="shaders/std_effects/stipple/stipple"&&!doubleSided);
   // The actual public ManagedEffect requires semantic registration first.
   assert(constants.registered&&constants.mapping==constants.registered);
   ++effectLoads;
   for(int index=0;index<2;++index){materials[index]=std::make_shared<ComplexEffectMaterial>();
    if(index==failedEffect)materials[index]->base->managed.reset();}
  }
  ComplexEffectMaterial* stockMaterial(bool skinned){return materials[skinned].get();}
 };
};
struct TextureManager {
 static TextureManager* instance(){static TextureManager manager;return &manager;}
 BaseTexturePtr get(const BW::StringRef&name,bool animation,bool mustExist,bool load,const char*){
  assert(name==configuredTexture&&animation&&mustExist&&load);++textureLoads;
  return failedTexture?BaseTexturePtr():BaseTexturePtr(new BaseTexture);
 }
};
}
struct Config {const BW::string& value()const{return configuredTexture;}} compoundStippleMap;
@CONSTANT@;
@BINDING@
@CLEAR_MATERIAL@
@RESOURCES@;
std::shared_ptr<CompoundRenderingResources> compoundRenderingResources;
@INITIALISE@
@FINI@
int main(){
 assert(initialiseStockVehicleAppearanceResources());
 auto owner=compoundRenderingResources;
 assert(owner&&owner->open&&owner->registered&&Moo::constants.mapping==owner->constant);
 std::weak_ptr<Moo::BaseTexture> texture=owner->texture;
 assert(initialiseStockVehicleAppearanceResources()&&registrations==1&&textureLoads==1&&effectLoads==1);
 // Exercise the actual StippleMap producer and detach all distinct deferred
 // effect bindings, native current effects, and pixel/vertex sampler slots.
 for(auto*effect:effects)assert((*owner->constant)(effect,"map"));
 testDevice.samplers[0]=owner->texture->pTexture();testDevice.samplers[257]=owner->texture->pTexture();
 IDirect3DBaseTexture9 unrelated;testDevice.samplers[3]=&unrelated;
 finiStockVehicleAppearanceResources();
 assert(!compoundRenderingResources&&!owner->open&&!owner->registered&&texture.expired());
 assert(!Moo::constants.mapping&&!Moo::constants.registered&&unregistrations==1);
 assert(!testDevice.samplers[0]&&!testDevice.samplers[257]&&testDevice.samplers[3]==&unrelated);
 assert(effects.empty());finiStockVehicleAppearanceResources();owner->close();
 assert(unregistrations==1&&errors==0);
 // A failed effect must roll back its semantic and texture before returning
 // false. A following successful init must own a fresh, usable resource set.
 failedEffect=1;assert(!initialiseStockVehicleAppearanceResources());
 assert(!compoundRenderingResources&&!Moo::constants.mapping&&!Moo::constants.registered&&effects.empty());
 failedEffect=0;assert(!initialiseStockVehicleAppearanceResources());
 assert(!compoundRenderingResources&&!Moo::constants.mapping&&!Moo::constants.registered&&effects.empty());
 failedEffect=-1;failedTexture=true;auto registered=registrations;
 assert(!initialiseStockVehicleAppearanceResources()&&registrations==registered);
 failedTexture=false;configuredTexture.clear();
 assert(!initialiseStockVehicleAppearanceResources()&&registrations==registered);
 configuredTexture="system/maps/stipple.dds";assert(initialiseStockVehicleAppearanceResources());
 finiStockVehicleAppearanceResources();assert(!Moo::constants.mapping&&!Moo::constants.registered&&effects.empty());
}
