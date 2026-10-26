#include "pch.hpp"
#include "stock_tree_scene.hpp"
#include "stock_scene_settings.hpp"
#include "stock_deferred_buffers.hpp"
#include "stock_tree_geometry.hpp"
#include "stock_tree_scene_data.hpp"
#include "stock_tree_lod.hpp"
#include "stock_tree_materials.hpp"
#include "stock_tree_wind.hpp"

#include "compiled_space/binary_format.hpp"
#include "compiled_space/collision_helpers.hpp"
#include "compiled_space/static_scene_types.hpp"
#include "compiled_space/string_table.hpp"
#include "moo/complex_effect_material.hpp"
#include "moo/device_callback.hpp"
#include "moo/draw_context.hpp"
#include "moo/effect_visual_context.hpp"
#include "moo/graphics_settings.hpp"
#include "moo/index_buffer.hpp"
#include "moo/render_context.hpp"
#include "moo/renderer.hpp"
#include "moo/texture_manager.hpp"
#include "moo/vertex_buffer.hpp"
#include "moo/vertex_declaration.hpp"
#include "resmgr/auto_config.hpp"
#include "resmgr/bin_section.hpp"
#include "resmgr/bwresource.hpp"
#include "romp/enviro_minder.hpp"
#include "romp/weather.hpp"
#include "scene/draw_operation.hpp"
#include "scene/collision_operation.hpp"
#include "scene/scene.hpp"
#include "scene/scene_draw_context.hpp"
#include "scene/scene_object.hpp"
#include "scene/spatial_query_operation.hpp"
#include "math/blend_transform.hpp"
#include "physics2/bsp.hpp"
#include "physics2/collision_obstacle.hpp"
#include "physics2/sweep_helpers.hpp"
#include "space/deprecated_space_helpers.hpp"
#include "space/client_space.hpp"

#include <atomic>
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <deque>
#include <limits>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

BW_BEGIN_NAMESPACE
namespace
{
BW::string engineString( const std::string & value )
{
    return BW::string( value.data(), value.size() );
}

float addTreeScalar( float a, float b ) { volatile float result = a + b; return result; }
float subTreeScalar( float a, float b ) { volatile float result = a - b; return result; }
float mulTreeScalar( float a, float b ) { volatile float result = a * b; return result; }
float divTreeScalar( float a, float b ) { volatile float result = a / b; return result; }

struct TreeDefinition
{
    Matrix world;
    uint32 resource;
    uint32 seed;
    uint32 flags;
    uint32 visibility;
};
static_assert( sizeof( TreeDefinition ) == 80, "Original SpTr3 rows must remain 80 bytes" );
static_assert( sizeof( std::array<float, 6> ) == 24, "Original SpTr3 wire bounds must remain 24 bytes" );

struct TreeInstanceConstants
{
    float rotation[4];
    float position[3];
    float uniformScale;
    float scale[3];
    float material;
    float windOffset;
    float blend;
    float alpha;
    float padding;
};
static_assert( sizeof( TreeInstanceConstants ) == 64, "Original tree instance constants must remain 64 bytes" );

struct TreeWindOwner : wot::StockTreeWindHistory
{
    wot::StockTreeWind value;
    float lastUpdate = -1.f;
};

struct TreeGpuPart
{
    Moo::VertexBuffer vertices;
    std::vector<Moo::IndexBuffer> lods;
    Moo::BaseTexturePtr diffuse;
    Moo::BaseTexturePtr normal;

    void release()
    {
        vertices.release();
        for (auto & buffer : lods) buffer.release();
        lods.clear();
        diffuse = NULL;
        normal = NULL;
    }
};

struct TreeType
{
    std::string resource;
    uint32 seed = 0;
    wot::StockTreeGeometry geometry;
    wot::StockTreeMaterials materials;
    wot::StockTreeLodConfiguration lod;
    std::shared_ptr<TreeWindOwner> wind;
    std::array<TreeGpuPart, 4> gpu;
    uint32 materialHandle = 0;
    bool registered = false;
    bool uploaded = false;
    // The retail old-ST4 constructor b00477 initialises this nullable owner.
    // Collision operations must consult it rather than substituting bounds.
    std::shared_ptr<BSPTree> collision;
    std::string error;

