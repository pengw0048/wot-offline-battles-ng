"""Exercise the production BWT2 owner with complete original map sections."""
import importlib.util
import os
from pathlib import Path
import re
import shutil
import struct
import subprocess
import sys
import tarfile
import tempfile
import unittest
import zipfile

ROOT = Path(__file__).resolve().parents[1]
REPOSITORY = ROOT.parents[1]
spec = importlib.util.spec_from_file_location('stock_space_terrain_patch',
    ROOT / 'bigworld/patch_stock_space_terrain.py')
patch = importlib.util.module_from_spec(spec)
spec.loader.exec_module(patch)

FILES = ('lib/compiled_space/terrain2_types.hpp',
         'lib/compiled_space/terrain2_scene_provider.hpp',
         'lib/compiled_space/terrain2_scene_provider.cpp',
         'lib/terrain/terrain_settings.hpp', 'lib/terrain/terrain_settings.cpp',
         'lib/terrain/terrain2/terrain_block2.cpp',
         'lib/terrain/terrain2/terrain_renderer2.cpp',
         'lib/terrain/terrain2/terrain_renderer2.hpp',
         'lib/terrain/terrain2/terrain_index_buffer.cpp',
         'lib/terrain/terrain_graphics_options.cpp')

PRELUDE = r'''
#include "wot/stock_terrain_binary.hpp"
#include <cassert>
#include <atomic>
#include <cfloat>
#include <fstream>
#include <map>
#include <memory>
#include <vector>
#define ASSET_MSG(...) ((void)0)
#define MF_ASSERT(x) assert(x)
#define MF_VERIFY(x) assert(x)
#define PROFILER_SCOPED(x)
namespace BW {
using uint32=std::uint32_t;using int32=std::int32_t;using int16=std::int16_t;
using uint8=std::uint8_t;using uint16=std::uint16_t;
using string=std::string;
template<class T>using vector=std::vector<T>;
template<class K,class V>using map=std::map<K,V>;
struct FourCC{explicit FourCC(const char*){}};
struct Vector3{
    float x,y,z;
    Vector3(float a=0,float b=0,float c=0):x(a),y(b),z(c){}
    void set(float a,float b,float c){x=a;y=b;z=c;}
    float operator[](size_t i)const{return i==0?x:i==1?y:z;}
};
struct Matrix{
    Vector3 position;
    Vector3 applyPoint(const Vector3& p)const{return Vector3(p.x+position.x,p.y+position.y,p.z+position.z);}
    Vector3 applyToOrigin()const{return position;}
};
struct AABB{
    Vector3 low,high;
    const Vector3& minBounds()const{return low;}
    const Vector3& maxBounds()const{return high;}
};
template<class T>struct ExternalArray{
    T* ptr=nullptr;size_t count=0;
    void reset(){ptr=nullptr;count=0;}
    void assignExternalStorage(T* p,size_t n,size_t){ptr=p;count=n;}
    bool valid()const{return ptr!=nullptr;}
    size_t size()const{return count;}
    T& operator[](size_t n){assert(n<count);return ptr[n];}
    const T& operator[](size_t n)const{assert(n<count);return ptr[n];}
};
struct DataSpan{uint16 first,last;};
template<class T,class C>struct LookUpTable{C data;C& storage(){return data;}};
struct StaticLooseOctree{
    using NodeIndex=uint16;
    struct NodeBoundData{AABB box;};
    using NodeCenter=Vector3;
    struct Node{uint16 child[8];};
    struct StorageType{
        struct Config{uint32 maxDepth_;Vector3 worldCenter_;float worldSize_;uint16 numDataEntries_;}config_;
        ExternalArray<NodeBoundData> bounds_;
        ExternalArray<NodeCenter> centers_;
        ExternalArray<Node> nodes_;
        ExternalArray<uint32> dataReferences_;
        ExternalArray<uint16> parents_;
    }data;
    StorageType& storage(){return data;}
};
namespace CompiledSpace {
namespace StringTableTypes{using Index=uint32;}
class BinaryFormat{
public:
    int closes=0;
    struct Stream{
        std::vector<unsigned char>& bytes;size_t cursor=0;
        explicit Stream(std::vector<unsigned char>& b):bytes(b){}
        size_t tell()const{return cursor;}size_t size()const{return bytes.size();}
        void* readRaw(size_t n){if(n>bytes.size()-cursor)return nullptr;
            void* result=bytes.data()+cursor;cursor+=n;return result;}
    };
    void closeSection(Stream*){++closes;}
};
struct StringTable{
    std::map<uint32,std::string> names;
    const char* entry(size_t key)const{auto i=names.find(uint32(key));return i==names.end()?nullptr:i->second.c_str();}
};
}}
@TYPES@
@ADAPTER@
'''

