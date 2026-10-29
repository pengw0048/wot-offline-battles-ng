#include "pch.hpp"
#include "stock_tree_scene.hpp"
#include "stock_destructibles.hpp"
#include "stock_scene_settings.hpp"
#include "stock_deferred_buffers.hpp"
#include "stock_dynamic_resolution.hpp"
#include "stock_tree_geometry.hpp"
#include "stock_tree_hiding.hpp"
#include "stock_tree_scene_data.hpp"
#include "stock_tree_lod.hpp"
#include "stock_tree_materials.hpp"
#include "stock_tree_wind.hpp"

#include "cstdmf/profiler.hpp"
#include "compiled_space/binary_format.hpp"
#include "compiled_space/collision_helpers.hpp"
#include "compiled_space/static_scene_types.hpp"
#include "compiled_space/string_table.hpp"
#include "moo/complex_effect_material.hpp"
#include "moo/device_callback.hpp"
#include "moo/draw_context.hpp"
#include "moo/effect_material.hpp"
#include "moo/effect_visual_context.hpp"
#include "moo/fullscreen_quad.hpp"
#include "moo/graphics_settings.hpp"
#include "moo/index_buffer.hpp"
#include "moo/moo_utilities.hpp"
#include "moo/render_context.hpp"
#include "moo/render_target.hpp"
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
#include "pyscript/script.hpp"
#include "pyscript/script_math.hpp"
#include "space/deprecated_space_helpers.hpp"
#include "space/client_space.hpp"

#include <atomic>
#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <deque>
#include <functional>
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
    float windMultiplier;
};
static_assert( sizeof( TreeInstanceConstants ) == 64, "Original tree instance constants must remain 64 bytes" );

struct PackedTreeInstance
{
    float rotation[4];
    float position[3];
    std::int16_t scale[3];
    std::int16_t material;
    std::uint8_t windOffset, blend, alpha, windMultiplier;
};
static_assert( sizeof( PackedTreeInstance ) == 40, "Original stream 9 tree instances must remain 40 bytes" );

std::int32_t truncateTreeInstanceValue( float value )
{
    // CVTTSS2SI in afd8d0 also defines the out-of-range/NaN result.
    return !std::isfinite( value ) || value < -2147483648.f || value >= 2147483648.f ?
        std::numeric_limits<std::int32_t>::min() : static_cast<std::int32_t>( value );
}

PackedTreeInstance packTreeInstance( const TreeInstanceConstants & source )
{
    // #1513 afd8d0 consumes 64-byte CPU records and writes the actual
    // speedtree_instancing declaration: FLOAT4, FLOAT3, SHORT4, UBYTE4.
    PackedTreeInstance result;
    std::memcpy( result.rotation, source.rotation, sizeof( result.rotation ) );
    std::memcpy( result.position, source.position, sizeof( result.position ) );
    for (unsigned axis = 0; axis < 3; ++axis)
        result.scale[axis] = static_cast<std::int16_t>( truncateTreeInstanceValue(
            addTreeScalar( mulTreeScalar( source.scale[axis], 511.f ), .5f ) ) );
    result.material = std::max<std::int16_t>( 0,
        static_cast<std::int16_t>( truncateTreeInstanceValue( source.material ) ) );
    result.windOffset = static_cast<std::uint8_t>( truncateTreeInstanceValue( source.windOffset ) );
    result.blend = static_cast<std::uint8_t>( truncateTreeInstanceValue(
        addTreeScalar( mulTreeScalar( source.blend, 255.f ), .5f ) ) );
    result.alpha = static_cast<std::uint8_t>( truncateTreeInstanceValue(
        addTreeScalar( mulTreeScalar( source.alpha, 255.f ), .5f ) ) );
    result.windMultiplier = static_cast<std::uint8_t>( truncateTreeInstanceValue(
        addTreeScalar( mulTreeScalar( source.windMultiplier, 255.f ), .5f ) ) );
    return result;
}

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
    std::string sortKey;
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
    bool bush = false;
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

// b08c9d: faded leaves are drawn only while this boolean option is ON.
class SemitransparentLeavesSetting : public Moo::GraphicsSetting
{
public:
    SemitransparentLeavesSetting() :
        GraphicsSetting( "SEMITRANSPARENT_LEAVES_ENABLED", "Semitransparent leaves", 0, false, false )
    {
        addOption( "OFF", "Off", true );
        addOption( "ON", "On", true );
    }
    virtual void onOptionSelected( int ) {}
};

struct TreeInstance;

