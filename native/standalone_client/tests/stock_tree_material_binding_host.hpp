#include <algorithm>
#include <array>
#include <cassert>
#include <climits>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <map>
#include <memory>
#include <string>
#include <vector>
using uint32 = std::uint32_t;
using UINT = unsigned;
using HRESULT = int;
using D3DXHANDLE = const char *;
constexpr HRESULT E_FAIL = -1;
constexpr unsigned D3DXFX_DONOTSAVESTATE = 1;
constexpr unsigned D3DPT_TRIANGLESTRIP = 5, D3DPT_TRIANGLELIST = 4;
constexpr unsigned D3DCLEAR_TARGET = 1, D3DCLEAR_ZBUFFER = 2, D3DCLEAR_STENCIL = 4;
#define FAILED(value) ((value) < 0)
#define SUCCEEDED(value) ((value) >= 0)
#define BW_GUARD
#define MF_ASSERT(value) assert(value)
#define ERROR_MSG(...) ((void)0)
namespace BW { template<class K,class V> using map = std::map<K,V>; }
namespace wot { enum { treeBranch, treeFrond, treeLeaf, treeBillboard, stock_speedtree = 0 }; }
struct Vector4 { float x,y,z,w; Vector4(float a,float b,float c,float d):x(a),y(b),z(c),w(d){} };
@INSTANCE@

