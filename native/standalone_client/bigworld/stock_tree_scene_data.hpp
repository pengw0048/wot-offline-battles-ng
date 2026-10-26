#pragma once

#include <array>
#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <limits>
#include <set>
#include <string>
#include <utility>
#include <vector>

namespace wot
{
struct StockTreeSceneDefinition
{
    std::array<float, 16> world;
    std::uint32_t resource;
    std::uint32_t seed;
    std::uint32_t flags;
    std::uint32_t visibility;
};
static_assert( sizeof( StockTreeSceneDefinition ) == 80, "SpTr3 definition width" );

struct StockTreeSceneData
{
    std::vector<StockTreeSceneDefinition> definitions;
    std::array<float, 6> bounds;
};

struct StockTreeWindHistory
{
    using Matrices = std::array<std::array<float, 16>, 6>;
    Matrices currentMatrices = {}, previousMatrices = {};
    std::vector<std::array<float, 4>> currentAngles, previousAngles;

    StockTreeWindHistory()
    {
        // b057d0: current matrices are value-zeroed by 6d0b70, whereas the
        // previous vector is explicitly filled from identity 01b5b390.
        for (auto & matrix : previousMatrices)
            matrix[0] = matrix[5] = matrix[10] = matrix[15] = 1.f;
    }

    void publish( const Matrices & matrices, const std::vector<std::array<float, 4>> & angles )
    {
        previousMatrices = currentMatrices;
        previousAngles = currentAngles;
        currentMatrices = matrices;
        currentAngles = angles;
    }
};

inline bool stockTreeShaderUsesStencil( std::uint32_t initialAdapterVendor, bool forwardPipeline )
{
    // 116ee00 classifies Intel as 2; DeferredPipeline::init 9c5c2c records
    // that adapter workaround. Its e4 getter 9c4ca0 is then inverted by each
    // tree state producer. ForwardPipeline's e4 slot is the real false thunk.
    return forwardPipeline || (initialAdapterVendor != 0x163c && initialAdapterVendor != 0x8086 && initialAdapterVendor != 0x8087);
}

// The original section consists of one ExternalArray<80-byte definition>,
// followed by one typed 24-byte AABB. There is no AABB array count.
inline bool decodeStockTreeSceneData( const std::uint8_t * bytes, std::size_t size,
    StockTreeSceneData & result, std::string & error )
{
    auto fail = [&error]( const char * message ) { error = message; return false; };
    if (!bytes || size < 36) return fail( "Truncated original SpTr3 section" );
    auto word = [bytes]( std::size_t offset ) {
        return std::uint32_t( bytes[offset] ) | (std::uint32_t( bytes[offset+1] ) << 8) |
            (std::uint32_t( bytes[offset+2] ) << 16) | (std::uint32_t( bytes[offset+3] ) << 24);
    };
    const std::uint32_t count = word( 4 );
    if (word( 0 ) != 80 || count > (size - 36) / 80 || size != 36 + std::size_t( count ) * 80)
        return fail( "Invalid original SpTr3 definition size/count or trailing bytes" );
    const std::size_t boundOffset = 8 + std::size_t( count ) * 80;
    if (word( boundOffset ) != 24) return fail( "Invalid original SpTr3 bounds width" );
    StockTreeSceneData decoded;
    decoded.definitions.resize( count );
    // Supported targets are little-endian x86/ARM64. Copy floats and integer
    // bit patterns without promoting the resource FNV key through text/float.
    for (std::size_t i = 0; i < count; ++i)
    {
        auto & definition = decoded.definitions[i];
        std::memcpy( definition.world.data(), bytes + 8 + i * 80, 64 );
        definition.resource = word( 8 + i * 80 + 64 );
        definition.seed = word( 8 + i * 80 + 68 );
        definition.flags = word( 8 + i * 80 + 72 );
        definition.visibility = word( 8 + i * 80 + 76 );
        for (float value : definition.world)
            if (!std::isfinite( value )) return fail( "Non-finite original tree transform" );
    }
    std::memcpy( decoded.bounds.data(), bytes + boundOffset + 4, 24 );
    for (float value : decoded.bounds)
        if (!std::isfinite( value )) return fail( "Non-finite original tree section bounds" );
    result = std::move( decoded );
    error.clear();
    return true;
}

class StockTreeMaterialHandles
{
public:
    std::uint32_t acquire()
    {
        std::uint32_t handle = static_cast<std::uint32_t>( live_.size() );
        if (!free_.empty()) { handle = free_.back(); free_.pop_back(); }
        live_.insert( handle );
        return handle;
    }

    bool release( std::uint32_t handle )
    {
        if (!live_.erase( handle )) return false;
        free_.push_back( handle );
        return true;
    }

    std::size_t size() const { return live_.size(); }

private:
    // Mirrors b03ba0/b02a70: decrement live count and reuse the free stack in
    // LIFO order. The set additionally prevents duplicate release corruption.
    std::set<std::uint32_t> live_;
    std::vector<std::uint32_t> free_;
};

// b01561..b016ca derives reflection correction from the active SpeedTree PBS
// albedo x/y/z. The SSE grouping is retained; pow is the CRT math boundary.
inline bool stockTreeReflectionCorrection( const std::array<float, 4> & albedo,
    float & correction, std::string & error )
{
    const auto add = []( float a, float b ) { volatile float r = a+b; return r; };
    const auto sub = []( float a, float b ) { volatile float r = a-b; return r; };
    const auto mul = []( float a, float b ) { volatile float r = a*b; return r; };
    const auto power = []( float a, float b ) { return static_cast<float>( std::pow( double( a ), double( b ) ) ); };
    const float x = albedo[0], y = albedo[1], z = albedo[2];
    if (!std::isfinite( x ) || !std::isfinite( y ) || !std::isfinite( z ) || y < 0.f)
    {
        error = "Invalid original SpeedTree PBS reflection parameters";
        return false;
    }
    float diffuse = x, reflected;
    if (y >= 1.f) reflected = power( y, 2.25f );
    else
    {
        if (y <= .6f)
        {
            const float t = mul( std::max( sub( x, .3f ), 0.f ), 1.42857146f );
            const float exponent = add( mul( sub( 1.f, t ), .75f ), t );
            diffuse = mul( power( mul( y, 1.6666666f ), exponent ), x );
        }
        const float squared = mul( y, y );
        reflected = add( mul( sub( 1.f, power( y, .75f ) ), mul( mul( squared, squared ), y ) ), power( y, 2.25f ) );
    }
    float exponent;
    if (z <= 1.f)
    {
        const float t = add( std::max( sub( z, .5f ), 0.f ), std::max( sub( z, .5f ), 0.f ) );
        exponent = add( mul( mul( z, z ), sub( 1.f, t ) ), mul( t, z ) );
    }
    else exponent = mul( z, z );
    const float value = mul( add( mul( power( .25f, exponent ), reflected ), diffuse ), 4.f );
    if (!std::isfinite( value ))
    {
        error = "Non-finite original SpeedTree PBS reflection correction";
        return false;
    }
    correction = value;
    error.clear();
    return true;
}
}
