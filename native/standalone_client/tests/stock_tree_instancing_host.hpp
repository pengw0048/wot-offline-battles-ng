#include <algorithm>
#include <array>
#include <cassert>
#include <climits>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <limits>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <vector>
#include "stock_tree_lod.hpp"
using uint32 = std::uint32_t;
using uint = unsigned;
using UINT = unsigned;
using DWORD = unsigned;
using HRESULT = int;
using D3DXHANDLE = const char *;
constexpr HRESULT E_FAIL = -1;
constexpr unsigned D3DUSAGE_DYNAMIC = 1, D3DUSAGE_WRITEONLY = 2, D3DPOOL_DEFAULT = 0, D3DLOCK_DISCARD = 4;
constexpr unsigned D3DSTREAMSOURCE_INSTANCEDATA = 0x80000000, D3DSTREAMSOURCE_INDEXEDDATA = 0x40000000;
constexpr unsigned D3DPT_TRIANGLESTRIP = 5, D3DPT_TRIANGLELIST = 4;
#define FAILED(value) ((value) < 0)
#define SUCCEEDED(value) ((value) >= 0)
#define BW_GUARD
#define MF_ASSERT(value) assert(value)
#define ERROR_MSG(...) ((void)0)
namespace wot { enum { treeBranch, treeFrond, treeLeaf, treeBillboard }; }
int operation = 0, failAt = 0;
std::vector<std::string> operations;
HRESULT checked(const char * name) { operations.emplace_back(name); return ++operation == failAt ? E_FAIL : 0; }
@INSTANCE@

