#pragma once
#include "math/matrix.hpp"
#include "math/vector4.hpp"
#include "cstdmf/bw_string.hpp"
#include <memory>
#include <string>

BW_BEGIN_NAMESPACE
class StockTPSVisualResource
{
public:
    virtual ~StockTPSVisualResource() = default;
    virtual bool load( std::string & error ) = 0;
    virtual bool draw( const Matrix & world, const Vector4 & colour,
        const Vector4 & lighting, std::string & error ) = 0;
};
std::shared_ptr<StockTPSVisualResource> createStockTPSVisualResource( const BW::string & resource );
float stockTPSGroundHeight( float x, float z );
BW_END_NAMESPACE
