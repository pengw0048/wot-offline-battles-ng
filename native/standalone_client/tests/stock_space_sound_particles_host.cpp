// Original/patched public particle lifecycle bodies; bounded scene/SDK boundaries.
#include <algorithm>
#include <cassert>
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
#define MF_ASSERT assert
namespace BW
{
struct ClientSpace {bool dead=false;bool cleared()const{return dead;}};
using ClientSpacePtr=std::shared_ptr<ClientSpace>;
}
@SOUND_SUPPORT@
namespace BW
{
struct AABB {AABB(const Vector3&,const Vector3&) {}};
struct SceneObject
{
    struct Flags {void isDynamic(bool){}} flags_;
    template<class T>void type(){}
    Flags& flags(){return flags_;}
    template<class T>void handle(T*){}
};
struct DynamicObjectHandle
{
    unsigned id=0;
    static DynamicObjectHandle INVALID(){return {};}
    bool operator!=(const DynamicObjectHandle& other)const{return id!=other.id;}
};
struct DynamicSceneProvider
{
    unsigned next=0,removed=0,invalidRemovals=0;
    std::set<unsigned> handles;
    std::function<void()> onAdd,onRemove;
    DynamicObjectHandle addObject(const SceneObject&,const Matrix&,const AABB&,const AABB&)
    {auto id=++next;handles.insert(id);if(auto callback=onAdd)callback();return {id};}
    void removeObject(DynamicObjectHandle handle)
    {if(!handles.erase(handle.id)){++invalidRemovals;return;}++removed;if(auto callback=onRemove)callback();}
};
struct TestParticleSystem
{
    unsigned detaches=0,leaves=0;
    void detach(){++detaches;}void leaveWorld(){++leaves;}
};
struct TestReader {unsigned closes=0;void closeSection(void*){++closes;}};
namespace CompiledSpace
{
struct ParticleSystemLoader
{
    struct Definition{Matrix worldTransform_;};
    struct Instance
    {
        Definition* pDef_=NULL;
        std::shared_ptr<TestParticleSystem> system_;
        DynamicObjectHandle dynamicObjectHandle_;
        BW::string soundResource_;
        StockSpaceSoundHandle soundHandle_=INVALID_STOCK_SPACE_SOUND;
    };
    DynamicSceneProvider& dynamicScene_;
    TestReader* pReader_=NULL;void* pStream_=NULL;
    struct Array {unsigned resets=0;void reset(){++resets;}}systemData_;
    ClientSpace* soundSpace_=NULL;StockSpaceSounds spaceSounds_;
    unsigned soundBindDepth_=0,soundUnloadDepth_=0;
    bool soundBound_=false,soundUnloadPending_=false;
    std::vector<Instance> instances_;
    explicit ParticleSystemLoader(DynamicSceneProvider& scene):dynamicScene_(scene){}
    bool doBind();void doUnload();void addToScene();void removeFromScene();void freeParticleSystems();
};
@METHODS@
}
}
int main()
{
    using namespace BW;using namespace BW::CompiledSpace;
    (void)&BW::fail;
    auto space=std::make_shared<ClientSpace>();DynamicSceneProvider scene;
    ParticleSystemLoader loader(scene);TestReader reader;int stream=0;
    auto definition=std::make_shared<Definition>();definition->name="particle.xml";definition->events={"event"};
    database.definitions[definition->name]=definition;
    ParticleSystemLoader::Definition data;
    auto system=std::make_shared<TestParticleSystem>();
    auto prepare=[&](){loader.pReader_=&reader;loader.pStream_=&stream;loader.soundSpace_=space.get();
        loader.instances_.resize(2);for(auto& instance:loader.instances_)
        {instance.pDef_=&data;instance.system_=system;instance.soundResource_="particle.xml";}};
    prepare();
#ifdef LEGACY_PARTICLE_LIFECYCLE
    scene.onAdd=[&](){scene.onAdd={};loader.doUnload();};
    assert(loader.doBind()&&loader.instances_.empty()&&scene.handles.size()==1);
    std::puts("Original particle bind publishes an orphan handle after synchronous unload: reproduced");return 0;
#else
    scene.onAdd=[&](){scene.onAdd={};loader.doUnload();assert(loader.soundUnloadPending_&&loader.instances_.size()==2);};
    assert(!loader.doBind()&&loader.instances_.empty()&&scene.handles.empty());
    assert(reader.closes==1&&system->detaches==2&&loader.soundBindDepth_==0&&scene.invalidRemovals==0);
    assert(loader.spaceSounds_==nullptr);
    prepare();assert(loader.doBind());auto owner=loader.spaceSounds_;
    assert(scene.handles.size()==2&&owner->groups.begin()->second->positions.size()==2);
    assert(loader.doBind()&&scene.handles.size()==2); // No duplicate registration.
    scene.onRemove=[&](){loader.doUnload();assert(!loader.doBind());};
    loader.doUnload();scene.onRemove={};
    assert(loader.instances_.empty()&&scene.handles.empty()&&owner->groups.empty());
    loader.doUnload();assert(reader.closes==2&&loader.soundUnloadDepth_==0);
    finiStockSpaceSounds();
    std::puts("Production particle bind/unload: real grouped registration, deferred synchronous unload, no duplicate/live/stale handles PASS");
#endif
}