struct Ref { void AddRef() {} void Release() {} };
template<class T> struct ComObjectWrap
{
    T * value = nullptr;
    T ** operator&() { return &value; }
    T * pComObject() const { return value; }
    explicit operator bool() const { return value != nullptr; }
};
namespace DX
{
struct VertexBuffer : Ref { std::vector<unsigned char> bytes; };
struct VertexDeclaration : Ref { DWORD fvf=0; };
struct Device
{
    UINT frequency[2] = {17,19}, offset = 23, stride = 29;
    DWORD fvf = 0;
    VertexBuffer previousBuffer, * buffer = &previousBuffer;
    VertexDeclaration previousDeclaration, * declaration = &previousDeclaration;
    HRESULT GetStreamSourceFreq(UINT stream, UINT * value) { if(FAILED(checked("get-freq")))return E_FAIL; *value=frequency[stream==9];return 0; }
    HRESULT GetStreamSource(UINT stream, VertexBuffer ** value, UINT * off, UINT * step)
    {assert(stream==9);if(FAILED(checked("get-stream")))return E_FAIL;*value=buffer;*off=offset;*step=stride;return 0;}
    HRESULT GetVertexDeclaration(VertexDeclaration ** value) {if(FAILED(checked("get-decl")))return E_FAIL;*value=declaration;return 0;}
    HRESULT GetFVF(DWORD * value) {if(FAILED(checked("get-fvf")))return E_FAIL;*value=fvf;return 0;}
    HRESULT SetStreamSourceFreq(UINT stream, UINT value) {if(FAILED(checked("set-freq")))return E_FAIL;frequency[stream==9]=value;return 0;}
    HRESULT SetStreamSource(UINT stream, VertexBuffer * value, UINT off, UINT step)
    {assert(stream==9);if(FAILED(checked("set-stream")))return E_FAIL;buffer=value;offset=off;stride=step;return 0;}
};
}
struct ID3DXEffect
{
    bool begun = false, pass = false;
    unsigned commits = 0, typeWrites = 0;
    TreeInstanceConstants constants = {};
    D3DXHANDLE GetParameterByName(void *, const char * name) { return name; }
    HRESULT SetValue(D3DXHANDLE, const void * value, unsigned bytes)
    { assert(bytes==sizeof(constants));if(FAILED(checked("instance-value")))return E_FAIL;std::memcpy(&constants,value,bytes);return 0; }
};
namespace Moo
{
enum ERenderingPassType { RENDERING_PASS_COLOR, RENDERING_PASS_REFLECTION, RENDERING_PASS_SHADOWS, RENDERING_PASS_DEPTH, RENDERING_PASS_COUNT };
enum ChannelType { NULL_CHANNEL, NOT_SUPPORTED_CHANNEL };
struct TechniqueInfo { int renderPassType_; bool instanced_; D3DXHANDLE handle_; };
struct ManagedEffect
{
    std::vector<TechniqueInfo> entries;
    ID3DXEffect effect;
    const std::vector<TechniqueInfo>& techniques(){return entries;}
    ID3DXEffect * pEffect(){return &effect;}
};
using ManagedEffectPtr=std::shared_ptr<ManagedEffect>;
struct EffectMaterial
{
    ManagedEffectPtr managed=std::make_shared<ManagedEffect>();
    D3DXHANDLE technique=nullptr;
    unsigned passes=1;
    bool recompiled=false;
    const ManagedEffectPtr& pEffect(){return managed;}
    bool skinned(){return false;} ChannelType channelType(){return NULL_CHANNEL;}
    bool checkEffectRecompiled(){bool value=recompiled;recompiled=false;return value;}
    D3DXHANDLE hTechnique(){return technique;}
    void hTechnique(D3DXHANDLE value){technique=value;}
    bool begin(){assert(!managed->effect.begun);if(!technique||FAILED(checked("begin")))return false;managed->effect.begun=true;return true;}
    unsigned numPasses(){return passes;}
    bool beginPass(unsigned){assert(managed->effect.begun&&!managed->effect.pass);if(FAILED(checked("begin-pass")))return false;managed->effect.pass=true;return true;}
    bool commitChanges(){assert(managed->effect.begun&&managed->effect.pass);++managed->effect.commits;return SUCCEEDED(checked("commit"));}
    bool endPass(){assert(managed->effect.pass);managed->effect.pass=false;return SUCCEEDED(checked("end-pass"));}
    bool end(){assert(managed->effect.begun&&!managed->effect.pass);managed->effect.begun=false;return SUCCEEDED(checked("end"));}
};
using EffectMaterialPtr=std::shared_ptr<EffectMaterial>;
struct ComplexEffectMaterial
{
    bool m_instanced=false,m_skinned=false;
    EffectMaterialPtr m_material=std::make_shared<EffectMaterial>();
    ChannelType m_channelType=NULL_CHANNEL;
    D3DXHANDLE m_techniques[RENDERING_PASS_COUNT][2]={};
    bool finishInit();
    bool checkEffectRecompiled()const;
    const EffectMaterialPtr& pass(ERenderingPassType,bool)const;
    @INSTANCED@
};
@COMPLEX@
struct VertexDeclaration
{
    DX::VertexDeclaration value;
    DX::VertexDeclaration * declaration(){return &value;}
};
struct Receipt { unsigned count; std::vector<unsigned char> records; };
struct RC
{
    DX::Device value;
    bool ex=false;
    std::vector<Receipt> receipts;
    DX::Device * device(){return &value;}
    bool usingD3DDeviceEx(){return ex;}
    HRESULT setVertexDeclaration(DX::VertexDeclaration * declaration)
    {if(FAILED(checked("set-decl")))return E_FAIL;value.declaration=declaration;value.fvf=declaration?declaration->fvf:0;return 0;}
    HRESULT setFVF(DWORD fvf)
    {
        if(FAILED(checked("set-fvf")))return E_FAIL;
        // A real FVF creates a declaration; zero leaves the current state alone.
        if(fvf){assert(fvf==value.previousDeclaration.fvf);value.fvf=fvf;value.declaration=&value.previousDeclaration;}
        return 0;
    }
    HRESULT drawIndexedInstancedPrimitive(unsigned,int,unsigned,unsigned,unsigned,unsigned,unsigned count)
    {
        assert(value.frequency[0]==(D3DSTREAMSOURCE_INDEXEDDATA|count));
        assert(value.frequency[1]==(D3DSTREAMSOURCE_INSTANCEDATA|1));
        assert(value.buffer&&value.stride==40&&value.offset+count*40<=value.buffer->bytes.size());
        if(FAILED(checked("draw-instanced")))return E_FAIL;
        auto begin=value.buffer->bytes.begin()+value.offset;
        receipts.push_back({count,{begin,begin+count*40}});return 0;
    }
    HRESULT drawIndexedPrimitive(unsigned,int,unsigned,unsigned,unsigned,unsigned)
    {assert(value.frequency[0]==1&&value.frequency[1]==1);if(FAILED(checked("draw-single")))return E_FAIL;receipts.push_back({1,{}});return 0;}
};
RC& rc(){static RC value;return value;}
struct VertexBuffer
{
    DX::VertexBuffer value;
    bool locked=false;
    unsigned creations=0;
    HRESULT create(unsigned size,unsigned usage,unsigned,unsigned pool,const char*)
    {assert(usage==(D3DUSAGE_DYNAMIC|D3DUSAGE_WRITEONLY)&&pool==D3DPOOL_DEFAULT);if(FAILED(checked("create")))return E_FAIL;value.bytes.resize(size);++creations;return 0;}
    HRESULT lock(unsigned offset,unsigned size,void ** out,unsigned flags)
    {assert(!locked&&offset==0&&flags==D3DLOCK_DISCARD&&size<=value.bytes.size());if(FAILED(checked("lock")))return E_FAIL;locked=true;*out=value.bytes.data();return 0;}
    HRESULT unlock(){assert(locked);locked=false;return checked("unlock");}
    HRESULT set(unsigned stream,unsigned offset,unsigned stride){return rc().device()->SetStreamSource(stream,&value,offset,stride);}
    void release(){assert(!locked);value.bytes.clear();}
};
}
struct Buffer { HRESULT set(unsigned=0,unsigned=0,unsigned=0){return checked("geometry");} };
struct Source { unsigned vertexCount=3,vertexStride=16;std::vector<std::vector<int>>lodIndices={{0,1,2},{0,1,2}}; };
struct GPU { Buffer vertices;std::vector<Buffer>lods=std::vector<Buffer>(2); };
struct TreeType
{
    struct{Source parts[4];}geometry;
    GPU gpu[4];unsigned materialHandle=0;
    unsigned released=0;
    void releaseGpu(){++released;}
};
struct TreeInstance {std::shared_ptr<TreeType>type;TreeInstanceConstants constants={};};
struct TreeDrawItem {TreeInstance*instance;wot::StockTreeLodValues lod;};
struct TreeRenderingOwner
{
    std::array<std::shared_ptr<Moo::ComplexEffectMaterial>,4>materials;
    Moo::VertexDeclaration storage[8];
    std::array<Moo::VertexDeclaration*,4>vertexDeclarations,instancedVertexDeclarations;
    Moo::VertexBuffer instanceBuffer;uint32 instanceCapacity=0;
    bool gpuReady=true;void*atlas=nullptr;void*hidingTargets[2]={};
    std::mutex mutex;std::map<int,std::shared_ptr<TreeType>>types;
    TreeRenderingOwner(){for(unsigned i=0;i<4;++i){vertexDeclarations[i]=&storage[i];instancedVertexDeclarations[i]=&storage[i+4];materials[i]=std::make_shared<Moo::ComplexEffectMaterial>();}}
    bool fail(const char*){return false;}
    @RESET@
};
bool setTreeTypeConstants(TreeRenderingOwner&,TreeType&,unsigned,ID3DXEffect*effect,const float(&)[4])
{++effect->typeWrites;return SUCCEEDED(checked("type-constants"));}
bool setTreeValue(ID3DXEffect*,const char*,const void*,unsigned){return SUCCEEDED(checked("reflection"));}
bool setTreeTexture(ID3DXEffect*,const char*,void*){return true;}
@TREE@