// Device boundary: private data and texture state belong to each effect object.
// Begin/Pass rejects invalid order; the generated Moo selector owns switching.
struct ID3DXEffect;
ID3DXEffect * activePass = nullptr;
struct ID3DXEffect
{
    int refs = 1;
    bool begun = false, pass = false, failSet = false;
    unsigned writes = 0;
    TreeInstanceConstants instance = {};
    std::map<std::string, bool> booleans;
    std::map<std::string, void *> textures;
    std::map<std::string, Vector4> vectors;
    std::map<std::string, int> integers;
    std::string technique;
    void AddRef() { ++refs; }
    void Release() { if (!--refs) delete this; }
    D3DXHANDLE GetParameterByName(void *, const char * name) { return name; }
    HRESULT SetValue(D3DXHANDLE, const void * value, unsigned size)
    { assert(size == sizeof(instance)); std::memcpy(&instance,value,size); ++writes; return 0; }
    HRESULT SetTexture(const char * name, void * value) { textures[name] = value; ++writes; return 0; }
    HRESULT SetBool(const char * name, bool value)
    { if (failSet) return E_FAIL; booleans[name]=value; ++writes; return 0; }
    HRESULT SetInt(const char * name, int value) { integers[name]=value; ++writes; return 0; }
    HRESULT SetFloat(const char *, float) { ++writes; return 0; }
    HRESULT SetVector(const char * name, const Vector4 * value) { vectors.insert_or_assign(name,*value); ++writes; return 0; }
    HRESULT SetTechnique(D3DXHANDLE value) { if (begun) return E_FAIL; technique=value; return 0; }
    HRESULT Begin(UINT * count, unsigned) { if (begun) return E_FAIL; begun=true; *count=1; return 0; }
    HRESULT BeginPass(unsigned index) { if (!begun || pass || index) return E_FAIL; pass=true; activePass=this; return 0; }
    HRESULT EndPass() { if (!begun || !pass) return E_FAIL; pass=false; activePass=nullptr; return 0; }
    HRESULT End() { if (!begun || pass) return E_FAIL; begun=false; return 0; }
    HRESULT CommitChanges() { return begun && pass ? 0 : E_FAIL; }
};
template<class T> struct ComObjectWrap
{
    T * pointer = nullptr;
    ComObjectWrap() = default;
    ComObjectWrap(T * value):pointer(value) { if(pointer) pointer->AddRef(); }
    ComObjectWrap(const ComObjectWrap & value):ComObjectWrap(value.pointer) {}
    ~ComObjectWrap() { if(pointer) pointer->Release(); }
    ComObjectWrap & operator=(const ComObjectWrap & value)
    { T * next=value.pointer; if(next)next->AddRef(); if(pointer)pointer->Release(); pointer=next; return *this; }
    T * pComObject()const{return pointer;} T * operator->()const{return pointer;}
    explicit operator bool()const{return pointer!=nullptr;}
    bool operator!=(const T * value)const{return pointer!=value;}
};
namespace wot
{
bool fail_create=false;
bool is_stock_effect(ID3DXEffect *) { return true; }
HRESULT create_stock_effect_material(ID3DXEffect *, ID3DXEffect ** output)
{ if(fail_create)return E_FAIL; *output=new ID3DXEffect; return 0; }
}
using EffectPropertyPtr=int;
namespace Moo
{
struct ManagedEffect
{
    using CompileMark=uint32; using Handle=D3DXHANDLE;
    ComObjectWrap<ID3DXEffect> pEffect_, activeBinding_;
    CompileMark compileMark_=1;
    unsigned numPasses_=0, stockPass_=UINT_MAX;
    bool stockBindingValid_=true;
    std::string resourceID_="original.fx";
    D3DXHANDLE technique_="BRANCH_ALPHA";
    ManagedEffect(){auto*p=new ID3DXEffect;pEffect_=p;p->Release();}
    ID3DXEffect*pEffect(){return activeBinding_?activeBinding_.pComObject():pEffect_.pComObject();}
    D3DXHANDLE currentTechnique(){return technique_;}
    void currentTechnique(D3DXHANDLE v,bool){technique_=v;}
    bool selectMaterialBinding(ComObjectWrap<ID3DXEffect>&,ComObjectWrap<ID3DXEffect>&,CompileMark&);
    bool beginCalled()const{return numPasses_!=0;}
    void setAutoConstants(){}
    bool begin(bool); bool beginPass(uint32); bool endPass(); bool end(); bool commitChanges();
};
struct EffectMaterial
{
    @PROPERTIES@
    std::shared_ptr<ManagedEffect> pManagedEffect_;
    D3DXHANDLE hOverriddenTechnique_="BRANCH_ALPHA";
    Properties properties_;
    explicit EffectMaterial(const std::shared_ptr<ManagedEffect>&m):pManagedEffect_(m){}
    ManagedEffect*pEffect(){return pManagedEffect_.get();}
    Properties&properties(){return properties_;}
    void checkEffectRecompiled(){}
    D3DXHANDLE hTechnique()const{return hOverriddenTechnique_;}
    bool hTechnique(D3DXHANDLE v){hOverriddenTechnique_=v;return true;}
    void setProperties()const{properties_.select(*pManagedEffect_);}
    bool begin()const;
    unsigned numPasses(){return pManagedEffect_->numPasses_;}
    bool beginPass(unsigned i){return pManagedEffect_->beginPass(i);}
    bool endPass(){return pManagedEffect_->endPass();}
    bool end(){return pManagedEffect_->end();}
    bool commitChanges(){return pManagedEffect_->commitChanges();}
};
@MANAGED@
@BEGIN@
using EffectMaterialPtr=std::shared_ptr<EffectMaterial>;
struct Target{unsigned width()const{return 128;}unsigned height()const{return 64;}void*pTexture(){return this;}};
using RenderTargetPtr=std::shared_ptr<Target>;
struct Receipt{TreeInstanceConstants instance;bool alpha;void*texture;std::string technique;};
std::vector<Receipt> receipts;
struct FSQuad
{
    bool drawChecked(EffectMaterial&material,unsigned,unsigned)
    {
        if(!material.begin()||!material.beginPass(0))return false;
        auto*effect=material.pEffect()->pEffect();
        receipts.push_back({effect->instance,effect->booleans["g_useAlpha"],effect->textures["g_srcMap"],effect->technique});
        bool result=material.commitChanges();return material.endPass()&&material.end()&&result;
    }
};
struct Device{HRESULT Clear(unsigned,void*,unsigned,unsigned,float,unsigned){return 0;}};
struct RC
{
    void setVertexDeclaration(int){}
    void drawIndexedPrimitive(int,int,int,int,int,int)
    {assert(activePass);receipts.push_back({activePass->instance,activePass->booleans["g_useAlpha"],activePass->textures["g_diffuseMap"],activePass->technique});}
    unsigned screenWidth(){return 512;}unsigned screenHeight(){return 256;}
    Device*device(){static Device value;return &value;}
    FSQuad&fsQuad(){static FSQuad value;return value;}
};
RC&rc(){static RC value;return value;}
}
struct Buffer{int set(int=0,int=0,int=0){return 0;}};
struct Source{unsigned vertexCount=3,vertexStride=16;std::vector<std::vector<int>>lodIndices={{0,1,2}};};
struct GPU{Buffer vertices;std::vector<Buffer>lods={Buffer{}};};
struct TreeType{struct{Source parts[4];}geometry;GPU gpu[4];unsigned materialHandle=0;};
struct TreeInstance{TreeType*type;TreeInstanceConstants constants={};float leavesAlpha=.4f,branchAlpha=.7f;};
struct TreeDrawItem{TreeInstance*instance;int lod;};
struct Decl{int declaration(){return 0;}};
struct TreeRenderingOwner
{
    std::shared_ptr<Decl>vertexDeclarations[4]={std::make_shared<Decl>(),std::make_shared<Decl>(),std::make_shared<Decl>(),std::make_shared<Decl>()};
    Moo::EffectMaterialPtr semitransparentMaterials[2][4], blurMaterial, upsampleMaterial;
    Moo::RenderTargetPtr hidingTargets[2]={std::make_shared<Moo::Target>(),std::make_shared<Moo::Target>()};
    unsigned failures=0;
    bool fail(const char*){++failures;return false;}
};
bool selectPartLods(unsigned,int,std::int32_t(&lods)[2],float(&alphas)[2])
{lods[0]=0;lods[1]=-1;alphas[0]=.2f;return true;}
bool setTreeTypeConstants(TreeRenderingOwner&,TreeType&type,unsigned,ID3DXEffect*effect,const float(&)[4])
{effect->SetTexture("g_diffuseMap",&type);return true;}
struct Conv{std::array<float,4>albedo={},gloss={},metallic={};};struct Settings{Conv categories[1];};Settings stockDeferredMaterialSettings(){return{};}
bool setTreeValue(ID3DXEffect*,const char*,const void*,unsigned){return true;}
bool setTreeTexture(ID3DXEffect*effect,const char*name,void*value){return SUCCEEDED(effect->SetTexture(name,value));}
struct HidingTargetScope{bool bind(const Moo::RenderTargetPtr&,bool){return true;}};
struct HidingScreenConstants{void apply(){}void restore(){}};
Vector4 hidingTargetProjection(const Moo::RenderTargetPtr&){return {1,1,0,0};}
std::shared_ptr<TreeRenderingOwner> owner;
std::shared_ptr<TreeRenderingOwner> currentRenderingOwner(){return owner;}
struct Hiding{struct{bool hideClosest=true;}settings;}hiding;
Hiding&treeHiding(){return hiding;}
unsigned drained=0,drawn=0;
void drawStockTreeSemitransparent(bool skip){++drained;if(!skip)++drawn;}
@TREE@

