// Real Moo format/declaration owners and the production BWSG/BSGD reader over
// original #1513 resources. Usage: probe <paths.txt> <formats.txt> [<maps.txt>]
#include "cstdmf/cstdmf_windows.hpp"
#include "cstdmf/log_msg.hpp"
#include "cstdmf/debug_filter.hpp"
#include "cstdmf/debug_message_callbacks.hpp"
#include "resmgr/auto_config.hpp"
#include "resmgr/bwresource.hpp"
#include "moo/init.hpp"
#include "moo/render_context.hpp"
#include "moo/vertex_format_cache.hpp"
#include "moo/vertex_declaration.hpp"
#include "moo/vertex_streams.hpp"
#include "moo/vertices.hpp"
#include "moo/vertices_manager.hpp"
#include "compiled_space/binary_format.hpp"
#include "compiled_space/static_geometry.hpp"
#include <psapi.h>
#include <cstdarg>
#include <cstdio>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

namespace
{
// Engine messages are part of the evidence; keep debug/trace noise out.
class StdoutMessages : public BW::DebugMessageCallback
{
public:
    size_t assets = 0, errors = 0;
    bool handleMessage( BW::DebugMessagePriority priority, const char *, BW::DebugMessageSource,
        const BW::LogMetaData &, const char * format, va_list arguments ) override
    {
        if (priority < BW::MESSAGE_PRIORITY_NOTICE) return true;
        if (priority == BW::MESSAGE_PRIORITY_ASSET) ++assets;
        if (priority == BW::MESSAGE_PRIORITY_ERROR || priority == BW::MESSAGE_PRIORITY_CRITICAL) ++errors;
        std::printf( "LOG %d ", int( priority ) );
        std::vprintf( format, arguments );
        return true;
    }
} messages;

std::vector<std::string> lines( const char * path )
{
    std::ifstream input( path );
    std::vector<std::string> result;
    for (std::string line; std::getline( input, line ); )
        if (!line.empty()) result.push_back( line );
    return result;
}

std::string describe( const BW::Moo::VertexFormat * format )
{
    if (!format) return "NULL";
    std::ostringstream out;
    out << format->name().to_string() << " streams=" << format->streamCount();
    for (BW::uint32 i = 0; i < format->streamCount(); ++i)
        if (format->streamStride( i )) out << " s" << i << '=' << format->streamStride( i );
    return out.str();
}

// Private bytes and the largest free address range after each space.
std::string memory()
{
    PROCESS_MEMORY_COUNTERS_EX counters = { sizeof( counters ) };
    GetProcessMemoryInfo( GetCurrentProcess(), (PROCESS_MEMORY_COUNTERS*)&counters, sizeof( counters ) );
    SIZE_T largest = 0;
    MEMORY_BASIC_INFORMATION info;
    for (char * address = NULL; VirtualQuery( address, &info, sizeof( info ) ) == sizeof( info );
        address = (char*)info.BaseAddress + info.RegionSize)
    {
        if (info.State == MEM_FREE && info.RegionSize > largest) largest = info.RegionSize;
        if ((char*)info.BaseAddress + info.RegionSize < address) break;
    }
    std::ostringstream out;
    out << " privateMB=" << counters.PrivateUsage / (1024 * 1024) << " largestFreeMB=" << largest / (1024 * 1024);
    return out.str();
}

// One BWSG resource expectation: resource id, vertex count, stream0 stride.
struct Expected { BW::string resource; BW::uint32 vertices, stride; };

void flushDevice()
{
    // D3D destroys released resources when queued work is flushed; a real
    // client does this every frame through Present.
    IDirect3DQuery9 * query = NULL;
    if (SUCCEEDED( BW::Moo::rc().device()->CreateQuery( D3DQUERYTYPE_EVENT, &query ) ))
    {
        query->Issue( D3DISSUE_END );
        while (query->GetData( NULL, 0, D3DGETDATA_FLUSH ) == S_FALSE) Sleep( 1 );
        query->Release();
    }
}

// Expected entries whose resource id the manager currently resolves.
size_t registered( const std::vector<Expected> & expected )
{
    size_t result = 0;
    for (const Expected & entry : expected)
    {
        BW::Moo::VerticesPtr found[64];
        size_t count = 0;
        BW::Moo::VerticesManager::instance()->find( entry.resource, found, count );
        for (size_t i = 0; i < count && i < 64; ++i)
            if (found[i]->resourceID() == entry.resource) ++result;
    }
    return result;
}

bool readSpace( BW::CompiledSpace::BinaryFormat & reader, BW::CompiledSpace::StaticGeometry & geometry,
    const std::string & map )
{
    const std::string resource = "spaces/" + map + "/space.bin";
    if (!reader.open( resource.c_str() )) return false;
    bool loaded = false;
    // CompiledSpaceMapping::loadBG reads static geometry on a loader thread.
    std::thread loader( [&]() { loaded = geometry.read( reader ); } );
    loader.join();
    return loaded;
}

// Keep one space loaded while a space sharing resource ids loads and unloads.
bool overlapSpaces( const std::string & keep, const std::vector<Expected> & kept,
    const std::string & other )
{
    BW::CompiledSpace::BinaryFormat keepReader, otherReader;
    BW::CompiledSpace::StaticGeometry keepGeometry, otherGeometry;
    const bool keepLoaded = readSpace( keepReader, keepGeometry, keep );
    const size_t before = registered( kept );
    const bool otherLoaded = readSpace( otherReader, otherGeometry, other );
    otherGeometry.close();
    otherReader.close();
    flushDevice();
    const size_t after = registered( kept );
    keepGeometry.close();
    keepReader.close();
    flushDevice();
    const size_t retained = registered( kept );
    std::cout << "OVERLAP keep=" << keep << " other=" << other << " keepRead=" << keepLoaded
        << " otherRead=" << otherLoaded << " expected=" << kept.size() << " registeredBefore=" << before
        << " registeredAfterOtherClose=" << after << " retainedAfterClose=" << retained << std::endl;
    return keepLoaded && otherLoaded && before == kept.size() && after == kept.size() && retained == 0;
}

bool loadSpace( const std::string & map, const std::vector<Expected> & expected, int pass )
{
    BW::CompiledSpace::BinaryFormat reader;
    BW::CompiledSpace::StaticGeometry geometry;
    const bool loaded = readSpace( reader, geometry, map );
    size_t published = 0, matched = 0;
    for (const Expected & entry : expected)
    {
        BW::Moo::VerticesPtr found[64];
        size_t count = 0;
        BW::Moo::VerticesManager::instance()->find( entry.resource, found, count );
        for (size_t i = 0; i < count && i < 64; ++i)
        {
            if (found[i]->resourceID() != entry.resource) continue;
            ++published;
            if (found[i]->nVertices() == entry.vertices && found[i]->pDecl() &&
                found[i]->vertexStride() == entry.stride &&
                found[i]->pDecl()->format().streamStride( 0 ) == entry.stride)
                ++matched;
        }
    }
    const float percent = geometry.percentLoaded();
    geometry.close();
    geometry.close();
    const size_t retained = registered( expected );
    reader.close();
    flushDevice();
    std::cout << "MAP " << map << " pass=" << pass << " read=" << (loaded ? "OK" : "FAILED")
        << " expected=" << expected.size() << " published=" << published
        << " matched=" << matched << " percent=" << percent
        << " retainedAfterClose=" << retained << memory() << std::endl;
    return loaded && published == expected.size() && matched == expected.size() &&
        percent == 1.f && retained == 0;
}
}

