"""Compile the production loader and exercise synchronous scene notifications.

The narrow engine shim reproduces ILoader's status guards and DynamicScene's
notification ordering. It proves loader ownership, not D3D rendering/BSP data.
"""
from pathlib import Path
import shutil
import subprocess
from native_test_compile_flags import native_test_compile_flags
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]

ENGINE = r'''
#pragma once
#include <cassert>
#include <cstdint>
#include <cstdio>
#include <functional>
#include <map>
#include <memory>
#include <set>
#include <string>
#include <vector>
#define BW_BEGIN_NAMESPACE namespace BW {
#define BW_END_NAMESPACE }
#define ERROR_MSG(...) std::fprintf(stderr,__VA_ARGS__)
namespace BW {
using uint32=std::uint32_t;
using string=std::string;
struct Matrix { float x=0; };
struct BoundingBox { bool empty=true;float minimum=0,maximum=0; };
struct DataSection {};
using DataSectionPtr=std::shared_ptr<DataSection>;
struct SceneObject { unsigned id=0; };
struct DynamicObjectHandle { unsigned id=0; };
static std::set<unsigned> liveObjects;
static unsigned nextObject=1;
struct SpatialQueryOperation {
 bool isSupported(const SceneObject& object) { return liveObjects.count(object.id)!=0; }
 void getWorldTransform(const SceneObject& object,Matrix* world) { assert(isSupported(object));world->x=float(object.id); }
 void getWorldVisibilityBoundingBox(const SceneObject& object,BoundingBox* bounds) {
  assert(isSupported(object));bounds->empty=false;bounds->minimum=float(object.id)-1;bounds->maximum=float(object.id)+1;
 }
};
struct Scene { SpatialQueryOperation spatial;template<class T>T* getObjectOperation(){return &spatial;} };
struct DynamicSceneProvider {
 struct Node { SceneObject object;Matrix world;BoundingBox collision,visibility; };
 std::map<unsigned,Node> nodes;
 std::function<void()> onAdd,onRemove;
 unsigned next=1;
 DynamicObjectHandle addObject(const SceneObject& object,const Matrix& world,const BoundingBox& collision,const BoundingBox& visibility) {
  assert(liveObjects.count(object.id));assert(world.x==float(object.id));
  assert(!visibility.empty&&visibility.minimum==world.x-1&&visibility.maximum==world.x+1);
  DynamicObjectHandle handle{next++};nodes.emplace(handle.id,Node{object,world,collision,visibility});
  // Native add notifies after insertion, before returning its handle.
  auto observer=std::move(onAdd);onAdd=nullptr;if(observer)observer();return handle;
 }
 void removeObject(const DynamicObjectHandle& handle) {
  auto found=nodes.find(handle.id);assert(found!=nodes.end());assert(liveObjects.count(found->second.object.id));
  // Native remove notifies while the object and its native handle still exist.
  auto observer=std::move(onRemove);onRemove=nullptr;if(observer)observer();
  found=nodes.find(handle.id);assert(found!=nodes.end());assert(liveObjects.count(found->second.object.id));nodes.erase(found);
 }
};
struct ClientSpace { bool clearedFlag=false;virtual ~ClientSpace(){}bool cleared()const{return clearedFlag;} };
// stock_space_sounds: one owner per live space, lookup-only named sounds,
// INVALID handles ignored by register/remove, every valid handle removed once.
struct StockSpaceSoundOwner { ClientSpace* space;std::map<std::uint64_t,std::string> live;std::map<std::uint64_t,std::pair<const void*,uint32>> objects; };
using StockSpaceSounds=std::shared_ptr<StockSpaceSoundOwner>;
using StockSpaceSoundHandle=std::uint64_t;
static const StockSpaceSoundHandle INVALID_STOCK_SPACE_SOUND=~StockSpaceSoundHandle(0);
static std::map<ClientSpace*,std::weak_ptr<StockSpaceSoundOwner>> soundOwners;
static std::set<StockSpaceSoundHandle> liveSounds;
static StockSpaceSoundHandle nextSound=1;
inline StockSpaceSounds acquireStockSpaceSounds(ClientSpace* space){
 if(!space||space->cleared())return {};
 auto owner=soundOwners[space].lock();
 if(!owner){owner=std::make_shared<StockSpaceSoundOwner>();owner->space=space;soundOwners[space]=owner;}
 return owner;
}
inline StockSpaceSoundHandle addStockSpaceNamedSound(const StockSpaceSounds& owner,const string& name,const Matrix& world){
 assert(world.x>0);
 if(!owner||name.compare(0,5,"sound")!=0)return INVALID_STOCK_SPACE_SOUND;
 const auto handle=nextSound++;owner->live[handle]=name;liveSounds.insert(handle);return handle;
}
inline void removeStockSpaceSound(const StockSpaceSounds& owner,StockSpaceSoundHandle handle){
 if(!owner||handle==INVALID_STOCK_SPACE_SOUND)return;
 assert(owner->live.erase(handle)==1&&liveSounds.erase(handle)==1);owner->objects.erase(handle);
}
inline void registerStockSpaceObjectSound(const StockSpaceSounds& owner,const void* mapping,bool tree,uint32 index,StockSpaceSoundHandle handle){
 if(!owner||handle==INVALID_STOCK_SPACE_SOUND)return;
 assert(tree&&mapping&&owner->live.count(handle)&&!owner->objects.count(handle));owner->objects[handle]={mapping,index};
}
namespace CompiledSpace {
struct BinaryFormat { bool valid=true;unsigned count=3; };
struct StringTable {};
struct CompiledSpace:ClientSpace {
 Scene scene_;DynamicSceneProvider dynamic;
 Scene& scene(){return scene_;}DynamicSceneProvider& dynamicScene(){return dynamic;}
};
class ILoader {
 enum Status {UNLOADED,LOADING,LOADED,FAILED};Status status=UNLOADED;
 public:virtual ~ILoader(){};virtual bool loadMustSucceed()const{return false;}
 bool loadFromSpace(ClientSpace* space,BinaryFormat& file,const DataSectionPtr& settings,const Matrix& transform,const StringTable& strings){
  assert(status!=LOADING&&status!=LOADED);status=LOADING;bool ok=doLoadFromSpace(space,file,settings,transform,strings);status=ok?LOADED:FAILED;return ok;
 }
 bool bind(){if(status==FAILED)return false;assert(status==LOADED);return doBind();}
 void unload(){doUnload();status=UNLOADED;}
 float loadStatus()const{return status==UNLOADED?0:status==LOADING?percentLoaded():1;}
 protected:
 virtual bool doLoadFromSpace(ClientSpace*,BinaryFormat&,const DataSectionPtr&,const Matrix&,const StringTable&)=0;
 virtual bool doBind(){return true;}virtual void doUnload()=0;virtual float percentLoaded()const=0;
};
struct StockTreeSceneHandler {
 std::vector<SceneObject> values;std::vector<bool> available;bool loaded=false;
 ~StockTreeSceneHandler(){unload();}
 bool load(BinaryFormat& file,const StringTable&,const DataSectionPtr&,uint32 mask){
  assert(!loaded);if(!file.valid)return false;loaded=true;
  for(unsigned i=0;i<file.count;++i){values.push_back(SceneObject{nextObject++});liveObjects.insert(values.back().id);available.push_back((mask&(i%2?2U:1U))!=0);}
  return true;
 }
 bool bind(){return loaded;}float loadPercent()const{return loaded?1.f:0.f;}
 size_t objectCount()const{return values.size();}const SceneObject* objects()const{return values.data();}
 bool objectAvailable(size_t i)const{return available[i];}
 void worldCollisionBounds(size_t i,BoundingBox& bounds)const {assert(i<values.size());bounds=BoundingBox{};}
 // Every third original tree resource has no sound definition.
 string soundResource(size_t i)const {assert(i<values.size());return i%3==2?"silent":"sound"+std::to_string(i);}
 void unload(){for(auto object:values)assert(liveObjects.erase(object.id)==1);values.clear();available.clear();loaded=false;}
};
}
}
'''