int main(int argc,char**argv)
{
    assert(argc==2);const std::string mode=argv[1];
    owner=std::make_shared<TreeRenderingOwner>();
    auto shared=std::make_shared<Moo::ManagedEffect>();
    for(auto&state:owner->semitransparentMaterials)
        for(auto&material:state)material=std::make_shared<Moo::EffectMaterial>(shared);
    owner->blurMaterial=std::make_shared<Moo::EffectMaterial>(std::make_shared<Moo::ManagedEffect>());
    owner->upsampleMaterial=std::make_shared<Moo::EffectMaterial>(std::make_shared<Moo::ManagedEffect>());
    if(mode=="parts")
    {
        TreeType type;TreeInstance instance{&type};TreeDrawItem item{&instance,0};float rotation[4]={};
        for(int frame=0;frame<3;++frame)for(unsigned part=0;part<4;++part)
        {
            auto&material=*owner->semitransparentMaterials[frame%2][part];
            instance.constants.position[0]=42.f+frame;
            if(!drawSemitransparentPart(*owner,material,part,item,rotation))return 10;
            if(Moo::receipts.empty())return 11;
            const auto&r=Moo::receipts.back();
            if(r.instance.position[0]!=42.f+frame||r.texture!=&type||
                r.instance.blend!=(part==wot::treeLeaf?.4f:part==wot::treeBillboard?1.f:.7f))return 41;
            auto*binding=material.properties().existingStockBinding();
            if(!binding||binding->textures["g_diffuseMap"]||binding->textures["g_normalMap"])return 12;
            if(shared->pEffect()!=shared->pEffect_.pComObject()||shared->pEffect_->writes)return 13;
        }
        auto unavailable=std::make_shared<Moo::ManagedEffect>();Moo::EffectMaterial material(unavailable);
        wot::fail_create=true;
        if(drawSemitransparentPart(*owner,material,0,item,rotation))return 14;
        wot::fail_create=false;
        if(!drawSemitransparentPart(*owner,material,0,item,rotation))return 15;
    }
    else if(mode=="alpha")
    {
        for(bool value:{true,false,true,false})
        {
            if(!setHidingAlpha(*owner,value))return 20;
            for(auto&state:owner->semitransparentMaterials)for(auto&material:state)
            {auto*b=material->properties().existingStockBinding();if(!b||b->booleans["g_useAlpha"]!=value)return 42;}
            if(shared->pEffect_->writes)return 21;
        }
        auto*bad=owner->semitransparentMaterials[0][0]->properties().existingStockBinding();
        bad->failSet=true;if(setHidingAlpha(*owner,true))return 22;
        bad->failSet=false;if(!setHidingAlpha(*owner,false))return 23;
    }
    else if(mode=="stage")
    {
        for(int frame=0;frame<3;++frame)
        {
            Moo::receipts.clear();drawStockTreeSemitransparentStage(false,true,true);
            if(owner->failures||Moo::receipts.size()!=3)return 30;
            void*expected[]={owner->hidingTargets[0]->pTexture(),owner->hidingTargets[1]->pTexture(),owner->hidingTargets[0]->pTexture()};
            const char*tech[]={"HORIZONTAL_PASS","VERTICAL_PASS","UPSAMPLE_RT"};
            for(unsigned i=0;i<3;++i)if(Moo::receipts[i].texture!=expected[i]||Moo::receipts[i].technique!=tech[i])return 43;
            for(auto&material:{owner->blurMaterial,owner->upsampleMaterial})
            {auto*b=material->properties().existingStockBinding();if(!b||b->textures["g_srcMap"]||material->pEffect()->pEffect_->writes)return 31;}
        }
        assert(drained==3&&drawn==3);
        owner->semitransparentMaterials[0][0]->properties().existingStockBinding()->failSet=true;
        const auto before=Moo::receipts.size();drawStockTreeSemitransparentStage(false,true,true);
        if(!owner->failures||drained!=4||drawn!=3||Moo::receipts.size()!=before)return 32;
    }
    else return 2;
    std::puts("Actual tree draw bodies and Moo binding transitions passed");
}
