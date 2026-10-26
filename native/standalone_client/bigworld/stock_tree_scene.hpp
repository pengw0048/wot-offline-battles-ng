#pragma once

#include "compiled_space/static_scene_type_handler.hpp"
#include "resmgr/datasection.hpp"
#include "math/boundbox.hpp"
#include <memory>

BW_BEGIN_NAMESPACE
class Scene;

// The compiled-space owner replaces the disabled SDK handler. It consumes the
// original SpTr3 definitions and the independently decoded SPT/ctree resources.
bool initialiseStockTreeSceneRendering();
void tickStockTreeSceneRendering( float dTime );
void finaliseStockTreeSceneRendering();

namespace CompiledSpace
{
class StockTreeSceneHandler : public IStaticSceneTypeHandler
{
public:
    StockTreeSceneHandler();
    virtual ~StockTreeSceneHandler();
    static void registerHandlers( Scene & scene );
    void visibilityMask( uint32 mask );
    void spaceSettings( const DataSectionPtr & settings );
    // The original mapping dispatches each native section directly, without
    // manufacturing a public BWSS/VLOS provider or a dummy reference.
    bool load( BinaryFormat & file, const StringTable & strings,
        const DataSectionPtr & settings, uint32 mask );
    const SceneObject * objects() const;
    size_t objectCount() const;
    bool objectAvailable( size_t index ) const;
    void worldCollisionBounds( size_t index, BoundingBox & bounds ) const;
    virtual SceneTypeSystem::RuntimeTypeID typeID() const;
    virtual SceneTypeSystem::RuntimeTypeID runtimeTypeID() const;
    virtual bool load( BinaryFormat & file, StaticSceneProvider & scene,
        const StringTable & strings, SceneObject * objects, size_t count );
    virtual bool bind();
    virtual void unload();
    virtual float loadPercent() const;

private:
    bool loadInstances( BinaryFormat & file, const StringTable & strings,
        SceneObject * objects, size_t count );
    StockTreeSceneHandler( const StockTreeSceneHandler & );
    StockTreeSceneHandler & operator=( const StockTreeSceneHandler & );
    struct Implementation;
    std::unique_ptr<Implementation> implementation_;
};
}
BW_END_NAMESPACE
