#include "pch.hpp"
#include "stock_tps_scene.hpp"
#include "stock_tps_emitter.hpp"
#include "stock_tps_geometry.hpp"
#include "stock_tps_particle.hpp"
#include "stock_tps_model.hpp"
#include "stock_tps_visual.hpp"
#include "stock_tps_wind.hpp"
#include "stock_tps_quality.hpp"
#include "stock_tps_random.hpp"
#include "stock_particle_quality.hpp"
#include "stock_graphics_settings_consumers.hpp"
#include "stock_battleground_elements.hpp"
#include "app.hpp"
#include "particle/particle_system.hpp"
#include "resmgr/bwresource.hpp"
#include "resmgr/multi_file_system.hpp"
#include "moo/render_context.hpp"
#include "moo/renderer.hpp"
#include "romp/enviro_minder.hpp"
#include "romp/weather.hpp"
#include "space/client_space.hpp"
#include "space/deprecated_space_helpers.hpp"
#include <algorithm>
#include <atomic>
#include <cstdlib>
#include <cctype>
#include <cstring>
#include <map>
#include <memory>
#include <mutex>
#include <limits>

BW_BEGIN_NAMESPACE
namespace
{
using TPSVector = wot::StockTPSVector3;
TPSVector vector( const Vector3 & value ) { return { value.x, value.y, value.z }; }
Vector3 vector( const TPSVector & value ) { return Vector3( value.x, value.y, value.z ); }
TPSVector add( const TPSVector & a, const TPSVector & b ) { return { a.x + b.x, a.y + b.y, a.z + b.z }; }
TPSVector subtract( const TPSVector & a, const TPSVector & b ) { return { a.x - b.x, a.y - b.y, a.z - b.z }; }
TPSVector scale( const TPSVector & value, float multiplier )
{ return { value.x * multiplier, value.y * multiplier, value.z * multiplier }; }
TPSVector fieldVector( const wot::StockTPSEmitterResource & resource, std::size_t offset )
{ return { resource.scalar( offset ), resource.scalar( offset + 4 ), resource.scalar( offset + 8 ) }; }
TPSVector transformPoint( const Matrix & matrix, const TPSVector & value )
{ return vector( matrix.applyPoint( vector( value ) ) ); }
TPSVector transformVector( const Matrix & matrix, const TPSVector & value )
{ return vector( matrix.applyVector( vector( value ) ) ); }
TPSVector approximateNormal( const TPSVector & value )
{
    // f9e8c0's original approximation, rather than replacing it with sqrt.
    const float squared = value.x * value.x + value.y * value.y + value.z * value.z;
    std::uint32_t bits;
    std::memcpy( &bits, &squared, sizeof(bits) );
    bits = (0xbe800000u - bits) >> 1;
    float estimate;
    std::memcpy( &estimate, &bits, sizeof(estimate) );
    const float inverse = (1.47f - squared * 0.47f * estimate * estimate) * estimate;
    return scale( value, inverse );
}
BW::string resourceString( const wot::StockTPSEmitterResource & resource, std::size_t offset, std::size_t size )
{
    if (offset > resource.descriptor.size() || size > resource.descriptor.size() - offset) return BW::string();
    const char * first = reinterpret_cast<const char *>( resource.descriptor.data() + offset );
    const char * last = std::find( first, first + size, '\0' );
    BW::string result( first, last );
    std::replace( result.begin(), result.end(), '\\', '/' );
    if (!result.empty() && result[0] == '/') result.erase( 0, 1 );
    return result;
}

struct FxParticle
{
    wot::StockTPSParticle movement;
    float rotation = 0.f, rotationSpeed = 0.f, rotationMultiplier = 1.f;
    float size = 1.f, aspect = 1.f, shaderParameter = 0.f;
    std::uint32_t renderFlags = 0;
    TPSVector direction;
    std::array<float, 4> rectangle{ 0.f, 0.f, 1.f, 1.f };
    unsigned columns = 1, rows = 1, frames = 1;
    int previousFrame = 0, nextFrame = 0;
    float frameInterval = 1.f, frameElapsed = 0.f, streakLength = 0.f;
};

struct BirthContext
{
    Matrix world = Matrix::identity;
    bool explicitTransform = true;
    TPSVector position, direction, baseVelocity, inheritedVelocity, previousPosition;
    bool hasPreviousPosition = false;
    TPSVector motionPosition;
    bool hasMotionPosition = false;
    float sizeMinimum = 0.f, sizeRange = 0.f;
};
void prepareBirthContext( BirthContext & context, const wot::StockTPSEmitterResource & source )
{
    // fa1df0 stores the float range in the real emitter before any birth.
    context.sizeMinimum = source.scalar(0x194);
    context.sizeRange = source.scalar(0x198) - context.sizeMinimum;
}
TPSVector spawnPosition( const wot::StockTPSEmitterResource & source, const BirthContext & context,
    wot::StockTPSRandom & random, bool local, bool visual )
{
    const TPSVector offset = fieldVector(source,0x68);
    if (context.explicitTransform)
    {
        TPSVector point = offset;
        random.scatter(point,source.word(0x78),fieldVector(source,0x7c));
        return local ? point : transformPoint(context.world,point);
    }
    // f9e010's implicit path scatters the parent position before adding the
    // authored offset. Reordering those additions changes the native float.
    TPSVector point = local && !visual ? TPSVector{} : context.position;
    random.scatter(point,source.word(0x78),fieldVector(source,0x7c));
    return add(offset,point);
}
struct SceneEffect;
struct VisualParticle
{
    wot::StockTPSModelParticle state;
    std::weak_ptr<SceneEffect> trail;
    std::uint32_t renderFlags = 0;
    float size = 1.f;
    Matrix basis = Matrix::identity;
};
Matrix buildVisualBasis( const TPSVector & angles, float size, const Matrix & attachment )
{
    // fad5a0: the authored size scales the rotation before the source matrix.
    Matrix basis;
    D3DXMatrixRotationYawPitchRoll(reinterpret_cast<D3DXMATRIX *>(&basis),angles.x,angles.y,angles.z);
    if (size != 1.f)
    {
        D3DXMATRIX scaling;
        D3DXMatrixScaling(&scaling,size,size,size);
        D3DXMatrixMultiply(reinterpret_cast<D3DXMATRIX *>(&basis),&scaling,
            reinterpret_cast<const D3DXMATRIX *>(&basis));
    }
    D3DXMatrixMultiply(reinterpret_cast<D3DXMATRIX *>(&basis),
        reinterpret_cast<const D3DXMATRIX *>(&basis),reinterpret_cast<const D3DXMATRIX *>(&attachment));
    return basis;
}

bool spawnFx( const wot::StockTPSEmitterResource & source, BirthContext & context,
    wot::StockTPSRandom & random, FxParticle & particle, std::string & error )
{
    // fa03f0's ordinary source creates tPS_Particle_Fx only for type one.
    if (source.word( 0x110 ) != 1) return false;
    const std::uint32_t flags = source.word( 8 ), render = source.word( 0x1bc );
    particle.movement.source = &source;
    particle.rectangle = { source.scalar( 0x1a8 ), 1.f - source.scalar( 0x1ac ),
        source.scalar( 0x1b0 ), 1.f - source.scalar( 0x1a4 ) };
    particle.columns = source.word( 0x1c0 );
    particle.rows = source.word( 0x1c4 );
    if (render & 3u)
    {
        if (!particle.columns || !particle.rows || particle.columns > 65535 ||
            particle.rows > 65535 || particle.columns * particle.rows > 0x7fffffffu)
        {
            error = "Invalid authored tPS texture animation grid";
            return false;
        }
        if (render & 1u)
        {
            if ((source.word( 0xe8 ) & 0xffffu) > 1)
            {
                particle.renderFlags |= 1u;
                particle.frames = particle.columns * particle.rows;
                particle.frameInterval = source.scalar( 0x1c8 );
                if (render & 2u)
                    particle.previousFrame = static_cast<int>(std::round( float(particle.frames) * random.next() ));
                if (render & 0x40u) particle.renderFlags |= 0x40u;
            }
        }
        else if (render & 2u)
        {
            const int tile = static_cast<int>(std::round( float(particle.columns * particle.rows - 1) * random.next() ));
            const float width = (particle.rectangle[2] - particle.rectangle[0]) / float(particle.columns);
            const float height = (particle.rectangle[3] - particle.rectangle[1]) / float(particle.rows);
            const float u = float(tile % int(particle.columns)) * width + particle.rectangle[0];
            const float v = particle.rectangle[3] - float(tile / int(particle.columns) + 1) * height;
            particle.rectangle = { u, v, width + u, v + height };
        }
    }
    if (render & 0x80u) particle.renderFlags |= 0x80u;
    if (render & 8u) particle.renderFlags |= 8u;
    const float windLow = source.scalar( 0xa4 ), windHigh = source.scalar( 0xa8 );
    particle.movement.windResponse = windHigh <= windLow ? windLow : random.between( windLow, windHigh );
    particle.movement.lifetime = random.between( source.scalar( 0xf4 ), source.scalar( 0xf8 ) );
    TPSVector position = spawnPosition(source,context,random,(render & 4u)!=0,false);
    if (render & 4u) particle.renderFlags |= 4u;
    float gravityLow = source.scalar( 0x1b8 ), gravityHigh = source.scalar( 0x1d0 );
    if (gravityLow > gravityHigh) std::swap( gravityLow, gravityHigh );
    particle.movement.gravity = random.between( gravityLow, gravityHigh );
    particle.rotation = random.between( source.scalar( 0x88 ), source.scalar( 0x8c ) );
    particle.rotationSpeed = random.between( source.scalar( 0x90 ), source.scalar( 0x94 ) );
    if (render & 8u)
    {
        TPSVector horizontal = context.direction;
        horizontal.y = 0.f;
        horizontal = approximateNormal( horizontal );
        float angle = 0.f;
        if (horizontal.x * horizontal.z == 0.f)
            angle = horizontal.x == 0.f ? (horizontal.z > 0.f ? 0.f : 3.1415927f) :
                (horizontal.x > 0.f ? -1.5707964f : 1.5707964f);
        else
        {
            angle = std::acos( horizontal.z );
            if (horizontal.x > 0.f)
            {
                if (horizontal.z < 0.f) angle = -angle;
                else if (horizontal.z > 0.f) angle = 6.2831855f - angle;
            }
        }
        particle.rotation = angle + particle.rotation;
    }
    if (flags & 0x200000u)
    {
        const TPSVector normal = approximateNormal( context.direction );
        const auto circle = random.nextCircle();
        TPSVector basis{ circle[0], circle[1], 0.f };
        if (!(normal.z > .91f || normal.z < -.91f))
            basis = normal.x > .91f || normal.x < -.91f ? TPSVector{ 0.f, circle[1], circle[0] } :
                TPSVector{ circle[0], 0.f, circle[1] };
        particle.movement.additionalVelocity = { basis.y * normal.z - basis.z * normal.y,
            basis.z * normal.x - basis.x * normal.z, basis.x * normal.y - basis.y * normal.x };
        particle.movement.windMultiplier = random.between( source.scalar( 0x98 ), source.scalar( 0x9c ) );
    }
    particle.aspect = source.scalar( 0xa0 );
    particle.shaderParameter = source.scalar( 0x1f0 );
    TPSVector direction = context.direction;
    if (flags & 0x80000000u)
    {
        TPSVector target = fieldVector( source, 0x3e4 );
        const TPSVector extent = fieldVector( source, 0x3f0 );
        target.x = random.between( -extent.x, extent.x ) + target.x;
        target.y = random.between( -extent.y, extent.y ) + target.y;
        target.z = random.between( -extent.z, extent.z ) + target.z;
        if (!context.explicitTransform) target = add(target,context.position);
        else if (!(render & 4u)) target = transformPoint(context.world,target);
        direction = approximateNormal( subtract( position, target ) );
    }
    const float speed = random.between( source.scalar( 0x19c ), source.scalar( 0x1a0 ) );
    particle.movement.velocity = scale( direction, speed );
    const float jitter = source.scalar( 0x274 ) * speed;
    if (jitter != 0.f)
    {
        particle.movement.velocity.x = random.between( -jitter, jitter ) + particle.movement.velocity.x;
        particle.movement.velocity.y = random.between( -jitter, jitter ) + particle.movement.velocity.y;
        particle.movement.velocity.z = random.between( -jitter, jitter ) + particle.movement.velocity.z;
    }
    particle.size = random.next() * context.sizeRange + context.sizeMinimum;
    if (!(source.word(0xac) & 0x400u)) particle.movement.velocity = add(context.baseVelocity,particle.movement.velocity);
    particle.movement.delay = random.between( source.scalar( 0xfc ), source.scalar( 0x100 ) );
    particle.direction = render & 4u ? transformVector( context.world, context.direction ) : direction;
    particle.movement.position = position;
    const std::uint32_t masks[] = { 0x400u, 0u, 0x2000u, 0x10000u, 0x4000u, 0x8000u, 0x800u, 0x20000u };
    for (unsigned index = 0; index < 8; ++index)
        if ((flags & masks[index]) && source.curves[index].times.size() > 1)
            particle.movement.flags |= masks[index];
    if (source.scalar( 0xb4 ) > 0.f && context.hasPreviousPosition)
    {
        particle.movement.inheritedVelocityDuration = source.scalar( 0xb4 );
        particle.movement.inheritedVelocity = scale( context.inheritedVelocity, source.scalar( 0xb8 ) );
    }
    if (render & 0x4000000u)
    {
        const TPSVector delta = subtract( context.position, context.previousPosition );
        const float distance = std::sqrt( delta.y * delta.y + delta.x * delta.x + delta.z * delta.z );
        particle.streakLength = distance * .5f * source.scalar( 0x1ec ) + source.scalar( 0x1cc );
        particle.movement.flags |= 0x4000000u;
    }
    context.previousPosition = context.position;
    return true;
}

float particleScalar( const FxParticle & particle, unsigned index, float initial )
{
    std::array<float, 4> value{};
    const auto & curve = particle.movement.source->curves[index];
    return curve.sample( particle.movement.age / particle.movement.lifetime, value ) ? value[0] : initial;
}
void advanceDelayedParticle( wot::StockTPSParticle & particle, float elapsed )
{
    // f98f10 takes the direct fa3770 early branch before wind substeps.
    if (particle.flags & 1u) particle.flags |= 0x200u;
    else if (particle.delay > 0.f) particle.delay -= elapsed;
}

struct DrawChunk
{
    std::shared_ptr<wot::StockTPSDrawResource> resource;
    int kind = 0;
    unsigned bucket = 1;
    std::vector<wot::StockTPSCommonVertex> common;
    std::vector<wot::StockTPSOffsetVertex> offsets;
    std::vector<std::uint16_t> indices;
};
struct VisualDraw
{
    std::shared_ptr<StockTPSVisualResource> resource;
    Matrix world;
    Vector4 colour, lighting;
};
struct SceneSource;
struct Runtime
{
    Runtime() : random( []() { return std::rand(); } ), normalQuads( false ), verticalQuads( true )
    {
        quality = stockParticleQuality().project( stockBinocularsEnabled(),
            Renderer::instance().pipelineType() == IRendererPipeline::TYPE_FORWARD_SHADING, false );
    }
    wot::StockTPSRandom random;
    wot::StockTPSQuadCache normalQuads, verticalQuads;
    std::mutex resourceMutex;
    std::map<BW::string, std::shared_ptr<const wot::StockTPSResource>> resources;
    std::map<BW::string, std::shared_ptr<wot::StockTPSDrawResource>> renderResources;
    std::map<BW::string, std::shared_ptr<StockTPSVisualResource>> visualResources;
    std::vector<std::shared_ptr<SceneEffect>> effects;
    std::vector<std::shared_ptr<SceneEffect>> pendingEffects;
    std::vector<DrawChunk> chunks;
    std::vector<VisualDraw> visuals;
    unsigned modelCount = 0;
    wot::StockParticleQualityOutput quality{};
    TPSVector wind, weatherWind, camera;
    wot::StockTPSWind windClock;
    std::atomic<float> time{0.f};
    float density = 1.f, lastStep = 0.f;
    float fov = 1.0471975f;
    std::uint64_t generation = 0, submitted = 0;
    std::uint64_t visualGeneration = 0;
    std::atomic<bool> closed{false};
    ClientSpacePtr space;
    std::shared_ptr<const wot::StockTPSResource> preload( const BW::string &, std::string & );
    std::shared_ptr<wot::StockTPSDrawResource> renderer( const BW::string & texture );
    std::shared_ptr<StockTPSVisualResource> visual( const BW::string & name );
    void append( SceneSource &, FxParticle &, float, std::uint32_t );
    void appendVisual( SceneSource &, VisualParticle &, std::uint32_t );
    void updateTrail( VisualParticle & );
    void detachTrail( VisualParticle & );
    void tick( float );
    bool draw( unsigned bucket );
    bool drawVisuals();
    void close();
};
std::shared_ptr<Runtime> runtime;
// 01a1bfd0 with the manager's paused (+3d8) and global hidden (02143680) flags.
std::atomic<bool> drawEnabled{true};
std::shared_ptr<Runtime> currentRuntime()
{ return std::atomic_load_explicit(&runtime,std::memory_order_acquire); }
float currentRuntimeTime()
{
    const auto owner = currentRuntime();
    return owner ? owner->time.load(std::memory_order_relaxed) : 0.f;
}

struct SceneSource
{
    Runtime & runtime;
    const wot::StockTPSEmitterResource & resource;
    BirthContext transform;
    wot::StockTPSEmissionClock clock;
    std::vector<FxParticle> particles;
    std::vector<VisualParticle> models;
    std::shared_ptr<wot::StockTPSDrawResource> renderResource;
    std::shared_ptr<StockTPSVisualResource> visualResource;
    std::array<std::uint16_t, 12> shading{};
    float delay, lifetime, age = 0.f, effectiveRate;
    std::uint32_t flags = 0;
    int renderKind = 0;
    std::uint32_t contextMask = 0;
    bool failed = false, warming = false, loadFailureReported = false;

