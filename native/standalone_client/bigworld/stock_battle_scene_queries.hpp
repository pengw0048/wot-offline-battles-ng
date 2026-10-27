#pragma once

#include "cstdmf/bw_namespace.hpp"
#include "math/vector3.hpp"
#include "math/vector4.hpp"
#include "physics2/worldtri.hpp"
#include "pyscript/script.hpp"
#include "space/forward_declarations.hpp"

BW_BEGIN_NAMESPACE

// One accepted wg_collideSegment surface (f63970 hit record).
struct StockSegmentHit
{
    Vector3 point;
    Vector3 normal;
    Vector3 translation;
    float distance;
    uint32 materialKind;
    uint32 chunkID;
    uint16 itemIndex;
};

enum StockSegmentResult
{
    STOCK_SEGMENT_HIT,
    STOCK_SEGMENT_MISS,
    STOCK_SEGMENT_NO_SPACE,
    STOCK_SEGMENT_FAILED    // A Python error is set.
};

// The native owner behind BigWorld.wg_collideSegment. The filter is the
// original optional fifth argument: an int selects collision-flag bits, a
// callable may reject one surface, and any other object is ignored.
StockSegmentResult collideStockSegment( SpaceID spaceID, const Vector3 & start,
    const Vector3 & end, WorldTriangle::Flags skipFlags, PyObject * filter,
    StockSegmentHit & hit );

// The native owner behind BigWorld.wg_collideWater: segment distance or -1.
float collideStockWater( const Vector3 & start, const Vector3 & end, bool edgeAlpha );

// The native owner behind BigWorld.wg_getSpaceBounds: camera-space x/z bounds.
Vector4 stockCameraSpaceBounds();

BW_END_NAMESPACE