int main( int argc, char ** argv )
{
    BW_SYSTEMSTAGE_MAIN();
    BW::LogMsg::automatedTest( true );
    SetErrorMode( SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX );
    if (argc < 3 || argc > 4) return 2;
    BW::DebugFilter::instance().addMessageCallback( &messages, false );
    BW::vector<BW::string> paths;
    for (const std::string & path : lines( argv[1] )) paths.push_back( BW::string( path.c_str() ) );
    if (!BW::BWResource::init( paths ))
    {
        std::cout << "BWResource initialisation failed" << std::endl;
        return 3;
    }
    if (!BW::AutoConfig::configureAllFrom( BW::AutoConfig::s_resourcesXML ))
    {
        std::cout << "AutoConfig initialisation failed" << std::endl;
        return 3;
    }
    const BW::StringRef device( BW::Moo::VertexDeclaration::getTargetDevice() );
    std::cout << "targetDevice=" << device.to_string() << std::endl;
    bool formatsResolved = true;
    std::vector<BW::string> targets;
    for (const std::string & name : lines( argv[2] ))
    {
        const BW::Moo::VertexFormat * source = BW::Moo::VertexFormatCache::get( name.c_str() );
        const BW::Moo::VertexFormat * target = source ?
            BW::Moo::VertexFormatCache::getTarget( *source, device, true ) : NULL;
        // A second lookup must return the cached owner, not a new load.
        const BW::Moo::VertexFormat * again = BW::Moo::VertexFormatCache::get( name.c_str() );
        std::cout << "FORMAT " << name << " source=" << describe( source )
            << " target=" << describe( target ) << " cached=" << (again == source) << std::endl;
        formatsResolved = formatsResolved && source && target && again == source;
        if (target) targets.push_back( target->name().to_string() );
    }
    int outcome = formatsResolved ? 0 : 1;
    if (argc == 4)
    {
        HWND window = CreateWindowExA( 0, "STATIC", "static geometry probe", WS_POPUP,
            0, 0, 64, 64, NULL, NULL, GetModuleHandle( NULL ), NULL );
        if (!window || !BW::Moo::init( true, false ) ||
            !BW::Moo::rc().createDevice( window, 0, 0, true, true, BW::Vector2( 64, 64 ), false, false ))
        {
            std::cout << "DEVICE creation failed" << std::endl;
            return 4;
        }
        std::cout << "DEVICE ex=" << BW::Moo::rc().usingD3DDeviceEx() << std::endl;
        BW::Moo::VertexDeclaration * instancing = BW::Moo::VertexDeclaration::get(
            BW::Moo::VertexFormatCache::getTargetName<BW::Moo::InstancingStream>( device, true ) );
        for (const BW::string & name : targets)
        {
            BW::Moo::VertexDeclaration * declaration = BW::Moo::VertexDeclaration::get( name );
            // The instancing stream is combined with the others, not with itself.
            if (declaration && declaration == instancing) continue;
            BW::Moo::VertexDeclaration * combined = declaration && instancing ?
                BW::Moo::VertexDeclaration::combine( declaration, instancing ) : NULL;
            std::cout << "DECLARATION " << name << " stride0="
                << (declaration ? declaration->format().streamStride( 0 ) : 0)
                << " instanced=" << (combined ? combined->name() : "NULL") << std::endl;
            if (!declaration || !combined) outcome = 1;
        }
        std::vector<std::pair<std::string, std::vector<Expected> > > maps;
        std::vector<std::pair<std::string, std::string> > overlaps;
        for (const std::string & line : lines( argv[3] ))
        {
            std::istringstream fields( line );
            std::string kind;
            fields >> kind;
            if (kind == "OVERLAP")
            {
                std::string keep, other;
                fields >> keep >> other;
                overlaps.push_back( std::make_pair( keep, other ) );
            }
            else if (kind == "MAP")
            {
                std::string name;
                fields >> name;
                maps.push_back( std::make_pair( name, std::vector<Expected>() ) );
            }
            else
            {
                Expected entry;
                fields >> entry.vertices >> entry.stride;
                std::string resource;
                std::getline( fields >> std::ws, resource );
                entry.resource = resource.c_str();
                maps.back().second.push_back( entry );
            }
        }
        size_t passed = 0;
        for (int pass = 0; pass < 2; ++pass)
            for (const auto & item : maps)
                if (loadSpace( item.first, item.second, pass )) ++passed;
        std::cout << "SPACES passed=" << passed << " of " << maps.size() * 2 << std::endl;
        if (passed != maps.size() * 2) outcome = 1;
        size_t overlapped = 0;
        for (const auto & pair : overlaps)
            for (const auto & item : maps)
                if (item.first == pair.first && overlapSpaces( pair.first, item.second, pair.second ))
                    ++overlapped;
        std::cout << "OVERLAPS passed=" << overlapped << " of " << overlaps.size() << std::endl;
        if (overlapped != overlaps.size()) outcome = 1;
    }
    std::cout << "MESSAGES asset=" << messages.assets << " error=" << messages.errors << std::endl;
    std::cout << "RESULT " << (outcome ? "FAILED" : "PASSED") << std::endl;
    std::cout.flush();
    // Leave the device and caches to process teardown; Moo::fini ordering is
    // not part of the static geometry contract this probe exercises.
    TerminateProcess( GetCurrentProcess(), outcome );
    return outcome;
}