TEST = r'''
#include "stock_tree_scene_loader.cpp"
using namespace BW;using namespace BW::CompiledSpace;
int main(){
 BW::CompiledSpace::CompiledSpace space;BinaryFormat file;StringTable strings;DataSectionPtr settings;Matrix mapping;mapping.x=800;
 StockTreeSceneLoader loader(1);
 assert(loader.loadMustSucceed());
 assert(loader.loadFromSpace(&space,file,settings,mapping,strings));assert(loader.loadStatus()==1);
 assert(space.dynamic.nodes.empty());assert(loader.bind());assert(space.dynamic.nodes.size()==2);
 for(const auto& item:space.dynamic.nodes)assert(item.second.collision.empty);
 // Mask 1 binds objects 0 and 2; object 2 has no sound definition.
 {auto owner=soundOwners[&space].lock();assert(owner&&owner->live.size()==1&&owner->objects.size()==1);
  assert(owner->objects.begin()->second.first==&file&&owner->objects.begin()->second.second==0);}
 assert(loader.bind()&&space.dynamic.nodes.size()==2);
 loader.unload();assert(space.dynamic.nodes.empty()&&liveObjects.empty()&&loader.loadStatus()==0);
 assert(liveSounds.empty()&&soundOwners[&space].expired());
 loader.unload();assert(space.dynamic.nodes.empty());
 StockTreeSceneLoader invisible(0);
 assert(invisible.loadFromSpace(&space,file,settings,mapping,strings)&&invisible.bind());
 assert(space.dynamic.nodes.empty());invisible.unload();assert(liveObjects.empty());
 file.valid=false;assert(!loader.loadFromSpace(&space,file,settings,mapping,strings));assert(!loader.bind());loader.unload();
 file.valid=true;ClientSpace wrong;assert(!loader.loadFromSpace(&wrong,file,settings,mapping,strings));loader.unload();
 // A synchronous add notification can unload before add returns its handle.
 assert(loader.loadFromSpace(&space,file,settings,mapping,strings));
 space.dynamic.onAdd=[&]{loader.unload();};
 assert(!loader.bind());assert(space.dynamic.nodes.empty()&&liveObjects.empty()&&liveSounds.empty());
 // Replacing the load during that notification must retain only the new
 // instances. The outer bind may remove only its own pending handle.
 assert(loader.loadFromSpace(&space,file,settings,mapping,strings));
 space.dynamic.onAdd=[&]{loader.unload();assert(loader.loadFromSpace(&space,file,settings,mapping,strings));assert(loader.bind());};
 assert(!loader.bind());assert(space.dynamic.nodes.size()==2&&liveObjects.size()==3&&loader.loadStatus()==1);
 assert(liveSounds.size()==1);
 space.dynamic.onRemove=[&]{loader.unload();};loader.unload();
 assert(space.dynamic.nodes.empty()&&liveObjects.empty()&&liveSounds.empty());
 // Native ILoader writes UNLOADED after doUnload returns. A nested fresh
 // load at this remove boundary must fail, never publish a lost owner.
 assert(loader.loadFromSpace(&space,file,settings,mapping,strings)&&loader.bind());
 space.dynamic.onRemove=[&]{loader.unload();assert(!loader.loadFromSpace(&space,file,settings,mapping,strings));};
 loader.unload();assert(space.dynamic.nodes.empty()&&liveObjects.empty()&&loader.loadStatus()==0);
 assert(loader.loadFromSpace(&space,file,settings,mapping,strings)&&loader.bind());
 loader.unload();assert(space.dynamic.nodes.empty()&&liveObjects.empty()&&liveSounds.empty());
 // A cleared space has no sound owner; trees still bind and retire.
 space.clearedFlag=true;
 assert(loader.loadFromSpace(&space,file,settings,mapping,strings)&&loader.bind());
 assert(space.dynamic.nodes.size()==2&&liveSounds.empty()&&soundOwners[&space].expired());
 loader.unload();assert(space.dynamic.nodes.empty()&&liveObjects.empty());
 std::puts("Production tree-loader notification, mask, failure and teardown tests passed");
}
'''


