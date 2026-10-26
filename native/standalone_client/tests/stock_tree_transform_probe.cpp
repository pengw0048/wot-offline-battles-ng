#include "math/blend_transform.hpp"
#include <array>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <fstream>

BW_BEGIN_NAMESPACE
int runStockTreeTransformProbe( const char * resource )
{
    std::ifstream input( resource, std::ios::binary );
    if (!input) return 2;
    std::array<float, 38> record;
    unsigned cases = 0;
    while (input.read( reinterpret_cast<char *>( record.data() ), sizeof( record ) ))
    {
        Matrix world, normalised;
        static_assert( sizeof( Matrix ) == 64, "Original matrix wire width" );
        std::memcpy( &world, record.data(), 64 );
        std::memcpy( &normalised, record.data()+16, 64 );
        const BlendTransform actual( world );
        D3DXQUATERNION expected;
        // The fixture captures 11bdcd0's exact matrix at this same original
        // DLL boundary. Both calls here use the real Win32 D3DX9_43 function.
        D3DXQuaternionRotationMatrix( &expected, &normalised );
        const Quaternion & rotation = actual.rotation();
        const float quaternion[] = { rotation.x, rotation.y, rotation.z, rotation.w };
        const Vector3 & position = actual.translation();
        const Vector3 & scale = actual.scaling();
        const float translation[] = { position.x, position.y, position.z };
        const float scaling[] = { scale.x, scale.y, scale.z };
        if (std::memcmp( quaternion, &expected, sizeof( quaternion ) ) ||
            std::memcmp( translation, record.data()+32, sizeof( translation ) ) ||
            std::memcmp( scaling, record.data()+35, sizeof( scaling ) ))
        {
            std::fprintf( stderr, "Original tree transform mismatch at case %u\n", cases );
            return 1;
        }
        ++cases;
    }
    if (!input.eof() || input.gcount()) return 3;
    std::printf( "Original tree transforms: %u exact Win32 BlendTransform/D3DX cases\n", cases );
    return cases ? 0 : 4;
}
BW_END_NAMESPACE

int main( int argc, char ** argv )
{
    if (argc != 2) return 2;
    return BW::runStockTreeTransformProbe( argv[1] );
}
