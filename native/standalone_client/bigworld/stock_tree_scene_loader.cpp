#include "pch.hpp"
#include "stock_tree_scene_loader.hpp"
#include "stock_tree_scene.hpp"
#include "compiled_space/compiled_space.hpp"
#include "scene/scene.hpp"
#include "scene/scene_object.hpp"
#include "scene/spatial_query_operation.hpp"
#include "space/dynamic_scene_provider.hpp"
#include <vector>

BW_BEGIN_NAMESPACE
namespace CompiledSpace
{
struct StockTreeSceneLoader::State
{
    StockTreeSceneHandler handler;
    CompiledSpace * space = NULL;
    std::vector<DynamicObjectHandle> membership;
    bool bound = false;
};

StockTreeSceneLoader::StockTreeSceneLoader( uint32 mask ) : mask_( mask ) {}
StockTreeSceneLoader::~StockTreeSceneLoader() { doUnload(); }

bool StockTreeSceneLoader::doLoadFromSpace( ClientSpace * space, BinaryFormat & reader,
    const DataSectionPtr & settings, const Matrix &, const StringTable & strings )
{
    if (state_ || unloadDepth_)
    {
        ERROR_MSG( "StockTreeSceneLoader: the previous mapping must unload before another load\n" );
        return false;
    }
    auto candidate = std::make_shared<State>();
    candidate->space = dynamic_cast<CompiledSpace *>( space );
    if (!candidate->space)
    {
        ERROR_MSG( "StockTreeSceneLoader: SpTr3 requires the actual compiled-space owner\n" );
        return false;
    }
    // 6c5bb0 passes each original definition.world directly to b010e0. Those
    // transforms are already world transforms; multiplying mappingTransform
    // here would transform them a second time.
    if (!candidate->handler.load( reader, strings, settings, mask_ )) return false;
    candidate->membership.reserve( candidate->handler.objectCount() );
    state_ = candidate;
    return true;
}

bool StockTreeSceneLoader::doBind()
{
    auto state = state_;
    if (!state) return false;
    if (state->bound) return true;
    if (!state->handler.bind() || state_ != state) return false;
    SpatialQueryOperation * spatial = state->space->scene().getObjectOperation<SpatialQueryOperation>();
    if (!spatial)
    {
        ERROR_MSG( "StockTreeSceneLoader: actual scene spatial operations are unavailable\n" );
        return false;
    }
    state->bound = true;
    const SceneObject * objects = state->handler.objects();
    for (size_t index = 0; index < state->handler.objectCount(); ++index)
    {
        if (!state->handler.objectAvailable( index )) continue;
        const SceneObject & object = objects[index];
        if (!spatial->isSupported( object ))
        {
            ERROR_MSG( "StockTreeSceneLoader: original tree spatial handler was not registered\n" );
            if (state_ == state) state_.reset();
            removeMembership( state );
            return false;
        }
        Matrix world;
        BoundingBox visibility, collision;
        spatial->getWorldTransform( object, &world );
        spatial->getWorldVisibilityBoundingBox( object, &visibility );
        state->handler.worldCollisionBounds( index, collision );
        // An absent native BSP remains an empty collision broadphase bound.
        // The visible cached mesh is not substituted for physical geometry.
        DynamicObjectHandle handle = state->space->dynamicScene().addObject( object, world, collision, visibility );
        // addObject synchronously notifies scene observers. Keep this retired
        // handler alive until its newly returned handle has also been removed.
        if (state_ != state)
        {
            state->space->dynamicScene().removeObject( handle );
            return false;
        }
        state->membership.push_back( handle );
    }
    return true;
}

void StockTreeSceneLoader::removeMembership( const std::shared_ptr<State> & state )
{
    state->bound = false;
    std::vector<DynamicObjectHandle> retired;
    retired.swap( state->membership );
    for (const auto & handle : retired) state->space->dynamicScene().removeObject( handle );
}

void StockTreeSceneLoader::doUnload()
{
    auto retired = state_;
    if (!retired) return;
    state_.reset();
    // ILoader::unload writes its status after this callback returns. A nested
    // load during removal cannot be published coherently through that API.
    // Reject it explicitly; a load after teardown or during add retirement is
    // still supported and has its own independent State.
    struct Retiring
    {
        explicit Retiring( uint32 & value ) : depth( value ) { ++depth; }
        ~Retiring() { --depth; }
        uint32 & depth;
    } retiring( unloadDepth_ );
    removeMembership( retired );
    // State owns the real handler. Its destructor unloads only after every
    // synchronous add/remove observer and any pending add handle have retired.
}

float StockTreeSceneLoader::percentLoaded() const
{
    return state_ ? state_->handler.loadPercent() : 0.f;
}
}
BW_END_NAMESPACE