WIRE_MAIN = r'''
template<class T>void record(std::ostream& out,const T& v){
    std::uint32_t size=sizeof(T);out.write(reinterpret_cast<const char*>(&size),4);
    out.write(reinterpret_cast<const char*>(&v),sizeof(T));
}
template<class T>void array(std::ostream& out,const std::vector<T>& v){
    std::uint32_t size=sizeof(T),count=std::uint32_t(v.size());
    out.write(reinterpret_cast<const char*>(&size),4);out.write(reinterpret_cast<const char*>(&count),4);
    if(count)out.write(reinterpret_cast<const char*>(v.data()),v.size()*sizeof(T));
}
void encode(std::ostream& out,const wot::StockTerrainBinary& v){
    record(out,v.header);array(out,v.blocks);array(out,v.grid);record(out,v.settings);
    array(out,v.lodDistances);array(out,v.mappings);record(out,v.config);array(out,v.bounds);
    array(out,v.centers);array(out,v.nodes);array(out,v.references);array(out,v.parents);
    array(out,v.spans);array(out,v.contents);
}
int main(int argc,char** argv){
    assert(argc==3);std::ifstream input(argv[1],std::ios::binary);std::ofstream out(argv[2],std::ios::binary);
    auto read=[&](std::uint32_t& n){return bool(input.read(reinterpret_cast<char*>(&n),4));};
    std::uint32_t count;if(!read(count))return 2;
    for(std::uint32_t i=0;i<count;++i){
        std::uint32_t bytes,names;if(!read(bytes)||bytes>10000000)return 2;
        std::vector<unsigned char> data(bytes);if(!input.read(reinterpret_cast<char*>(data.data()),bytes)||!read(names))return 2;
        BW::CompiledSpace::StringTable strings;
        for(std::uint32_t j=0;j<names;++j){
            std::uint32_t key,length;if(!read(key)||!read(length)||length>10000)return 2;
            std::string value(length,'\0');if(!input.read(&value[0],length))return 2;strings.names[key]=value;
        }
        BW::CompiledSpace::BinaryFormat::Stream stream(data);
        BW::CompiledSpace::StockTerrain2Data owner;
        const std::uint32_t valid=owner.read(&stream,strings);out.write(reinterpret_cast<const char*>(&valid),4);
        if(!valid)continue;
        assert(stream.tell()==stream.size());
        BW::ExternalArray<BW::CompiledSpace::Terrain2Types::BlockData> blocks;
        BW::ExternalArray<BW::uint32> grid,contents;
        BW::ExternalArray<BW::DataSpan> spans;BW::StaticLooseOctree tree;
        owner.publish(blocks,grid,tree,spans,contents);
        assert(blocks.size()==owner.source->blocks.size()&&grid.size()==owner.source->grid.size());
        assert(spans.size()==owner.source->spans.size()&&contents.size()==owner.source->contents.size());
        assert(tree.storage().nodes_.size()==owner.source->nodes.size());
        assert(!tree.storage().nodes_.ptr||reinterpret_cast<std::uintptr_t>(tree.storage().nodes_.ptr)%alignof(BW::StaticLooseOctree::Node)==0);
        assert(!spans.ptr||reinterpret_cast<std::uintptr_t>(spans.ptr)%alignof(BW::DataSpan)==0);
        if(blocks.size())assert(std::memcmp(blocks.ptr,owner.source->blocks.data(),blocks.size()*8)==0);
        if(spans.size())assert(std::memcmp(spans.ptr,owner.source->spans.data(),spans.size()*4)==0);
        if(contents.size())assert(std::memcmp(contents.ptr,owner.source->contents.data(),contents.size()*4)==0);
        auto lease=owner.source;std::weak_ptr<wot::StockTerrainBinary> weak=lease;
        // No array or retained settings may borrow bytes from the mapped stream.
        std::fill(data.begin(),data.end(),0);encode(out,*owner.source);
        owner.clear();assert(!weak.expired());lease.reset();assert(weak.expired());
    }
    return input.peek()==std::char_traits<char>::eof()&&out?0:2;
}
'''