    void releaseGpu()
    {
        for (auto & part : gpu) part.release();
        uploaded = false;
    }
};

bool readTreeBytes( const std::string & resource, BinaryPtr & bytes, std::string & error )
{
    DataSectionPtr section = BWResource::openSection( engineString( resource ), false, BinSection::creator() );
    bytes = section ? section->asBinary() : NULL;
    if (!bytes || bytes->len() <= 0)
    {
        error = "Cannot read original tree resource: " + resource;
        return false;
    }
    return true;
}

bool loadTreeType( TreeType & type, const std::shared_ptr<TreeWindOwner> & defaultWind )
{
    BinaryPtr source, geometry;
    if (!readTreeBytes( type.resource, source, type.error )) return false;
    const std::uint8_t * data = reinterpret_cast<const std::uint8_t *>( source->cdata() );
    const std::size_t size = static_cast<std::size_t>( source->len() );
    std::size_t consumed = 0;
    if (!wot::decodeStockTreeMaterialPrefix( data, size, type.materials, consumed, type.error ) ||
        !wot::decodeStockTreeLodConfiguration( data, size, type.lod, type.error )) return false;
    const std::size_t extension = type.resource.find_last_of( '.' );
    if (extension == std::string::npos)
    {
        type.error = "Original tree resource has no cache extension";
        return false;
    }
    if (!readTreeBytes( type.resource.substr( 0, extension ) + ".ctree", geometry, type.error ) ||
        !wot::decodeStockTreeGeometry( reinterpret_cast<const std::uint8_t *>( geometry->cdata() ),
            static_cast<std::size_t>( geometry->len() ), type.geometry, type.error )) return false;
    for (std::size_t part = 0; part < 3; ++part)
    {
        if (type.geometry.parts[part].vertexCount &&
            type.geometry.parts[part].lodIndices.size() != static_cast<std::size_t>( type.lod.partCounts[part] ))
        {
            type.error = "Original SPT LOD count does not match its nonempty cached geometry";
            return false;
        }
    }
    type.wind = defaultWind;
    const std::size_t slash = type.resource.find_last_of( '/' );
    const std::string windResource = (slash == std::string::npos ? std::string() : type.resource.substr( 0, slash + 1 )) + "speedwind.ini";
    if (BWResource::fileExists( engineString( windResource ) ))
    {
        BinaryPtr windBytes;
        wot::StockTreeWindParameters parameters;
        auto wind = std::make_shared<TreeWindOwner>();
        std::string error;
        if (readTreeBytes( windResource, windBytes, error ) &&
            wot::decodeStockTreeWindParameters( windBytes->cdata(), static_cast<std::size_t>( windBytes->len() ), parameters, error ) &&
            wind->value.initialise( parameters, error ))
        {
            type.wind = wind;
        }
        else
        {
            // b05ba0 retains the actual loaded default wind when an existing
            // per-directory override fails. It does not fabricate animation.
            ERROR_MSG( "StockTreeScene: %s; retaining original default wind\n", error.c_str() );
        }
    }
    return true;
}

bool loadTreeTexture( const std::string & resource, Moo::BaseTexturePtr & texture )
{
    if (resource.empty()) { texture = NULL; return true; }
    if (!BWResource::fileExists( engineString( resource ) )) return false;
    texture = Moo::TextureManager::instance()->get( engineString( resource ), true, false, true, "Art/SpeedTree/Textures" );
    return texture && texture->pTexture() && texture->status() == Moo::BaseTexture::STATUS_READY;
}

bool uploadTreeGeometry( TreeType & type )
{
    type.releaseGpu();
    for (std::size_t part = 0; part < 4; ++part)
    {
        const auto & source = type.geometry.parts[part];
        auto & gpu = type.gpu[part];
        if (!loadTreeTexture( source.diffuseMap, gpu.diffuse ) || !loadTreeTexture( source.normalMap, gpu.normal ))
        {
            type.error = "Cannot load original cached-tree diffuse/normal texture";
            type.releaseGpu();
            return false;
        }
        if (!source.vertexCount) continue;
        if (source.vertices.size() > UINT_MAX || FAILED( gpu.vertices.create(
            static_cast<uint32>( source.vertices.size() ), D3DUSAGE_WRITEONLY, 0, D3DPOOL_MANAGED, "vertex buffer/original tree" ) ))
        {
            type.error = "Cannot create original tree vertex buffer";
            type.releaseGpu();
            return false;
        }
        void * vertices = NULL;
        if (FAILED( gpu.vertices.lock( 0, 0, &vertices, 0 ) ) || !vertices)
        {
            type.error = "Cannot lock original tree vertex buffer";
            type.releaseGpu();
            return false;
        }
        std::memcpy( vertices, source.vertices.data(), source.vertices.size() );
        if (FAILED( gpu.vertices.unlock() ))
        {
            type.error = "Cannot publish original tree vertex buffer";
            type.releaseGpu();
            return false;
        }
        gpu.lods.resize( source.lodIndices.size() );
        const D3DFORMAT format = source.vertexCount <= 65536 ? D3DFMT_INDEX16 : D3DFMT_INDEX32;
        for (std::size_t lod = 0; lod < source.lodIndices.size(); ++lod)
        {
            const auto & indices = source.lodIndices[lod];
            if (indices.empty()) continue;
            if (indices.size() > INT_MAX || FAILED( gpu.lods[lod].create( static_cast<int>( indices.size() ),
                format, D3DUSAGE_WRITEONLY, D3DPOOL_MANAGED, "index buffer/original tree" ) ))
            {
                type.error = "Cannot create original tree LOD index buffer";
                type.releaseGpu();
                return false;
            }
            Moo::IndicesReference destination = gpu.lods[lod].lock();
            if (!destination.valid())
            {
                type.error = "Cannot lock original tree LOD index buffer";
                type.releaseGpu();
                return false;
            }
            for (std::size_t index = 0; index < indices.size(); ++index)
                destination[static_cast<int32>( index )] = indices[index];
            if (FAILED( gpu.lods[lod].unlock() ))
            {
                type.error = "Cannot publish original tree LOD index buffer";
                type.releaseGpu();
                return false;
            }
        }
    }
    type.uploaded = true;
    type.error.clear();
    return true;
}

const AutoConfigString treeConfigurationResource( "system/speedTreeXML" );

class TreeQualitySetting : public Moo::GraphicsSetting
{
public:
    TreeQualitySetting() : GraphicsSetting( "SPEEDTREE_QUALITY", "SpeedTree quality", 0, false, false )
    {
        addOption( "MAX", "Maximum", true );
        addOption( "HIGH", "High", true );
        addOption( "MEDIUM", "Medium", true );
        addOption( "LOW", "Low", true );
    }
    // Selection is read from GraphicsSetting::activeOption by each draw. No
    // renderer pointer survives in the process-owned graphics-settings list.
    virtual void onOptionSelected( int ) {}
};

struct TreeConfiguration
{
    float mode = -2.f, nearDistance = 50.f, farDistance = 300.f;
    float yardstick = 130.f, variance = 1.f, leafRockFar = 400.f;
    std::array<float, 4> qualityBias = {{ 1.f, .8f, .6f, .4f }};
};

class TreeRenderingOwner : public Moo::DeviceCallback
{
public:
    bool initialise()
    {
        adapterVendor = Moo::rc().deviceInfo( Moo::rc().deviceIndex() ).identifier_.VendorId;
        DataSectionPtr config = BWResource::openSection( treeConfigurationResource );
        if (!config) return fail( "Original SpeedTree settings are unavailable" );
        configuration.mode = config->readFloat( "lodMode", configuration.mode );
        configuration.nearDistance = config->readFloat( "lodNear", configuration.nearDistance );
        configuration.farDistance = config->readFloat( "lodFar", configuration.farDistance );
        configuration.yardstick = config->readFloat( "lod0Yardstick", configuration.yardstick );
        configuration.variance = config->readFloat( "lod0Variance", configuration.variance );
        configuration.leafRockFar = config->readFloat( "leafRockFar", configuration.leafRockFar );
        const char * names[] = { "lods/MAX", "lods/HIGH", "lods/MEDIUM", "lods/LOW" };
        for (unsigned i = 0; i < 4; ++i)
            configuration.qualityBias[i] = config->readFloat( names[i], configuration.qualityBias[i] );
        if (configuration.mode == -1.f)
            return fail( "Original SDK-distance LOD mode requires its SPT distance owner" );
        const float scalars[] = { configuration.mode, configuration.nearDistance, configuration.farDistance,
            configuration.yardstick, configuration.variance, configuration.leafRockFar };
        for (float value : scalars)
            if (!std::isfinite( value )) return fail( "Non-finite original SpeedTree configuration" );
        for (float bias : configuration.qualityBias)
            if (!std::isfinite( bias ) || !(bias > 0.f)) return fail( "Invalid original SpeedTree quality bias" );
        if (!(configuration.farDistance > configuration.nearDistance) || !(configuration.yardstick > 0.f))
            return fail( "Invalid original SpeedTree LOD distances" );
        BinaryPtr bytes;
        std::string error;
        wot::StockTreeWindParameters parameters;
        defaultWind = std::make_shared<TreeWindOwner>();
        const BW::string windPath = config->readString( "speedWindINI", "speedtree/SpeedWind.ini" );
        if (!readTreeBytes( std::string( windPath.data(), windPath.size() ), bytes, error ) ||
            !wot::decodeStockTreeWindParameters( bytes->cdata(), static_cast<std::size_t>( bytes->len() ), parameters, error ) ||
            !defaultWind->value.initialise( parameters, error )) return fail( error );
        quality = Moo::GraphicsSetting::getFromLabel( "SPEEDTREE_QUALITY" );
        if (!quality)
        {
            quality = new TreeQualitySetting;
            Moo::GraphicsSetting::add( quality );
        }
        createManagedObjects();
        return gpuReady;
    }