struct TreeDrawItem
{
    TreeInstance * instance;
    wot::StockTreeLodValues lod;
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
        semitransparentLeaves = Moo::GraphicsSetting::getFromLabel( "SEMITRANSPARENT_LEAVES_ENABLED" );
        if (!semitransparentLeaves)
        {
            semitransparentLeaves = new SemitransparentLeavesSetting;
            Moo::GraphicsSetting::add( semitransparentLeaves );
        }
        // b05940: lower-cased section names of the optional bush list.
        if (DataSectionPtr list = BWResource::openSection( "speedtree/bushes.xml" ))
            for (int i = 0; i < list->countChildren(); ++i)
            {
                const BW::string section = list->openChild( i )->sectionName();
                std::string name( section.data(), section.size() );
                for (auto & c : name) c = static_cast<char>( std::tolower( static_cast<unsigned char>( c ) ) );
                bushes.push_back( name );
            }
        createManagedObjects();
        createHidingTargets();
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
        type->sortKey = key;
        if (!loadTreeType( *type, defaultWind ))
        {
            fail( type->error );
            return std::shared_ptr<TreeType>();
        }
        const BW::string path = engineString( resource );
        const BW::StringRef file = BWResource::getFilename( path );
        std::string lower( file.data(), file.size() );
        for (auto & c : lower) c = static_cast<char>( std::tolower( static_cast<unsigned char>( c ) ) );
        type->bush = wot::stockTreeIsBush( bushes, lower );
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
        vertexDeclarations.fill( NULL );
        instancedVertexDeclarations.fill( NULL );
        for (auto & material : materials) material = NULL;
        for (auto & state : semitransparentMaterials)
            for (auto & material : state) material = NULL;
        upsampleMaterial = NULL;
        blurMaterial = NULL;
    }

    void deleteGpuObjects()
    {
        gpuReady = false;
        instanceBuffer.release();
        instanceCapacity = 0;
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
        instanceBuffer.release();
        instanceCapacity = 0;
        // D3D9Ex patches MANAGED buffers/textures to DEFAULT. They must be
        // released before ResetEx, while the logical type IDs remain owned.
        if (Moo::rc().usingD3DDeviceEx()) deleteGpuObjects();
        for (auto & target : hidingTargets) target = NULL;
    }

    virtual void createUnmanagedObjects()
    {
        if (Moo::rc().usingD3DDeviceEx() && !gpuReady && active) createGpuObjects();
        if (active) createHidingTargets();
    }

    // 9c4c20 registers two A16B16G16R16F targets of size -2 (998990: the back
    // buffer halved twice, truncated) with depth mode 3: each owns a
    // non-multisampled depth in the device depth format. 998170 allocates
    // and clears the colour to zero at creation and reset, so pSurface does.
    void createHidingTargets()
    {
        const auto & size = Moo::rc().backBufferDesc();
        const uint width = static_cast<uint>( mulTreeScalar( mulTreeScalar( float( size.Width ), .5f ), .5f ) );
        const uint height = static_cast<uint>( mulTreeScalar( mulTreeScalar( float( size.Height ), .5f ), .5f ) );
        const char * names[] = { "SpeedTree/hiding", "SpeedTree/hiding scratch" };
        for (unsigned i = 0; i < 2; ++i)
        {
            hidingTargets[i] = new Moo::RenderTarget( names[i] );
            hidingTargets[i]->clearOnRecreate( true );
            ComObjectWrap<DX::Surface> surface;
            if (!width || !height || !hidingTargets[i]->create( width, height, false, D3DFMT_A16B16G16R16F, NULL,
                D3DFMT_UNKNOWN, false ) || FAILED( hidingTargets[i]->pSurface( surface ) ))
            {
                hidingTargets[i] = NULL;
                fail( "Cannot create the original tree-hiding target" );
            }
        }
    }

    virtual void createManagedObjects()
    {
        gpuReady = false;
        const char * effects[] = { "shaders/speedtree/branches.fx", "shaders/speedtree/branches.fx",
            "shaders/speedtree/leaves.fx", "shaders/speedtree/billboards.fx" };
        const char * declarations[] = { "speedtree_branch", "speedtree_branch", "speedtree_leaf", "speedtree_billboard" };
        // b09906/b09939 combine the authored stream 9 with every tree part.
        Moo::VertexDeclaration * instances = Moo::VertexDeclaration::get( "speedtree_instancing" );
        if (!instances) { fail( "Cannot load original tree instance declaration" ); deleteManagedObjects(); return; }
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
            instancedVertexDeclarations[part] = Moo::VertexDeclaration::combine( vertexDeclarations[part], instances );
            if (!instancedVertexDeclarations[part])
            {
                fail( "Cannot combine original tree instance declaration" );
                deleteManagedObjects();
                return;
            }
        }
        // b0978c: the depth and alpha states of the faded-tree pass.
        const char * techniques[2][4] = { { "BRANCH_DEPTH", "FROND_DEPTH", "LEAF_DEPTH", "BILLBOARD_DEPTH" },
            { "BRANCH_ALPHA", "FROND_ALPHA", "LEAF_ALPHA", "BILLBOARD_ALPHA" } };
        for (unsigned state = 0; state < 2; ++state)
            for (unsigned part = 0; part < 4; ++part)
            {
                auto & material = semitransparentMaterials[state][part];
                material = new Moo::EffectMaterial;
                if (!material->initFromEffect( "shaders/speedtree/semitransparent.fx" ) ||
                    !material->hTechnique( techniques[state][part] ))
                {
                    fail( "Cannot load original semitransparent tree technique" );
                    deleteManagedObjects();
                    return;
                }
            }
        // 9c4680 and 9d62e0: the optimizer's own owners. Their absence stops
        // only the near-camera hiding path, never the tree renderer.
        upsampleMaterial = new Moo::EffectMaterial;
        if (!upsampleMaterial->initFromEffect( "shaders/speedtree/semitransparent.fx" ) ||
            !upsampleMaterial->hTechnique( "UPSAMPLE_RT" ))
        {
            upsampleMaterial = NULL;
            fail( "Cannot load the original tree-hiding upsample technique" );
        }
        if (!Moo::createEffect( blurMaterial, "shaders/auxiliary/bilateral_blur.fx" ))
        {
            blurMaterial = NULL;
            fail( "Cannot load the original tree-hiding blur" );
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
        semitransparent.clear();
        for (auto & target : hidingTargets) target = NULL;
        deleteManagedObjects();
        std::lock_guard<std::mutex> lock( mutex );
        pending.clear();
        types.clear();
    }

    TreeConfiguration configuration;
    Moo::GraphicsSetting::GraphicsSettingPtr quality, semitransparentLeaves;
    std::vector<std::string> bushes;
    std::shared_ptr<TreeWindOwner> defaultWind;
    std::array<SmartPointer<Moo::ComplexEffectMaterial>, 4> materials;
    std::array<std::array<Moo::EffectMaterialPtr, 4>, 2> semitransparentMaterials;
    Moo::EffectMaterialPtr upsampleMaterial, blurMaterial;
    std::array<Moo::RenderTargetPtr, 2> hidingTargets;
    // afde73 keeps the colour pass camera; afef60 drains the faded trees.
    Matrix alphaPassView = Matrix::identity, alphaPassProjection = Matrix::identity;
    std::vector<TreeDrawItem> semitransparent;
    std::array<Moo::VertexDeclaration *, 4> vertexDeclarations = {};
    std::array<Moo::VertexDeclaration *, 4> instancedVertexDeclarations = {};
    Moo::VertexBuffer instanceBuffer;
    uint32 instanceCapacity = 0;
    ComObjectWrap<DX::Texture> atlas;
    std::atomic<bool> active{ true };
    float time = 0.f;
    // 748370 samples the retained body even with zero SRT instances. The
    // shipped old-ST4 shaders do not consume this new-renderer matrix.
    std::array<float, 16> collisionBodyMatrix = {};
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
    BW::string filename;
    Matrix inverse;
    std::shared_ptr<TreeType> type;
    TreeInstanceConstants constants = {};
    // afe460 [record+38]/[record+3c]: leaf and branch fades of the last colour pass.
    float leavesAlpha = 1.f, branchAlpha = 1.f;
};

bool updateTreeInstanceTransform( TreeInstance & instance, const Matrix & world, std::string & error )
{
    const float * values = reinterpret_cast<const float *>( &world );
    for (unsigned index = 0; index < 16; ++index)
        if (!std::isfinite( values[index] )) { error = "Original tree transform is nonfinite"; return false; }
    Matrix inverse;
    if (!inverse.invert( world )) { error = "Cannot invert original tree instance transform"; return false; }
    instance.definition.world = world;
    instance.inverse = inverse;
    // 11bdcd0 uses the same normalise-rows/flip-Z algorithm as BlendTransform.
    const BlendTransform transform( world );
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
    error.clear();
    return true;
}

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

