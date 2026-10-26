#include "stock_tree_scene_data.hpp"
#include <cassert>
#include <fstream>
#include <iostream>
#include <iterator>

using Bytes = std::vector<std::uint8_t>;
void word( Bytes & bytes, std::uint32_t value )
{
    for (unsigned i = 0; i < 4; ++i) bytes.push_back( static_cast<std::uint8_t>( value >> (i*8) ) );
}
Bytes sceneFixture()
{
    Bytes bytes;
    word( bytes, 80 ); word( bytes, 2 );
    for (unsigned i = 0; i < 2; ++i)
    {
        std::array<float,16> matrix = {};
        matrix[0] = matrix[5] = matrix[10] = matrix[15] = 1.f;
        matrix[12] = i ? -712.25f : 130.5f;
        const auto * first = reinterpret_cast<const std::uint8_t *>( matrix.data() );
        bytes.insert( bytes.end(), first, first+64 );
        word( bytes, 0xff198276U+i ); word( bytes, 37+i );
        word( bytes, i ? 49 : 3 ); word( bytes, i ? 0x7fff4000 : 0xffffbfff );
    }
    word( bytes, 24 );
    const float bounds[] = { -800, 0, -900, 700, 70, 800 };
    const auto * first = reinterpret_cast<const std::uint8_t *>( bounds );
    bytes.insert( bytes.end(), first, first+24 );
    return bytes;
}
Bytes encode( const wot::StockTreeSceneData & scene )
{
    Bytes bytes;
    word( bytes, 80 ); word( bytes, static_cast<std::uint32_t>( scene.definitions.size() ) );
    for (const auto & item : scene.definitions)
    {
        const auto * first = reinterpret_cast<const std::uint8_t *>( item.world.data() );
        bytes.insert( bytes.end(), first, first+64 );
        word( bytes, item.resource ); word( bytes, item.seed ); word( bytes, item.flags ); word( bytes, item.visibility );
    }
    word( bytes, 24 );
    const auto * first = reinterpret_cast<const std::uint8_t *>( scene.bounds.data() );
    bytes.insert( bytes.end(), first, first+24 );
    return bytes;
}
void tests()
{
    for (std::uint32_t vendor : { 0x163cU, 0x8086U, 0x8087U })
    {
        assert( !wot::stockTreeShaderUsesStencil( vendor, false ) );
        assert( wot::stockTreeShaderUsesStencil( vendor, true ) );
    }
    for (std::uint32_t vendor : { 0U, 0x10deU, 0x1002U, 0x1022U, 0x15adU, 0x8085U, 0x8088U })
        assert( wot::stockTreeShaderUsesStencil( vendor, false ) && wot::stockTreeShaderUsesStencil( vendor, true ) );
    wot::StockTreeWindHistory history;
    assert( history.currentAngles.empty() && history.previousAngles.empty() );
    for (const auto & matrix : history.currentMatrices)
        for (float value : matrix) assert( value == 0.f );
    for (const auto & matrix : history.previousMatrices)
        for (unsigned i = 0; i < 16; ++i) assert( matrix[i] == (i%5 == 0 ? 1.f : 0.f) );
    auto matrices = history.previousMatrices;
    matrices[0][0] = .7f;
    const std::vector<std::array<float,4>> angles( 8, {{ .15f, .25f, 0.f, 0.f }} );
    history.publish( matrices, angles );
    assert( history.previousMatrices[0][0] == 0.f && history.previousAngles.empty() );
    assert( history.currentMatrices == matrices && history.currentAngles == angles );
    auto next = matrices;
    next[5][10] = .8f;
    history.publish( next, angles );
    assert( history.previousMatrices == matrices && history.previousAngles == angles );
    wot::StockTreeMaterialHandles handles;
    assert( handles.acquire() == 0 ); assert( handles.acquire() == 1 ); assert( handles.acquire() == 2 );
    assert( handles.release( 1 ) ); assert( handles.release( 0 ) );
    assert( !handles.release( 0 ) && handles.size() == 1 );
    assert( handles.acquire() == 0 ); assert( handles.acquire() == 1 );
    assert( handles.release( 1 ) ); assert( handles.release( 2 ) ); assert( handles.release( 0 ) );
    // Emptying the registry preserves the real free-stack order.
    assert( handles.acquire() == 0 ); assert( handles.acquire() == 2 ); assert( handles.acquire() == 1 );
    assert( handles.acquire() == 3 );
    for (std::uint32_t i = 4; i < 35; ++i) assert( handles.acquire() == i );
    assert( handles.release( 34 ) && handles.acquire() == 34 );
    const Bytes original = sceneFixture();
    wot::StockTreeSceneData scene;
    std::string error;
    assert( wot::decodeStockTreeSceneData( original.data(), original.size(), scene, error ) );
    assert( scene.definitions.size() == 2 && scene.definitions[0].resource == 0xff198276U );
    assert( scene.definitions[1].flags == 49 && scene.definitions[1].visibility == 0x7fff4000 );
    assert( encode( scene ) == original );
    for (std::size_t size = 0; size < original.size(); ++size)
    {
        assert( !wot::decodeStockTreeSceneData( original.data(), size, scene, error ) );
        assert( encode( scene ) == original );
    }
    for (std::size_t offset : { std::size_t(0), std::size_t(4), original.size()-28 })
    {
        Bytes corrupt = original;
        for (unsigned i = 0; i < 4; ++i) corrupt[offset+i] = 255;
        assert( !wot::decodeStockTreeSceneData( corrupt.data(), corrupt.size(), scene, error ) );
        assert( encode( scene ) == original );
    }
    Bytes trailing = original; trailing.push_back( 0 );
    assert( !wot::decodeStockTreeSceneData( trailing.data(), trailing.size(), scene, error ) );
    Bytes notFinite = original;
    notFinite[8] = 0; notFinite[9] = 0; notFinite[10] = 128; notFinite[11] = 127;
    assert( !wot::decodeStockTreeSceneData( notFinite.data(), notFinite.size(), scene, error ) );
    assert( encode( scene ) == original );
    Bytes empty;
    word( empty, 80 ); word( empty, 0 ); word( empty, 24 ); empty.resize( 36 );
    assert( wot::decodeStockTreeSceneData( empty.data(), empty.size(), scene, error ) && scene.definitions.empty() );
    float correction = -2.f;
    assert( !wot::stockTreeReflectionCorrection( {{ .1f, -1.f, .5f, 0.f }}, correction, error ) && correction == -2.f );
    assert( wot::stockTreeReflectionCorrection( {{ .25f, 1.f, 1.f, 0.f }}, correction, error ) && correction == 2.f );
}
int main( int argc, char ** argv )
{
    tests();
    if (argc == 1) { std::cout << "Original tree scene data/handle ownership tests passed\n"; return 0; }
    assert( argc == 3 || argc == 4 );
    std::ifstream input( argv[1], std::ios::binary );
    std::ofstream output( argv[2], std::ios::binary );
    assert( input && output );
    if (argc == 4)
    {
        assert( std::string( argv[3] ) == "--reflection" );
        std::array<float,4> albedo;
        while (input.read( reinterpret_cast<char *>( albedo.data() ), sizeof( albedo ) ))
        {
            float correction = 0;
            std::string error;
            assert( wot::stockTreeReflectionCorrection( albedo, correction, error ) );
            output.write( reinterpret_cast<const char *>( &correction ), sizeof( correction ) );
        }
        assert( input.eof() && input.gcount() == 0 );
    }
    else
    {
        std::uint32_t length;
        while (input.read( reinterpret_cast<char *>( &length ), 4 ))
        {
            Bytes bytes( length );
            assert( input.read( reinterpret_cast<char *>( bytes.data() ), length ) );
            wot::StockTreeSceneData scene;
            std::string error;
            assert( wot::decodeStockTreeSceneData( bytes.data(), bytes.size(), scene, error ) );
            const Bytes encoded = encode( scene );
            output.write( reinterpret_cast<const char *>( &length ), 4 );
            output.write( reinterpret_cast<const char *>( encoded.data() ), encoded.size() );
        }
        assert( input.eof() && input.gcount() == 0 );
    }
    assert( output.good() );
}