    SceneSource( Runtime & owner, const wot::StockTPSEmitterResource & source ) : runtime( owner ),
        resource( source ), clock( source.scalar( 0x74 ) <= 0.000001f ? 0.000001f : source.scalar( 0x74 ) ), delay( owner.random.between( source.scalar( 0x60 ), source.scalar( 0x64 ) ) ),
        lifetime( owner.random.between( source.scalar( 0x58 ), source.scalar( 0x5c ) ) ),
        effectiveRate( source.scalar( 0x74 ) <= 0.000001f ? 0.000001f : source.scalar( 0x74 ) )
    {
        prepareBirthContext(transform,source);
        if (source.word( 0xac ) & 0x4200u)
        {
            // These are distinct native emitter subclasses, not ordinary Fx
            // sources. Report the unimplemented operation rather than drawing
            // a plausible ordinary sprite for external or nested particles.
            ERROR_MSG( "tPS: native external/nested emitter subclass is not implemented: %s\n",
                resourceString( source, 0x10, 0x40 ).c_str() );
            failed = true;flags = 1u;return;
        }
        // fa1df0 clones resource curves into a real emitter before GPU use.
        const BW::string texture = resourceString( source, 0x114, 0x80 );
        if (source.word( 0xac ) & 0x800u)
            visualResource = runtime.visual( resourceString( source, 0x4e4, 0x100 ) );
        else
        {
            renderResource = runtime.renderer( texture );
            std::string error;
            // fa76c0/fa9f50 load the real Fx shader/texture while creating the
            // source; a material failure does not replace its CPU simulation.
            if (!renderResource->load(error))
            { ERROR_MSG("tPS: %s\n",error.c_str());loadFailureReported = true; }
        }
        BW::string shader = resourceString( source, 0x1f4, 0x80 );
        std::transform( shader.begin(), shader.end(), shader.begin(),
            [](unsigned char value) { return char(std::tolower(value)); } );
        renderKind = shader == "data/shaders/ps.fx" ? 3 : shader == "data/shaders/ps_long.fx" ? 4 :
            shader == "data/shaders/ps_solid.fx" ? 2 : 0;
        float values[12];
        for (unsigned index = 0; index < 12; ++index) values[index] = source.scalar( 0x434 + index * 4 );
        D3DXFLOAT16 converted[12];
        D3DXFloat32To16Array( converted, values, 12 );
        const unsigned order[] = { 1, 2, 3, 9, 10, 0, 4, 12, 5, 6, 7, 8 };
        for (unsigned index = 0; index < 12; ++index)
            if (order[index] != 12) std::memcpy( &shading[index], &converted[order[index]], 2 );
    }
    ~SceneSource()
    {
        for (auto & model : models) runtime.detachTrail( model );
        runtime.modelCount -= unsigned(models.size());
    }
    void setWorld( const Matrix & matrix )
    {
        // fa7c40: ordinary explicit transforms are real attachment matrices.
        transform.world = matrix;
        transform.explicitTransform = true;
        transform.position = transformPoint( matrix, fieldVector( resource, 0x68 ) );
        const std::uint32_t sourceFlags = resource.word( 8 );
        if (sourceFlags & 0x20000000u) transform.direction = vector( matrix.applyToUnitAxisVector(1) );
        else if (sourceFlags & 0x40000000u)
        {
            const Vector3 first = matrix.applyToUnitAxisVector(2), second = matrix.applyToUnitAxisVector(1);
            transform.direction = vector( first.crossProduct( second ) );
        }
        else if (resource.word( 0x1bc ) & 4u) transform.direction = { 0.f, 0.f, 1.f };
        else transform.direction = vector( matrix.applyToUnitAxisVector(2) );
    }
    void setImplicitPose( const TPSVector & position, const TPSVector & direction )
    {
        transform.explicitTransform = false;
        transform.world = Matrix::identity;
        transform.position = position;transform.direction = direction;
    }
    void multRate( float multiplier )
    {
        // fa2d30 rescales the pending clock before changing the source rate.
        const float base = resource.scalar( 0x74 );
        float remaining = clock.remaining;
        if (multiplier != 0.f && effectiveRate * base != 0.f)
            remaining = effectiveRate / base / multiplier * remaining;
        effectiveRate = base * multiplier;
        if (effectiveRate <= .000001f) effectiveRate = .000001f;
        else if (remaining > 4.f) remaining = runtime.lastStep * .75f;
        clock = wot::StockTPSEmissionClock( effectiveRate );clock.remaining = remaining;
    }
    void stop() { delay = 0.f;lifetime = -1.f; }
    bool finished() const { return (flags & 1u) && particles.empty() && models.empty(); }
    void advanceBase( float elapsed )
    {
        if (flags & 1u) return;
        if (delay > 0.f) { delay -= elapsed;return; }
        if (delay < 0.f) delay = 0.f;
        age += elapsed;
        if (lifetime < age) flags |= 1u;
    }
    void advanceChildren( float elapsed )
    {
        for (auto & model : models)
        {
            if (warming) model.state.warmup( elapsed, -1 );
            else
            {
                auto & movement = model.state.movement;
                const float height = model.state.rotationFlags & 8u ?
                    stockTPSGroundHeight( movement.position.x, movement.position.z ) : 0.f;
                runtime.updateTrail( model );
                model.state.tick( elapsed, height, -1 );
                if ((contextMask & 0x900000u) == 0x900000u) movement.flags |= 1u;
                if (!movement.retired()) runtime.appendVisual( *this, model, contextMask );
            }
        }
        const auto retired = std::remove_if( models.begin(), models.end(), [&](VisualParticle & model)
        {
            if (!model.state.movement.retired()) return false;
            runtime.detachTrail( model );--runtime.modelCount;return true;
        } );
        models.erase( retired, models.end() );
        for (auto & particle : particles)
        {
            if (warming) particle.movement.tick( elapsed, runtime.wind, -1, true );
            else
            {
                if ((contextMask & 0x900000u) == 0x900000u) particle.movement.flags |= 1u;
                if (particle.movement.delay > 0.f || (particle.movement.flags & 1u))
                    advanceDelayedParticle( particle.movement, elapsed );
                else
                {
                    particle.movement.tick( elapsed, runtime.wind, -1 );
                    runtime.append( *this, particle, elapsed, contextMask );
                }
            }
        }
        particles.erase( std::remove_if( particles.begin(), particles.end(),
            [](const FxParticle & value) { return value.movement.retired(); } ), particles.end() );
    }
    void emit()
    {
        if (failed) return;
        if (visualResource) { emitVisual();return; }
        const int count = wot::StockTPSEmissionClock::burstCount( resource, runtime.density );
        for (int index = 0; index < count; ++index)
        {
            FxParticle particle;
            std::string error;
            if (!spawnFx( resource, transform, runtime.random, particle, error ))
            {
                if (!error.empty()) ERROR_MSG( "tPS: %s\n", error.c_str() );
                return;
            }
            particles.push_back( std::move(particle) );
        }
    }
    void emitVisual();
    void warmup( float elapsed )
    {
        warming = true;
        const auto advance = [this]( float value ) { advanceChildren( value ); };
        if (elapsed < delay)
        {
            if (!particles.empty()) advance( elapsed );
            advanceBase( elapsed );
        }
        else if (delay <= 0.f && !(flags & 1u))
        {
            if (resource.word( 8 ) & 0x100u)
            {
                advance( elapsed );emit();delay = 0.f;lifetime = -1.f;advanceBase( elapsed );
            }
            else
            {
                const float scaled = runtime.density * elapsed;
                if (0.f <= clock.remaining - scaled)
                {
                    advance( elapsed );advanceBase( elapsed );clock.remaining -= scaled;
                }
                else
                {
                    const float interval = 1.f / effectiveRate;
                    if (scaled <= interval)
                    {
                        advance( elapsed );advanceBase( elapsed );emit();clock.remaining = interval;
                    }
                    else
                    {
                        float cursor = interval - clock.remaining;
                        if (cursor >= 0.f)
                        {
                            float step = cursor;
                            unsigned count = 0;
                            for (; cursor < scaled && count < 256; cursor += interval, ++count)
                            {
                                advance( step );emit();step = interval;
                            }
                            if (cursor < scaled) clock.remaining = interval;
                            else
                            {
                                clock.remaining = interval - (cursor - elapsed);
                                advance( clock.remaining );advanceBase( clock.remaining );
                            }
                        }
                        else { advance( elapsed );advanceBase( elapsed ); }
                    }
                }
            }
        }
        else { advance( elapsed );advanceBase( elapsed ); }
        warming = false;
    }
    void tick( float elapsed, std::uint32_t mask )
    {
        contextMask = mask;
        if (resource.scalar( 0xb4 ) > 0.f && elapsed > 0.f)
        {
            const TPSVector current = transform.explicitTransform ? vector(transform.world.applyToOrigin()) : transform.position;
            if (transform.hasMotionPosition)
                transform.inheritedVelocity = scale( subtract( current, transform.motionPosition ), 1.f / elapsed );
            transform.motionPosition = current;
            transform.hasMotionPosition = transform.hasPreviousPosition = true;
        }
        if (delay > 0.f)
        {
            if (!particles.empty()) advanceChildren( elapsed );
            advanceBase( elapsed );
            return;
        }
        if (!(flags & 1u) && !(mask & 0x800000u))
        {
            if (resource.word( 8 ) & 0x100u)
            {
                advanceChildren( elapsed );emit();delay = 0.f;lifetime = -1.f;
                advanceBase( elapsed );
                return;
            }
            clock.advance( elapsed, runtime.density, *this );
            return;
        }
        advanceChildren( elapsed );advanceBase( elapsed );
    }
};

struct SceneGroup
{
    const wot::StockTPSEffectResource & resource;
    std::vector<std::unique_ptr<SceneSource>> sources;
    explicit SceneGroup( Runtime & runtime, const wot::StockTPSEffectResource & value ) : resource( value )
    {
        for (const auto & source : resource.emitters)
            if (!(source.word(8) & 0x400000u)) sources.emplace_back( new SceneSource( runtime, source ) );
    }
};
struct SceneEffect
{
    std::shared_ptr<const wot::StockTPSResource> lease;
    std::vector<std::unique_ptr<SceneGroup>> groups;
    Matrix world = Matrix::identity;
    bool dynamicLods = true;
    ClientSpacePtr owningSpace = DeprecatedSpaceHelpers::cameraSpace();
    explicit SceneEffect( Runtime & runtime, std::shared_ptr<const wot::StockTPSResource> source,
        const Matrix & initialWorld ) : lease( std::move(source) ), world( initialWorld )
    {
        dynamicLods = (lease->descriptor()[34] & 1u) != 0;
        const TPSVector delta = subtract(vector(initialWorld.applyToOrigin()),runtime.camera);
        const auto * performer = stockTPSPerformer();
        const float distance = std::sqrt( delta.y * delta.y + delta.x * delta.x + delta.z * delta.z ) *
            (runtime.quality.lod_multiplier * (performer ? performer->lodMultiplier : 1.f)) +
            runtime.quality.lod_offset + (performer ? performer->lodOffset : 0.f);
        for (const auto & value : lease->effects())
        {
            if (!dynamicLods && (!(value.word( 0x58 ) & runtime.quality.type_mask) ||
                distance < value.scalar( 0x4c ) || distance > value.scalar( 0x50 ))) continue;
            groups.emplace_back( new SceneGroup( runtime, value ) );
        }
    }
    void multRate( float value )
    {
        for (auto & group : groups) for (auto & source : group->sources) source->multRate( value );
    }
    void resetMotion()
    {
        for (auto & group : groups) for (auto & source : group->sources) source->transform.hasMotionPosition = false;
    }
    void warmup( float elapsed )
    {
        for (auto & group : groups)
        {
            const float authored = group->resource.scalar( 0x54 );
            const float duration = authored <= elapsed ? elapsed : authored;
            if (duration > 0.f) for (auto & source : group->sources) source->warmup( duration );
        }
    }
    void stop()
    {
        for (auto & group : groups) for (auto & source : group->sources) source->stop();
    }
    void setWorld( const Matrix & value )
    {
        world = value;
        for (auto & group : groups) for (auto & source : group->sources) source->setWorld( value );
    }
    void setImplicitPose( const TPSVector & position, const TPSVector & direction )
    {
        world = Matrix::identity;world.translation(vector(position));
        for (auto & group : groups) for (auto & source : group->sources) source->setImplicitPose(position,direction);
    }
    bool finished() const
    {
        for (const auto & group : groups) for (const auto & source : group->sources)
            if (!source->finished()) return false;
        return true;
    }
    void tick( Runtime & runtime, float elapsed, const TPSVector & camera )
    {
        const TPSVector position = vector( world.applyToOrigin() );
        const TPSVector delta = subtract( position, camera );
        const float distance = std::sqrt( delta.y * delta.y + delta.x * delta.x + delta.z * delta.z );
        const auto * performer = stockTPSPerformer();
        const float lodMultiplier = runtime.quality.lod_multiplier * (performer ? performer->lodMultiplier : 1.f);
        const float lodOffset = runtime.quality.lod_offset + (performer ? performer->lodOffset : 0.f);
        const float secondaryMultiplier = runtime.quality.secondary_lod_multiplier * (performer ? performer->lodMultiplier : 1.f);
        const float secondaryOffset = runtime.quality.secondary_lod_offset + (performer ? performer->lodOffset : 0.f);
        const float primary = lodMultiplier * distance + lodOffset;
        const float secondary = secondaryMultiplier * distance + secondaryOffset;
        for (auto & group : groups)
        {
            std::uint32_t mask = 0x10u;
            const float minimum = group->resource.scalar( 0x4c ), maximum = group->resource.scalar( 0x50 );
            const float nearestLOD = primary <= secondary ? primary : secondary;
            if (!(group->resource.word( 0x58 ) & runtime.quality.type_mask)) mask |= 0x900000u;
            else if (dynamicLods)
            {
                if (nearestLOD <= maximum && minimum <= runtime.quality.lod_limit)
                {
                    if (primary < minimum || maximum < primary) mask |= 0x100000u;
                }
                else mask |= 0x900000u;
            }
            for (auto & source : group->sources) source->tick( elapsed, mask );
        }
    }
};

void SceneSource::emitVisual()
{
    int count = 1;
    if (resource.word(8) & 0x100u)
    {
        const float value = resource.scalar(0x74);
        count = value >= -2147483648.f && value < 2147483648.f ?
            static_cast<int>(value) : (std::numeric_limits<int>::min)();
    }
    for (int index = 0; index < count; ++index)
    {
        // fad7c0 enforces the original manager-wide live model budget.
        if (runtime.modelCount >= 8192) return;
        VisualParticle model;
        auto & state = model.state;
        auto & movement = state.movement;
        movement.source = &resource;
        state.rotationFlags = 7;
        const auto angle = [&](std::size_t offset)
        {
            const float centre = resource.scalar(offset), range = resource.scalar(offset + 12);
            const float minimum = centre - range;
            return ((range + centre) - minimum) * runtime.random.next() + minimum;
        };
        state.angles.x = angle(0x404);
        state.angularVelocity.x = runtime.random.between( resource.scalar(0x1d4), resource.scalar(0x1d8) );
        state.angles.y = angle(0x408);
        state.angularVelocity.y = runtime.random.between( resource.scalar(0x1dc), resource.scalar(0x1e0) );
        state.angles.z = angle(0x40c);
        state.angularVelocity.z = runtime.random.between( resource.scalar(0x1e4), resource.scalar(0x1e8) );
        std::string error;
        if (!visualResource->load(error))
        { ERROR_MSG( "tPS: %s\n", error.c_str() );return; }
        D3DXMatrixScaling( reinterpret_cast<D3DXMATRIX *>(&model.basis), .001f, .001f, .001f );
        const float lowWind = resource.scalar(0xa4), highWind = resource.scalar(0xa8);
        movement.windResponse = highWind <= lowWind ? lowWind : runtime.random.between(lowWind, highWind);
        movement.lifetime = runtime.random.between( resource.scalar(0xf4), resource.scalar(0xf8) );
        const std::uint32_t flags = resource.word(8), render = resource.word(0x1bc);
        model.renderFlags = render & 4u;
        TPSVector position = spawnPosition(resource,transform,runtime.random,(render & 4u)!=0,true);
        movement.position = position;
        const float gravityLow = resource.scalar(0x1b8), gravityHigh = resource.scalar(0x1d0);
        // f9f610 intentionally differs from the ordinary Fx gravity range.
        movement.gravity = gravityLow <= gravityHigh ? gravityLow : runtime.random.between(gravityLow, gravityHigh);
        if (flags & 0x200000u)
        {
            Vector3 initial(1.f,1.f,1.f);initial.normalise();
            const auto circle = runtime.random.nextCircle();
            movement.additionalVelocity = { -circle[1] * initial.y,
                circle[1] * initial.x - circle[0] * initial.z, circle[0] * initial.y };
            movement.windMultiplier = runtime.random.between( resource.scalar(0x98), resource.scalar(0x9c) );
            movement.flags |= 0x200000u;
        }
        TPSVector direction = transform.direction;
        if (flags & 0x80000000u)
        {
            TPSVector target = fieldVector(resource,0x3e4);
            const TPSVector extent = fieldVector(resource,0x3f0);
            target.x += runtime.random.between(-extent.x,extent.x);
            target.y += runtime.random.between(-extent.y,extent.y);
            target.z += runtime.random.between(-extent.z,extent.z);
            if (!transform.explicitTransform) target = add(target,transform.position);
            else if (!(render & 4u)) target = transformPoint(transform.world,target);
            direction = approximateNormal(subtract(position,target));
        }
        const float speed = runtime.random.between( resource.scalar(0x19c), resource.scalar(0x1a0) );
        movement.velocity = scale(direction,speed);
        const float jitter = resource.scalar(0x274) * speed;
        if (jitter != 0.f)
        {
            movement.velocity.x += runtime.random.between(-jitter,jitter);
            movement.velocity.y += runtime.random.between(-jitter,jitter);
            movement.velocity.z += runtime.random.between(-jitter,jitter);
        }
        model.size = runtime.random.next() * transform.sizeRange + transform.sizeMinimum;
        if (!(resource.word(0xac) & 0x400u)) movement.velocity = add(transform.baseVelocity,movement.velocity);
        const std::uint32_t masks[] = { 0x400u,0u,0x2000u,0x10000u,0x4000u,0x8000u,0x800u,0x20000u };
        for (unsigned curve = 0; curve < 8; ++curve)
            if ((flags & masks[curve]) && resource.curves[curve].times.size() > 1) movement.flags |= masks[curve];
        if (resource.word(0xac) & 0x2000u) state.rotationFlags |= 8u;
        const BW::string trail = resourceString(resource,0x114,0x80);
        if (!trail.empty() && _stricmp(trail.c_str(),"none") != 0)
        {
            const auto data = runtime.preload(trail,error);
            if (data)
            {
                Matrix world = Matrix::identity;world.translation(vector(position));
                auto effect = std::make_shared<SceneEffect>(runtime,data,world);
                effect->setImplicitPose(position,approximateNormal(movement.velocity));
                runtime.pendingEffects.push_back(effect);model.trail = effect;
            }
            else ERROR_MSG( "tPS: %s\n", error.c_str() );
        }
        models.push_back(std::move(model));++runtime.modelCount;
    }
    transform.previousPosition = transform.position;
}

std::shared_ptr<const wot::StockTPSResource> Runtime::preload( const BW::string & filename, std::string & error )
{
    std::lock_guard<std::mutex> lock( resourceMutex );
    if (closed)
    { error = "The native tPS resource owner is closed";return nullptr; }
    const auto found = resources.find( filename );
    if (found != resources.end()) return found->second;
    BinaryPtr bytes = BWResource::instance().fileSystem()->readFile( filename );
    if (!bytes)
    {
        error = "Cannot read original tPS resource: " + std::string(filename.c_str(), filename.size());
        return nullptr;
    }
    auto resource = std::make_shared<wot::StockTPSResource>();
    if (!resource->read( bytes->data(), bytes->len(), error )) return nullptr;
    resources.emplace( filename, resource );
    return resource;
}
std::shared_ptr<wot::StockTPSDrawResource> Runtime::renderer( const BW::string & texture )
{
    const auto found = renderResources.find( texture );
    if (found != renderResources.end()) return found->second;
    auto resource = createStockTPSDrawResource( texture );
    renderResources.emplace( texture, resource );
    return resource;
}
std::shared_ptr<StockTPSVisualResource> Runtime::visual( const BW::string & name )
{
    const auto found = visualResources.find(name);
    if (found != visualResources.end()) return found->second;
    auto resource = createStockTPSVisualResource(name);
    visualResources.emplace(name,resource);return resource;
}
void Runtime::detachTrail( VisualParticle & model )
{
    // f9d370 clears only the child's backlink. Its real Main lease continues
    // the authored emitter/particle lifetimes after the Visual disappears.
    model.trail.reset();
}
void Runtime::updateTrail( VisualParticle & model )
{
    if (model.state.stopped) return;
    if (const auto trail = model.trail.lock())
        trail->setImplicitPose(model.state.movement.position,approximateNormal(model.state.movement.velocity));
}
void Runtime::appendVisual( SceneSource & source, VisualParticle & model, std::uint32_t mask )
{
    if (!model.state.stopped)
    {
        const auto & state = model.state;
        const TPSVector angles = state.rotationFlags & 7u ? state.angles : TPSVector{};
        float size = model.size;
        std::array<float,4> sampled{};
        if ((state.movement.flags & 0x400u) && source.resource.curves[0].sample(
            state.movement.age / state.movement.lifetime,sampled)) size *= sampled[0];
        model.basis = buildVisualBasis(angles,size,source.transform.world);
    }
    if (!(model.renderFlags & 4u)) model.basis.translation(vector(model.state.movement.position));
    if (mask & 0x100000u) return;
    // fad520 admits up to 2048 live visual draw requests in one frame.
    if (visuals.size() >= 2048) return;
    VisualDraw draw;
    draw.resource = source.visualResource;draw.world = model.basis;
    draw.colour = Vector4(model.state.colour[0],model.state.colour[1],model.state.colour[2],model.state.colour[3]);
    D3DXFloat16To32Array(&draw.lighting.x,reinterpret_cast<const D3DXFLOAT16 *>(source.shading.data()),4);
    visuals.push_back(std::move(draw));
}
void Runtime::append( SceneSource & source, FxParticle & particle, float elapsed, std::uint32_t mask )
{
    if ((mask & 0x100000u) || source.failed) return;
    std::string error;
    if (!source.renderResource->load( error ))
    {
        if (!source.loadFailureReported) ERROR_MSG( "tPS: %s\n", error.c_str() );
        source.loadFailureReported = true;return;
    }
    source.loadFailureReported = false;
    const float fraction = particle.movement.lifetime > 0.f ? particle.movement.age / particle.movement.lifetime : 1.f;
    if (particle.movement.flags & 0x8000u) particle.rotationMultiplier = particleScalar( particle, 5, particle.rotationMultiplier );
    particle.rotation = particle.rotationMultiplier * particle.rotationSpeed * elapsed + particle.rotation;
    float aspect = particle.aspect;
    if (particle.movement.flags & 0x20000u) aspect = particleScalar( particle, 7, 1.f );
    wot::StockTPSCommonVertex common{};
    const TPSVector position = particle.renderFlags & 4u ? transformPoint( source.transform.world, particle.movement.position ) :
        particle.movement.position;
    common.position = { position.x, position.y, position.z };
    common.colour = { 1.f, 1.f, 1.f, 1.f };
    if (particle.movement.flags & 0x800u)
    {
        std::array<float, 4> colour{};
        if (source.resource.curves[6].sample( fraction, colour ))
        {
            const float multiplier = source.resource.scalar( 0x430 );
            common.colour = { colour[0] * multiplier, colour[1] * multiplier, colour[2] * multiplier,
                colour[3] > 1.f ? 1.f : colour[3] };
        }
    }
    common.parameters = { source.resource.scalar( 0x41c ) + 0.0125f, aspect,
        particle.shaderParameter, source.resource.scalar( 0x464 ) };
    common.shading = source.shading;
    float size = particle.size;
    if (particle.movement.flags & 0x400u) size = particleScalar( particle, 0, 1.f ) * size;
    const bool streak = (particle.movement.flags & 0x4000000u) != 0;
    const float renderSize = !(particle.renderFlags & 8u) && !streak ? size * 1.0471975f / fov : size;
    common.direction = { renderSize, 0.f, 0.f };
    const auto * cached = (particle.renderFlags & 8u ? verticalQuads : normalQuads).find(
        streak ? 0.7853982f : particle.rotation );
    if (!cached) return;
    wot::StockTPSQuad quad = *cached;
    if (streak)
    {
        Vector3 direction = vector(particle.direction);direction.normalise();
        common.direction = { direction.x < 0.f ? -renderSize : renderSize, direction.y, direction.z };
        const float length = particle.streakLength;
        quad.corners = { -renderSize, -length, 0.f, renderSize, -length, 0.f,
            -renderSize, length, 0.f, renderSize, length, 0.f };
    }
    int maximum = std::min( quality.tessellation_maximum, 3 );
    maximum = std::min( maximum, int(std::floor( source.resource.scalar( 0x460 ) * 3.f )) );
    const unsigned tessellation = unsigned(std::max( 0, std::min( maximum,
        int(std::floor( renderSize * quality.tessellation_scale )) ) ));
    auto firstFrame = particle.rectangle, nextFrame = particle.rectangle;
    if (particle.renderFlags & 1u)
    {
        particle.frameElapsed += elapsed;
        if (particle.frameInterval < particle.frameElapsed)
        {
            particle.previousFrame = particle.nextFrame;
            if (!(particle.renderFlags & 0x40u))
                particle.nextFrame = particle.nextFrame + 1 >= int(particle.frames) ? 0 : particle.nextFrame + 1;
            else if (particle.nextFrame + 1 < int(particle.frames)) ++particle.nextFrame;
            particle.frameElapsed -= particle.frameInterval;
        }
        const float width = (particle.rectangle[2] - particle.rectangle[0]) / float(particle.columns);
        const float height = (particle.rectangle[3] - particle.rectangle[1]) / float(particle.rows);
        const auto frame = [&]( int index )
        {
            const float u = float(index % int(particle.columns)) * width + particle.rectangle[0];
            const float v = particle.rectangle[3] - float(index / int(particle.columns) + 1) * height;
            return std::array<float, 4>{ u, v, u + width, v + height };
        };
        firstFrame = frame( particle.previousFrame );
        nextFrame = particle.renderFlags & 0x80u ? firstFrame : frame( particle.nextFrame );
        if (!(particle.renderFlags & 0x80u)) common.timing[0] = particle.frameElapsed / particle.frameInterval;
    }
    const unsigned bucket = classifyStockBattlegroundSprite( position.x, position.y, position.z );
    const unsigned vertices = (tessellation + 2) * (tessellation + 2);
    DrawChunk * chunk = nullptr;
    for (auto & previous : chunks)
        if (previous.resource == source.renderResource && previous.kind == source.renderKind &&
            previous.bucket == bucket) chunk = &previous;
    if (chunk && chunk->common.size() + vertices > 256) chunk = nullptr;
    if (!chunk)
    {
        // fa9670 permits 100 blocks of at most 256 vertices across the frame.
        if (chunks.size() >= 100) return;
        chunks.emplace_back();chunk = &chunks.back();
        chunk->resource = source.renderResource;chunk->kind = source.renderKind;chunk->bucket = bucket;
    }
    wot::appendStockTPSQuad( common, quad, firstFrame, nextFrame, tessellation,
        chunk->common, chunk->offsets, chunk->indices );
}

void Runtime::tick( float elapsed )
{
    if (closed) return;
    chunks.clear();visuals.clear();++generation;
    effects.insert(effects.end(),pendingEffects.begin(),pendingEffects.end());pendingEffects.clear();
    if (!std::isfinite( elapsed )) return;
    // fa4034 takes the frozen update while paused; nothing is emitted or drawn.
    if (!drawEnabled.load( std::memory_order_relaxed )) return;
    elapsed = std::max( 0.f, std::min( 1.f, elapsed ) );
    const ClientSpacePtr current = DeprecatedSpaceHelpers::cameraSpace();
    if (current && !current->cleared() && current->enviro().weather())
    {
        const Vector2 & value = current->enviro().weather()->wind();
        weatherWind = { value.x, 0.f, value.y };
    }
    wind = weatherWind;
    space = current;
    effects.erase( std::remove_if( effects.begin(), effects.end(), [&](const std::shared_ptr<SceneEffect> & effect)
        { return effect->owningSpace && (effect->owningSpace != current || effect->owningSpace->cleared()); } ), effects.end() );
    fov = Moo::rc().camera().fov();
    camera = vector( Moo::rc().invView().applyToOrigin() );
    quality = stockParticleQuality().project( stockBinocularsEnabled(),
        Renderer::instance().pipelineType() == IRendererPipeline::TYPE_FORWARD_SHADING, false );
    auto * performer = stockTPSPerformer();
    float simulationTime = quality.time_multiplier;
    if (performer)
    {
        performer->normalMultiplier( quality.lod_multiplier );
        performer->normalOffset( quality.lod_offset );
        performer->secondaryMultiplier( quality.secondary_lod_multiplier );
        performer->secondaryOffset( quality.secondary_lod_offset );
        simulationTime = performer->timeMultiplier( quality.time_multiplier );
    }
    if (quality.time_multiplier < 0.00001f) simulationTime = 0.00001f;
    simulationTime = simulationTime * elapsed;
    time.store(time.load(std::memory_order_relaxed) + elapsed,std::memory_order_relaxed);
    // fa4020 samples the old time coefficient before ticking performancer.
    tickStockTPSQuality( elapsed );
    if (simulationTime > 4.f)
    {
        if (performer) performer->freeze( true );
        density = performer ? performer->density : 1.f;
        for (auto & effect : effects) effect->warmup( simulationTime - .125f );
        if (performer) performer->freeze( false );
        simulationTime = .125f;
    }
    density = performer ? performer->density : 1.f;
    simulationTime = std::max( simulationTime, .00005f );
    wind = windClock.advance(weatherWind,simulationTime,random);
    simulationTime += std::numeric_limits<float>::epsilon();
    lastStep = simulationTime;
    for (auto & effect : effects) effect->tick( *this, simulationTime, camera );
    effects.erase( std::remove_if( effects.begin(), effects.end(),
        [](const std::shared_ptr<SceneEffect> & effect) { return effect->finished(); } ), effects.end() );
}
bool Runtime::draw( unsigned bucket )
{
    if (closed) return false;
    bool success = true;
    const float aspect = Moo::rc().camera().aspectRatio();
    for (const auto & chunk : chunks)
    {
        if (chunk.bucket != bucket) continue;
        const wot::StockTPSDrawParameters parameters = { quality.render_quality,
            chunk.kind == 2 || chunk.kind == 3 ? 0 : chunk.kind == 4 ? 2 : 1,
            0, 0, chunk.kind == 2 ? 1 : 0, aspect > .0001f ? 1.f / aspect : .75f, 1.f };
        std::string error;
        if (!chunk.resource->draw( chunk.common, chunk.offsets, chunk.indices, parameters, error ))
        {
            ERROR_MSG( "tPS: %s\n", error.c_str() );success = false;
        }
    }
    return success;
}
bool Runtime::drawVisuals()
{
    if (closed) return false;
    if (visualGeneration == generation) return true;
    visualGeneration = generation;
    bool success = true;
    for (const auto & visual : visuals)
    {
        std::string error;
        if (!visual.resource->draw(visual.world,visual.colour,visual.lighting,error))
        { ERROR_MSG("tPS: %s\n",error.c_str());success = false; }
    }
    return success;
}
void Runtime::close()
{
    closed = true;
    for (auto & effect : effects) effect->stop();
    effects.clear();pendingEffects.clear();chunks.clear();visuals.clear();renderResources.clear();visualResources.clear();
    std::lock_guard<std::mutex> lock( resourceMutex );resources.clear();
}

class InfluxPSA : public ParticleSystemAction
{
public:
    InfluxPSA() : lastStart_( currentRuntimeTime() ) {}
    ~InfluxPSA() override { stop(); }
    ParticleSystemActionPtr clone() const override
    {
        InfluxPSA * copy = new InfluxPSA();
        ParticleSystemAction::clone( copy );
        copy->filename_ = filename_;
        copy->localEffect_ = localEffect_;
        return copy;
    }
    std::size_t sizeInBytes() const override { return sizeof(*this); }
    int typeID() const override { return 16; }
    const BW::string & nameID() const override { return stockInfluxActionName(); }
    void execute( ParticleSystem & system, float elapsed ) override
    {
        if (delay_ > age_ || elapsed <= 0.f) { age_ += elapsed;return; }
        sync( system );
    }
    void lateExecute( ParticleSystem & system, float ) override { sync( system ); }
    void resetMotion() { resetMotion_ = true; }
    void multRate( float value )
    {
        rateMultiplier_ = value;
        if (const auto effect = effect_.lock()) effect->multRate( value );
    }
private:
    void sync( ParticleSystem & system )
    {
        const auto owner = currentRuntime();
        if (!owner || owner->closed) return;
        const Matrix world = system.worldTransform();
        if (dirty_)
        {
            const float now = owner->time.load(std::memory_order_relaxed);
            const float gap = now - lastStart_;
            if (!filename_.empty() && enabled_)
            {
                stop();
                std::string error;
                const auto resource = owner->preload( filename_, error );
                if (resource)
                {
                    auto effect = std::make_shared<SceneEffect>( *owner, resource, world );
                    effect->setWorld( world );
                    effect->multRate( rateMultiplier_ );
                    owner->pendingEffects.push_back( effect );
                    effect_ = effect;
                }
                else ERROR_MSG( "tPS: %s\n", error.c_str() );
            }
            dirty_ = false;
            if (const auto effect = effect_.lock()) effect->warmup( gap > 2.f ? gap : -1.f );
            if (gap > 2.f) lastStart_ = now;
        }
        if (const auto effect = effect_.lock())
        {
            if (resetMotion_) effect->resetMotion();
            effect->setWorld( world );
        }
        resetMotion_ = false;
    }
protected:
    void loadInternal( DataSectionPtr section ) override
    {
        filename_ = section->readString( "fn_influx_effect", filename_ );
        const BW::string forward = section->readString( "fn_influx_effect_forward", "" );
        const BW::string deferred = section->readString( "fn_influx_effect_deferred", "" );
        localEffect_ = section->readBool( "b_is_local_effect", localEffect_ );
        if (!forward.empty() && !deferred.empty())
            filename_ = Renderer::instance().pipelineType() == IRendererPipeline::TYPE_FORWARD_SHADING ? forward : deferred;
        dirty_ = true;
        const auto owner = currentRuntime();
        if (owner && !filename_.empty())
        {
            std::string error;
            if (!owner->preload( filename_, error )) ERROR_MSG( "tPS: %s\n", error.c_str() );
        }
    }
    void saveInternal( DataSectionPtr section ) const override
    {
        section->writeString( "fn_influx_effect", filename_ );
        section->writeString( "fn_influx_effect_forward", "" );
        section->writeString( "fn_influx_effect_deferred", "" );
        section->writeBool( "b_is_local_effect", localEffect_ );
    }
private:
    void stop() { if (const auto effect = effect_.lock()) effect->stop();effect_.reset(); }
    BW::string filename_;
    bool dirty_ = true, localEffect_ = false, resetMotion_ = false;
    float lastStart_ = 0.f, rateMultiplier_ = 1.f;
    std::weak_ptr<SceneEffect> effect_;
};

class PyInfluxPSA : public PyParticleSystemAction
{
    Py_Header( PyInfluxPSA, PyParticleSystemAction )
public:
    explicit PyInfluxPSA( ParticleSystemActionPtr action ) : PyParticleSystemAction( action, &s_type_ ) {}
    int typeID() const override { return 16; }
    PY_METHOD_DECLARE( py_MultRate );
    PY_FACTORY_METHOD_LINK_DECLARE()
};
PY_TYPEOBJECT( PyInfluxPSA )
PY_BEGIN_METHODS( PyInfluxPSA )
    PY_METHOD( MultRate )
PY_END_METHODS()
PY_BEGIN_ATTRIBUTES( PyInfluxPSA )
PY_END_ATTRIBUTES()
// Original Pixie.InfluxPSA exposes the type with its inherited constructor;
// resource loading and generic createPyAction construct the real native owner.
PY_FACTORY_NAMED( PyInfluxPSA, "InfluxPSA", Pixie )
PyObject * PyInfluxPSA::py_MultRate( PyObject * args )
{
    if (PyTuple_Size( args ) != 1)
    {
        PyErr_SetString( PyExc_TypeError, "MultRate expects one float" );return NULL;
    }
    float value;
    if (!Script::setData( PyTuple_GET_ITEM(args, 0), value, "MultRate argument 1" )) return NULL;
    auto * action = dynamic_cast<InfluxPSA *>( pAction().getObject() );
    if (!action) { PyErr_SetString( PyExc_RuntimeError, "Invalid native Influx action owner" );return NULL; }
    action->multRate( value );Py_RETURN_NONE;
}
struct DeferredTPSDraw
{
    std::shared_ptr<Runtime> runtime;
    std::uint64_t generation;
};
std::vector<DeferredTPSDraw> deferredDraws;
class TPSDrawItem : public Moo::DrawContext::UserDrawItem
{
public:
    explicit TPSDrawItem( std::shared_ptr<Runtime> value ) : runtime_(std::move(value)), generation_(runtime_->generation) {}
    void draw() override
    {
        if (!live()) return;
        runtime_->draw( 0 );runtime_->drawVisuals();runtime_->draw( 1 );
    }
    void drawSplit()
    {
        if (!live()) return;
        runtime_->draw( 0 );
        deferredDraws.push_back( { runtime_, generation_ } );
    }
private:
    bool live() const { return !runtime_->closed && runtime_->generation == generation_; }
    std::shared_ptr<Runtime> runtime_;
    std::uint64_t generation_;
};
class TPSVisualDrawItem : public Moo::DrawContext::UserDrawItem
{
public:
    explicit TPSVisualDrawItem( std::shared_ptr<Runtime> value ) : runtime_(std::move(value)), generation_(runtime_->generation) {}
    void draw() override
    {
        if (!runtime_->closed && runtime_->generation == generation_) runtime_->drawVisuals();
    }
private:
    std::shared_ptr<Runtime> runtime_;
    std::uint64_t generation_;
};
}

