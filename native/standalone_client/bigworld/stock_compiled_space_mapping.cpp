#include "pch.hpp"
#include "stock_compiled_space_mapping.hpp"
#include "wot/offline_mapping_policy.hpp"
#include "stock_static_model_scene.hpp"
#include "stock_destructibles.hpp"
#include "stock_space_lights.hpp"
#include "stock_space_flares.hpp"
#include "stock_space_water_scene.hpp"
#include "stock_space_decals.hpp"
#include "stock_space_sounds.hpp"
#include "stock_space_control_points.hpp"
#include "stock_tree_scene_loader.hpp"
#include "compiled_space/compiled_space.hpp"
#include "compiled_space/compiled_space_mapping.hpp"
#include "compiled_space/particle_system_loader.hpp"
#include "compiled_space/terrain2_scene_provider.hpp"

BW_BEGIN_NAMESPACE
namespace CompiledSpace
{
namespace
{
wot::OfflineMappingPolicy localMappingPolicy;
// 006b9380 dispatches the original sections to their own loaders. Original
// maps have no public BWSS/VLOS or BWTS container to instantiate or synthesize.
class StockCompiledSpaceMapping : public CompiledSpaceMapping
{
public:
    StockCompiledSpaceMapping( const BW::string & path, const Matrix & transform,
        const DataSectionPtr & settings, CompiledSpace & space,
        AssetClient * assetClient, uint32 visibilityMask ) :
        CompiledSpaceMapping( path, transform, settings, space, assetClient ),
        models_( visibilityMask ), trees_( visibilityMask ), flares_( visibilityMask ),
        decals_( visibilityMask ), controlPoints_( visibilityMask ),
        particles_( space.dynamicScene() )
    {
        addLoader( &terrain_ );
        addLoader( &models_ );
        addLoader( &trees_ );
        addLoader( &particles_ );
        addLoader( &lights_ );
        addLoader( &water_ );
        addLoader( &flares_ );
        addLoader( &decals_ );
        addLoader( &destructibles_ );
        addLoader( &controlPoints_ );
        // Original WTau binds after the model, tree, particle and WGDE owners.
        addLoader( &sounds_ );
    }

    virtual ~StockCompiledSpaceMapping()
    {
        // Stop/join the background loader and remove scene memberships while
        // every concrete loader still exists. The base destructor is idempotent.
        unloadSync();
        removeLoader( &sounds_ );
        removeLoader( &controlPoints_ );
        removeLoader( &destructibles_ );
        removeLoader( &decals_ );
        removeLoader( &flares_ );
        removeLoader( &water_ );
        removeLoader( &lights_ );
        removeLoader( &particles_ );
        removeLoader( &trees_ );
        removeLoader( &models_ );
        removeLoader( &terrain_ );
    }

private:
    Terrain2SceneProvider terrain_;
    StockStaticModelSceneLoader models_;
    StockTreeSceneLoader trees_;
    StockFlareSceneLoader flares_;
    StockSpaceDecalsLoader decals_;
    StockControlPointSceneLoader controlPoints_;
    ParticleSystemLoader particles_;
    StockLightSceneLoader lights_;
    StockWaterSceneLoader water_;
    StockDestructiblesSceneLoader destructibles_;
    StockSpaceSoundLoader sounds_;
};
}

uint32 stockLocalGeometryMappingMask() { return localMappingPolicy.mask(); }
long applyStockStandardGameplayMapping() { return localMappingPolicy.apply(); }
long restoreStockStandardGameplayMapping() { return localMappingPolicy.restore(); }

CompiledSpaceMapping * createStockCompiledSpaceMapping(
    const BW::string & path, const Matrix & transform,
    const DataSectionPtr & settings, CompiledSpace & space,
    AssetClient * assetClient, uint32 visibilityMask )
{
    return new StockCompiledSpaceMapping( path, transform, settings, space,
        assetClient, visibilityMask );
}

void registerStockCompiledSpaceHandlers( Scene & scene )
{
    StockStaticModelSceneLoader::registerHandlers( scene );
    StockWaterSceneLoader::registerHandlers( scene );
    StockFlareSceneLoader::registerHandlers( scene );
    StockControlPointSceneLoader::registerHandlers( scene );
}
}
BW_END_NAMESPACE