    bool fail( const std::string & error )
    {
        ERROR_MSG( "StockTreeScene: %s\n", error.c_str() );
        return false;
    }

    std::shared_ptr<TreeType> acquire( const std::string & resource, uint32 seed )
    {
        const std::string key = resource + "/" + std::to_string( seed );
        {
            std::lock_guard<std::mutex> lock( mutex );
            if (!active) return std::shared_ptr<TreeType>();
            auto found = types.find( key );
            if (found != types.end()) return found->second;
        }
        auto type = std::make_shared<TreeType>();
        type->resource = resource;
        type->seed = seed;
        if (!loadTreeType( *type, defaultWind ))
        {
            fail( type->error );
            return std::shared_ptr<TreeType>();
        }
        std::lock_guard<std::mutex> lock( mutex );
        if (!active) return std::shared_ptr<TreeType>();
        auto inserted = types.emplace( key, type );
        if (inserted.second) pending.push_back( type );
        return inserted.first->second;
    }

    // Only the render thread touches registered IDs, GPU objects and atlas.
    // Background loads publish CPU owners through the short queue lock.
    bool beginFrame()
    {
        if (!gpuReady || !active) return false;
        const uint32 frame = Moo::rc().frameTimestamp();
        if (hasFrame && lastFrame == frame) return !atlasDirty;
        hasFrame = true;
        lastFrame = frame;
        std::shared_ptr<TreeType> candidate;
        {
            std::lock_guard<std::mutex> lock( mutex );
            if (!pending.empty()) { candidate = pending.front(); pending.pop_front(); }
        }
        bool changed = false;
        if (candidate)
        {
            if (uploadTreeGeometry( *candidate ))
            {
                candidate->materialHandle = handles.acquire();
                candidate->registered = true;
                changed = true;
            }
            else fail( candidate->error );
            candidate.reset();
        }
        std::vector<std::shared_ptr<TreeType>> retired;
        {
            std::lock_guard<std::mutex> lock( mutex );
            for (auto it = types.begin(); it != types.end(); )
            {
                if (it->second.use_count() == 1)
                {
                    auto & type = *it->second;
                    if (type.registered)
                    {
                        handles.release( type.materialHandle );
                        changed = true;
                    }
                    retired.push_back( it->second );
                    it = types.erase( it );
                }
                else ++it;
            }
        }
        // Device releases may enter the engine. Never hold the registry lock.
        retired.clear();
        atlasDirty = atlasDirty || changed;
        if (atlasDirty && updateAtlas()) atlasDirty = false;
        return !atlasDirty;
    }

    bool updateAtlas()
    {
        std::array<std::uint8_t, 1024> pixels = {};
        {
            std::lock_guard<std::mutex> lock( mutex );
            for (const auto & item : types)
                if (item.second->registered)
                    wot::writeStockTreeMaterialAtlasSlot( item.second->materials, item.second->materialHandle, pixels );
        }
        if (!atlas.hasComObject()) return false;
        ComObjectWrap<DX::Texture> staging = atlas;
        if (Moo::rc().usingD3DDeviceEx())
            staging = Moo::rc().createTexture( 2, 128, 1, 0, D3DFMT_A8R8G8B8, D3DPOOL_SYSTEMMEM, "SpeedTree/Materials staging" );
        D3DLOCKED_RECT locked;
        if (!staging.hasComObject() || FAILED( staging->LockRect( 0, &locked, NULL, 0 ) ))
            return fail( "Cannot lock original tree-material atlas" );
        for (unsigned row = 0; row < 128; ++row)
            std::memcpy( static_cast<char *>( locked.pBits ) + row * locked.Pitch, pixels.data() + row * 8, 8 );
        if (FAILED( staging->UnlockRect( 0 ) )) return fail( "Cannot publish original tree-material atlas" );
        if (Moo::rc().usingD3DDeviceEx() && FAILED( Moo::rc().device()->UpdateTexture( staging.pComObject(), atlas.pComObject() ) ))
            return fail( "Cannot upload original tree-material atlas" );
        return true;
    }

    virtual void deleteManagedObjects()
    {
        deleteGpuObjects();
        for (auto & material : materials) material = NULL;
    }

    void deleteGpuObjects()
    {
        gpuReady = false;
        atlas = NULL;
        std::vector<std::shared_ptr<TreeType>> snapshot;
        {
            std::lock_guard<std::mutex> lock( mutex );
            for (const auto & item : types) snapshot.push_back( item.second );
        }
        for (auto & type : snapshot) type->releaseGpu();
    }

    virtual void deleteUnmanagedObjects()
    {
        // D3D9Ex patches MANAGED buffers/textures to DEFAULT. They must be
        // released before ResetEx, while the logical type IDs remain owned.
        if (Moo::rc().usingD3DDeviceEx()) deleteGpuObjects();
    }

