// SDK Water::draw, parameter cache and material lifecycle run below. Only the
// D3D device, geometry/resources and unrelated surface constants are recorded.
#include <algorithm>
#include <cassert>
#include <cmath>
#include <climits>
#include <cstdio>
#include <cstring>
#include <map>
#include <memory>
#include <string>
#include <vector>
#define BW_BEGIN_NAMESPACE
#define BW_END_NAMESPACE
#define BW_GUARD
#define BW_GUARD_PROFILER(...)
#define GPU_PROFILER_SCOPE(...)
#define MF_ASSERT(x) assert(x)
#define FAILED(x) ((x)<0)
#define SUCCEEDED(x) ((x)>=0)
#define ERROR_MSG(...)
#define D3DXFX_DONOTSAVESTATE 1
using uint=unsigned;using uint32=unsigned;using UINT=unsigned;using HRESULT=int;
using D3DXHANDLE=const char*;
namespace BW {using string=std::string;template<class A,class B>using map=std::map<A,B>;}
enum {D3DRS_FILLMODE,D3DFILL_WIREFRAME,D3DFILL_SOLID,D3DTSS_TEXTURETRANSFORMFLAGS,D3DTTFF_DISABLE};
bool materialAvailable=true,failBegin=false,failPass=false;
struct Texture {int id,refs=1;};
namespace DX {using BaseTexture=Texture;}
struct Vector2 {float x=0,y=0;};
struct Vector4 {float x,y,z,w;Vector4(float a,float b,float c,float d):x(a),y(b),z(c),w(d){}};
struct Matrix {};
template<class T>struct ComObjectWrap {
    T* value=nullptr;ComObjectWrap()=default;ComObjectWrap(T* v):value(v){}
    ComObjectWrap& operator=(T* v){value=v;return *this;}
    T* pComObject()const{return value;}T* operator->()const{return value;}
    operator bool()const{return value!=nullptr;}
};
struct ID3DXEffect {
    std::map<std::string,float> floats;std::map<std::string,Texture*> textures;
    bool begun=false,pass=false;int draws=0;
    void Release(){}
    D3DXHANDLE GetParameterByName(D3DXHANDLE,const char* name){return name;}
    HRESULT SetFloat(const char* name,float value){floats[name]=value;return 0;}
    HRESULT SetBool(const char* name,bool value){floats[name]=value;return 0;}
    HRESULT SetTexture(const char* name,Texture* value){if(value)++value->refs;if(textures[name])--textures[name]->refs;textures[name]=value;return 0;}
    HRESULT SetMatrix(const char*,const Matrix*){return 0;}
    HRESULT SetVector(const char*,const Vector4*){return 0;}
    HRESULT SetVectorArray(const char*,const Vector4*,unsigned){return 0;}
    HRESULT SetFloatArray(const char*,const float*,unsigned){return 0;}
    HRESULT SetTechnique(D3DXHANDLE){return 0;}
    HRESULT Begin(UINT* count,unsigned){if(failBegin)return -1;assert(!begun);begun=true;*count=1;return 0;}
    HRESULT BeginPass(unsigned){assert(begun&&!pass);if(failPass)return -1;pass=true;return 0;}
    HRESULT CommitChanges(){assert(begun&&pass);return 0;}
    HRESULT EndPass(){assert(begun&&pass);pass=false;return 0;}
    HRESULT End(){assert(begun&&!pass);begun=false;return 0;}
};
namespace wot {
bool is_stock_effect(ID3DXEffect*){return true;}
std::vector<std::unique_ptr<ID3DXEffect>> bindings;
HRESULT create_stock_effect_material(ID3DXEffect*,ID3DXEffect** result){
    if(!materialAvailable)return -1;
    bindings.emplace_back(new ID3DXEffect);*result=bindings.back().get();return 0;
}
}
@CACHE@
namespace Moo {
struct ManagedEffect {
    using CompileMark=unsigned;using Handle=D3DXHANDLE;
    ID3DXEffect definition;ComObjectWrap<ID3DXEffect> pEffect_{&definition},activeBinding_;
    uint32 stockPass_=UINT_MAX,numPasses_=0;bool stockBindingValid_=true;
    CompileMark compileMark_=0;BW::string resourceID_="water";D3DXHANDLE technique="water_rt";
    ID3DXEffect* pEffect(){return activeBinding_?activeBinding_.pComObject():pEffect_.pComObject();}
    bool beginCalled(){return numPasses_!=0;}
    D3DXHANDLE currentTechnique(){return technique;}
    void currentTechnique(D3DXHANDLE v,bool){technique=v;}
    void setAutoConstants(){}
    bool selectMaterialBinding(ComObjectWrap<ID3DXEffect>&,ComObjectWrap<ID3DXEffect>&,CompileMark&);
    bool begin(bool);bool beginPass(uint32);bool endPass();bool end();bool commitChanges();
};
@MANAGED@
struct EffectProperty {void apply(ID3DXEffect*,D3DXHANDLE){}};
using EffectPropertyPtr=std::shared_ptr<EffectProperty>;
struct EffectMaterial {
    @PROPERTIES@
    std::shared_ptr<ManagedEffect> pManagedEffect_=std::make_shared<ManagedEffect>();
    Properties properties_;D3DXHANDLE hOverriddenTechnique_="water_rt";
    bool checkEffectRecompiled(){return false;}
    std::shared_ptr<ManagedEffect> pEffect(){return pManagedEffect_;}
    Properties& properties(){return properties_;}
    D3DXHANDLE hTechnique()const{return hOverriddenTechnique_;}
    uint32 numPasses()const{return pManagedEffect_->numPasses_;}
    bool begin()const;void setProperties()const;
    bool beginPass(uint32)const;bool endPass()const;bool end()const;
};
@MATERIAL@
struct EffectMaterialPtr {
    std::shared_ptr<EffectMaterial> value;
    EffectMaterialPtr()=default;EffectMaterialPtr(EffectMaterial* v):value(v){}
    EffectMaterial* operator->()const{return value.get();}
    operator bool()const{return bool(value);}bool hasObject()const{return bool(value);}
};
struct Camera {float farPlane(){return 1000;}};
struct Context {
    int stack=0,states=0;Matrix matrix;Camera camera_;
    void push(){++stack;}void pop(){assert(stack>0);--stack;}
    void world(Matrix){}Matrix& world(){return matrix;}
    Camera& camera(){return camera_;}
    void pushRenderState(int){++states;}void popRenderState(){assert(states>0);--states;}
    void setRenderState(int,int){}void setTextureStageState(int,int,int){}
};
Context& rc(){static Context context;return context;}
}
struct DogWatch {DogWatch(const char*){}void start(){}void stop(){}};
struct IRendererPipeline {enum{TYPE_DEFERRED_SHADING,TYPE_FORWARD_SHADING};};
struct Renderer {int type=0;static Renderer& instance(){static Renderer v;return v;}int pipelineType(){return type;}};
struct VertexBuffer {void activate(){}void deactivate(){}int size(){return 4;}};
struct Target {Texture texture{100};Texture* pTexture(){return &texture;}};
struct WaterCell {
    bool active=true,binding=true;Target target;Vector2 min_{0,0},max_{10,10};
    float cellDistance(){return 0;}uint32 bufferIndex(){return 0;}bool simulationActive(){return active;}
    Target* simTexture(){return &target;}const Vector2& min(){return min_;}const Vector2& max(){return max_;}
    bool bind(){return binding;}void draw(int);
};
struct SimulationManager {Target rain;static SimulationManager& instance(){static SimulationManager v;return v;}Target* rainTexture(){return &rain;}};
struct Waters {
    static Moo::EffectMaterialPtr s_effect_,s_simulationEffect_;bool drawWireframe_=false;
    static Waters& instance(){static Waters v;return v;}
};
Moo::EffectMaterialPtr Waters::s_effect_,Waters::s_simulationEffect_;
struct Draw {ID3DXEffect* effect;float useSimulation,combine,color;Texture* simulation;};
std::vector<Draw> draws;
void WaterCell::draw(int){
    auto* effect=Waters::s_effect_->pEffect()->pEffect();assert(effect->begun&&effect->pass);++effect->draws;
    draws.push_back({effect,effect->floats["useSimulation"],effect->floats["combineSimulation"],effect->floats["color"],effect->textures["simulationMap"]});
}
struct Scene {int releases=0;void deleteUnmanagedObjects(){++releases;}};
struct Water {
    std::vector<WaterCell> cells_=std::vector<WaterCell>(2);bool createdCells_=true,visible_=true,raining_=false,initOK=true;
    int nVertices_=4;float color=1;Matrix transform_;EffectParameterCache cache,*paramCache_=&cache;
    struct State {float simCellSize_=10,tessellation_=1,textureTessellation_=1;}state_;
    std::vector<std::shared_ptr<VertexBuffer>> vertexBuffers_{std::make_shared<VertexBuffer>()};
    static bool s_cullCells_,s_enableDrawPrim;static float s_cullDistance_;static std::shared_ptr<Target> s_rt;
    Scene scene,*waterScene_=&scene;
    bool init(){return initOK;}
    void setupRenderState(float){paramCache_->setFloat("color",color);paramCache_->setTexture("bbCopy",s_rt->pTexture());}
    void draw(Waters&,float);void deleteUnmanagedObjects();
};
bool Water::s_cullCells_=true,Water::s_enableDrawPrim=true;float Water::s_cullDistance_=1;
std::shared_ptr<Target> Water::s_rt=std::make_shared<Target>();
@RELEASE@
@DELETE@
@DRAW@
int main(){
    Waters::s_effect_=new Moo::EffectMaterial;Waters::s_simulationEffect_=new Moo::EffectMaterial;
    Water a,b;a.color=3;b.color=7;a.cells_[1].active=false;
    // Cached load completion previously retained the definition permanently.
    auto* definition=Waters::s_effect_->pEffect()->pEffect();a.cache.effect(definition);b.cache.effect(definition);
    a.draw(Waters::instance(),.03f);assert(draws.size()==2);
    auto* binding=Waters::s_effect_->properties().existingStockBinding();
    assert(binding&&binding!=definition&&draws[0].effect==binding);
    assert(draws[0].useSimulation==1&&draws[0].simulation==a.cells_[0].simTexture()->pTexture());
    assert(draws[1].useSimulation==0&&draws[0].color==3);
    assert(definition->textures.empty()&&definition->floats.empty());
    b.raining_=true;b.draw(Waters::instance(),.03f);assert(draws.size()==4&&draws[2].color==7);
    assert(draws[2].combine==1&&draws[2].simulation==b.cells_[0].simTexture()->pTexture());
    // Another material with the same Definition cannot see this Water's data.
    Moo::EffectMaterial other;other.pManagedEffect_=Waters::s_effect_->pEffect();
    assert(other.properties().select(*other.pEffect()));auto* unrelated=other.pEffect()->pEffect();
    assert(unrelated!=binding&&unrelated->textures.empty()&&unrelated->floats.empty());
    a.draw(Waters::instance(),.03f);assert(draws.size()==6&&draws[4].effect==binding&&draws[4].color==3);
    // Reload creates a new binding; refresh cached identity before any write.
    ++Waters::s_effect_->pEffect()->compileMark_;a.draw(Waters::instance(),.03f);
    auto* replacement=Waters::s_effect_->properties().existingStockBinding();assert(replacement!=binding&&draws.back().effect==replacement);
    const auto count=draws.size();
    failBegin=true;a.draw(Waters::instance(),.03f);failBegin=false;assert(draws.size()==count);
    failPass=true;a.draw(Waters::instance(),.03f);failPass=false;assert(draws.size()==count);
    materialAvailable=false;++Waters::s_effect_->pEffect()->compileMark_;a.draw(Waters::instance(),.03f);
    materialAvailable=true;assert(draws.size()==count);
    assert(Moo::rc().stack==0&&Moo::rc().states==0);
    a.visible_=false;a.draw(Waters::instance(),.03f);a.visible_=true;assert(draws.size()==count);
    a.initOK=false;a.draw(Waters::instance(),.03f);a.initOK=true;assert(draws.size()==count);
    a.draw(Waters::instance(),.03f);replacement=Waters::s_effect_->properties().existingStockBinding();
    assert(replacement->textures["bbCopy"]&&replacement->textures["simulationMap"]);
    auto& sim=Waters::s_simulationEffect_;assert(sim->properties().select(*sim->pEffect()));
    auto* simBinding=sim->pEffect()->pEffect();Texture old{900};
    for(const char* name:{"heightMap","previousHeightMap","neighbourHeightMap0","neighbourHeightMap1","neighbourHeightMap2","neighbourHeightMap3"})simBinding->SetTexture(name,&old);
    assert(old.refs==7);
    a.deleteUnmanagedObjects();a.deleteUnmanagedObjects();
    for(const auto& item:replacement->textures)assert(!item.second);
    for(const auto& item:simBinding->textures)assert(!item.second);
    assert(old.refs==1);
    assert(!a.cache.hasEffect()&&a.scene.releases==2);
    Water::s_rt=std::make_shared<Target>();a.draw(Waters::instance(),.03f);
    assert(draws.back().effect==replacement&&replacement->textures["bbCopy"]==Water::s_rt->pTexture());
    assert(Moo::rc().stack==0&&Moo::rc().states==0);
    std::puts("SDK Water::draw and cache/material/reset consumer passed");
}