LIFECYCLE = r'''
namespace BW {
struct FakeScene{
    bool present=false;int adds=0,removes=0;
    void addProvider(void*){assert(!present);present=true;++adds;}
    void removeProvider(void*){assert(present);present=false;++removes;}
};
FakeScene scene;
struct FakeSpace{FakeScene& scene(){return BW::scene;}};
namespace Terrain {
struct TerrainFinder{struct Details{void* pBlock_;const Matrix *pMatrix_,*pInvMatrix_;};};
struct FakeBlock{
    bool background=false;
    ~FakeBlock(){assert(!BW::scene.present);}
    bool doingBackgroundTask()const{return background;}
    float heightAt(float x,float z)const{return x*2+z*3;}
};
struct Settings{int activations=0;void setActiveRenderer(){++activations;}};
}
namespace CompiledSpace {
class Terrain2SceneProvider{
public:
    class Detail;
    struct BlockInstance{
        Matrix transform_,inverseTransform_;
        AABB worldBB_;
        std::shared_ptr<Terrain::FakeBlock> pBlock_;
    };
    StockTerrain2Data stockData_;
    bool stockBound_=false;
    BinaryFormat* pReader_=nullptr;BinaryFormat::Stream* pStream_=nullptr;
    Terrain2Types::Header* pHeader_=nullptr;
    ExternalArray<Terrain2Types::BlockData> blockData_;
    ExternalArray<uint32> blockGridLookup_,sceneContents_;
    LookUpTable<DataSpan,ExternalArray<DataSpan>> sceneOctreeContents_;
    StaticLooseOctree sceneOctree_;
    std::shared_ptr<Terrain::Settings> pTerrainSettings_;
    BW::string resourcesPath_;
    std::vector<BlockInstance> blockInstances_;
    std::atomic<size_t> numBlocksLoaded_{0};
    FakeSpace space_;FakeSpace& space(){return space_;}
    bool doBind();void doUnload();bool isValid()const;
    void freeBlockInstances();float percentLoaded()const;
    float findTerrainHeight(const Vector3&)const;
    bool findTerrainBlock(const Vector3&,Terrain::TerrainFinder::Details&);
};
@DETAIL@;
@METHODS@
}}
int main(){
    using namespace BW;using namespace BW::CompiledSpace;
    Terrain2SceneProvider p;
    // Unload before load must never consult an uninitialised scene/space lease.
    p.doUnload();p.doUnload();assert(scene.removes==0);
    p.stockData_.source=std::make_shared<wot::StockTerrainBinary>();
    p.pTerrainSettings_=std::make_shared<Terrain::Settings>();
    p.stockData_.header.blockSize_=100;p.stockData_.header.gridMinX_=-1;
    p.stockData_.header.gridMaxX_=-1;p.stockData_.header.gridMinZ_=2;p.stockData_.header.gridMaxZ_=2;
    p.pHeader_=&p.stockData_.header;
    p.stockData_.grid.push_back(0);
    p.blockGridLookup_.assignExternalStorage(p.stockData_.grid.data(),1,1);
    p.blockInstances_.resize(3);p.numBlocksLoaded_=3;
    p.blockInstances_[0].pBlock_=std::make_shared<Terrain::FakeBlock>();
    p.blockInstances_[1].pBlock_=std::make_shared<Terrain::FakeBlock>();
    p.blockInstances_[1].pBlock_->background=true;
    assert(p.percentLoaded()==1.f); // Native progress counts processed records once.
    p.numBlocksLoaded_=2;assert(p.percentLoaded()==2.f/3.f);
    p.numBlocksLoaded_=3;assert(p.percentLoaded()==1.f);
    auto& block=p.blockInstances_[0];
    block.transform_.position=Vector3(-100,12,200);
    block.inverseTransform_.position=Vector3(100,-12,-200);
    block.worldBB_.low=Vector3(-100,-100,200);block.worldBB_.high=Vector3(0,100,300);
    assert(p.findTerrainHeight(Vector3(-95,50,230))==112);
    assert(p.findTerrainHeight(Vector3(1,0,230))==FLT_MAX);
    assert(p.findTerrainHeight(Vector3(std::numeric_limits<float>::quiet_NaN(),0,230))==FLT_MAX);
    assert(p.findTerrainHeight(Vector3(1.e30f,0,230))==FLT_MAX);
    Terrain::TerrainFinder::Details details{};
    assert(p.findTerrainBlock(Vector3(-95,50,230),details));
    assert(details.pBlock_==block.pBlock_.get()&&details.pMatrix_==&block.transform_);
    p.stockData_.grid[0]=99;assert(p.findTerrainHeight(Vector3(-95,0,230))==FLT_MAX);
    p.stockData_.grid[0]=0;
    BinaryFormat reader;std::vector<unsigned char> bytes(1);BinaryFormat::Stream stream(bytes);
    p.pReader_=&reader;p.pStream_=&stream;
    p.doBind();p.doBind();assert(scene.adds==1&&p.pTerrainSettings_->activations==1);
    p.doUnload();assert(reader.closes==1&&scene.removes==1&&!p.isValid());
    assert(!p.pHeader_&&!p.blockGridLookup_.ptr&&!p.sceneOctree_.storage().nodes_.ptr);
    assert(p.findTerrainHeight(Vector3(-95,0,230))==FLT_MAX);
    p.doUnload();assert(reader.closes==1&&scene.removes==1&&p.percentLoaded()==1.f);
}
'''