    virtual void createUnmanagedObjects()
    {
        if (Moo::rc().usingD3DDeviceEx() && !gpuReady && active) createGpuObjects();
    }

    virtual void createManagedObjects()
    {
        gpuReady = false;
        const char * effects[] = { "shaders/speedtree/branches.fx", "shaders/speedtree/branches.fx",
            "shaders/speedtree/leaves.fx", "shaders/speedtree/billboards.fx" };
        const char * declarations[] = { "speedtree_branch", "speedtree_branch", "speedtree_leaf", "speedtree_billboard" };
        for (unsigned part = 0; part < 4; ++part)
        {
            materials[part] = new Moo::ComplexEffectMaterial;
            vertexDeclarations[part] = Moo::VertexDeclaration::get( declarations[part] );
            if (!materials[part]->initFromEffect( effects[part] ) || !vertexDeclarations[part])
            {
                fail( "Cannot load original tree effect/declaration" );
                deleteManagedObjects();
                return;
            }
        }
        createGpuObjects();
    }

    void createGpuObjects()
    {
        gpuReady = false;
        atlas = Moo::rc().createTexture( 2, 128, 1, 0, D3DFMT_A8R8G8B8, D3DPOOL_MANAGED, "SpeedTree/MaterialsMap" );
        if (!updateAtlas()) { deleteGpuObjects(); return; }
        std::vector<std::shared_ptr<TreeType>> snapshot;
        {
            std::lock_guard<std::mutex> lock( mutex );
            for (const auto & item : types)
                if (item.second->registered) snapshot.push_back( item.second );
        }
        for (auto & type : snapshot)
            if (!uploadTreeGeometry( *type ))
            {
                fail( type->error );
                deleteGpuObjects();
                return;
            }
        gpuReady = true;
        atlasDirty = false;
    }
    virtual bool recreateForD3DExDevice() const { return true; }

    void shutdown()
    {
        active = false;
        deleteManagedObjects();
        std::lock_guard<std::mutex> lock( mutex );
        pending.clear();
        types.clear();
    }

