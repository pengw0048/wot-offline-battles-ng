#include "pch.hpp"
#include "stock_static_model_scene.hpp"
#include "stock_space_sounds.hpp"
#include "stock_space_sounds_data.hpp"
#include "stock_scene_identity_data.hpp"
#include "wot/stock_static_model_binary.hpp"
#include "compiled_space/binary_format.hpp"
#include "compiled_space/compiled_space.hpp"
#include "compiled_space/collision_helpers.hpp"
#include "compiled_space/string_table.hpp"
#include "space/dynamic_scene_provider.hpp"
#include "scene/scene.hpp"
#include "scene/scene_object.hpp"
#include "scene/scene_draw_context.hpp"
#include "scene/draw_operation.hpp"
#include "scene/collision_operation.hpp"
#include "scene/spatial_query_operation.hpp"
#include "scene/tick_scene_view.hpp"
#include "moo/complex_effect_material.hpp"
#include "moo/vertices_manager.hpp"
#include "moo/primitive_manager.hpp"
#include "moo/texture_manager.hpp"
#include "moo/animation.hpp"
#include "moo/lod_settings.hpp"
#include "moo/streamed_animation_channel.hpp"
#include "stock_scene_settings.hpp"
#include "resmgr/primitive_file.hpp"
#include "physics2/bsp.hpp"
#include "physics2/collision_obstacle.hpp"
#include "physics2/collision_interface.hpp"
#include "physics2/sweep_helpers.hpp"
#include <algorithm>
#include <cstring>
#include <vector>

BW_BEGIN_NAMESPACE
namespace CompiledSpace
{
struct StockStaticModelSceneLoader::State : std::enable_shared_from_this<State>
{
    using Binary = wot::StockStaticModelBinary;
    struct Render
    {
        Moo::VerticesPtr vertices;
        Moo::PrimitivePtr primitives;
    };
    struct Animation
    {
        Moo::AnimationPtr source;
        int first = 0, last = 0;
        float duration = 0.f;
    };
    // Declared before animations: their StreamedAnimation must be destroyed
    // before the cache closes its FILE and retires asynchronous block reads.
    std::vector<std::shared_ptr<Moo::StreamedDataCache>> animationCaches;
    std::vector<Animation> animations;
    std::vector<Matrix> localPoses, worldPoses[2];
    size_t currentPose = 0, previousPose = 0;
    struct Instance
    {
        State * owner = NULL;
        size_t index = 0;
        Matrix world, inverse;
        BoundingBox visibility, collision;
        SceneObject object;
        bool active = false;
        wot::StockSceneItemIdentity item;   // 006ba9b0 static-item identity
    };
    struct DrawHandler;
    struct SpatialHandler;
    struct CollisionHandler;
    struct TickHandler;
    Binary binary;
    CompiledSpace * space = NULL;
    std::vector<SmartPointer<Moo::ComplexEffectMaterial>> materials;
    std::vector<Render> renders;
    std::vector<float> lodThresholds;
    std::vector<std::shared_ptr<BSPTree>> colliders;
    std::vector<Instance> instances;
    std::vector<DynamicObjectHandle> membership;
    const void* soundMapping = NULL;
    StockSpaceSounds soundOwner;
    std::vector<StockSpaceSoundHandle> soundHandles;
    bool bound = false;

