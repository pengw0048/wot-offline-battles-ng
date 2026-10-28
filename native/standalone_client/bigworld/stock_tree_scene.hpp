#pragma once

#include "compiled_space/static_scene_type_handler.hpp"
#include "resmgr/datasection.hpp"
#include "math/boundbox.hpp"
#include <memory>
#include <string>

BW_BEGIN_NAMESPACE
class MatrixProvider;
class Scene;
struct StockDestructibleSnapshot;

// The compiled-space owner replaces the disabled SDK handler. It consumes the
// original SpTr3 definitions and the independently decoded SPT/ctree resources.
bool initialiseStockTreeSceneRendering();
void tickStockTreeSceneRendering( float dTime );
void finaliseStockTreeSceneRendering();
// 00746d60/00748370 retain and sample this body even with zero SRT instances.
// The exact client's old-ST4 resources do not render body deformation.
void setStockTreeCollisionBody( MatrixProvider * target );
// #1513 00b030b0: trees between the camera and this target fade in the
// colour pass and are drawn by drawStockTreeSemitransparent (00afef30).
void setStockTreeCameraTarget( MatrixProvider * target );
// Script teardown closes the target owner; the next script init reopens it.
void closeStockTreeCameraTarget();
void openStockTreeCameraTarget();
void drawStockTreeSemitransparent( bool skipDrawing );
// #1513 009c4ed0: the deferred stage, with the near-camera optimizer path.
void drawStockTreeSemitransparentStage( bool skipDrawing, bool optimiser, bool blur );

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
    BW::string soundResource( size_t index ) const;
    bool destructibleSnapshot( size_t index, StockDestructibleSnapshot & output ) const;
    bool setDestructibleTransform( size_t index, const Matrix & world, float wind, std::string & error );
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