    TreeConfiguration configuration;
    Moo::GraphicsSetting::GraphicsSettingPtr quality;
    std::shared_ptr<TreeWindOwner> defaultWind;
    std::array<SmartPointer<Moo::ComplexEffectMaterial>, 4> materials;
    std::array<Moo::VertexDeclaration *, 4> vertexDeclarations = {};
    ComObjectWrap<DX::Texture> atlas;
    std::atomic<bool> active{ true };
    float time = 0.f;
    uint32 adapterVendor = 0;
    bool gpuReady = false;

private:
    std::mutex mutex;
    std::map<std::string, std::shared_ptr<TreeType>> types;
    std::deque<std::shared_ptr<TreeType>> pending;
    wot::StockTreeMaterialHandles handles;
    uint32 lastFrame = 0;
    bool hasFrame = false, atlasDirty = false;
};

std::mutex renderingOwnerMutex;
std::shared_ptr<TreeRenderingOwner> renderingOwner;

std::shared_ptr<TreeRenderingOwner> currentRenderingOwner()
{
    std::lock_guard<std::mutex> lock( renderingOwnerMutex );
    return renderingOwner;
}

class TreeAtlasConstant : public Moo::EffectConstantValue
{
public:
    virtual bool operator()( ID3DXEffect * effect, D3DXHANDLE handle )
    {
        auto owner = currentRenderingOwner();
        return owner && owner->active && owner->gpuReady && owner->atlas.hasComObject() &&
            SUCCEEDED( effect->SetTexture( handle, owner->atlas.pComObject() ) );
    }
};

struct TreeInstance
{
    TreeDefinition definition;
    Matrix inverse;
    std::shared_ptr<TreeType> type;
    TreeInstanceConstants constants = {};
};

uint32 treeRandom( void * ) { return static_cast<uint32>( std::rand() ); }

bool advanceTreeWind( TreeRenderingOwner & owner, TreeWindOwner & wind, const Vector2 & weather )
{
    if (wind.lastUpdate == owner.time) return true;
    const float strength = std::min( mulTreeScalar( std::sqrt( addTreeScalar(
        mulTreeScalar( weather.x, weather.x ), mulTreeScalar( weather.y, weather.y ) ) ), .06666667f ), 1.f );
    // b06100 normalises the real weather direction before SDK Set normalises
    // its input again. Preserve both binary32 boundaries.
    D3DXVECTOR3 direction( weather.x, 0.f, weather.y );
    D3DXVec3Normalize( &direction, &direction );
    std::string error;
    if (!wind.value.setStrengthAndDirection( strength, {{ direction.x, direction.y, direction.z }}, error ) ||
        !wind.value.advance( owner.time, treeRandom, NULL, error )) return owner.fail( error );
    // b057d0 zeroes the current published matrices; its previous buffer is
    // identity. b06100 copies the former before publishing the first advance.
    wind.publish( wind.value.matrices(), wind.value.leafAngles() );
    wind.lastUpdate = owner.time;
    return true;
}

bool selectInstanceLods( TreeRenderingOwner & owner, TreeInstance & instance,
    const Vector3 & camera, wot::StockTreeLodValues & lod )
{
    const auto & config = owner.configuration;
    const auto & type = *instance.type;
    const auto & constants = instance.constants;
    float level = config.mode;
    if (level == -2.f)
    {
        const unsigned quality = static_cast<unsigned>( std::max( 0, std::min( 3, owner.quality->activeOption() ) ) );
        const float height = subTreeScalar( type.geometry.maximumBounds[1], type.geometry.minimumBounds[1] );
        const float y = std::max( constants.position[1], std::min( addTreeScalar( constants.position[1], height ), camera.y ) );
        const float dx = subTreeScalar( constants.position[0], camera.x );
        const float dy = subTreeScalar( y, camera.y );
        const float dz = subTreeScalar( constants.position[2], camera.z );
        const float squared = addTreeScalar( addTreeScalar( mulTreeScalar( dx, dx ), mulTreeScalar( dy, dy ) ), mulTreeScalar( dz, dz ) );
        const float zoom = Moo::rc().lodZoomFactor();
        const float distance = mulTreeScalar( std::sqrt( squared ), std::min( mulTreeScalar( 7.f - quality, zoom ), 1.f ) );
        const float nearDistance = mulTreeScalar( config.qualityBias[quality], config.nearDistance );
        const float farDistance = mulTreeScalar( config.qualityBias[quality], config.farDistance );
        level = addTreeScalar( subTreeScalar( 1.f, divTreeScalar( subTreeScalar( distance, nearDistance ),
            subTreeScalar( farDistance, nearDistance ) ) ), mulTreeScalar( subTreeScalar(
                divTreeScalar( mulTreeScalar( height, constants.scale[2] ), config.yardstick ), .5f ), config.variance ) );
        level = std::max( 0.f, std::min( level, std::max( zoom, .25f ) ) );
        if (level < .0001f) level = 0.f;
    }
    level = std::max( 0.f, std::min( level, 1.f ) );
    std::string error;
    if (!wot::selectStockTreeLods( type.lod, level, true, lod, error )) return owner.fail( error );
    return true;
}

bool setTreeValue( ID3DXEffect * effect, const char * name, const void * value, UINT bytes )
{
    D3DXHANDLE handle = effect->GetParameterByName( NULL, name );
    // Different real pass variants optimise unused constants out of the FX.
    return !handle || SUCCEEDED( effect->SetValue( handle, value, bytes ) );
}

bool setTreeBool( ID3DXEffect * effect, const char * name, bool value )
{
    D3DXHANDLE handle = effect->GetParameterByName( NULL, name );
    return !handle || SUCCEEDED( effect->SetBool( handle, value ) );
}

bool setTreeTexture( ID3DXEffect * effect, const char * name, const Moo::BaseTexturePtr & texture )
{
    D3DXHANDLE handle = effect->GetParameterByName( NULL, name );
    return !handle || SUCCEEDED( effect->SetTexture( handle, texture ? texture->pTexture() : NULL ) );
}

bool setTreeTypeConstants( TreeRenderingOwner & owner, TreeType & type, unsigned part,
    ID3DXEffect * effect, const float (&billboardRotation)[4] )
{
    // #1513 b03150 deliberately uses the leaf maps for fronds, as does its
    // original shader consumer. The cached frond map names are preserved.
    const auto & texturePart = type.gpu[part == wot::treeFrond ? wot::treeLeaf : part];
    const auto & material = part == wot::treeBranch ? type.materials.branch :
        (part == wot::treeFrond ? type.materials.frond : type.materials.leaf);
    const float colours[] = { material[0], material[1], material[2], 1.f, material[3], material[4], material[5], 1.f };
    const float angleScalars[] = { type.geometry.leafRockScalar, type.geometry.leafRustleScalar, 0.f, 0.f };
    const bool useStencil = wot::stockTreeShaderUsesStencil( owner.adapterVendor,
        Renderer::instance().pipelineType() == IRendererPipeline::TYPE_FORWARD_SHADING );
    bool result = setTreeValue( effect, "g_material", colours, sizeof( colours ) ) &&
        setTreeBool( effect, "alphaTestEnable", part != wot::treeBranch ) &&
        setTreeBool( effect, "g_texturedTrees", true ) &&
        setTreeBool( effect, "g_useZPrePass", false ) &&
        setTreeBool( effect, "g_useStencil", useStencil ) &&
        setTreeBool( effect, "g_branchesCullEnabled", part == wot::treeBranch ) &&
        setTreeBool( effect, "g_useHighQuality", owner.quality->activeOption() < 2 ) &&
        setTreeBool( effect, "g_useNormalMap", texturePart.normal != NULL ) &&
        setTreeTexture( effect, "g_diffuseMap", texturePart.diffuse ) &&
        setTreeTexture( effect, "g_normalMap", texturePart.normal ) &&
        setTreeValue( effect, "g_leafLightAdj", &type.materials.leafLightingAdjustment, sizeof( float ) ) &&
        setTreeValue( effect, "g_leafAngleScalars", angleScalars, sizeof( angleScalars ) ) &&
        setTreeValue( effect, "g_leafRockFar", &owner.configuration.leafRockFar, sizeof( float ) ) &&
        setTreeValue( effect, "g_billboardCameraRotation", billboardRotation, sizeof( billboardRotation ) );
    if (part != wot::treeBillboard && type.wind)
    {
        const auto & wind = *type.wind;
        result = result && setTreeValue( effect, "g_windMatrices", wind.currentMatrices.data(), sizeof( wind.previousMatrices ) ) &&
            setTreeValue( effect, "g_prevWindMatrices", wind.previousMatrices.data(), sizeof( wind.previousMatrices ) );
        if (part == wot::treeLeaf)
        {
            if (!wind.currentAngles.empty())
                result = result && setTreeValue( effect, "g_leafAngles", wind.currentAngles.data(),
                    static_cast<UINT>( wind.currentAngles.size() * sizeof( std::array<float, 4> ) ) );
            if (!wind.previousAngles.empty())
                result = result && setTreeValue( effect, "g_prevLeafAngles", wind.previousAngles.data(),
                    static_cast<UINT>( wind.previousAngles.size() * sizeof( std::array<float, 4> ) ) );
        }
    }
    return result;
}

struct TreeDrawItem
{
    TreeInstance * instance;
    wot::StockTreeLodValues lod;
};

bool drawTreePart( TreeRenderingOwner & owner, unsigned part, Moo::ERenderingPassType pass,
    const std::vector<TreeDrawItem> & items, const float (&billboardRotation)[4], float reflectionCorrection )
{
    Moo::EffectMaterialPtr material = owner.materials[part]->pass( pass, false );
    if (!material || !material->hTechnique() || !material->begin())
        return owner.fail( "Original tree effect has no usable real rendering-pass technique" );
    ID3DXEffect * effect = material->pEffect()->pEffect();
    D3DXHANDLE instanceHandle = effect->GetParameterByName( NULL, "g_instance" );
    bool result = instanceHandle != NULL;
    for (uint32 effectPass = 0; result && effectPass < material->numPasses(); ++effectPass)
    {
        if (!material->beginPass( effectPass )) { result = false; break; }
        for (const auto & item : items)
        {
            auto & instance = *item.instance;
            auto & type = *instance.type;
            const auto & source = type.geometry.parts[part];
            auto & gpu = type.gpu[part];
            if (!source.vertexCount) continue;
            std::int32_t lods[2] = { -1, -1 };
            float alphas[2] = { -1.f, -1.f };
            if (part == wot::treeBranch) { lods[0] = item.lod.branch; alphas[0] = item.lod.branchAlpha * .003921569f; }
            if (part == wot::treeFrond) { lods[0] = item.lod.frond; alphas[0] = item.lod.frondAlpha * .003921569f; }
            if (part == wot::treeLeaf)
                for (unsigned i = 0; i < 2; ++i) { lods[i] = item.lod.leaves[i]; alphas[i] = item.lod.leafAlpha[i] * .003921569f; }
            if (part == wot::treeBillboard && item.lod.billboardFade > 0.f)
            {
                lods[0] = 0;
                alphas[0] = addTreeScalar( mulTreeScalar( subTreeScalar( divTreeScalar( 84.f, 255.f ), 1.f ), item.lod.billboardFade ), 1.f );
            }
            if (lods[0] < 0 && lods[1] < 0) continue;
            result = setTreeTypeConstants( owner, type, part, effect, billboardRotation );
            if (pass == Moo::RENDERING_PASS_REFLECTION)
                result = result && setTreeValue( effect, "g_reflCorrection", &reflectionCorrection, sizeof( reflectionCorrection ) );
            result = result && SUCCEEDED( gpu.vertices.set( 0, 0, source.vertexStride ) );
            Moo::rc().setVertexDeclaration( owner.vertexDeclarations[part]->declaration() );
            for (unsigned i = 0; result && i < 2; ++i)
            {
                const std::int32_t lod = lods[i];
                if (lod < 0) continue;
                if (static_cast<std::size_t>( lod ) >= gpu.lods.size()) { result = false; break; }
                const auto & indices = source.lodIndices[lod];
                if (indices.empty()) continue;
                instance.constants.material = static_cast<float>( type.materialHandle * 4 + part );
                instance.constants.alpha = alphas[i];
                result = SUCCEEDED( effect->SetValue( instanceHandle, &instance.constants, sizeof( instance.constants ) ) ) &&
                    SUCCEEDED( gpu.lods[lod].set() ) && material->commitChanges();
                if (!result) break;
                const bool strip = part == wot::treeBranch || part == wot::treeFrond;
                const uint32 primitives = static_cast<uint32>( strip ? indices.size() - 2 : indices.size() / 3 );
                Moo::rc().drawIndexedPrimitive( strip ? D3DPT_TRIANGLESTRIP : D3DPT_TRIANGLELIST,
                    0, 0, source.vertexCount, 0, primitives );
            }
            if (!result) break;
        }
        setTreeTexture( effect, "g_diffuseMap", NULL );
        setTreeTexture( effect, "g_normalMap", NULL );
        material->endPass();
    }
    material->end();
    return result || owner.fail( "Cannot upload/draw original tree part constants and geometry" );
}

class TreeDrawHandler : public IDrawOperationTypeHandler
{
public:
    virtual void doDraw( const SceneDrawContext & context, const SceneObject * objects, size_t count )
    {
        auto owner = currentRenderingOwner();
        if (!owner || !owner->beginFrame()) return;
        ClientSpacePtr space = DeprecatedSpaceHelpers::cameraSpace();
        if (!space || !space->enviro().weather())
        {
            owner->fail( "Original tree wind requires the active camera-space weather" );
            return;
        }
        Matrix inverseView;
        if (!inverseView.invert( context.lodCameraView() )) return;
        const Vector3 camera = inverseView.applyToOrigin();
        const auto pass = context.drawContext().renderingPassType();
        const int decorLevel = stockDecorLevel();
        std::vector<TreeDrawItem> items;
        items.reserve( count );
        const Vector2 weather = space->enviro().weather()->wind();
        for (std::size_t i = 0; i < count; ++i)
        {
            auto * instance = objects[i].getAs<TreeInstance>();
            if (!instance || !instance->type || !instance->type->uploaded || !instance->type->registered) continue;
            const uint32 flags = instance->definition.flags;
            const uint32 requiredDecor = flags & 0x20 ? 2 : ((flags >> 4) & 1);
            if ((flags & 8) || (requiredDecor && static_cast<int>( requiredDecor ) > decorLevel) ||
                (pass == Moo::RENDERING_PASS_REFLECTION && !(flags & 2)) ||
                (pass == Moo::RENDERING_PASS_SHADOWS && !(flags & 1))) continue;
            if (!instance->type->wind || !advanceTreeWind( *owner, *instance->type->wind, weather )) continue;
            TreeDrawItem item;
            item.instance = instance;
            if (selectInstanceLods( *owner, *instance, camera, item.lod )) items.push_back( item );
        }
        // Native batches traverse the actual filename/seed registry order.
        std::stable_sort( items.begin(), items.end(), []( const TreeDrawItem & a, const TreeDrawItem & b ) {
            const auto & x = *a.instance->type;
            const auto & y = *b.instance->type;
            return x.resource + "/" + std::to_string( x.seed ) < y.resource + "/" + std::to_string( y.seed );
        } );
        if (items.empty()) return;
        float reflectionCorrection = 0.f;
        if (pass == Moo::RENDERING_PASS_REFLECTION)
        {
            std::string error;
            const auto settings = stockDeferredMaterialSettings();
            if (!wot::stockTreeReflectionCorrection( settings.categories[wot::stock_speedtree].albedo, reflectionCorrection, error ))
            { owner->fail( error ); return; }
        }
        D3DXVECTOR3 forward( Moo::rc().invView().applyToUnitAxisVector( 2 ) );
        D3DXVec3Normalize( &forward, &forward );
        const D3DXVECTOR3 up = std::fabs( forward.y ) <= .999999f ? D3DXVECTOR3( 0, 1, 0 ) : D3DXVECTOR3( 1, 0, 0 );
        D3DXVECTOR3 right, vertical;
        D3DXVec3Cross( &right, &forward, &up );
        D3DXVec3Normalize( &right, &right );
        D3DXVec3Cross( &vertical, &right, &forward );
        D3DXVec3TransformNormal( &right, &right, &Moo::rc().view() );
        D3DXVec3TransformNormal( &vertical, &vertical, &Moo::rc().view() );
        D3DXVec3Normalize( &right, &right );
        D3DXVec3Normalize( &vertical, &vertical );
        const float billboardRotation[] = { right.x, vertical.x, right.y, vertical.y };
        Moo::rc().effectVisualContext().worldMatrix( NULL );
        Moo::rc().effectVisualContext().isOutside( true );
        Moo::rc().effectVisualContext().updateSharedConstants( Moo::CONSTANTS_PER_VIEW );
        auto * pipeline = Renderer::instance().pipeline();
        if (!pipeline) return;
        pipeline->setupSystemStencil( IRendererPipeline::STENCIL_USAGE_SPEEDTREE );
        const unsigned order[] = { wot::treeBillboard, wot::treeFrond, wot::treeBranch, wot::treeLeaf };
        for (unsigned part : order)
            if (!drawTreePart( *owner, part, pass, items, billboardRotation, reflectionCorrection )) break;
        pipeline->restoreSystemStencil();
    }
};

class TreeSpatialHandler : public ISpatialQueryOperationTypeHandler
{
public:
    virtual void getWorldTransform( const SceneObject & object, Matrix * transform )
    {
        *transform = object.getAs<TreeInstance>()->definition.world;
    }
    virtual void getWorldVisibilityBoundingBox( const SceneObject & object, AABB * bounds )
    {
        const auto & instance = *object.getAs<TreeInstance>();
        *bounds = BoundingBox::s_insideOut_;
        if (!instance.type) return;
        const auto & geometry = instance.type->geometry;
        bounds->setBounds( Vector3( geometry.minimumBounds[0], geometry.minimumBounds[1], geometry.minimumBounds[2] ),
            Vector3( geometry.maximumBounds[0], geometry.maximumBounds[1], geometry.maximumBounds[2] ) );
        bounds->transformBy( instance.definition.world );
    }
};

class TreeCollisionHandler : public ICollisionTypeHandler
{
public:
    virtual bool doCollide( const SceneObject & object, const Vector3 & source,
        const Vector3 & extent, const SweepParams & sweep, CollisionState & state ) const
    {
        return collide( object, source, extent, sweep, state );
    }
    virtual bool doCollide( const SceneObject & object, const WorldTriangle & source,
        const Vector3 & extent, const SweepParams & sweep, CollisionState & state ) const
    {
        return collide( object, source, extent, sweep, state );
    }
private:
    template<class Shape> bool collide( const SceneObject & object, const Shape & source,
        const Vector3 &, const SweepParams & sweep, CollisionState & state ) const
    {
        const auto & instance = *object.getAs<TreeInstance>();
        // Exact 6c4e20/6c4ba0 -> afe2e0 tests the old type's nullable BSP
        // owner. Neither the visual bounds nor ctree triangles are collision.
        if (!instance.type || !instance.type->collision) return false;
        const BSPTree & bsp = *instance.type->collision;
        CollisionObstacle obstacle( &instance.definition.world, &instance.inverse, object );
        BSPCollisionInterface collision( obstacle, bsp );
        return SweepHelpers::collideIntoObject<Shape>( source, sweep, collision,
            bsp.boundingBox(), instance.inverse, state );
    }
};
}