SETTINGS_PROBE = r'''
#include <algorithm>
#define BW_BEGIN_NAMESPACE namespace BW {
#define BW_END_NAMESPACE }
#define BW_GUARD
#define MF_WATCH(...) ((void)0)
namespace BW {
using std::max;
class SafeReferenceCount{};
using DataSectionPtr=std::shared_ptr<int>;
template<class T>class SmartPointer{
    T* value_=nullptr;
public:
    SmartPointer()=default;SmartPointer(T* value):value_(value){}
    SmartPointer& operator=(T* value){value_=value;return *this;}
    T* operator->()const{return value_;}T* getObject()const{return value_;}
    explicit operator bool()const{return value_!=nullptr;}
};
namespace Moo {
class BaseTexture{public:void* pTexture()const{return const_cast<BaseTexture*>(this);}};
class TextureManager{
public:
    std::vector<std::string> requested;bool failAll=false,failMap=false;
    static TextureManager* instance(){static TextureManager value;return &value;}
    SmartPointer<BaseTexture> get(const std::string& name,bool animation,bool required,bool load,const char*){
        requested.push_back(name);assert(load);
        if(name.find("maps/landscape/")==0){assert(!animation&&!required);if(failMap)return nullptr;}
        else assert(animation&&required&&name=="system/maps/col_white.dds");
        static BaseTexture texture;return failAll?nullptr:&texture;
    }
};
}
struct BWResource{static bool exists;static bool fileExists(const std::string& name){
    assert(name=="maps/landscape/100_thepit/border.dds");return exists;}};
bool BWResource::exists=true;
struct AutoConfigString{
    explicit AutoConfigString(const char* key){assert(std::string(key)=="system/whiteBmp");}
    const std::string& value()const{static const std::string result="system/maps/col_white.dds";return result;}
};
AutoConfigString stockTerrainWhiteMap("system/whiteBmp");
namespace Terrain {
class BaseTerrainRenderer{};
class TerrainVertexLod:public BW::vector<float>{
    float start_=0,end_=0,size_=0;
public:
    void startBias(float f){start_=f;}float startBias()const{return start_;}
    void endBias(float f){end_=f;}float endBias()const{return end_;}
    void setBlockSize(float f){size_=f;}
};
}
}
#define private public
@HEADER@
#undef private
namespace BW {namespace Terrain {
struct TerrainGraphicsOptions{
    std::vector<TerrainSettings*> registered;
    float vertex=1,texture=1,normal=1,bump=1;bool enabled=true;
    static TerrainGraphicsOptions* instance(){static TerrainGraphicsOptions value;return &value;}
    void delSettings(TerrainSettings* p){registered.erase(std::remove(registered.begin(),registered.end(),p),registered.end());}
    void addSettings(TerrainSettings* p){assert(std::find(registered.begin(),registered.end(),p)==registered.end());registered.push_back(p);}
    bool bumpMapping()const{return enabled;}
    float lodModifier()const{return vertex;}float lodTextureModifier()const{return texture;}
    float lodNormalModifier()const{return normal;}float lodBumpModifier()const{return bump;}
};
struct Manager{static Manager* pInstance(){static Manager value;return &value;}};
struct TerrainRenderer2:BaseTerrainRenderer{
    static bool init(){return true;}
    static TerrainRenderer2* instance(){static TerrainRenderer2 renderer;return &renderer;}
    enum{RTM_DrawLOD=1,RTM_DrawBlend=2,RTM_DrawBump=4,RTM_PreLoadBlend=8,RTM_PreloadNormals=32};
};
const uint32 MINIMUM_TERRAIN_HEIGHT_MAP=32;
const uint32 TerrainSettings::TERRAIN2_VERSION=200;
float TerrainSettings::s_zoomFactor_=1,TerrainSettings::s_invZoomFactor_=1;
uint32 TerrainSettings::s_topVertexLod_=0;
bool TerrainSettings::s_useLodTexture_=true;
@METHODS@
bool TerrainSettings::useLodTexture(){return s_useLodTexture_;}
}}
int main(){
    using namespace BW;using namespace BW::Terrain;
    auto& graphics=*TerrainGraphicsOptions::instance();
    auto data=std::make_shared<wot::StockTerrainBinary>();
    data->header.blockSize=100;data->settings[0]=200;
    for(size_t i=2;i<10;++i)data->settings[i]=256;
    data->settings[3]=16;data->settings[4]=512;data->settings[5]=64;
    data->settings[27]=6;data->settings[30]=0;
    auto set=[&](size_t word,float f){std::memcpy(&data->settings[word],&f,4);};
    set(14,600);set(15,50);set(16,600);set(17,50);set(18,600);
    set(28,.8f);set(29,.99f);set(31,500);
    data->lodDistances={150,350,650,1400};
    const BW::vector<BW::string> resources={"maps/detail.dds","",""};
    const BW::map<BW::string,uint32> mappings={{"stone",3}};
    {
        TerrainSettings settings;
        assert(settings.initStockCompiled(data,resources,mappings));
        assert(settings.heightMapSize()==64&&settings.heightMapEditorSize()==512);
        assert(settings.numVertexLods()==6&&settings.defaultHeightMapLod()==0);
        assert(settings.vertexLod().size()==4&&settings.aoMapSize()==16);
        assert(settings.stockCompiledResources_==resources&&settings.stockCompiledMappings_==mappings);
        assert(graphics.registered.size()==1);
        assert(settings.stockTextureMask(649,700)==47); // LOD + blend + bump + stream.
        assert(settings.stockTextureMask(650,700)==47);
        assert(settings.stockTextureMask(651,700)==1); // Inclusive original boundary.
        graphics.vertex=.8f;graphics.texture=.8f;graphics.bump=.6f;
        assert(settings.initStockCompiled(data,resources,mappings));
        assert(graphics.registered.size()==1&&settings.vertexLod().size()==4);
        assert(settings.vertexLod()[0]==120&&settings.vertexLod()[1]==297);
        assert(settings.lodTextureStart()==480&&settings.bumpFadingStart()==360);
        TerrainSettings::s_invZoomFactor_=2;
        assert(settings.lodTextureStart()==960&&settings.lodTextureDistance()==40);
        assert(settings.stockTextureMask(999,1100)==43); // Bump ends at 750.
        assert(settings.stockTextureMask(1001,1100)==1);
        TerrainSettings::s_useLodTexture_=false;
        assert(settings.stockTextureMask(1001,1100)==42); // Real blend-only mode.
        assert(settings.stockCompiledData()==data.get());
        assert(settings.loadStockCompiledResources("spaces/100_thepit/"));
        auto& textures=*Moo::TextureManager::instance();
        assert(textures.requested.back()=="maps/landscape/100_thepit/border.dds");
        assert(settings.initStockCompiled(data,resources,mappings));
        BWResource::exists=false;
        assert(settings.loadStockCompiledResources("spaces/100_thepit/"));
        assert(textures.requested.back()=="system/maps/col_white.dds");
        assert(settings.initStockCompiled(data,resources,mappings));
        textures.failAll=true;assert(!settings.loadStockCompiledResources("spaces/100_thepit/"));
    }
    assert(graphics.registered.empty());
    auto morph=wot::StockTerrainBinary::lodParameters(400,600);
    assert(morph[0]==.005f&&morph[1]==-2.f&&morph[2]==0&&morph[3]==0);
    set(20,-250);set(21,-300);set(22,350);set(23,300);set(24,.77f);set(25,.3f);
    const auto border=data->borderParameters();
    assert(border.blend==.6f&&border.outer[0]<-250&&border.outer[2]>350);
    assert(border.inverseDifference[0]<0&&border.inverseDifference[2]>0);
}
'''