class StockTreeSceneLoaderTests(unittest.TestCase):
    def test_actual_loader_with_guarded_engine_notification_order(self):
        if not shutil.which('c++'):
            raise unittest.SkipTest('C++ compiler unavailable: c++')
        with tempfile.TemporaryDirectory(prefix='stock-tree-loader-') as directory:
            directory = Path(directory)
            for relative in ('pch.hpp', 'stock_tree_scene.hpp', 'stock_space_sounds.hpp', 'compiled_space/loader.hpp',
                             'compiled_space/compiled_space.hpp', 'scene/scene.hpp', 'scene/scene_object.hpp',
                             'scene/spatial_query_operation.hpp', 'space/dynamic_scene_provider.hpp'):
                path = directory / relative
                path.parent.mkdir(parents=True, exist_ok=True)
                path.write_text('#include "engine.hpp"\n')
            for name in ('stock_tree_scene_loader.cpp', 'stock_tree_scene_loader.hpp'):
                (directory / name).write_bytes((ROOT / 'bigworld' / name).read_bytes())
            (directory / 'engine.hpp').write_text(ENGINE)
            (directory / 'test.cpp').write_text(TEST)
            binary = directory / 'probe'
            subprocess.run(['c++', *native_test_compile_flags('c++'), '-std=c++14', '-O1', '-g', '-Wall', '-Wextra', '-Werror',
                             '-fno-omit-frame-pointer',
                            '-I'+str(directory), str(directory / 'test.cpp'), '-o', str(binary)], check=True)
            subprocess.run([str(binary)], check=True)


if __name__ == '__main__':
    unittest.main()