// distance is afe910's out value: 1e6 unless the authored distance mode ran.
bool selectInstanceLods( TreeRenderingOwner & owner, TreeInstance & instance,
    const Vector3 & camera, wot::StockTreeLodValues & lod, float & distance )
{
    const auto & config = owner.configuration;
    const auto & type = *instance.type;
    const auto & constants = instance.constants;
    float level = config.mode;
    distance = 1e6f;
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
        distance = mulTreeScalar( std::sqrt( squared ), std::min( mulTreeScalar( 7.f - quality, zoom ), 1.f ) );
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

// The active LODs of one part and their alpha-test references.
bool selectPartLods( unsigned part, const wot::StockTreeLodValues & lod, std::int32_t (&lods)[2], float (&alphas)[2] )
{
    lods[0] = lods[1] = -1;
    alphas[0] = alphas[1] = -1.f;
    if (part == wot::treeBranch) { lods[0] = lod.branch; alphas[0] = lod.branchAlpha * .003921569f; }
    if (part == wot::treeFrond) { lods[0] = lod.frond; alphas[0] = lod.frondAlpha * .003921569f; }
    if (part == wot::treeLeaf)
        for (unsigned i = 0; i < 2; ++i) { lods[i] = lod.leaves[i]; alphas[i] = lod.leafAlpha[i] * .003921569f; }
    if (part == wot::treeBillboard && lod.billboardFade > 0.f)
    {
        lods[0] = 0;
        alphas[0] = addTreeScalar( mulTreeScalar( subTreeScalar( divTreeScalar( 84.f, 255.f ), 1.f ), lod.billboardFade ), 1.f );
    }
    return lods[0] >= 0 || lods[1] >= 0;
}

struct TreePartInstance
{
    TreeType * type;
    unsigned lod;
    TreeInstanceConstants constants;
};

bool collectTreePartInstances( unsigned part, const std::vector<TreeDrawItem> & items,
    std::vector<TreePartInstance> & records )
{
    PROFILER_SCOPED( StockTrees_collectPart );
    // b05560/b05b00 collect each active part/LOD before drawing. The caller
    // already supplies the native filename/seed order; preserve it here.
    records.clear();
    for (std::size_t first = 0; first < items.size(); )
    {
        TreeType * type = items[first].instance->type.get();
        const std::size_t typeBegin = records.size();
        std::size_t end = first;
        while (end < items.size() && items[end].instance->type.get() == type) ++end;
        const auto & source = type->geometry.parts[part];
        if (source.vertexCount)
            for (std::size_t index = first; index < end; ++index)
            {
                std::int32_t lods[2];
                float alphas[2];
                if (!selectPartLods( part, items[index].lod, lods, alphas )) continue;
                for (unsigned i = 0; i < 2; ++i)
                {
                    // The exact producer has six 3D LOD buckets. Both active
                    // leaf LODs retain their own alpha reference and record.
                    if (lods[i] < 0 || lods[i] >= 6) continue;
                    const unsigned lod = static_cast<unsigned>( lods[i] );
                    if (lod >= source.lodIndices.size() || lod >= type->gpu[part].lods.size()) return false;
                    if (source.lodIndices[lod].empty()) continue;
                    TreePartInstance record = { type, lod, items[index].instance->constants };
                    record.constants.material = static_cast<float>( type->materialHandle * 4 + part );
                    record.constants.alpha = alphas[i];
                    records.push_back( record );
                }
            }
        std::stable_sort( records.begin() + typeBegin, records.end(),
            []( const TreePartInstance & a, const TreePartInstance & b ) { return a.lod < b.lod; } );
        first = end;
    }
    return true;
}

bool uploadTreeInstances( TreeRenderingOwner & owner, const std::vector<TreePartInstance> & records )
{
    PROFILER_SCOPED( StockTrees_upload );
    if (records.empty() || records.size() > UINT_MAX / sizeof( PackedTreeInstance )) return false;
    const uint32 count = static_cast<uint32>( records.size() );
    const uint32 bytes = count * sizeof( PackedTreeInstance );
    if (owner.instanceCapacity < count)
    {
        owner.instanceCapacity = 0;
        if (FAILED( owner.instanceBuffer.create( bytes, D3DUSAGE_DYNAMIC | D3DUSAGE_WRITEONLY, 0,
            D3DPOOL_DEFAULT, "vertex buffer/original tree instances" ) )) return false;
        owner.instanceCapacity = count;
    }
    void * destination = NULL;
    // One upload serves every type/LOD of this part. DISCARD lets the driver
    // retain earlier passes while this dynamic owner is reused.
    if (FAILED( owner.instanceBuffer.lock( 0, bytes, &destination, D3DLOCK_DISCARD ) ) || !destination) return false;
    auto * packed = static_cast<PackedTreeInstance *>( destination );
    for (std::size_t i = 0; i < records.size(); ++i) packed[i] = packTreeInstance( records[i].constants );
    return SUCCEEDED( owner.instanceBuffer.unlock() );
}

class TreeInstanceStreams
{
public:
    TreeInstanceStreams() : device_( Moo::rc().device() )
    {
        valid_ = device_ && SUCCEEDED( device_->GetStreamSourceFreq( 0, &frequency0_ ) ) &&
            SUCCEEDED( device_->GetStreamSourceFreq( 9, &frequency9_ ) ) &&
            SUCCEEDED( device_->GetStreamSource( 9, &buffer_, &offset_, &stride_ ) ) &&
            SUCCEEDED( device_->GetVertexDeclaration( &declaration_ ) ) &&
            SUCCEEDED( device_->GetFVF( &fvf_ ) );
    }
    ~TreeInstanceStreams()
    {
        if (!valid_) return;
        device_->SetStreamSourceFreq( 0, frequency0_ );
        device_->SetStreamSourceFreq( 9, frequency9_ );
        device_->SetStreamSource( 9, buffer_.pComObject(), offset_, stride_ );
        // SetFVF(0) is a no-op; an originally empty declaration must be
        // explicitly restored instead of leaving the tree declaration active.
        if (declaration_ || !fvf_) Moo::rc().setVertexDeclaration( declaration_.pComObject() );
        else Moo::rc().setFVF( fvf_ );
    }
    bool valid() const { return valid_; }
private:
    DX::Device * device_;
    UINT frequency0_ = 1, frequency9_ = 1, offset_ = 0, stride_ = 0;
    DWORD fvf_ = 0;
    ComObjectWrap<DX::VertexBuffer> buffer_;
    ComObjectWrap<DX::VertexDeclaration> declaration_;
    bool valid_ = false;
};

bool drawTreePart( TreeRenderingOwner & owner, unsigned part, Moo::ERenderingPassType pass,
    const std::vector<TreeDrawItem> & items, const float (&billboardRotation)[4], float reflectionCorrection )
{
    PROFILER_SCOPED( StockTrees_drawPart );
    std::vector<TreePartInstance> records;
    records.reserve( items.size() );
    if (!collectTreePartInstances( part, items, records )) return owner.fail( "Original tree LOD has no geometry" );
    if (records.empty()) return true;
    // #1513 b09bd0 preserves instancing for forward rendering too. Select the
    // actual annotated technique, rather than the public SDK's pipeline veto.
    bool instanced = owner.materials[part]->instanced();
    Moo::EffectMaterialPtr material = owner.materials[part]->pass( pass, instanced );
    if (instanced && (!material || !material->hTechnique()))
    {
        instanced = false;
        material = owner.materials[part]->pass( pass, false );
    }
    if (instanced && !uploadTreeInstances( owner, records )) return owner.fail( "Cannot upload original tree instances" );
    TreeInstanceStreams streams;
    if (!streams.valid()) return owner.fail( "Cannot retain the caller's tree instance streams" );
    auto * device = Moo::rc().device();
    Moo::VertexDeclaration * declaration = instanced ? owner.instancedVertexDeclarations[part] : owner.vertexDeclarations[part];
    if (!declaration || FAILED( device->SetStreamSourceFreq( 0, 1 ) ) ||
        FAILED( device->SetStreamSourceFreq( 9, instanced ? (D3DSTREAMSOURCE_INSTANCEDATA | 1) : 1 ) ) ||
        FAILED( Moo::rc().setVertexDeclaration( declaration->declaration() ) ))
        return owner.fail( "Cannot bind original tree instance streams" );
    if (!material || !material->hTechnique() || !material->begin())
        return owner.fail( "Original tree effect has no usable real rendering-pass technique" );
    ID3DXEffect * effect = material->pEffect()->pEffect();
    D3DXHANDLE instanceHandle = instanced ? NULL : effect->GetParameterByName( NULL, "g_instance" );
    bool result = instanced || instanceHandle != NULL;
    for (uint32 effectPass = 0; result && effectPass < material->numPasses(); ++effectPass)
    {
        if (!material->beginPass( effectPass )) { result = false; break; }
        TreeType * previousType = NULL;
        for (std::size_t first = 0; result && first < records.size(); )
        {
            auto & type = *records[first].type;
            const auto & source = type.geometry.parts[part];
            auto & gpu = type.gpu[part];
            const unsigned lod = records[first].lod;
            std::size_t end = first + 1;
            while (end < records.size() && records[end].type == &type && records[end].lod == lod) ++end;
            if (previousType != &type)
            {
                result = setTreeTypeConstants( owner, type, part, effect, billboardRotation );
                if (pass == Moo::RENDERING_PASS_REFLECTION)
                    result = result && setTreeValue( effect, "g_reflCorrection", &reflectionCorrection, sizeof( reflectionCorrection ) );
                result = result && SUCCEEDED( gpu.vertices.set( 0, 0, source.vertexStride ) );
                if (instanced) result = result && material->commitChanges();
                previousType = &type;
            }
            const auto & indices = source.lodIndices[lod];
            const bool strip = part == wot::treeBranch || part == wot::treeFrond;
            const uint32 primitives = static_cast<uint32>( strip ? indices.size() - 2 : indices.size() / 3 );
            result = result && SUCCEEDED( gpu.lods[lod].set() );
            if (instanced)
            {
                const uint32 count = static_cast<uint32>( end - first );
                result = result && SUCCEEDED( owner.instanceBuffer.set( 9,
                    static_cast<UINT>( first * sizeof( PackedTreeInstance ) ), sizeof( PackedTreeInstance ) ) ) &&
                    SUCCEEDED( device->SetStreamSourceFreq( 0, D3DSTREAMSOURCE_INDEXEDDATA | count ) );
                if (result) result = SUCCEEDED( Moo::rc().drawIndexedInstancedPrimitive(
                    strip ? D3DPT_TRIANGLESTRIP : D3DPT_TRIANGLELIST, 0, 0, source.vertexCount, 0, primitives, count ) );
            }
            else
                for (std::size_t i = first; result && i < end; ++i)
                {
                    result = SUCCEEDED( effect->SetValue( instanceHandle, &records[i].constants,
                        sizeof( TreeInstanceConstants ) ) ) && material->commitChanges();
                    if (result) result = SUCCEEDED( Moo::rc().drawIndexedPrimitive(
                        strip ? D3DPT_TRIANGLESTRIP : D3DPT_TRIANGLELIST, 0, 0, source.vertexCount, 0, primitives ) );
                }
            first = end;
        }
        setTreeTexture( effect, "g_diffuseMap", NULL );
        setTreeTexture( effect, "g_normalMap", NULL );
        result = material->endPass() && result;
    }
    result = material->end() && result;
    return result || owner.fail( "Cannot upload/draw original tree part constants and geometry" );
}

// #1513 SpeedTreeRenderer statics: s_pTarget 01d59f74, its b035a0 snapshot
// and the transparency state the BigWorld tree-hiding functions write.
struct TreeCollisionBodyState
{
    MatrixProviderPtr target;
    uint32 generation = 0;
    bool open = true;
};

TreeCollisionBodyState & treeCollisionBody()
{
    // Script/render teardown explicitly releases this Python-owned provider.
    static TreeCollisionBodyState * state = new TreeCollisionBodyState;
    return *state;
}

struct TreeHidingState
{
    MatrixProviderPtr target;
    wot::StockTreeHidingFrame frame;
    wot::StockTreeHidingSettings settings;
    // Each target change is a generation; the frame belongs to one of them.
    uint32 generation = 0, frameGeneration = ~0u;
    // Closed between script fini and the next script init.
    bool open = true;
};

TreeHidingState & treeHiding()
{
    // Never destroyed: a static destructor must not release a Python provider.
    static TreeHidingState * state = new TreeHidingState;
    return *state;
}

// The D3DX calls of afe460, so Windows reproduces the original arithmetic.
struct TreeHidingD3DX
{
    static void transformNormal( float * out, const float * v, const float * m )
    {
        D3DXVec3TransformNormal( reinterpret_cast<D3DXVECTOR3 *>( out ),
            reinterpret_cast<const D3DXVECTOR3 *>( v ), reinterpret_cast<const D3DXMATRIX *>( m ) );
    }
    static void inverse( float * m )
    {
        D3DXMatrixInverse( reinterpret_cast<D3DXMATRIX *>( m ), NULL, reinterpret_cast<const D3DXMATRIX *>( m ) );
    }
    static void transformPoint( float * out, const float * v, const float * m )
    {
        D3DXVECTOR4 result;
        D3DXVec3Transform( &result, reinterpret_cast<const D3DXVECTOR3 *>( v ), reinterpret_cast<const D3DXMATRIX *>( m ) );
        out[0] = result.x;
        out[1] = result.y;
        out[2] = result.z;
    }
};

// afe460 for one colour-pass instance. A faded tree waits for the
// semitransparent stage instead of the opaque batches (b05560).
bool fadeTreeInstance( TreeInstance & instance, const wot::StockTreeFadeInput & input )
{
    const TreeHidingState & hiding = treeHiding();
    const wot::StockTreeFade fade = wot::computeStockTreeFade<TreeHidingD3DX>(
        hiding.settings, hiding.target && hiding.frameGeneration == hiding.generation ? &hiding.frame : NULL, input );
    instance.leavesAlpha = fade.leaves;
    instance.branchAlpha = fade.branches;
    return fade.faded();
}

void treeBillboardRotation( float (&rotation)[4] )
{
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
    rotation[0] = right.x;
    rotation[1] = vertical.x;
    rotation[2] = right.y;
    rotation[3] = vertical.y;
}

ID3DXEffect * selectTreeBinding( Moo::EffectMaterial & material )
{
    if (!material.pEffect() || !material.properties().select( *material.pEffect() )) return NULL;
    return material.pEffect()->pEffect();
}

// b07e90/b08030/b081d0/b07e30: one part of one faded tree with a
// semitransparent.fx state. Leaves take the leaf fade, billboards stay opaque.
bool drawSemitransparentPart( TreeRenderingOwner & owner, Moo::EffectMaterial & material, unsigned part,
    const TreeDrawItem & item, const float (&billboardRotation)[4] )
{
    auto & instance = *item.instance;
    auto & type = *instance.type;
    const auto & source = type.geometry.parts[part];
    auto & gpu = type.gpu[part];
    std::int32_t lods[2];
    float alphas[2];
    if (!source.vertexCount || !selectPartLods( part, item.lod, lods, alphas )) return true;
    instance.constants.blend = part == wot::treeLeaf ? instance.leavesAlpha :
        (part == wot::treeBillboard ? 1.f : instance.branchAlpha);
    instance.constants.material = static_cast<float>( type.materialHandle * 4 + part );
    // begin() selects this material's private ARIE binding. Select it before
    // manual writes too, so they reach the same owner as beginPass/commit.
    ID3DXEffect * effect = selectTreeBinding( material );
    if (!effect) return owner.fail( "Cannot select original semitransparent tree material" );
    D3DXHANDLE instanceHandle = effect->GetParameterByName( NULL, "g_instance" );
    Moo::rc().setVertexDeclaration( owner.vertexDeclarations[part]->declaration() );
    bool result = instanceHandle && SUCCEEDED( gpu.vertices.set( 0, 0, source.vertexStride ) ) &&
        setTreeTypeConstants( owner, type, part, effect, billboardRotation ) && material.begin();
    if (!result) return owner.fail( "Cannot prepare original semitransparent tree part" );
    // a54fa0: every pass carries the SpeedTree deferred material conversions.
    const auto conversion = stockDeferredMaterialSettings().categories[wot::stock_speedtree];
    for (unsigned i = 0; result && i < 2; ++i)
    {
        const std::int32_t lod = lods[i];
        if (lod < 0) continue;
        if (static_cast<std::size_t>( lod ) >= gpu.lods.size()) { result = false; break; }
        const auto & indices = source.lodIndices[lod];
        if (indices.empty()) continue;
        instance.constants.alpha = alphas[i];
        for (uint32 effectPass = 0; result && effectPass < material.numPasses(); ++effectPass)
        {
            if (!material.beginPass( effectPass )) continue;
            result = setTreeValue( effect, "g_albedoConversions", conversion.albedo.data(), sizeof( conversion.albedo ) ) &&
                setTreeValue( effect, "g_glossConversions", conversion.gloss.data(), sizeof( conversion.gloss ) ) &&
                setTreeValue( effect, "g_metallicConversions", conversion.metallic.data(), sizeof( conversion.metallic ) ) &&
                SUCCEEDED( effect->SetValue( instanceHandle, &instance.constants, sizeof( instance.constants ) ) ) &&
                SUCCEEDED( gpu.lods[lod].set() ) && material.commitChanges();
            if (result)
            {
                const bool strip = part == wot::treeBranch || part == wot::treeFrond;
                const uint32 primitives = static_cast<uint32>( strip ? indices.size() - 2 : indices.size() / 3 );
                Moo::rc().drawIndexedPrimitive( strip ? D3DPT_TRIANGLESTRIP : D3DPT_TRIANGLELIST,
                    0, 0, source.vertexCount, 0, primitives );
            }
            material.endPass();
        }
    }
    setTreeTexture( effect, "g_diffuseMap", NULL );
    setTreeTexture( effect, "g_normalMap", NULL );
    material.end();
    return result || owner.fail( "Cannot draw original semitransparent tree part" );
}

class TreeDrawHandler : public IDrawOperationTypeHandler
{
public:
    virtual void doDraw( const SceneDrawContext & context, const SceneObject * objects, size_t count )
    {
        PROFILER_SCOPED( StockTrees_draw );
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
        const bool colour = pass == Moo::RENDERING_PASS_COLOR;
        if (colour)
        {
            owner->alphaPassView = Moo::rc().view();
            owner->alphaPassProjection = Moo::rc().projection();
        }
        // afe460 only fades in the colour pass; its eye is the drawing camera.
        const Vector3 eye = Moo::rc().invView().applyToOrigin();
        const int decorLevel = stockDecorLevel();
        std::vector<TreeDrawItem> items;
        items.reserve( count );
        const Vector2 weather = space->enviro().weather()->wind();
        {
            PROFILER_SCOPED( StockTrees_collect );
            for (std::size_t i = 0; i < count; ++i)
            {
                auto * instance = objects[i].getAs<TreeInstance>();
                if (!instance || !instance->type || !instance->type->uploaded || !instance->type->registered) continue;
                const uint32 flags = instance->definition.flags;
                const uint32 requiredDecor = flags & 0x20 ? 2 : ((flags >> 4) & 1);
                // 006c57e4: SpTr3's bit 4 is distinct from BSMI's bit 7.
                if ((context.stockLowDetailedMode() && (flags & 0x10)) ||
                    (flags & 8) || (requiredDecor && static_cast<int>( requiredDecor ) > decorLevel) ||
                    (pass == Moo::RENDERING_PASS_REFLECTION && !(flags & 2)) ||
                    (pass == Moo::RENDERING_PASS_SHADOWS && !(flags & 1))) continue;
                if (!instance->type->wind || !advanceTreeWind( *owner, *instance->type->wind, weather )) continue;
                TreeDrawItem item;
                item.instance = instance;
                float lodDistance;
                if (!selectInstanceLods( *owner, *instance, camera, item.lod, lodDistance )) continue;
                if (colour)
                {
                    const auto & type = *instance->type;
                    const auto & parts = type.geometry.parts;
                    wot::StockTreeFadeInput input = {};
                    input.world = reinterpret_cast<const float *>( &instance->definition.world );
                    for (unsigned axis = 0; axis < 3; ++axis)
                    {
                        input.boundsMin[axis] = type.geometry.minimumBounds[axis];
                        input.boundsMax[axis] = type.geometry.maximumBounds[axis];
                        input.eye[axis] = eye[axis];
                    }
                    input.lodDistance = lodDistance;
                    input.bush = type.bush;
                    input.model3d = (item.lod.branch >= 0 && parts[wot::treeBranch].vertexCount) ||
                        (item.lod.frond >= 0 && parts[wot::treeFrond].vertexCount) ||
                        ((item.lod.leaves[0] >= 0 || item.lod.leaves[1] >= 0) && parts[wot::treeLeaf].vertexCount);
                    if (fadeTreeInstance( *instance, input )) { owner->semitransparent.push_back( item ); continue; }
                }
                items.push_back( item );
            }
        }
        {
            PROFILER_SCOPED( StockTrees_sort );
            // Native batches traverse the actual filename/seed registry order.
            std::stable_sort( items.begin(), items.end(), []( const TreeDrawItem & a, const TreeDrawItem & b ) {
                const auto & x = *a.instance->type;
                const auto & y = *b.instance->type;
                return x.sortKey < y.sortKey;
            } );
        }
        if (items.empty()) return;
        float reflectionCorrection = 0.f;
        if (pass == Moo::RENDERING_PASS_REFLECTION)
        {
            std::string error;
            const auto settings = stockDeferredMaterialSettings();
            if (!wot::stockTreeReflectionCorrection( settings.categories[wot::stock_speedtree].albedo, reflectionCorrection, error ))
            { owner->fail( error ); return; }
        }
        float billboardRotation[4];
        treeBillboardRotation( billboardRotation );
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
    treeCollisionBody().open = treeHiding().open;
    Moo::rc().effectVisualContext().registerAutoConstant(
        Moo::EffectVisualContext::AUTO_CONSTANT_TYPE_PER_FRAME, "SpeedTreeMaterials", new TreeAtlasConstant );
    return true;
}

void tickStockTreeSceneRendering( float dTime )
{
    // b035a0 snapshots the target first. matrix() may run Python, so the
    // provider stays referenced; a result for a replaced or closed target
    // is dropped, and the fade waits for the next tick.
    TreeHidingState & hiding = treeHiding();
    if (MatrixProviderPtr target = hiding.target)
    {
        const uint32 generation = hiding.generation;
        Matrix world;
        target->matrix( world );
        if (hiding.generation == generation)
        {
            const Vector3 camera = Moo::rc().invView().applyToOrigin();
            const float eye[3] = { camera.x, camera.y, camera.z };
            wot::captureStockTreeHiding( reinterpret_cast<const float *>( &world ), eye, hiding.frame );
            hiding.frameGeneration = generation;
        }
    }
    auto owner = currentRenderingOwner();
    if (owner && owner->active)
    {
        // 748370 -> 941a10 -> the active pipeline's renderer: sampling does
        // not depend on SRT instance count or collision deformation settings.
        TreeCollisionBodyState & body = treeCollisionBody();
        if (body.open)
        {
            const uint32 generation = body.generation;
            const MatrixProviderPtr source = body.target;
            if (source)
            {
                Matrix sampled;
                source->matrix( sampled );
                if (body.open && body.generation == generation && owner->active &&
                    currentRenderingOwner() == owner)
                    std::memcpy( owner->collisionBodyMatrix.data(), &sampled, sizeof( sampled ) );
            }
            else
                owner->collisionBodyMatrix[15] = 0.f;
        }
        // A provider callback may have retired or replaced the renderer.
        if (!owner->active || currentRenderingOwner() != owner) return;
        owner->time = addTreeScalar( owner->time, std::min( dTime, .05f ) );
    }
}

void setStockTreeCollisionBody( MatrixProvider * target )
{
    TreeCollisionBodyState & body = treeCollisionBody();
    if (body.target.get() == target || (target && !body.open)) return;
    MatrixProviderPtr previous( target );
    std::swap( body.target, previous );
    ++body.generation;
}

void closeStockTreeCollisionBody()
{
    treeCollisionBody().open = false;
    setStockTreeCollisionBody( NULL );
}

void setSpeedTreeCollisionBody( MatrixProviderPtr target )
{
    setStockTreeCollisionBody( target.get() );
}
PY_AUTO_MODULE_FUNCTION( RETVOID, setSpeedTreeCollisionBody,
    ARG( MatrixProviderPtr, END ), BigWorld )

void setStockTreeCameraTarget( MatrixProvider * target )
{
    TreeHidingState & hiding = treeHiding();
    // b030b0 keeps an unchanged target. A closed owner accepts only release.
    if (hiding.target.get() == target || (target && !hiding.open)) return;
    // The new target is published before the old one can be released.
    MatrixProviderPtr previous( target );
    std::swap( hiding.target, previous );
    ++hiding.generation;
}

void closeStockTreeCameraTarget()
{
    // A release may run a destructor that publishes again; closing first
    // refuses it, so nothing stays retained across script teardown.
    treeHiding().open = false;
    closeStockTreeCollisionBody();
    setStockTreeCameraTarget( NULL );
}

void openStockTreeCameraTarget()
{
    treeHiding().open = true;
    treeCollisionBody().open = true;
}

namespace
{
class HidingTargetScope
{
public:
    HidingTargetScope() : saved_( Moo::rc().pushRenderTarget() ) {}
    ~HidingTargetScope()
    {
        // RenderTargetStack::pop restores the targets, viewport and screen size.
        if (saved_) Moo::rc().popRenderTarget();
    }
    bool bind( const Moo::RenderTargetPtr & target, bool depth )
    {
        ComObjectWrap<DX::Surface> surface;
        if (!saved_ || !target || FAILED( target->pSurface( surface ) ) ||
            FAILED( Moo::rc().device()->SetDepthStencilSurface( depth ? target->depthBuffer() : NULL ) ) ||
            FAILED( Moo::rc().setRenderTarget( 0, surface ) )) return false;
        for (unsigned i = 1; i < 4; ++i)
            if (FAILED( Moo::rc().setRenderTarget( i, NULL ) )) return false;
        // 999220 binds the whole pool target; its DRR use (blur +0x28) keeps the
        // scene-resolution rectangle.
        DX::Viewport viewport = { 0, 0, static_cast<DWORD>( target->width() ), static_cast<DWORD>( target->height() ), 0.f, 1.f };
        if (FAILED( setStockSceneViewport( &viewport, true ) )) return false;
        Moo::rc().screenWidth( target->width() );
        Moo::rc().screenHeight( target->height() );
        return true;
    }
private:
    bool saved_;
};

// Shared per-screen constants applied under a quarter target stay cached after
// its scope pops. 9c511d reapplies them for the scene target; every earlier
// exit does the same once the target scopes have restored the screen size.
class HidingScreenConstants
{
public:
    HidingScreenConstants() = default;
    HidingScreenConstants( const HidingScreenConstants & ) = delete;
    HidingScreenConstants & operator=( const HidingScreenConstants & ) = delete;
    ~HidingScreenConstants() { restore(); }
    // 9c4fa8 and the blur passes: the bound quarter target's size.
    void apply()
    {
        Moo::rc().effectVisualContext().updateSharedConstants( Moo::CONSTANTS_PER_SCREEN );
        quarter_ = true;
    }
    // 9c511d: the restored scene target's size.
    void restore()
    {
        if (!quarter_) return;
        quarter_ = false;
        Moo::rc().effectVisualContext().updateSharedConstants( Moo::CONSTANTS_PER_SCREEN );
    }
private:
    bool quarter_ = false;
};

Vector4 hidingTargetProjection( const Moo::RenderTargetPtr & target )
{
    // 998e30: the DRR projection of the pool target, as targetProjection.
    const auto * resolution = stockDynamicResolution();
    const auto value = resolution ? resolution->state().project( target->width(), target->height() ) :
        wot::StockDynamicResolution().project( target->width(), target->height() );
    return Vector4( value.parameters[0], value.parameters[1], value.parameters[2], value.parameters[3] );
}

bool setHidingAlpha( TreeRenderingOwner & owner, bool value )
{
    bool result = true;
    for (auto & state : owner.semitransparentMaterials)
        for (auto & material : state)
            if (material && material->pEffect())
            {
                ID3DXEffect * effect = selectTreeBinding( *material );
                result = (effect && SUCCEEDED( effect->SetBool( "g_useAlpha", value ) )) && result;
            }
    return result;
}

// 9d62e0 with 9c5080's parameters: identical to a69bb7's Gaussian except
// +0x28, which takes the DRR source projection instead of (1,1,0,0).
bool blurHidingTarget( TreeRenderingOwner & owner, HidingScreenConstants & constants )
{
    const Moo::RenderTargetPtr inputs[] = { owner.hidingTargets[0], owner.hidingTargets[1] };
    const Moo::RenderTargetPtr outputs[] = { owner.hidingTargets[1], owner.hidingTargets[0] };
    const char * techniques[] = { "HORIZONTAL_PASS", "VERTICAL_PASS" };
    auto & material = owner.blurMaterial;
    for (unsigned pass = 0; pass < 2; ++pass)
    {
        const uint width = outputs[pass]->width(), height = outputs[pass]->height();
        HidingTargetScope target;
        // a69bb7's blur binds no depth; this pass has no depth test either.
        if (!target.bind( outputs[pass], false ) || !material->hTechnique( techniques[pass] )) return false;
        constants.apply();
        ID3DXEffect * effect = selectTreeBinding( *material );
        if (!effect) return false;
        const Vector4 screen( float( width ), float( height ), 1.f / width, 1.f / height );
        const Vector4 projection = hidingTargetProjection( inputs[pass] );
        const bool success = !(FAILED( effect->SetInt( "g_depthMode", 2 ) ) ||
            FAILED( effect->SetBool( "g_useMaskRT", false ) ) ||
            FAILED( effect->SetBool( "g_useBilateralFilter", false ) ) ||
            FAILED( effect->SetBool( "g_bilateralFilterLowQuality", false ) ) ||
            FAILED( effect->SetBool( "g_bilateralFilterConsiderSurfacePlane", false ) ) ||
            FAILED( effect->SetBool( "g_processOnlyOpaque", false ) ) ||
            FAILED( effect->SetFloat( "g_blurMip", 0.f ) ) ||
            FAILED( effect->SetVector( "g_blurScreen", &screen ) ) ||
            FAILED( effect->SetVector( "g_srcMapDRRParams", &projection ) ) ||
            FAILED( effect->SetTexture( "g_srcMap", inputs[pass]->pTexture() ) ) ||
            !Moo::rc().fsQuad().drawChecked( *material, width, height ));
        effect->SetTexture( "g_srcMap", NULL );
        if (!success) return false;
    }
    return true;
}
}

// #1513 009c4ed0. Near-camera hiding renders the faded trees into a quarter
// target, optionally blurs it and composites it with UPSAMPLE_RT.
void drawStockTreeSemitransparentStage( bool skipDrawing, bool optimiser, bool blur )
{
    auto owner = currentRenderingOwner();
    if (!owner || !treeHiding().settings.hideClosest || !optimiser)
    {
        drawStockTreeSemitransparent( skipDrawing );
        return;
    }
    const auto & targets = owner->hidingTargets;
    if (!targets[0] || !targets[1] || !owner->upsampleMaterial || !owner->blurMaterial)
    {
        // The original asserts its owners exist. Contain their absence here.
        owner->fail( "Original tree-hiding resources are unavailable; faded trees were not drawn" );
        drawStockTreeSemitransparent( true );
        return;
    }
    HidingScreenConstants constants;
    {
        HidingTargetScope target;
        if (!target.bind( targets[0], true ))
        {
            owner->fail( "Cannot bind the original tree-hiding target; faded trees were not drawn" );
            drawStockTreeSemitransparent( true );
            return;
        }
        // 9c4fa8: the draw context's vslot 0x14(2) applies the per-screen
        // shared constants for the quarter target (2 = CONSTANTS_PER_SCREEN).
        constants.apply();
        // 998ad0 clears the colour to 99daa0's packed zero; 998a20 clears the
        // depth target with flags 3 (depth and stencil) to 1 and 0.
        if (FAILED( Moo::rc().device()->Clear( 0, NULL, D3DCLEAR_TARGET | D3DCLEAR_ZBUFFER | D3DCLEAR_STENCIL,
            0x00000000, 1.f, 0 ) ))
        {
            owner->fail( "Cannot clear the original tree-hiding target; faded trees were not drawn" );
            drawStockTreeSemitransparent( true );
            return;
        }
        const bool alphaSet = setHidingAlpha( *owner, true );
        drawStockTreeSemitransparent( skipDrawing || !alphaSet );
        const bool alphaReset = setHidingAlpha( *owner, false );
        if (!alphaSet || !alphaReset)
        {
            owner->fail( "Cannot bind original tree-hiding alpha mode" );
            return;
        }
    }
    if (blur && !blurHidingTarget( *owner, constants ))
    {
        owner->fail( "Cannot blur the original tree-hiding target" );
        return;
    }
    // 9c511d: per-screen constants again for the restored scene target, then
    // UPSAMPLE_RT samples the quarter target (99b8e0 also binds its DRR
    // parameters).
    constants.restore();
    auto & material = owner->upsampleMaterial;
    ID3DXEffect * effect = selectTreeBinding( *material );
    if (!effect)
    {
        owner->fail( "Cannot select original tree-hiding composite material" );
        return;
    }
    const Vector4 projection = hidingTargetProjection( targets[0] );
    const bool success = material->hTechnique( "UPSAMPLE_RT" ) &&
        SUCCEEDED( effect->SetTexture( "g_srcMap", targets[0]->pTexture() ) ) &&
        SUCCEEDED( effect->SetVector( "g_srcMapDRRParams", &projection ) ) &&
        Moo::rc().fsQuad().drawChecked( *material, static_cast<uint>( Moo::rc().screenWidth() ),
            static_cast<uint>( Moo::rc().screenHeight() ) );
    effect->SetTexture( "g_srcMap", NULL );
    if (!success) owner->fail( "Cannot composite the original tree-hiding target" );
}

// #1513 006b1800, 006b3d00 and 006b1880. The first two also forward to the
// SRT SpeedTreeRender (00748190, 007481d0), read only for .srt instances.
void wg_enableTreeHiding( bool enable )
{
    treeHiding().settings.hideClosest = enable;
}

void wg_setTreeHidingRadius( float radius, float alphaRadius )
{
    auto & settings = treeHiding().settings;
    settings.hideRadius = radius;
    settings.hideRadiusAlpha = alphaRadius;
    settings.squaredHideRadius = radius * radius;
}

void wg_enableTreeTransparency( bool enable )
{
    treeHiding().settings.transparency = enable;
}

PY_AUTO_MODULE_FUNCTION( RETVOID, wg_enableTreeHiding, ARG( bool, END ), BigWorld )
PY_AUTO_MODULE_FUNCTION( RETVOID, wg_setTreeHidingRadius, ARG( float, ARG( float, END ) ), BigWorld )
PY_AUTO_MODULE_FUNCTION( RETVOID, wg_enableTreeTransparency, ARG( bool, END ), BigWorld )

void drawStockTreeSemitransparent( bool skipDrawing )
{
    auto owner = currentRenderingOwner();
    if (!owner) return;
    // afef60 drains every collected tree, including when drawing is skipped.
    std::vector<TreeDrawItem> trees;
    trees.swap( owner->semitransparent );
    if (skipDrawing || trees.empty() || !owner->active || !owner->gpuReady) return;
    std::sort( trees.begin(), trees.end(), []( const TreeDrawItem & a, const TreeDrawItem & b ) {
        return a.instance->leavesAlpha > b.instance->leavesAlpha;
    } );
    float billboardRotation[4];
    treeBillboardRotation( billboardRotation );
    // afef60: 9635d0 updates the view transforms, then the draw context's
    // vslot 0x14(4) (94b5a0 -> 9b0e80) applies the per-view shared constants.
    // Unlike the public SDK it sets no pipeline stencil usage.
    Moo::rc().view( owner->alphaPassView );
    Moo::rc().projection( owner->alphaPassProjection );
    Moo::rc().updateViewTransforms();
    Moo::rc().effectVisualContext().updateSharedConstants( Moo::CONSTANTS_PER_VIEW );
    const bool leaves = owner->semitransparentLeaves && owner->semitransparentLeaves->activeOption() != 0;
    bool result = true;
    for (const auto & tree : trees)
        for (unsigned state = 0; result && state < 2; ++state)
        {
            auto & materials = owner->semitransparentMaterials[state];
            if (leaves)
                result = drawSemitransparentPart( *owner, *materials[wot::treeLeaf], wot::treeLeaf, tree, billboardRotation );
            const unsigned parts[] = { wot::treeFrond, wot::treeBranch, wot::treeBillboard };
            for (unsigned part : parts)
                result = result && drawSemitransparentPart( *owner, *materials[part], part, tree, billboardRotation );
        }
}

void finaliseStockTreeSceneRendering()
{
    closeStockTreeCollisionBody();
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
        const char * resource = strings.entry( source.resource );
        if (resource) instance.filename = resource;
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
        instance.constants.windMultiplier = 1.f;
        instance.type = owner->acquire( filename, source.seed );
        if (!instance.type) continue;
        std::string error;
        if (!updateTreeInstanceTransform( instance, instance.definition.world, error ))
        {
            instance.type.reset();
            owner->fail( error );
        }
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
    // Faded trees of this frame must not outlive their scene instances.
    if (auto owner = currentRenderingOwner())
    {
        const auto & instances = implementation_->instances;
        auto & faded = owner->semitransparent;
        faded.erase( std::remove_if( faded.begin(), faded.end(), [&instances]( const TreeDrawItem & item ) {
            const std::less<const TreeInstance *> before;
            return !instances.empty() && !before( item.instance, instances.data() ) &&
                before( item.instance, instances.data() + instances.size() );
        } ), faded.end() );
    }
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

BW::string StockTreeSceneHandler::soundResource( size_t index ) const
{
    // 6c5b30/6c5b50 use the live tree instance's original
    // resource name plus the unmodified instance world transform.
    if (!objectAvailable( index )) return BW::string();
    return engineString( implementation_->instances[index].type->resource );
}

bool StockTreeSceneHandler::destructibleSnapshot( size_t index, StockDestructibleSnapshot & output ) const
{
    if (!implementation_->loaded || index >= implementation_->instances.size()) return false;
    const auto & instance = implementation_->instances[index];
    StockDestructibleSnapshot result;
    result.world = instance.definition.world;
    // 6b44e5 passes true to slot+0x40, but 6c5ae0 still returns identity.
    result.hardpointWorld = Matrix::identity;
    result.filename = instance.filename;
    result.category = 0; // Original compiled-tree model-operation slot +0x30.
    result.fallingParameters = Matrix::identity; // 6c5aa0 returns the identity matrix.
    if (instance.type)
    {
        const auto & geometry = instance.type->geometry;
        if (geometry.minimumBounds[0] <= geometry.maximumBounds[0] &&
            geometry.minimumBounds[1] <= geometry.maximumBounds[1] &&
            geometry.minimumBounds[2] <= geometry.maximumBounds[2])
            result.height = geometry.maximumBounds[1] - geometry.minimumBounds[1];
    }
    output = result;
    return true;
}

bool StockTreeSceneHandler::setDestructibleTransform( size_t index, const Matrix & world,
    float wind, std::string & error )
{
    if (!implementation_->loaded || index >= implementation_->instances.size())
    { error = "The original compiled-tree instance is unavailable"; return false; }
    if (!std::isfinite( wind )) { error = "The original tree wind multiplier is nonfinite"; return false; }
    auto & instance = implementation_->instances[index];
    if (!updateTreeInstanceTransform( instance, world, error )) return false;
    // b03580 -> afe506: the original per-instance float reaches g_instance
    // byte60 (row3.w), separately from random windOffset at byte48.
    instance.constants.windMultiplier = wind;
    return true;
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