SHADER_PROBE = r'''
#include "wot/stock_shader_binary.hpp"
#include "wot/stock_effect_binding.hpp"
#include "wot/stock_shader_runtime.hpp"
#include <cassert>
#include <fstream>
#include <iterator>
#include <set>
bool hasUniform(const wot::StockEffectBinary& binary,const std::string& name){
    for(const auto& buffer:binary.uniform_buffers)for(const auto& uniform:buffer.uniforms)
        if(uniform.name==name)return true;
    return false;
}
void set(wot::StockEffectBinding& binding,const wot::StockEffectBinary& binary,
    const std::string& name,std::int32_t value){
    for(size_t b=0;b<binary.uniform_buffers.size();++b)
        for(size_t u=0;u<binary.uniform_buffers[b].uniforms.size();++u)
            if(binary.uniform_buffers[b].uniforms[u].name==name){
                std::string error;assert(binding.set_uniform(b,u,&value,4,std::nullopt,error));return;
            }
    assert(false);
}
int main(int argc,char** argv){
    assert(argc==5);
    for(int i=1;i<argc;++i){
        std::ifstream input(argv[i],std::ios::binary);
        std::vector<unsigned char> data((std::istreambuf_iterator<char>(input)),{});
        auto binary=std::make_shared<wot::StockEffectBinary>();std::string error;
        assert(wot::decode_stock_dx9_effect(data.data(),data.size(),*binary,error));
        assert(hasUniform(*binary,"lodParams")&&hasUniform(*binary,"terrainScale")&&hasUniform(*binary,"world"));
        wot::StockEffectUniformPool pool;wot::StockEffectUniformContext context(pool);
        wot::StockEffectBinding binding(binary,context);
        const std::vector<std::string> names=i<=2?std::vector<std::string>{"Color","Reflection"}:
            i==3?std::vector<std::string>{"ZPass"}:std::vector<std::string>{"ShadowCast"};
        if(i<=2){
            assert(hasUniform(*binary,"outerBorder")&&hasUniform(*binary,"invBorderDiffMul2Blend"));
            bool map=false;for(const auto& view:binary->resource_views)if(view.name=="borderBlendMap")map=true;
            assert(map);
        }
        if(i==1){
            for(const char* name:{"g_useBump","g_bumpMaskIndex","useMultipassBlendingBranch",
                    "enablePuddles","puddlesColor","useDetailNormal"})assert(hasUniform(*binary,name));
            std::set<unsigned> shaders;
            for(unsigned value=0;value<256;++value){
                set(binding,*binary,"g_bumpMaskIndex",value&15);
                set(binding,*binary,"g_useBump",(value>>4)&1);
                set(binding,*binary,"enablePuddles",(value>>5)&1);
                set(binding,*binary,"useMultipassBlending",(value>>6)&1);
                set(binding,*binary,"useMultipassBlendingBranch",(value>>6)&1);
                set(binding,*binary,"g_PBSResourcesReady",(value>>7)&1);
                for(const auto& name:names){
                    size_t index=0;while(index<binary->techniques.size()&&binary->techniques[index].name!=name)++index;
                    assert(index<binary->techniques.size());wot::StockDx9PassSelection selection;
                    assert(binding.select_pass(index,0,selection,error)&&selection.vertex_shader&&selection.pixel_shader);
                    assert(*selection.vertex_shader<binary->shaders.size()&&*selection.pixel_shader<binary->shaders.size());
                    shaders.insert(*selection.pixel_shader);
                }
            }
            assert(shaders.size()>16);
        }
        for(const auto& name:names){
            size_t index=0;while(index<binary->techniques.size()&&binary->techniques[index].name!=name)++index;
            assert(index<binary->techniques.size());wot::StockDx9PassSelection selection;
            assert(binding.select_pass(index,0,selection,error)&&selection.vertex_shader);
        }
        if(i==3)assert(hasUniform(*binary,"g_useHoles"));
    }
}
'''


