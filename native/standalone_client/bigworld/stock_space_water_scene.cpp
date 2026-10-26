#include "pch.hpp"
#include "stock_space_water_scene.hpp"
#include "stock_space_water.hpp"
#include "stock_space_water_geometry.hpp"
#include "compiled_space/binary_format.hpp"
#include "compiled_space/compiled_space.hpp"
#include "compiled_space/string_table.hpp"
#include "space/dynamic_scene_provider.hpp"
#include "scene/scene.hpp"
#include "scene/scene_object.hpp"
#include "scene/draw_operation.hpp"
#include "scene/spatial_query_operation.hpp"
#include "romp/water.hpp"
#include "romp/romp_collider.hpp"
#include <vector>

BW_BEGIN_NAMESPACE
namespace CompiledSpace
{
namespace
{
using Binary = wot::StockSpaceWaterData;
Vector3 vector3(const Binary::Definition& source, size_t offset)
{
    return Vector3(source.scalar(offset), source.scalar(offset+4), source.scalar(offset+8));
}
Vector2 vector2(const Binary::Definition& source, size_t offset)
{
    return Vector2(source.scalar(offset), source.scalar(offset+4));
}
Vector4 vector4(const Binary::Definition& source, size_t offset)
{
    return Vector4(source.scalar(offset), source.scalar(offset+4), source.scalar(offset+8), source.scalar(offset+12));
}
bool flag(const Binary::Definition& source, size_t offset)
{
    return ((source.word(offset & ~size_t(3)) >> ((offset & 3)*8)) & 255) != 0;
}
Water::WaterState state(const Binary::Definition& source)
{
    // a2eb80 assigns these fields to actual shader parameters. The original
    // state has no fresnelConstant/fresnelExponent; never cast the wire layout.
    Water::WaterState result = {};
    result.position_ = vector3(source,0); result.size_ = vector2(source,0xc);
    result.orientation_ = source.scalar(0x14); result.tessellation_ = source.scalar(0x18);
    result.textureTessellation_ = source.scalar(0x1c); result.consistency_ = source.scalar(0x20);
    result.reflectionScale_ = source.scalar(0x24); result.refractionScale_ = source.scalar(0x28);
    result.refractionPower_ = source.scalar(0x2c); result.animationSpeed_ = source.scalar(0x30);
    result.scrollSpeed1_ = vector2(source,0x34); result.scrollSpeed2_ = vector2(source,0x3c);
    result.waveScale_ = vector2(source,0x44); result.sunPower_ = source.scalar(0x4c);
    result.sunScale_ = source.scalar(0x50); result.sunScaleDeferred_ = source.scalar(0x54);
    result.windVelocity_ = source.scalar(0x58); result.depth_ = source.scalar(0x5c);
    result.reflectionTint_ = vector4(source,0x60); result.refractionTint_ = vector4(source,0x70);
    result.simCellSize_ = source.scalar(0x80); result.smoothness_ = source.scalar(0x84);
    result.useEdgeAlpha_ = flag(source,0x88); result.useSimulation_ = flag(source,0x89);
    result.visibility_ = source.word(0x8c); result.reflectBottom_ = flag(source,0x90);
    result.forwardWaterParameters_ = vector4(source,0x94); result.shallowWaterBiasForward_ = source.scalar(0xa4);
    result.deepColour_ = vector4(source,0xa8); result.deepColourScale_ = source.scalar(0xb8);
    result.fadeDepth_ = source.scalar(0xbc); result.foamIntersection_ = source.scalar(0xc0);
    result.foamMultiplier_ = source.scalar(0xc4); result.foamTiling_ = source.scalar(0xc8);
    result.foamFrequency_ = source.scalar(0xcc); result.foamAmplitude_ = source.scalar(0xd0);
    result.foamWidth_ = source.scalar(0xd4); result.bypassDepth_ = flag(source,0xd8);
    result.freqX = source.scalar(0xdc); result.freqZ = source.scalar(0xe0);
    result.waveHeight = source.scalar(0xe4); result.causticsPower = source.scalar(0xe8);
    result.waveTextureNumber_ = source.word(0xec); result.useShadows_ = flag(source,0xf0);
    return result;
}
BoundingBox bounds(const Binary::Bounds& source)
{
    return BoundingBox(Vector3(source.minimum[0],source.minimum[1],source.minimum[2]),
        Vector3(source.maximum[0],source.maximum[1],source.maximum[2]));
}
}

struct StockWaterSceneLoader::State
{
    struct Cell
    {
        State* owner = NULL;
        size_t surface = 0;
        BoundingBox local, visible;
        Matrix transform;
        SceneObject object;
    };
    struct DrawHandler : IDrawOperationTypeHandler
    {
        virtual void doDraw(const SceneDrawContext&, const SceneObject* objects, size_t count)
        {
            // 6c7730 first clears each not-yet-enqueued owner's visible bounds,
            // then appends every selected cell before deduplicated draw-list admission.
            for (size_t i=0;i<count;++i)
            {
                Cell* cell = objects[i].getAs<Cell>();
                if (cell->owner->bound) cell->owner->waters[cell->surface]->stockBeginVisibleCells();
            }
            for (size_t i=0;i<count;++i)
            {
                Cell* cell = objects[i].getAs<Cell>();
                if (!cell->owner->bound) continue;
                Water* water = cell->owner->waters[cell->surface];
                water->stockAddVisibleCell(cell->local);
                Waters::addToDrawList(water);
                water->outsideVisible(true);
            }
        }
    };
    struct SpatialHandler : ISpatialQueryOperationTypeHandler
    {
        virtual void getWorldTransform(const SceneObject& object, Matrix* output)
        { *output = object.getAs<Cell>()->transform; }
        virtual void getWorldVisibilityBoundingBox(const SceneObject& object, AABB* output)
        { *output = object.getAs<Cell>()->visible; }
    };
    Binary binary;
    std::vector<Water*> waters;
    std::vector<Cell> cells;
    std::vector<DynamicObjectHandle> memberships;
    CompiledSpace* space = NULL;
    bool bound = false;
    ~State()
    {
        for (Water* water : waters) if (water) Water::deleteWater(water);
    }
    static bool fail(const char* message)
    { ASSET_MSG("BWWa2: %s\n",message); return false; }
    bool load(BinaryFormat& file,const StringTable& strings)
    {
        const size_t section = file.findSection(FourCC("BWWa"));
        if (section == BinaryFormat::NOT_FOUND || file.sectionVersion(section) != 2)
            return fail("Missing or incompatible original water section");
        auto* stream = file.openSection(section,true);
        if (!stream) return fail("Cannot map original water section");
        const size_t size = stream->size();
        const void* bytes = stream->readRaw(size);
        std::string error;
        const bool parsed = bytes && binary.read(bytes,size,error);
        file.closeSection(stream);
        if (!parsed) return fail(error.empty()?"Cannot read original water section":error.c_str());
        if (!binary.resolve([&strings](uint32 key){return strings.entry(key);},error)) return fail(error.c_str());
        cells.resize(binary.cells().size());
        return true;
    }
};

StockWaterSceneLoader::StockWaterSceneLoader() {}
StockWaterSceneLoader::~StockWaterSceneLoader() { doUnload(); }
void StockWaterSceneLoader::registerHandlers(Scene& scene)
{
    scene.getObjectOperation<DrawOperation>()->addHandler<State::Cell,State::DrawHandler>();
    scene.getObjectOperation<SpatialQueryOperation>()->addHandler<State::Cell,State::SpatialHandler>();
}
bool StockWaterSceneLoader::doLoadFromSpace(ClientSpace* space, BinaryFormat& file,
    const DataSectionPtr&,const Matrix&,const StringTable& strings)
{
    if (state_ || unloadDepth_) return State::fail("Previous water mapping has not unloaded");
    auto candidate = std::make_shared<State>();
    candidate->space = dynamic_cast<CompiledSpace*>(space);
    if (!candidate->space) return State::fail("Water requires the actual compiled-space owner");
    // 6c7510 uses authored world-space placements; do not apply mapping twice.
    if (!candidate->load(file,strings)) return false;
    state_ = candidate;
    return true;
}
bool StockWaterSceneLoader::doBind()
{
    auto live = state_;
    if (!live) return false;
    if (live->bound) return true;
    for (const auto& instance : live->binary.instances())
    {
        Water::WaterResources resources;
        resources.foamTexture_ = instance.resources[0].c_str();
        resources.waveTexture_ = instance.resources[1].c_str();
        resources.reflectionTexture_ = instance.resources[2].c_str();
        resources.transparencyTable_ = instance.resources[3].c_str();
        live->waters.push_back(new Water(state(live->binary.definitions()[instance.definition]),resources,
            RompCollider::createDefault(RompCollider::TERRAIN_ONLY)));
        if (state_ != live) return false;
    }
    live->bound = true;
    for (size_t index=0;index<live->cells.size();++index)
    {
        auto& cell = live->cells[index];
        const auto& source = live->binary.cells()[index];
        cell.owner = live.get(); cell.surface = source.surface; cell.local = bounds(source.bounds);
        cell.transform = stockWaterCellTransform(cell.local,live->waters[cell.surface]->transform());
        // Disabled original slots retain their data but have no scene membership.
        if (!stockWaterCellEnabled(cell.transform)) continue;
        cell.visible = stockWaterCellBounds(cell.transform);
        cell.object.handleAndType(&cell);
        cell.object.flags().isDynamic(false);
        cell.object.flags().isReflectionVisible(false);
        const auto handle = live->space->dynamicScene().addObject(cell.object,cell.transform,
            BoundingBox::s_insideOut_,cell.visible);
        if (state_ != live)
        {
            live->space->dynamicScene().removeObject(handle);
            return false;
        }
        live->memberships.push_back(handle);
    }
    return true;
}
void StockWaterSceneLoader::removeMembership(const std::shared_ptr<State>& live)
{
    live->bound = false;
    std::vector<DynamicObjectHandle> retired;
    retired.swap(live->memberships);
    for (const auto& handle : retired) live->space->dynamicScene().removeObject(handle);
}
void StockWaterSceneLoader::doUnload()
{
    auto live = state_;
    if (!live) return;
    state_.reset();
    struct Retiring
    {
        uint32& depth;
        explicit Retiring(uint32& value):depth(value){++depth;}
        ~Retiring(){--depth;}
    } retiring(unloadDepth_);
    removeMembership(live);
}
float StockWaterSceneLoader::percentLoaded() const { return state_?1.f:0.f; }
}
BW_END_NAMESPACE
