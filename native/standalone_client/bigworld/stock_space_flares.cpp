#include "pch.hpp"
#include "stock_space_flares.hpp"
#include "stock_space_flares_data.hpp"
#include "compiled_space/binary_format.hpp"
#include "compiled_space/compiled_space.hpp"
#include "compiled_space/string_table.hpp"
#include "space/dynamic_scene_provider.hpp"
#include "scene/scene.hpp"
#include "scene/scene_object.hpp"
#include "scene/scene_draw_context.hpp"
#include "scene/draw_operation.hpp"
#include "scene/spatial_query_operation.hpp"
#include "romp/lens_effect.hpp"
#include "romp/lens_effect_manager.hpp"
#include "resmgr/bwresource.hpp"
#include <vector>

BW_BEGIN_NAMESPACE
namespace CompiledSpace
{
struct StockFlareSceneLoader::State
{
    struct Instance
    {
        State* owner = NULL;
        const wot::StockFlareData::Definition* definition = NULL;
        LensEffect effect;
        Matrix transform;
        BoundingBox bounds;
        SceneObject object;
        bool active = false;
        size_t id() const { return reinterpret_cast<size_t>(definition); }
    };
    struct DrawHandler : IDrawOperationTypeHandler
    {
        virtual void doDraw(const SceneDrawContext& context,const SceneObject* objects,size_t count)
        {
            if (!context.renderFlares()) return;
            for (size_t i=0;i<count;++i)
            {
                Instance* value=objects[i].getAs<Instance>();
                if (!value->owner->bound || !value->active) continue;
                const auto& definition=*value->definition;
                const uint32 oldColour=value->effect.colour();
                const bool tint=(definition.flags&1)!=0;
                if (tint) value->effect.colour(wot::StockFlareData::modulate(oldColour,definition.colour));
                LensEffectManager::instance().add(value->id(),Vector3(definition.position[0],
                    definition.position[1],definition.position[2]),value->effect);
                if (tint) value->effect.colour(oldColour);
            }
        }
    };
    struct SpatialHandler : ISpatialQueryOperationTypeHandler
    {
        virtual void getWorldTransform(const SceneObject& object,Matrix* output)
        { *output=object.getAs<Instance>()->transform; }
        virtual void getWorldVisibilityBoundingBox(const SceneObject& object,AABB* output)
        { *output=object.getAs<Instance>()->bounds; }
    };
    wot::StockFlareData binary;
    std::vector<Instance> instances;
    std::vector<DynamicObjectHandle> memberships;
    CompiledSpace* space=NULL;
    bool bound=false;
    ~State()
    {
        // Manager::add copies the effect; forget every definition ID before
        // those stable addresses can be reused by another mapping.
        for (const auto& value:instances) LensEffectManager::instance().forget(value.id());
    }
    static bool fail(const char* message) { ASSET_MSG("BWfr2: %s\n",message); return false; }
    bool load(BinaryFormat& file,const StringTable& strings,uint32 mappingMask)
    {
        const size_t section=file.findSection(FourCC("BWfr"));
        if (section==BinaryFormat::NOT_FOUND || file.sectionVersion(section)!=2)
            return fail("Missing or incompatible original flare section");
        auto* stream=file.openSection(section,true);
        if (!stream) return fail("Cannot map original flare section");
        const size_t size=stream->size();
        const void* bytes=stream->readRaw(size);
        std::string error;
        const bool parsed=bytes&&binary.read(bytes,size,error);
        file.closeSection(stream);
        if (!parsed) return fail(error.empty()?"Cannot read original flare section":error.c_str());
        instances.resize(binary.definitions.size());
        for (size_t i=0;i<instances.size();++i)
        {
            auto& value=instances[i];
            value.owner=this; value.definition=&binary.definitions[i];
            const auto& definition=*value.definition;
            // 6be5b0 resolves and opens the actual FNV-keyed resource before
            // testing the mapping mask. A missing resource disables only it.
            const char* resource=strings.entry(definition.resource);
            if (!resource || !*resource) continue;
            DataSectionPtr root=BWResource::openSection(resource);
            if (!root || !(definition.visibilityMask&mappingMask)) continue;
            if (!value.effect.load(root)) continue;
            value.effect.maxDistance(definition.maxDistance);
            value.effect.area(definition.area);
            value.effect.fadeSpeed(definition.fadeSpeed);
            value.active=true;
            value.transform.setTranslate(Vector3(definition.position[0],definition.position[1],definition.position[2]));
            // 6be040 registers only a translation matrix. 11bf890/11bc780
            // derive its static culler bounds from the +/-0.5 unit cube.
            const Vector3 low(definition.position[0]-.5f,definition.position[1]-.5f,definition.position[2]-.5f);
            value.bounds.setBounds(low,Vector3(low.x+1.f,low.y+1.f,low.z+1.f));
            value.object.handleAndType(&value);
            value.object.flags().isDynamic(false);
            value.object.flags().isReflectionVisible(false);
        }
        return true;
    }
};
StockFlareSceneLoader::StockFlareSceneLoader(uint32 mask):mappingMask_(mask) {}
StockFlareSceneLoader::~StockFlareSceneLoader() { doUnload(); }
void StockFlareSceneLoader::registerHandlers(Scene& scene)
{
    scene.getObjectOperation<DrawOperation>()->addHandler<State::Instance,State::DrawHandler>();
    scene.getObjectOperation<SpatialQueryOperation>()->addHandler<State::Instance,State::SpatialHandler>();
}
bool StockFlareSceneLoader::doLoadFromSpace(ClientSpace* space,BinaryFormat& file,
    const DataSectionPtr&,const Matrix&,const StringTable& strings)
{
    if (state_||unloadDepth_) return State::fail("Previous flare mapping has not unloaded");
    auto next=std::make_shared<State>();
    next->space=dynamic_cast<CompiledSpace*>(space);
    if (!next->space) return State::fail("Flare requires the actual compiled-space owner");
    // Original placements are authored world positions, with no mapping multiply.
    if (!next->load(file,strings,mappingMask_)) return false;
    state_=next;
    loaded_.store(true,std::memory_order_release);
    return true;
}
bool StockFlareSceneLoader::doBind()
{
    auto live=state_;
    if (!live) return false;
    if (live->bound) return true;
    live->bound=true;
    for (auto& value:live->instances)
    {
        if (!value.active) continue;
        const auto handle=live->space->dynamicScene().addObject(value.object,value.transform,
            BoundingBox::s_insideOut_,value.bounds);
        if (state_!=live) { live->space->dynamicScene().removeObject(handle); return false; }
        live->memberships.push_back(handle);
    }
    return true;
}
void StockFlareSceneLoader::removeMembership(const std::shared_ptr<State>& live)
{
    live->bound=false;
    std::vector<DynamicObjectHandle> retired;
    retired.swap(live->memberships);
    for (const auto& handle:retired) live->space->dynamicScene().removeObject(handle);
}
void StockFlareSceneLoader::doUnload()
{
    loaded_.store(false,std::memory_order_release);
    auto live=state_;
    if (!live) return;
    state_.reset();
    struct Retiring { uint32& depth; explicit Retiring(uint32& value):depth(value){++depth;} ~Retiring(){--depth;} } retiring(unloadDepth_);
    removeMembership(live);
    live.reset();
}
float StockFlareSceneLoader::percentLoaded() const
{ return loaded_.load(std::memory_order_acquire)?1.f:0.f; }
}
BW_END_NAMESPACE