bool initialiseStockTreeSceneRendering()
{
    if (currentRenderingOwner()) return true;
    auto owner = std::make_shared<TreeRenderingOwner>();
    if (!owner->initialise()) { owner->shutdown(); return false; }
    {
        std::lock_guard<std::mutex> lock( renderingOwnerMutex );
        renderingOwner = owner;
    }
    Moo::rc().effectVisualContext().registerAutoConstant(
        Moo::EffectVisualContext::AUTO_CONSTANT_TYPE_PER_FRAME, "SpeedTreeMaterials", new TreeAtlasConstant );
    return true;
}

void tickStockTreeSceneRendering( float dTime )
{
    auto owner = currentRenderingOwner();
    if (owner && owner->active)
        owner->time = addTreeScalar( owner->time, std::min( dTime, .05f ) );
}

void finaliseStockTreeSceneRendering()
{
    auto owner = currentRenderingOwner();
    if (!owner) return;
    Moo::rc().effectVisualContext().unregisterAutoConstant(
        Moo::EffectVisualContext::AUTO_CONSTANT_TYPE_PER_FRAME, "SpeedTreeMaterials" );
    owner->shutdown();
    std::lock_guard<std::mutex> lock( renderingOwnerMutex );
    renderingOwner.reset();
}

namespace CompiledSpace
{
struct StockTreeSceneHandler::Implementation
{
    std::vector<TreeInstance> instances;
    std::vector<SceneObject> objects;
    DataSectionPtr settings;
    uint32 mask = 0;
    bool maskProvided = false;
    bool loaded = false;
};

StockTreeSceneHandler::StockTreeSceneHandler() : implementation_( new Implementation ) {}
StockTreeSceneHandler::~StockTreeSceneHandler() { unload(); }

void StockTreeSceneHandler::visibilityMask( uint32 mask )
{
    MF_ASSERT( !implementation_->loaded );
    implementation_->mask = mask;
    implementation_->maskProvided = true;
}

void StockTreeSceneHandler::spaceSettings( const DataSectionPtr & settings )
{
    implementation_->settings = settings;
}

SceneTypeSystem::RuntimeTypeID StockTreeSceneHandler::typeID() const
{
    return StaticSceneTypes::StaticObjectType::SPEED_TREE;
}

SceneTypeSystem::RuntimeTypeID StockTreeSceneHandler::runtimeTypeID() const
{
    return SceneObject::typeOf<TreeInstance>();
}

bool StockTreeSceneHandler::load( BinaryFormat & file, StaticSceneProvider &,
    const StringTable & strings, SceneObject * objects, size_t count )
{
    return loadInstances( file, strings, objects, count );
}

bool StockTreeSceneHandler::load( BinaryFormat & file, const StringTable & strings,
    const DataSectionPtr & settings, uint32 mask )
{
    unload();
    visibilityMask( mask );
    spaceSettings( settings );
    return loadInstances( file, strings, NULL, size_t( -1 ) );
}

bool StockTreeSceneHandler::loadInstances( BinaryFormat & file, const StringTable & strings,
    SceneObject * objects, size_t count )
{
    unload();
    auto owner = currentRenderingOwner();
    if (!owner || !implementation_->maskProvided)
    {
        ERROR_MSG( "StockTreeScene: renderer or actual compiled-space visibility mask is unavailable\n" );
        return false;
    }
    const size_t section = file.findSection( FourCC( "SpTr" ) );
    if (section == BinaryFormat::NOT_FOUND || file.sectionVersion( section ) != 3)
        return owner->fail( "Original SpTr3 section is unavailable" );
    BinaryFormat::Stream * stream = file.openSection( section );
    if (!stream) return owner->fail( "Cannot map original SpTr3 section" );
    const size_t length = stream->length();
    const auto * bytes = static_cast<const std::uint8_t *>( stream->readRaw( length ) );
    wot::StockTreeSceneData decoded;
    std::string error;
    const bool valid = wot::decodeStockTreeSceneData( bytes, length, decoded, error );
    file.closeSection( stream );
    if (!valid) return owner->fail( error );
    const bool externalObjects = count != size_t( -1 );
    if (externalObjects && (decoded.definitions.size() != count || (count && !objects)))
        return owner->fail( "Original SpTr3 rows do not match static-scene object count" );
    count = decoded.definitions.size();
    implementation_->instances.resize( count );
    implementation_->objects.resize( count );
    for (size_t i = 0; i < count; ++i)
    {
        const auto & source = decoded.definitions[i];
        auto & instance = implementation_->instances[i];
        std::memcpy( &instance.definition.world, source.world.data(), 64 );
        instance.definition.resource = source.resource;
        instance.definition.seed = source.seed;
        instance.definition.flags = source.flags;
        instance.definition.visibility = source.visibility;
        SceneObject object;
        object.type<TreeInstance>();
        object.handle( &instance );
        object.flags().isDynamic( false );
        object.flags().isReflectionVisible( (source.flags & 2) != 0 );
        const bool shadow = (source.flags & 1) != 0 && (source.flags & 8) == 0;
        object.flags().isCastingStaticShadow( shadow );
        object.flags().isCastingDynamicShadow( shadow );
        implementation_->objects[i] = object;
        if (externalObjects) objects[i] = object;
        if (!(source.visibility & implementation_->mask)) continue;
        const char * resource = strings.entry( source.resource );
        if (!resource || !*resource)
        {
            owner->fail( "Original tree resource FNV key is absent from the string table" );
            continue;
        }
        const std::string filename( resource );
        if (filename.find( ".srt" ) != std::string::npos)
        {
            owner->fail( "SRT instance requires the separate original ST6 owner" );
            continue;
        }
        // afcf30 consumes the per-thread Win32 CRT RNG before type loading.
        instance.constants.windOffset = static_cast<float>( (std::rand() * 10) / 32767 );
        instance.constants.blend = 1.f;
        instance.constants.padding = 1.f;
        instance.type = owner->acquire( filename, source.seed );
        if (!instance.type) continue;
        if (!instance.inverse.invert( instance.definition.world ))
        {
            instance.type.reset();
            owner->fail( "Cannot invert original tree instance transform" );
            continue;
        }
        // Original 11bdcd0 is speedtree_util::decomposeMatrix. Its exact
        // normalise-rows/flip-Z algorithm is shared by BlendTransform::init;
        // D3DXMatrixDecompose has a different reflected-scale contract.
        const BlendTransform transform( instance.definition.world );
        const Quaternion & rotation = transform.rotation();
        const Vector3 & position = transform.translation();
        const Vector3 & scale = transform.scaling();
        instance.constants.rotation[0] = rotation.x;
        instance.constants.rotation[1] = rotation.y;
        instance.constants.rotation[2] = rotation.z;
        instance.constants.rotation[3] = rotation.w;
        for (unsigned component = 0; component < 3; ++component)
        {
            instance.constants.position[component] = position[component];
            instance.constants.scale[component] = scale[component];
        }
        instance.constants.uniformScale = mulTreeScalar( addTreeScalar( addTreeScalar( scale.x, scale.y ), scale.z ), .333f );
    }
    implementation_->loaded = true;
    return true;
}

bool StockTreeSceneHandler::bind()
{
    auto owner = currentRenderingOwner();
    if (!implementation_->loaded || !owner || !owner->active) return false;
    // b013e0 applies the real per-space near/far resource after the types load.
    DataSectionPtr settings = implementation_->settings;
    DataSectionPtr tree = settings ? settings->openSection( "speedtree" ) : NULL;
    if (tree)
    {
        owner->configuration.nearDistance = tree->readFloat( "lodNear", owner->configuration.nearDistance );
        owner->configuration.farDistance = tree->readFloat( "lodFar", owner->configuration.farDistance );
        if (!(owner->configuration.farDistance > owner->configuration.nearDistance))
            return owner->fail( "Invalid original space SpeedTree LOD distances" );
    }
    return true;
}

void StockTreeSceneHandler::unload()
{
    implementation_->objects.clear();
    implementation_->instances.clear();
    implementation_->loaded = false;
}

float StockTreeSceneHandler::loadPercent() const { return implementation_->loaded ? 1.f : 0.f; }

const SceneObject * StockTreeSceneHandler::objects() const
{
    return implementation_->objects.empty() ? NULL : implementation_->objects.data();
}

size_t StockTreeSceneHandler::objectCount() const { return implementation_->objects.size(); }

bool StockTreeSceneHandler::objectAvailable( size_t index ) const
{
    return index < implementation_->instances.size() && implementation_->instances[index].type != NULL;
}

void StockTreeSceneHandler::worldCollisionBounds( size_t index, BoundingBox & bounds ) const
{
    bounds = BoundingBox::s_insideOut_;
    if (index >= implementation_->instances.size()) return;
    const auto & instance = implementation_->instances[index];
    if (!instance.type || !instance.type->collision) return;
    bounds = instance.type->collision->boundingBox();
    bounds.transformBy( instance.definition.world );
}

void StockTreeSceneHandler::registerHandlers( Scene & scene )
{
    scene.getObjectOperation<DrawOperation>()->addHandler<TreeInstance, TreeDrawHandler>();
    scene.getObjectOperation<CollisionOperation>()->addHandler<TreeInstance, TreeCollisionHandler>();
    scene.getObjectOperation<SpatialQueryOperation>()->addHandler<TreeInstance, TreeSpatialHandler>();
}
}
BW_END_NAMESPACE