def without_includes(text):
    return '\n'.join(line for line in text.splitlines()
                     if not line.startswith(('#include', '#pragma once')))


def source_function(text, signature):
    start = text.index(signature)
    body = text.index('{', start)
    end, depth = body + 1, 1
    while depth:
        depth += (text[end] == '{') - (text[end] == '}')
        end += 1
    return text[start:end]


def fixture():
    record = lambda data: struct.pack('<I', len(data)) + data
    array = lambda stride, data: struct.pack('<II', stride, len(data) // stride) + data
    settings = [0] * 37
    settings[:10] = [200, 27, 256, 16, 512, 64, 128, 64, 128, 256]
    settings[27] = 6
    for word, value in ((14, 600), (15, 50), (16, 600), (17, 50), (18, 600),
                        (19, 50), (24, 1), (25, .4), (28, .8), (29, .99), (31, 500)):
        settings[word] = struct.unpack('<I', struct.pack('<f', value))[0]
    parts = [record(struct.pack('<f4i3I', 100, -1, -1, 2, 2, 0x8100, 0x8101, 0x8101)),
             array(8, struct.pack('<Ihh', 0x80000001, -1, 2)),
             array(4, struct.pack('<I', 0)), record(struct.pack('<37I', *settings)),
             array(4, struct.pack('<4f', 150, 350, 650, 1400)),
             array(8, struct.pack('<II', 0x8200, 3)),
             record(struct.pack('<I4fHH', 4, -50, 0, 250, 100, 1, 0)),
             array(24, struct.pack('<6f', -100, 0, 200, 0, 20, 300)),
             array(12, struct.pack('<3f', -50, 10, 250)),
             array(16, struct.pack('<8H', *([0xffff] * 8))),
             array(4, struct.pack('<I', 0)), array(2, struct.pack('<H', 0xffff)),
             array(4, struct.pack('<HH', 0, 0)), array(4, struct.pack('<I', 0))]
    offsets, current = [], 0
    for part in parts:
        offsets.append(current)
        current += len(part)
    names = {0x8100: b'maps/detail.dds', 0x8101: b'',
             0x80000001: b'fffafffao.cdata_processed/terrain2', 0x8200: b'Mat/Stone'}
    return b''.join(parts), offsets, names


class StockSpaceTerrainTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        archive = Path(os.environ.get('WOT_PUBLIC_ENGINE_ARCHIVE', '/tmp/wot-bigworld-engine-14.4.1.tar.gz'))
        cls.compiler = shutil.which('c++')
        if not archive.is_file() or not cls.compiler:
            raise unittest.SkipTest('Pinned public engine archive and C++ compiler are required')
        cls.temporary = tempfile.TemporaryDirectory(prefix='stock-space-terrain-')
        cls.root = Path(cls.temporary.name)
        cls.original = {}
        with tarfile.open(archive) as source:
            for name in FILES:
                member = next(n for n in source.getnames() if n.endswith('/programming/bigworld/' + name))
                path = cls.root / name
                path.parent.mkdir(parents=True, exist_ok=True)
                cls.original[name] = source.extractfile(member).read()
                path.write_bytes(cls.original[name])
        patch.patch(cls.root)
        before = {p: p.read_bytes() for p in cls.root.rglob('*') if p.is_file()}
        patch.patch(cls.root)
        assert before == {p: p.read_bytes() for p in cls.root.rglob('*') if p.is_file()}
        cls.prelude = PRELUDE.replace('@TYPES@', without_includes(
            (cls.root / FILES[0]).read_text())).replace('@ADAPTER@', without_includes(
            (ROOT / 'bigworld/stock_space_terrain.hpp').read_text()))
        cls.binary = cls.compile('wire', cls.prelude + WIRE_MAIN)

    @classmethod
    def compile(cls, name, source, extra_sources=()):
        path = cls.root / (name + '.cpp')
        path.write_text(source)
        binary = cls.root / name
        flags = ['-std=c++17', '-Wall', '-Wextra', '-Werror', '-O1', '-Wno-unused-const-variable']
        if os.name != 'nt':
            flags += ['-fsanitize=address,undefined', '-fno-omit-frame-pointer']
        result = subprocess.run([cls.compiler] + flags + ['-I', str(ROOT / 'include'),
            str(path)] + [str(p) for p in extra_sources] + ['-o', str(binary)], capture_output=True, text=True)
        if result.returncode:
            raise AssertionError(result.stdout + result.stderr)
        return binary

    @classmethod
    def tearDownClass(cls):
        cls.temporary.cleanup()

    def run_cases(self, cases, fixture_output=False):
        data = bytearray(struct.pack('<I', len(cases)))
        expected = bytearray()
        for payload, valid, names in cases:
            data += struct.pack('<I', len(payload)) + payload + struct.pack('<I', len(names))
            for key, name in sorted(names.items()):
                data += struct.pack('<II', key, len(name)) + name
            expected += struct.pack('<I', valid)
            if valid:
                expected += payload
        source, result = self.root / 'input.bin', self.root / 'output.bin'
        source.write_bytes(data)
        result_run = subprocess.run([str(self.binary), str(source), str(result)], capture_output=True, text=True)
        self.assertEqual(result_run.returncode, 0, result_run.stdout + result_run.stderr)
        self.assertEqual(result.read_bytes(), expected)
        destination = os.environ.get('WOT_SPACE_TERRAIN_FIXTURE_OUT') if fixture_output else None
        if destination:
            destination = Path(destination)
            destination.mkdir(parents=True, exist_ok=True)
            shutil.copy2(self.root / 'wire.cpp', destination / 'probe.cpp')
            (destination / 'inputs.bin').write_bytes(data)
            (destination / 'expected.bin').write_bytes(expected)

    def test_complete_layout_bounds_and_resource_key_resolution(self):
        data, offsets, names = fixture()
        cases = [(data, True, names)]
        cases.extend((data[:size], False, names) for size in range(len(data)))
        for offset in offsets:
            changed = bytearray(data)
            struct.pack_into('<I', changed, offset, 0xffffffff)
            cases.append((changed, False, names))
        for index in (1, 2, 4, 5, 7, 8, 9, 10, 11, 12, 13):
            changed = bytearray(data)
            struct.pack_into('<I', changed, offsets[index] + 4, 0xffffffff)
            cases.append((changed, False, names))
        for offset, fmt, value in ((offsets[2] + 8, '<I', 1),
                (24, '<I', 0),
                (offsets[9] + 8, '<H', 0), (offsets[10] + 8, '<I', 1),
                (offsets[12] + 10, '<H', 1), (offsets[13] + 8, '<I', 1)):
            changed = bytearray(data)
            struct.pack_into(fmt, changed, offset, value)
            cases.append((changed, False, names))
        cases.append((data + b'ignored-tail', False, names))
        cases.append((data, False, {k: v for k, v in names.items() if k != 0x8100}))
        cases.append((data, False, {k: v for k, v in names.items() if k != 0x8200}))
        # A missing block key is contained by the original per-block load path.
        cases.append((data, True, {k: v for k, v in names.items() if k != 0x80000001}))
        self.run_cases(cases)

    def test_all_original_terrain_sections_and_owned_spatial_tail(self):
        client = os.environ.get('WOT_0922_CLIENT')
        if not client:
            self.skipTest('WOT_0922_CLIENT is required for original terrain resources')
        sys.path.insert(0, str(REPOSITORY / 'tools'))
        from space_bin_0922 import CompiledSpace
        cases, count = [], 0
        for package in (Path(client) / 'res/packages').glob('*.pkg'):
            with zipfile.ZipFile(package) as archive:
                for name in archive.namelist():
                    if not name.endswith('/space.bin'):
                        continue
                    space = CompiledSpace(archive.read(name))
                    data = space.section_data('BWST')
                    stride, rows = struct.unpack_from('<II', data)
                    self.assertEqual(stride, 12)
                    pool = 12 + rows * 12
                    strings = {}
                    for index in range(rows):
                        key, offset, size = struct.unpack_from('<III', data, 8 + index * 12)
                        strings[key] = data[pool + offset:pool + offset + size]
                    data = space.section_data('BWT2')
                    count += struct.unpack_from('<I', data, 40)[0]
                    cases.append((data, True, strings))
        self.assertTrue(cases)
        self.run_cases(cases, fixture_output=True)
        print('Production BWT2 reader round-tripped %d original spaces, %d terrain blocks and every spatial record' % (len(cases), count))

    def test_production_bind_unload_progress_and_height_finder_lifetimes(self):
        source = (self.root / FILES[2]).read_text()
        detail = source_function(source, 'class Terrain2SceneProvider::Detail')
        methods = '\n'.join(source_function(source, signature) for signature in (
            'bool Terrain2SceneProvider::doBind()', 'void Terrain2SceneProvider::doUnload()',
            'bool Terrain2SceneProvider::isValid() const',
            'void Terrain2SceneProvider::freeBlockInstances()',
            'float Terrain2SceneProvider::percentLoaded() const',
            'float Terrain2SceneProvider::findTerrainHeight(',
            'bool Terrain2SceneProvider::findTerrainBlock('))
        binary = self.compile('lifecycle', self.prelude + LIFECYCLE.replace(
            '@DETAIL@', detail).replace('@METHODS@', methods))
        result = subprocess.run([str(binary)], capture_output=True, text=True)
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)

    def test_actual_public_settings_header_and_production_authored_lod_consumers(self):
        source = (self.root / 'lib/terrain/terrain_settings.cpp').read_text()
        methods = '\n'.join(source_function(source, signature) for signature in (
            'TerrainSettings::TerrainSettings()', 'TerrainSettings::~TerrainSettings()',
            'bool TerrainSettings::initStockCompiled(', 'bool TerrainSettings::loadStockCompiledResources(',
            'uint8 TerrainSettings::stockTextureMask(',
            'void TerrainSettings::applyLodModifier(', 'void TerrainSettings::applyLodTextureModifier(',
            'void TerrainSettings::applyLodNormalModifier(', 'void TerrainSettings::applyBumpMappingModifier(',
            'float TerrainSettings::calculateMinDistance() const', 'uint32 TerrainSettings::topVertexLod() const'))
        header = without_includes((self.root / 'lib/terrain/terrain_settings.hpp').read_text())
        binary = self.compile('settings', self.prelude + SETTINGS_PROBE.replace(
            '@HEADER@', header).replace('@METHODS@', methods))
        result = subprocess.run([str(binary)], capture_output=True, text=True)
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)

    def test_original_render_effects_techniques_and_actual_bump_branch_domain(self):
        client = os.environ.get('WOT_0922_CLIENT')
        if not client:
            self.skipTest('WOT_0922_CLIENT is required for original terrain effects')
        import io
        paths = []
        with zipfile.ZipFile(Path(client) / 'res/packages/shaders.pkg') as package:
            for name in ('terrain2', 'terrain2_lod_texture', 'terrain2_z_pass', 'terrain2_shadow_cast'):
                with zipfile.ZipFile(io.BytesIO(package.read('shaders/terrain/' + name + '.10.dx9.fxo'))) as effect:
                    path = self.root / (name + '.effect')
                    path.write_bytes(effect.read('effect'))
                    paths.append(str(path))
        binary = self.compile('effects', SHADER_PROBE, [ROOT / 'src' / name for name in (
            'stock_shader_binary.cpp', 'stock_effect_binding.cpp', 'stock_shader_runtime.cpp')])
        result = subprocess.run([str(binary)] + paths, capture_output=True, text=True)
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)

    def test_patch_transaction_and_original_quality_and_legacy_boundaries(self):
        settings = (self.root / 'lib/terrain/terrain_settings.cpp').read_text()
        legacy = source_function(settings, 'bool TerrainSettings::init(').replace(patch.RESET_COMPILED, '')
        original = source_function(self.original['lib/terrain/terrain_settings.cpp'].decode('utf-8-sig'),
            'bool TerrainSettings::init(')
        self.assertEqual(legacy, original)
        quality = (self.root / 'lib/terrain/terrain_graphics_options.cpp').read_text()
        rows = re.findall(r'graphicsOptions_\.push_back\(GraphicsOption\((true|false),\s*(\d+),\s*([.\df]+),\s*([.\df]+),\s*([.\df]+),\s*([.\df]+)', quality)
        # Original adaed0 constructs these real GraphicsOption values.
        self.assertEqual([(row[0] == 'true', int(row[1])) + tuple(float(v.rstrip('f')) for v in row[2:]) for row in rows],
            [(True, 0, 1, 1, 1, 1), (True, 0, .8, .8, .8, .6),
             (True, 0, .7, .6, .6, .2), (False, 0, .6, .4, .4, 1),
             (False, 1, .6, .2, .2, 1)])
        target = self.root / 'lib/terrain/terrain2/terrain_renderer2.cpp'
        valid = target.read_bytes()
        target.write_bytes(valid.replace(b'stockUseBump_ = ', b'stockUseBumpBroken_ = ', 1))
        sources = [self.root / name for name in FILES]
        before = {p: p.read_bytes() for p in sources}
        try:
            with self.assertRaises(ValueError):
                patch.patch(self.root)
            self.assertEqual({p: p.read_bytes() for p in sources}, before)
        finally:
            target.write_bytes(valid)


if __name__ == '__main__':
    unittest.main()