const BW::string & stockInfluxActionName()
{
    static const BW::string value( "Influx" );return value;
}
ParticleSystemActionPtr createStockInfluxPSA() { return new InfluxPSA(); }
PyParticleSystemActionPtr wrapStockInfluxPSA( ParticleSystemAction * action )
{
    return PyParticleSystemActionPtr( new PyInfluxPSA( action ), true );
}
void resetStockInfluxMotion( ParticleSystemAction * action )
{
    if (auto * influx = dynamic_cast<InfluxPSA *>( action )) influx->resetMotion();
}
bool playStockTPSOneShot( const BW::string & filename, const Vector3 & position, std::string & error )
{
    error.clear();
    const auto owner = currentRuntime();
    if (!owner || owner->closed) { error = "tPS runtime is unavailable";return false; }
    const auto resource = owner->preload( filename, error );
    if (!resource) return false;
    Matrix world;
    world.setTranslate( position );
    auto effect = std::make_shared<SceneEffect>( *owner, resource, world );
    for (const auto & group : effect->groups) for (const auto & source : group->sources)
        if (source->failed) { error = "tPS one-shot emitter could not be created: " + filename;return false; }
    // fa7fa0 -> fa7f40 -> f9a2c0 writes only the emitter position. The
    // f9c7d0 constructor's direction remains (0,1,0), and no explicit
    // attachment-matrix flag is set by fa16e0.
    effect->setImplicitPose( TPSVector{position.x,position.y,position.z}, TPSVector{0.f,1.f,0.f} );
    // fa16e0 submits the independent effect to the same manager and marks it
    // for retirement. No PSA last-start timer or externally owned weak view is
    // involved; Runtime retains it until every source and particle finishes.
    if (owner->closed) { error = "tPS runtime closed during one-shot creation";return false; }
    owner->pendingEffects.push_back( std::move(effect) );
    return true;
}
bool initialiseStockTPSRuntime()
{
    if (!initialiseStockTPSQuality()) return false;
    ensureStockTPSPerformer();
    const auto owner = currentRuntime();
    if (!owner || owner->closed)
        std::atomic_store_explicit(&runtime,std::make_shared<Runtime>(),std::memory_order_release);
    return true;
}
void tickStockTPSRuntime( float elapsed ) { if (const auto owner = currentRuntime()) owner->tick( elapsed ); }
bool drawStockTPSRuntime()
{
    const auto owner = currentRuntime();
    if (!owner || owner->closed || !drawEnabled.load( std::memory_order_relaxed ) ||
        (owner->chunks.empty() && owner->visuals.empty()) ||
        owner->submitted == owner->generation) return true;
    App::instance().drawContext( App::COLOUR_DRAW_CONTEXT ).drawUserItem(
        new TPSDrawItem( owner ), Moo::DrawContext::TRANSPARENT_CHANNEL_MASK );
    if (!owner->visuals.empty())
        App::instance().drawContext( App::COLOUR_DRAW_CONTEXT ).drawUserItem(
            new TPSVisualDrawItem( owner ), Moo::DrawContext::TRANSPARENT_CHANNEL_MASK );
    owner->submitted = owner->generation;
    return true;
}
void setStockTPSDrawEnabled( bool enabled ) { drawEnabled.store( enabled, std::memory_order_relaxed ); }
bool stockTPSDrawEnabled() { return drawEnabled.load( std::memory_order_relaxed ); }
bool isStockTPSParticleItem( Moo::DrawContext::UserDrawItem * item ) { return dynamic_cast<TPSDrawItem *>( item ) != NULL; }
void drawStockTPSParticleItem( Moo::DrawContext::UserDrawItem * item ) { static_cast<TPSDrawItem *>(item)->drawSplit(); }
void finishStockTPSParticleItems()
{
    std::vector<DeferredTPSDraw> pending;pending.swap( deferredDraws );
    for (const auto & draw : pending)
        if (!draw.runtime->closed && draw.runtime->generation == draw.generation) draw.runtime->draw( 1 );
}
void finiStockTPSRuntime()
{
    const auto owner = std::atomic_exchange_explicit(&runtime,std::shared_ptr<Runtime>{},std::memory_order_acq_rel);
    if (owner) owner->close();
    deferredDraws.clear();finiStockTPSQuality();
}
BW_END_NAMESPACE
