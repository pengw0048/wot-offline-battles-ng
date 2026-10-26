// Actual production State/draw methods are inserted at the marked seam.
#include "wot/stock_local_lighting.hpp"
#include <algorithm>
#include <cassert>
#include <climits>
#include <cstdint>
#include <cstring>
#include <map>
#include <memory>
#include <string>
#include <vector>
using uint=unsigned;using uint32=std::uint32_t;using UINT=unsigned;using HRESULT=int;using D3DXHANDLE=const char*;
#define FAILED(x) ((x)<0)
#define SUCCEEDED(x) ((x)>=0)
enum {D3DUSAGE_DYNAMIC=1,D3DUSAGE_WRITEONLY=2,D3DPOOL_DEFAULT=0,D3DLOCK_DISCARD=1};
struct Matrix{void multiply(const Matrix&,const Matrix&) {}};
namespace DX {struct BaseTexture {unsigned refs=1;void AddRef(){++refs;}void Release(){assert(refs);--refs;}};using CubeTexture=BaseTexture;struct Surface{};}
template<class T>struct ComObjectWrap {T* value=nullptr;T* pComObject()const{return value;}operator bool()const{return value!=nullptr;}};
struct D3DVERTEXBUFFER_DESC{unsigned Size=0;};
struct ID3DXEffect {
    bool definition=false,spot=false,committed=false,packet=false,instancing=false;
    unsigned bytes=0;wot::StockLocalLightMath::Packet data{};DX::BaseTexture* texture=nullptr;
    std::map<std::string,bool> booleans;
    ~ID3DXEffect(){if(texture)texture->Release();}
    D3DXHANDLE GetParameterByName(void*,const char* name){return std::string(name)=="g_enableSSR"?nullptr:name;}
    HRESULT SetBool(const char* name,bool value){assert(!definition&&std::string(name)!="g_enableSSR");booleans[name]=value;if(std::string(name)==(spot?"g_spotInstancing":"g_omniInstancing"))instancing=value;return 0;}
    HRESULT SetFloat(const char* name,float value){assert(!definition&&std::string(name)==(spot?"g_coneApproximationError":"g_sphereApproximationError"));assert(value==(spot?.1f:.05f));return 0;}
    HRESULT SetValue(const char* name,const void* source,unsigned size){assert(!definition&&std::string(name)==(spot?"g_spotLight":"g_omniLight"));assert(size==(spot?64:48));bytes=size;std::memcpy(&data,source,size);packet=true;return 0;}
    HRESULT SetTexture(const char* name,DX::BaseTexture* value){assert(!definition&&std::string(name)==(spot?"g_spotShadowMap":"g_omniShadowMap"));if(value)value->AddRef();if(texture)texture->Release();texture=value;return 0;}
    HRESULT SetMatrix(const char* name,const Matrix*){assert(spot&&!definition&&std::string(name)=="g_spotShadowMatrix");return 0;}
    HRESULT CommitChanges(){assert(!definition&&(packet||instancing));committed=true;return 0;}
};
static ID3DXEffect* activeEffect=nullptr;
namespace Moo {
struct OmniLight{};struct SpotLight{};using OmniLightPtr=std::shared_ptr<OmniLight>;using SpotLightPtr=std::shared_ptr<SpotLight>;
struct VertexBuffer {
    std::vector<unsigned char> data;bool locked=false,failLock=false;unsigned creates=0;
    HRESULT getDesc(D3DVERTEXBUFFER_DESC* d){d->Size=unsigned(data.size());return 0;}
    HRESULT create(unsigned bytes,unsigned,unsigned,unsigned,const char*){data.resize(bytes);++creates;return 0;}
    HRESULT lock(unsigned,unsigned bytes,void** destination,unsigned){if(failLock)return -1;assert(!locked&&bytes<=data.size());locked=true;*destination=data.data();return 0;}
    HRESULT unlock(){assert(locked);locked=false;return 0;}
};
struct EffectMaterial {
    ID3DXEffect definition,binding;struct Managed {ID3DXEffect* active;ID3DXEffect* pEffect(){return active;}} managed;
    struct Properties {EffectMaterial* owner;ID3DXEffect* existingStockBinding(){return &owner->binding;}bool select(Managed& m){m.active=&owner->binding;return true;}} properties_;
    std::string technique;bool failBegin=false;int failPass=-1;unsigned begins=0,ends=0,passEnds=0;
    explicit EffectMaterial(bool spot):managed{&definition},properties_{this}{definition.definition=true;definition.spot=binding.spot=spot;}
    bool setTechnique(const char* name){technique=name;return true;}Properties& properties(){return properties_;}Managed* pEffect(){return &managed;}
    bool begin(){++begins;managed.active=&definition;return !failBegin;}
    unsigned numPasses(){return technique.find("FALLBACK")!=std::string::npos?1:3;}
    bool beginPass(unsigned pass){if(int(pass)==failPass)return false;managed.active=&binding;binding.packet=false;binding.committed=false;binding.instancing=false;activeEffect=&binding;return true;}
    void endPass(){++passEnds;}void end(){++ends;managed.active=&definition;activeEffect=&definition;}
};using EffectMaterialPtr=std::shared_ptr<EffectMaterial>;
struct Visual {
    struct Draw {bool instanced,spot,shadow;unsigned count;wot::StockLocalLightMath::Packet data;};
    std::vector<Draw> draws;VertexBuffer* stream=nullptr;
    bool isOK(){return true;}void instancingStream(VertexBuffer* value){stream=value;}
    void record(bool instanced,unsigned count){assert(activeEffect&&!activeEffect->definition&&activeEffect->committed);assert(activeEffect->instancing==instanced);
        auto data=activeEffect->data;if(instanced){assert(stream&&count*64<=stream->data.size());std::memcpy(&data,stream->data.data(),64);}else assert(activeEffect->packet);
        draws.push_back({instanced,activeEffect->spot,activeEffect->booleans[activeEffect->spot?"g_spotShadow":"g_omniShadow"],count,data});}
    void justDrawPrimitives(){record(false,1);}void justDrawPrimitivesInstanced(unsigned offset,unsigned count){assert(!offset);record(true,count);}
};using VisualPtr=std::shared_ptr<Visual>;
struct RenderTarget{DX::BaseTexture texture;DX::BaseTexture* pTexture(){return &texture;}};using RenderTargetPtr=std::shared_ptr<RenderTarget>;
}
bool drawInstances(const Moo::VisualPtr& geometry,const Moo::VertexBuffer&,uint count){geometry->record(true,count);return true;}
struct StockDeferredBuffers {bool ambientOcclusionEnabled()const{return true;}};
StockDeferredBuffers* stockDeferredBuffers(){static StockDeferredBuffers value;return &value;}
class StockLocalLighting {public:struct State;};
using LocalMath=wot::StockLocalLightMath;
@PRODUCTION@
int main(){
    using M=wot::StockLocalLightMath;
    StockLocalLighting::State state;state.prepared=true;
    state.omniMaterial=std::make_shared<Moo::EffectMaterial>(false);state.spotMaterial=std::make_shared<Moo::EffectMaterial>(true);
    state.sphere=std::make_shared<Moo::Visual>();state.cone=std::make_shared<Moo::Visual>();
    state.spotTargets[0]=std::make_shared<Moo::RenderTarget>();DX::CubeTexture cube;state.cube.value=&cube;
    for(auto group:{M::Volume,M::Fallback}){
        StockLocalLighting::State::Omni omni;omni.group=group;omni.shadow=0;
        omni.data=M::omni({{2,3,4}},{{.2f,.4f,.8f,1}},25,5,20,{{0,0,0}});state.preparedOmnis.push_back(omni);
        StockLocalLighting::State::Spot spot;spot.group=group;spot.shadow=0;
        spot.data=M::spot({{5,6,7}},{{.2f,.4f,.8f,1}},50,6,30,.8f,.75f,{{0,0,0,1}});state.preparedSpots.push_back(spot);
    }
    for(unsigned i=0;i<1001;++i){auto omni=state.preparedOmnis[0];omni.group=M::Instanced;omni.shadow=-1;state.preparedOmnis.push_back(omni);auto spot=state.preparedSpots[0];spot.group=M::Instanced;spot.shadow=-1;state.preparedSpots.push_back(spot);}
    assert(state.drawVolumes(false)&&state.drawVolumes(true));
    for(auto geometry:{state.sphere,state.cone}){assert(geometry->stream==nullptr&&geometry->draws.size()==7);assert(!geometry->draws[0].instanced&&geometry->draws[0].shadow);assert(geometry->draws[3].instanced&&geometry->draws[3].count==1001&&!geometry->draws[3].shadow);assert(!geometry->draws[6].instanced&&geometry->draws[6].shadow);}
    assert(state.instances.creates==1&&state.instances.data.size()==1001*64&&!state.instances.locked);
    assert(cube.refs==1&&state.spotTargets[0]->texture.refs==1);
    state.omniMaterial->failBegin=true;
    assert(!state.drawOne(state.omniMaterial,state.sphere,M::Instanced,&state.preparedOmnis[0].data,1,-1,nullptr));assert(state.sphere->stream==nullptr);
    state.omniMaterial->failBegin=false;state.omniMaterial->failPass=1;const auto ended=state.omniMaterial->ends;
    assert(!state.drawOne(state.omniMaterial,state.sphere,M::Volume,&state.preparedOmnis[0].data,1,0,nullptr));
    assert(state.omniMaterial->ends==ended+1&&cube.refs==1&&state.sphere->stream==nullptr);
    state.omniMaterial->failPass=-1;state.instances.failLock=true;
    assert(!state.drawOne(state.omniMaterial,state.sphere,M::Instanced,&state.preparedOmnis[0].data,1,-1,nullptr));assert(!state.instances.locked);
    state.prepared=false;assert(!state.drawVolumes(false));
}