    static bool fail( const char * message )
    {
        ERROR_MSG( "StockStaticModelScene: %s\n", message );
        return false;
    }
    static Matrix matrix( const Binary::Matrix & source )
    {
        Matrix result;
        for (int row = 0; row < 4; ++row)
            result.row( row, Vector4( source[row * 4], source[row * 4 + 1],
                source[row * 4 + 2], source[row * 4 + 3] ) );
        return result;
    }
    static Binary::Matrix values( const Matrix & source )
    {
        Binary::Matrix result;
        for (int row = 0; row < 4; ++row)
            for (int column = 0; column < 4; ++column)
                result[row * 4 + column] = source.row( row )[column];
        return result;
    }
    static BoundingBox bounds( const Binary::Bounds & source )
    {
        return BoundingBox( Vector3( source.minimum[0], source.minimum[1], source.minimum[2] ),
            Vector3( source.maximum[0], source.maximum[1], source.maximum[2] ) );
    }
    bool load( BinaryFormat & file, const StringTable & strings, uint32 mappingMask );
    bool loadItemIdentities( BinaryFormat & file );
    bool loadMaterials( const StringTable & strings );
    bool loadModels( const StringTable & strings );
    bool loadAnimations( const StringTable & strings );
    void updateAnimations( float dTime );
    static bool available( const Instance & instance )
    {
        const uint32 flags = instance.owner->binary.instances.flags[instance.index].flags;
        const uint32 decor = flags & 0x100 ? 2 : ((flags >> 7) & 1);
        return instance.active && (!decor || static_cast<int>( decor ) <= stockDecorLevel());
    }
};

bool StockStaticModelSceneLoader::State::loadMaterials( const StringTable & strings )
{
    const auto & source = binary.materials;
    materials.resize( source.records.size() );
    for (size_t index = 0; index < materials.size(); ++index)
    {
        const auto & record = source.records[index];
        // 9924a0 leaves a NULL effect in this slot. A source .model material
        // or the public engine's default material is not a replacement.
        if (record.effect == -1) continue;
        const char * effect = strings.entry( source.effects[record.effect] );
        if (!effect) return fail( "BSMA effect resource key is absent from BWST" );
        SmartPointer<Moo::ComplexEffectMaterial> material = new Moo::ComplexEffectMaterial;
        if (!material->initFromEffect( effect )) return fail( "Cannot initialise original compiled material effect" );
        Moo::EffectMaterialPtr base = material->baseMaterial();
        if (record.first != -1)
        {
            for (std::int32_t property = record.first; property <= record.last; ++property)
            {
                const auto & p = source.properties[property];
                // 93e6b0 has no case zero: retain the actual effect default.
                if (!p.kind) continue;
                const char * name = strings.entry( p.name );
                if (!name) return fail( "BSMA property key is absent from BWST" );
                // 0093e6b0 does not branch on the native setters' result.
                // A shader can omit or optimize away an authored property;
                // that does not invalidate this model or its collision data.
                switch (p.kind)
                {
                case 1: base->setProperty( name, p.value == 1, true ); break;
                case 2:
                {
                    float value;
                    std::memcpy( &value, &p.value, 4 );
                    base->setProperty( name, value, true );
                    break;
                }
                case 3:
                {
                    std::int32_t value;
                    std::memcpy( &value, &p.value, 4 );
                    base->setProperty( name, static_cast<int>( value ), true );
                    break;
                }
                case 4: base->setProperty( name, matrix( source.matrices[p.value] ), true ); break;
                case 5:
                {
                    const auto & v = source.vectors[p.value];
                    base->setProperty( name, Vector4( v[0], v[1], v[2], v[3] ), true );
                    break;
                }
                case 6:
                {
                    const char * resource = strings.entry( p.value );
                    if (!resource) return fail( "BSMA texture key is absent from BWST" );
                    Moo::BaseTexturePtr texture;
                    if (*resource) texture = Moo::TextureManager::instance()->get( resource );
                    base->setProperty( name, texture, true );
                    break;
                }
                }
            }
        }
        materials[index] = material;
    }
    return true;
}

bool StockStaticModelSceneLoader::State::loadModels( const StringTable & strings )
{
    lodThresholds.resize( binary.models.lodDistancesSquared.size() );
    for (size_t model = 0; model < binary.models.lods.size(); ++model)
    {
        const auto & range = binary.models.lods[model];
        if (range.first == 0xffffffffu) continue;
        for (uint32 lod = range.first; lod <= range.last; ++lod)
            lodThresholds[lod] = Binary::lodThreshold( binary.models.bounds[model],
                binary.models.lodDistancesSquared[lod] );
    }
    renders.resize( binary.models.renders.size() );
    for (size_t index = 0; index < renders.size(); ++index)
    {
        const auto & source = binary.models.renders[index];
        const char * vertexName = strings.entry( source.vertices );
        const char * primitiveName = strings.entry( source.primitives );
        if (!vertexName || !primitiveName) return fail( "BSMO geometry key is absent from BWST" );
        auto & render = renders[index];
        // The BWSG/BSGD mapping owner has already populated these exact native
        // resources. Keep its real GPU buffers; do not rebuild a source visual.
        render.vertices = Moo::VerticesManager::instance()->get( vertexName );
        render.primitives = Moo::PrimitiveManager::instance()->get( primitiveName );
        if (!render.vertices || !render.primitives || source.group >= render.primitives->nPrimGroups())
            return fail( "Cannot load original compiled geometry primitive group" );
    }
    colliders.resize( binary.models.colliders.size() );
    for (size_t index = 0; index < colliders.size(); ++index)
    {
        const auto & source = binary.models.colliders[index];
        // 6d4230 tests the key itself before resolving a nullable collider.
        if (!source.resource) continue;
        const char * resource = strings.entry( source.resource );
        if (!resource) return fail( "BSMO collision key is absent from BWST" );
        DataSectionPtr primitive = PrimitiveFile::get( resource );
        BinaryPtr data;
        if (primitive) data = primitive->readBinary( "bsp2" );
        std::shared_ptr<BSPTree> tree( data ? BSPTreeTool::loadBSP( data ) : NULL );
        if (!tree) continue;
        if (source.materialFirst != 0xffffffffu)
        {
            RealWTriangleSet triangles;
            // 6d4230 emits only matching triangles, in material-record order.
            // A remap of the entire source BSP incorrectly adds other states.
            for (uint32 material = source.materialFirst; material <= source.materialLast; ++material)
            {
                const auto & remap = binary.models.bspMaterials[material];
                for (uint32 n = 0; n < tree->numTriangles(); ++n)
                {
                    WorldTriangle copy = tree->triangles()[n];
                    if (copy.flags() != remap.source) continue;
                    copy.flags( static_cast<WorldTriangle::Flags>( remap.flags ) );
                    triangles.push_back( copy );
                }
            }
            tree.reset( BSPTreeTool::buildBSP( triangles ) );
        }
        colliders[index] = tree;
    }
    return true;
}

bool StockStaticModelSceneLoader::State::loadAnimations( const StringTable & strings )
{
    animations.resize( binary.models.animations.size() );
    animationCaches.resize( animations.size() );
    for (size_t index = 0; index < animations.size(); ++index)
    {
        const auto & definition = binary.models.animations[index];
        const char * resource = strings.entry( definition.resource );
        const char * cacheResource = strings.entry( definition.cache );
        if (!resource || !cacheResource) return fail( "BSMO animation key is absent from BWST" );
        auto cache = std::make_shared<Moo::StreamedDataCache>( cacheResource, false );
        if (!cache->good() || !cache->findEntryData( resource, 6, 0 )) cache.reset();
        // Exact loader 6d4230 supplies the authored cache to the real version-6
        // Animation reader. The original .animation source is usually absent.
        struct LoadingCache
        {
            Moo::StreamedDataCache * previous;
            explicit LoadingCache( Moo::StreamedDataCache * value ) :
                previous( Moo::StreamedAnimation::s_loadingAnimCache_ )
            { Moo::StreamedAnimation::s_loadingAnimCache_ = value; }
            ~LoadingCache() { Moo::StreamedAnimation::s_loadingAnimCache_ = previous; }
        } loading( cache.get() );
        Moo::AnimationPtr source = new Moo::Animation;
        if (!source->load( resource )) continue;
        const size_t channels = definition.nodeFirst == 0xffffffffu ? 0 :
            size_t( definition.nodeLast ) - definition.nodeFirst + 1;
        if (source->nChannelBinders() != channels)
            return fail( "Original animation channel count differs from compiled affectors" );
        auto & animation = animations[index];
        const int total = static_cast<int>( source->totalTime() );
        const auto clampFrame = [total]( int frame ) {
            if (frame < 0) frame += 1 + total;
            return std::min( total, std::max( 0, frame ) );
        };
        animation.first = clampFrame( definition.firstFrame );
        animation.last = clampFrame( definition.lastFrame );
        animation.duration = static_cast<float>( std::abs( animation.last - animation.first ) ) / definition.frameRate;
        animation.source = source;
        animationCaches[index] = cache;
    }
    localPoses.reserve( binary.instances.poseNodes.size() );
    for (uint32 node : binary.instances.poseNodes)
        localPoses.push_back( matrix( binary.models.nodes[node].transform ) );
    worldPoses[0].resize( localPoses.size() );
    worldPoses[1].resize( localPoses.size() );
    return true;
}

void StockStaticModelSceneLoader::State::updateAnimations( float dTime )
{
    for (auto & definition : binary.instances.animations)
    {
        if (definition.animation < 0) continue;
        const int offset = binary.instances.poseOffsets[definition.instance];
        if (offset < 0) continue;
        auto & animation = animations[definition.animation];
        if (animation.source)
        {
            const auto & model = binary.models.animations[definition.animation];
            definition.time += dTime * definition.rate;
            if (animation.duration > 0.f)
                definition.time -= static_cast<float>( static_cast<int64>( definition.time / animation.duration ) ) * animation.duration;
            else definition.time = 0.f;
            const float step = definition.time * model.frameRate;
            const float firstFrame = static_cast<float>( animation.first );
            const float frame = animation.last < animation.first ? firstFrame - step : firstFrame + step;
            animation.source->touchStockCompiledFrame( frame );
            for (uint32 channel = 0; channel < animation.source->nChannelBinders(); ++channel)
            {
                const auto & binder = animation.source->channelBinder( channel );
                if (!binder.channel()) continue;
                const uint32 target = binary.models.animationNodes[model.nodeFirst + channel];
                if (target == 0xffffffffu) binder.channel()->nodeless( frame, 1.f );
                else binder.channel()->result( frame, localPoses[size_t( offset ) + target] );
                if (!bound) return;
            }
        }
        // 6c2bc0 composes the root with this instance's authored world matrix,
        // then walks backward-relative parents until the next shared pose root.
        auto & output = worldPoses[currentPose];
        output[offset].multiply( localPoses[offset], matrix( binary.instances.transforms[definition.instance] ) );
        for (size_t node = size_t( offset ) + 1; node < localPoses.size(); ++node)
        {
            const uint32 parent = binary.models.nodes[binary.instances.poseNodes[node]].parentOffset;
            if (parent == 0xffffffffu || parent > node - size_t( offset )) break;
            output[node].multiply( localPoses[node], output[node - parent] );
        }
    }
}

bool StockStaticModelSceneLoader::State::load( BinaryFormat & file,
    const StringTable & strings, uint32 mappingMask )
{
    std::vector<unsigned char> sections[3];
    const char * names[] = { "BSMI", "BSMO", "BSMA" };
    const uint32 versions[] = { 2, 1, 1 };
    for (size_t index = 0; index < 3; ++index)
    {
        const size_t section = file.findSection( FourCC( names[index] ) );
        if (section == BinaryFormat::NOT_FOUND || file.sectionVersion( section ) != versions[index])
            return fail( "Missing or incompatible original compiled-model section" );
        auto * stream = file.openSection( section, true );
        if (!stream) return fail( "Cannot map original compiled-model section" );
        const size_t size = stream->size();
        const auto * bytes = static_cast<const unsigned char *>( stream->readRaw( size ) );
        if (bytes) sections[index].assign( bytes, bytes + size );
        file.closeSection( stream );
        if (!bytes) return fail( "Cannot read original compiled-model section" );
    }
    std::string error;
    if (!binary.read( sections[0].data(), sections[0].size(), sections[1].data(), sections[1].size(),
        sections[2].data(), sections[2].size(), error )) return fail( error.c_str() );
    if (!loadMaterials( strings ) || !loadModels( strings ) || !loadAnimations( strings )) return false;
    instances.resize( binary.instances.transforms.size() );
    for (size_t index = 0; index < instances.size(); ++index)
    {
        auto & instance = instances[index];
        instance.owner = this;
        instance.index = index;
        instance.active = binary.active( index, mappingMask );
        instance.world = matrix( binary.instances.transforms[index] );
        instance.inverse.invert( instance.world );
        const uint32 model = binary.instances.models[index];
        instance.visibility = bounds( binary.models.bounds[model] );
        instance.visibility.transformBy( instance.world );
        instance.collision = BoundingBox::s_insideOut_;
        if (colliders[model])
        {
            instance.collision = colliders[model]->boundingBox();
            instance.collision.transformBy( instance.world );
        }
        auto & object = instance.object;
        object.type<Instance>();
        object.handle( &instance );
        const uint32 flags = binary.instances.flags[index].flags;
        object.flags().isDynamic( (flags & 0x40) != 0 );
        object.flags().isReflectionVisible( (flags & 2) != 0 );
        object.flags().isCastingStaticShadow( (flags & 1) && !(flags & 0x40) );
        object.flags().isCastingDynamicShadow( (flags & 1) != 0 );
    }
    membership.reserve( instances.size() );
    return loadItemIdentities( file );
}

bool StockStaticModelSceneLoader::State::loadItemIdentities( BinaryFormat & file )
{
    // The same mapping's WGDE1 arrays the DestructiblesSceneProvider binds.
    const size_t section = file.findSection( FourCC( "WGDE" ) );
    if (section == BinaryFormat::NOT_FOUND) return true;
    if (file.sectionVersion( section ) != 1)
        return fail( "Incompatible original WGDE static-item identities" );
    auto * stream = file.openSection( section, true );
    if (!stream) return fail( "Cannot map original WGDE static-item identities" );
    const size_t size = stream->size();
    const void * bytes = stream->readRaw( size );
    wot::StockSpaceSoundIdentities wgde;
    std::string error;
    const bool read = bytes && wgde.read( bytes, size, error );
    file.closeSection( stream );
    if (!read) return fail( error.empty() ? "Cannot read original WGDE static-item identities" : error.c_str() );
    const auto items = wot::stockSceneItemIdentities( wgde, instances.size() );
    for (size_t index = 0; index < instances.size(); ++index)
        instances[index].item = items[index];
    return true;
}

struct StockStaticModelSceneLoader::State::DrawHandler : IDrawOperationTypeHandler
{
    virtual void doDraw( const SceneDrawContext & context, const SceneObject * objects, size_t count )
    {
        Moo::DrawContext & draw = context.drawContext();
        const auto pass = draw.renderingPassType();
        const auto view = values( context.lodCameraView() );
        const Matrix & projection = Moo::rc().projection();
        // abb170 writes SceneDrawContext+54 from a36cd0, the actual active
        // OBJECT_LOD FloatOption value. The real projection already contains FOV.
        const float lodScale = LodSettings::instance().stockCompiledLodScale();
        for (size_t n = 0; n < count; ++n)
        {
            const auto & instance = *objects[n].getAs<Instance>();
            auto live = instance.owner->shared_from_this();
            if (!live->bound || !available( instance )) continue;
            const auto & binary = live->binary;
            const uint32 flags = binary.instances.flags[instance.index].flags;
            // 006d122d..006d1242: only authored bit 7 yields to the primary scene's
            // low-detail mode. Collision keeps its separate authored rule.
            if ((context.stockLowDetailedMode() && (flags & 0x80)) ||
                (pass == Moo::RENDERING_PASS_REFLECTION && !(flags & 2)) ||
                (pass == Moo::RENDERING_PASS_SHADOWS ? !(flags & 1) : (flags & 8))) continue;
            const uint32 model = binary.instances.models[instance.index];
            const auto & lods = binary.models.lods[model];
            if (lods.first == 0xffffffffu) continue;
            const float metric = Binary::lodMetric( binary.models.bounds[model],
                binary.instances.transforms[instance.index], view, lodScale, projection.row( 0 ).x, projection.row( 1 ).y );
            uint32 lod = lods.first;
            for (; lod <= lods.last; ++lod)
            {
                const uint32 relative = lod - lods.first;
                if (relative >= 5) { fail( "Original model exceeds its five LOD multipliers" ); break; }
                float multiplier = binary.instances.parameters[instance.index].values[relative];
                if (multiplier == -1.f) multiplier = LodSettings::instance().stockCompiledLodDefault( relative );
                const float threshold = live->lodThresholds[lod];
                if (threshold < 0.f || threshold * multiplier <= metric) break;
            }
            if (lod > lods.last || lod - lods.first >= 5) continue;
            const auto & range = binary.models.lodRenders[lod];
            if (range.first == 0xffffffffu) continue;
            const int pose = binary.instances.poseOffsets[instance.index];
            for (uint32 render = range.first; render <= range.last; ++render)
            {
                const auto & definition = binary.models.renders[render];
                const auto & material = live->materials[definition.material];
                if (!material) continue;
                const bool palette = (definition.flags & 1) && pose >= 0 && definition.nodeFirst != 0xffffffffu;
                const uint32 bones = palette ? definition.nodeLast - definition.nodeFirst + 1 : 0;
                auto * packet = draw.allocStockInstanceData( bones );
                if (palette)
                {
                    Vector4 * output = packet->palette();
                    // 6d57b0 transposes each selected node matrix and writes
                    // exactly its first three float4 rows, then previous rows.
                    for (size_t frame : { live->currentPose, live->previousPose })
                    {
                        for (uint32 node = definition.nodeFirst; node <= definition.nodeLast; ++node)
                        {
                            const uint32 target = binary.models.renderNodes[node];
                            const Matrix & world = live->worldPoses[frame][size_t( pose ) + target];
                            for (int row = 0; row < 3; ++row)
                                *output++ = Vector4( world.row( 0 )[row], world.row( 1 )[row], world.row( 2 )[row], world.row( 3 )[row] );
                        }
                    }
                }
                else
                {
                    Matrix * matrices = packet->matrix();
                    if (pose >= 0 && definition.nodeFirst != 0xffffffffu)
                    {
                        const size_t node = size_t( pose ) + binary.models.renderNodes[definition.nodeFirst];
                        matrices[0] = live->worldPoses[live->currentPose][node];
                        matrices[1] = live->worldPoses[live->previousPose][node];
                        packet->stockAnimatedWorld_ = true;
                    }
                    else
                    {
                        // The compiled placement is immutable; its actual
                        // previous world is the same authored placement.
                        matrices[0] = instance.world;
                        matrices[1] = instance.world;
                    }
                }
                auto & geometry = live->renders[render];
                draw.drawRenderOp( material.getObject(), geometry.vertices.getObject(),
                    geometry.primitives.getObject(), definition.group, packet, instance.visibility );
            }
        }
    }
};

struct StockStaticModelSceneLoader::State::SpatialHandler : ISpatialQueryOperationTypeHandler
{
    virtual void getWorldTransform( const SceneObject & object, Matrix * world )
    { *world = object.getAs<Instance>()->world; }
    virtual void getWorldVisibilityBoundingBox( const SceneObject & object, AABB * box )
    { *box = object.getAs<Instance>()->visibility; }
};

struct StockStaticModelSceneLoader::State::CollisionHandler : ICollisionTypeHandler
{
    virtual bool doCollide( const SceneObject & object, const Vector3 & source,
        const Vector3 & extent, const SweepParams & sweep, CollisionState & state ) const
    { return collide( object, source, extent, sweep, state ); }
    virtual bool doCollide( const SceneObject & object, const WorldTriangle & source,
        const Vector3 & extent, const SweepParams & sweep, CollisionState & state ) const
    { return collide( object, source, extent, sweep, state ); }
    template<class Shape> bool collide( const SceneObject & object, const Shape & source,
        const Vector3 &, const SweepParams & sweep, CollisionState & state ) const
    {
        const auto & instance = *object.getAs<Instance>();
        auto live = instance.owner->shared_from_this();
        if (!live->bound || !available( instance ) ||
            (live->binary.instances.flags[instance.index].flags & 0x80)) return false;
        const auto & bsp = live->colliders[live->binary.instances.models[instance.index]];
        if (!bsp) return false;
        CollisionObstacle obstacle( &instance.world, &instance.inverse, object );
        BSPCollisionInterface collision( obstacle, *bsp );
        return SweepHelpers::collideIntoObject<Shape>( source, sweep, collision,
            bsp->boundingBox(), instance.inverse, state );
    }
};

struct StockStaticModelSceneLoader::State::TickHandler : ITickOperationTypeHandler
{
    virtual void doTick( SceneObject * objects, size_t count, float )
    {
        std::vector<State *> visited;
        for (size_t n = 0; n < count; ++n)
        {
            State * owner = objects[n].getAs<Instance>()->owner;
            if (std::find( visited.begin(), visited.end(), owner ) != visited.end()) continue;
            visited.push_back( owner );
            if (!owner->bound) continue;
            // 6c25d0 -> 6d1f70 changes the actual current/previous palette slot
            // once per mapping tick, independently of animation evaluation.
            owner->previousPose = owner->currentPose;
            owner->currentPose = (owner->currentPose + 1) & 1;
        }
    }
    virtual void doUpdateAnimations( SceneObject * objects, size_t count, float dTime )
    {
        std::vector<State *> visited;
        for (size_t n = 0; n < count; ++n)
        {
            State * owner = objects[n].getAs<Instance>()->owner;
            if (std::find( visited.begin(), visited.end(), owner ) != visited.end()) continue;
            visited.push_back( owner );
            auto live = owner->shared_from_this();
            if (live->bound) live->updateAnimations( dTime );
        }
    }
};

void StockStaticModelSceneLoader::registerHandlers( Scene & scene )
{
    scene.getObjectOperation<DrawOperation>()->addHandler<State::Instance, State::DrawHandler>();
    scene.getObjectOperation<CollisionOperation>()->addHandler<State::Instance, State::CollisionHandler>();
    scene.getObjectOperation<SpatialQueryOperation>()->addHandler<State::Instance, State::SpatialHandler>();
    scene.getObjectOperation<TickOperation>()->addHandler<State::Instance, State::TickHandler>();
}

StockStaticModelSceneLoader::StockStaticModelSceneLoader( uint32 mappingMask ) :
    mappingMask_( mappingMask ) {}
StockStaticModelSceneLoader::~StockStaticModelSceneLoader() { doUnload(); }

bool StockStaticModelSceneLoader::itemIdentity( const SceneObject & object,
    uint32 & chunkID, uint32 & itemIndex )
{
    if (!object.isType<State::Instance>()) return false;
    const State::Instance * instance = object.getAs<State::Instance>();
    if (!instance || !instance->item.known) return false;
    chunkID = instance->item.chunkID;
    itemIndex = instance->item.itemIndex;
    return true;
}

bool StockStaticModelSceneLoader::doLoadFromSpace( ClientSpace * space, BinaryFormat & file,
    const DataSectionPtr &, const Matrix &, const StringTable & strings )
{
    if (state_ || unloadDepth_) return State::fail( "Previous mapping has not unloaded" );
    auto candidate = std::make_shared<State>();
    candidate->space = dynamic_cast<CompiledSpace *>( space );
    candidate->soundMapping = &file;
    if (!candidate->space) return State::fail( "BSMI requires the actual compiled-space owner" );
    // Original BSMI transforms are world matrices. The mapping matrix must not
    // be applied to a compiled placement a second time.
    if (!candidate->load( file, strings, mappingMask_ )) return false;
    state_ = candidate;
    return true;
}

bool StockStaticModelSceneLoader::doBind()
{
    auto state = state_;
    if (!state) return false;
    if (state->bound) return true;
    state->bound = true;
    state->updateAnimations( 0.f );
    state->previousPose = state->currentPose;
    state->soundOwner = acquireStockSpaceSounds( state->space );
    if (state_ != state) return false;
    for (auto & instance : state->instances)
    {
        if (!instance.active || instance.visibility.insideOut()) continue;
        // 6c2490 addresses DB.names by BSMO.sounds[model]. 6c24f0
        // composes the authored fragile hardpoint with the instance world;
        // its hardPoint string parameter is not a second lookup or override.
        const uint32 model = state->binary.instances.models[instance.index];
        const uint32 definition = state->binary.models.sounds[model];
        if (definition != 0xffffffffu && state->soundOwner)
        {
            State::Binary::Matrix localValues,worldValues;
            std::string error;
            if (!wot::stockSpaceModelSoundMatrices(state->binary,instance.index,localValues,worldValues,error))
                ERROR_MSG("StaticSceneSoundFeature: %s\n",error.c_str());
            else
            {
                const Matrix local=State::matrix(localValues),sourceWorld=State::matrix(worldValues);
                Matrix world;
                D3DXMatrixMultiply( &world, &local, &sourceWorld );
                const auto sound = addStockSpaceModelSound( state->soundOwner, definition, world );
                if (state_ != state)
                {
                    removeStockSpaceSound( state->soundOwner, sound );return false;
                }
                registerStockSpaceObjectSound( state->soundOwner,state->soundMapping,
                    false,static_cast<uint32>(instance.index),sound );
                state->soundHandles.push_back( sound );
            }
        }
        const auto handle = state->space->dynamicScene().addObject( instance.object,
            instance.world, instance.collision, instance.visibility );
        if (state_ != state)
        {
            state->space->dynamicScene().removeObject( handle );
            return false;
        }
        state->membership.push_back( handle );
    }
    return true;
}

void StockStaticModelSceneLoader::removeMembership( const std::shared_ptr<State> & state )
{
    state->bound = false;
    std::vector<StockSpaceSoundHandle> sounds;sounds.swap( state->soundHandles );
    for (const auto handle:sounds) removeStockSpaceSound( state->soundOwner, handle );
    state->soundOwner.reset();
    std::vector<DynamicObjectHandle> retired;
    retired.swap( state->membership );
    for (const auto & handle : retired) state->space->dynamicScene().removeObject( handle );
}

void StockStaticModelSceneLoader::doUnload()
{
    auto retired = state_;
    if (!retired) return;
    state_.reset();
    struct Retiring
    {
        uint32 & depth;
        explicit Retiring( uint32 & value ) : depth( value ) { ++depth; }
        ~Retiring() { --depth; }
    } retiring( unloadDepth_ );
    // Remove real scene membership before destroying animations, resource
    // leases, BSPs and compiled arrays. Synchronous observers retain State.
    removeMembership( retired );
}

float StockStaticModelSceneLoader::percentLoaded() const
{
    return state_ ? 1.f : 0.f;
}
}
BW_END_NAMESPACE