void configure(TreeRenderingOwner&owner,bool instanced,unsigned passes=1)
{
    for(auto&material:owner.materials)
    {
        material->m_material->passes=passes;
        auto&entries=material->m_material->managed->entries;
        entries={{Moo::RENDERING_PASS_COLOR,false,"COLOR"},{Moo::RENDERING_PASS_DEPTH,false,"DEPTH"},
                 {Moo::RENDERING_PASS_REFLECTION,false,"REFLECTION"}};
        if(instanced){entries.push_back({Moo::RENDERING_PASS_COLOR,true,"COLOR_INSTANCED"});entries.push_back({Moo::RENDERING_PASS_DEPTH,true,"DEPTH_INSTANCED"});}
        assert(material->finishInit());
    }
}
struct Fixture
{
    std::shared_ptr<TreeType>a=std::make_shared<TreeType>(),b=std::make_shared<TreeType>();
    TreeInstance instances[4];std::vector<TreeDrawItem>items;
    Fixture()
    {
        a->materialHandle=3;b->materialHandle=8;
        for(unsigned i=0;i<4;++i){auto&v=instances[i];v.type=i<3?a:b;v.constants.position[0]=float(i+1);v.constants.scale[0]=v.constants.scale[1]=v.constants.scale[2]=1;v.constants.rotation[3]=1;v.constants.blend=.5f;v.constants.windMultiplier=1;}
        items={{&instances[0],{0,84,1,90,{{0,1}},{{91,92}},.5f}},
               {&instances[1],{0,85,1,93,{{1,-1}},{{94,0}},1.f}},
               {&instances[2],{1,86,-1,0,{{-1,-1}},{{0,0}},0.f}},
               {&instances[3],{0,87,0,95,{{0,-1}},{{96,0}},1.f}}};
    }
};
bool restored(unsigned declarationState)
{
    auto&d=Moo::rc().value;
    return d.frequency[0]==17&&d.frequency[1]==19&&d.buffer==&d.previousBuffer&&d.offset==23&&d.stride==29&&
        d.declaration==(declarationState==1?nullptr:&d.previousDeclaration)&&d.fvf==(declarationState==2?41u:0u);
}
void reset(unsigned declarationState=0)
{
    operation=0;operations.clear();failAt=0;Moo::rc().receipts.clear();
    auto&d=Moo::rc().value;d.frequency[0]=17;d.frequency[1]=19;d.buffer=&d.previousBuffer;d.offset=23;d.stride=29;
    d.fvf=declarationState==2?41:0;d.previousDeclaration.fvf=d.fvf;d.declaration=declarationState==1?nullptr:&d.previousDeclaration;
}
int main(int argc,char**argv)
{
    assert(argc>=2);std::string mode=argv[1];
    if(mode=="pack")
    {
        assert(argc==4);std::ifstream input(argv[2],std::ios::binary);std::ofstream output(argv[3],std::ios::binary);
        TreeInstanceConstants row;while(input.read(reinterpret_cast<char*>(&row),sizeof(row))){auto packed=packTreeInstance(row);output.write(reinterpret_cast<const char*>(&packed),sizeof(packed));}return 0;
    }
    Fixture fixture;float rotation[4]={};
    if(mode=="collect")
    {
        assert(argc==3);std::ofstream output(argv[2],std::ios::binary);
        for(unsigned part=0;part<4;++part){std::vector<TreePartInstance>records;assert(collectTreePartInstances(part,fixture.items,records));for(auto&r:records){uint32 header[3]={part,r.type==fixture.a.get()?0u:1u,r.lod};output.write(reinterpret_cast<char*>(header),sizeof(header));output.write(reinterpret_cast<char*>(&r.constants),sizeof(r.constants));}}
        return 0;
    }
    if(mode=="draw")
    {
        const std::vector<std::vector<unsigned>>expected={{2,1,1},{2,1},{1,2,1},{2,1}};
        for(bool instanced:{false,true})for(unsigned passCount:{1u,2u})for(unsigned part=0;part<4;++part)
        {
            reset();TreeRenderingOwner owner;configure(owner,instanced,passCount);
            assert(drawTreePart(owner,part,Moo::RENDERING_PASS_COLOR,fixture.items,rotation,1));
            auto&effect=owner.materials[part]->m_material->managed->effect;
            assert(effect.typeWrites==2*passCount&&restored(false));
            unsigned total=0;for(auto n:expected[part])total+=n;
            assert(Moo::rc().receipts.size()==(instanced?expected[part].size():total)*passCount);
            assert(effect.commits==(instanced?2:total)*passCount);
            for(unsigned i=0;instanced&&i<Moo::rc().receipts.size();++i)assert(Moo::rc().receipts[i].count==expected[part][i%expected[part].size()]);
            assert(owner.instanceBuffer.creations==(instanced?1u:0u));
        }
        reset();TreeRenderingOwner owner;configure(owner,true);
        assert(drawTreePart(owner,0,Moo::RENDERING_PASS_REFLECTION,fixture.items,rotation,1));
        assert(owner.instanceBuffer.creations==0&&Moo::rc().receipts.size()==4&&restored(false));
        std::vector<TreePartInstance>records;
        fixture.items[0].lod.branch=6;fixture.items[1].lod.branch=-1;
        assert(collectTreePartInstances(0,fixture.items,records)&&records.size()==2);
        fixture.a->geometry.parts[0].lodIndices.resize(1);
        assert(!collectTreePartInstances(0,fixture.items,records));
        fixture.a->geometry.parts[0].vertexCount=0;
        assert(collectTreePartInstances(0,fixture.items,records)&&records.size()==1);
    }
    else if(mode=="failure")
    {
        for(bool instanced:{false,true})for(unsigned declarationState:{0u,1u,2u})
        {
            reset(declarationState);TreeRenderingOwner baseline;configure(baseline,instanced);
            assert(drawTreePart(baseline,0,Moo::RENDERING_PASS_COLOR,fixture.items,rotation,1));
            assert(restored(declarationState));
            int count=operation-4;
            for(int failure=1;failure<=count;++failure)
            {
                reset(declarationState);failAt=failure;TreeRenderingOwner owner;configure(owner,instanced);
                bool result=drawTreePart(owner,0,Moo::RENDERING_PASS_COLOR,fixture.items,rotation,1);
                if(result||!restored(declarationState)){std::fprintf(stderr,"failure=%d/%d operation=%s result=%d restored=%d\n",failure,count,operations[failure-1].c_str(),result,restored(declarationState));return 1;}
                auto&e=owner.materials[0]->m_material->managed->effect;assert(!e.begun&&!e.pass&&!owner.instanceBuffer.locked);
            }
        }
    }
    else if(mode=="reset")
    {
        for(bool ex:{false,true})
        {
            reset();Moo::rc().ex=ex;TreeRenderingOwner owner;configure(owner,true);owner.types[0]=fixture.a;
            assert(drawTreePart(owner,0,Moo::RENDERING_PASS_COLOR,fixture.items,rotation,1));
            unsigned creates=owner.instanceBuffer.creations;
            assert(drawTreePart(owner,0,Moo::RENDERING_PASS_COLOR,fixture.items,rotation,1));
            assert(owner.instanceBuffer.creations==creates);
            owner.deleteUnmanagedObjects();owner.deleteUnmanagedObjects();
            assert(owner.instanceCapacity==0&&owner.instanceBuffer.value.bytes.empty());
            assert(drawTreePart(owner,0,Moo::RENDERING_PASS_COLOR,fixture.items,rotation,1));
            assert(owner.instanceBuffer.creations==creates+1&&restored(false));
        }
    }
    else return 2;
    return 0;
}
