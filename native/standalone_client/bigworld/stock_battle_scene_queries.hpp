#pragma once

#include "cstdmf/bw_namespace.hpp"
#include "cstdmf/bw_string.hpp"
#include "math/vector3.hpp"
#include "math/vector4.hpp"
#include "physics2/worldtri.hpp"
#include "pyscript/script.hpp"
#include "space/forward_declarations.hpp"

#include <functional>

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
    uint64 itemID;     // Composite identity, see StockNativeSegmentHit.
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

// Exact registered Python wrapper identity, including custom-builtin rejection.
bool stockSegmentQueryCallable( PyObject * object );

// One f642b0 result: +4 point, +10 normal, +1c materialKind, +20 origin,
// +30 identity. identity is 0 without static-item identity, otherwise
// chunkID | uint64(uint16 itemIndex) << 32 | uint64(1) << 48 (f63ac0).
struct StockNativeSegmentHit
{
    Vector3 point;
    Vector3 normal;
    uint32 materialKind;
    Vector3 origin;
    uint64 identity;
};

// ClosestCcbNative filter (f62400): false rejects only that surface. An empty
// function accepts every surface without an identity lookup.
typedef std::function< bool( uint32 materialKind, uint32 collisionFlags, uint64 identity ) >
    StockNativeSegmentFilter;

// Status-preserving typed query for synchronous native engine-query owners.
// The caller retains the Python error channel and never materializes a hit.
StockSegmentResult collideStockSegmentFiltered( SpaceID spaceID,
    const Vector3 & start, const Vector3 & end, WorldTriangle::Flags skipFlags,
    const StockNativeSegmentFilter & filter, StockSegmentHit & hit );

struct StockMaterialHit
{
    Vector3 point, normal;
    uint32 materialKind = 0, chunkID = 0;
    uint16 itemIndex = 0;
    BW::string filename;
    StockMaterialHit() : point(0.f,0.f,0.f), normal(0.f,0.f,0.f) {}
};
// The broken predicate has the original material query's rejection polarity.
// Its filename is the previously accepted filename, as in #1513 f6124e.
using StockNativeBrokenFilter = std::function<bool(uint32,uint16,uint32,const BW::string&)>;
StockSegmentResult stockMaterialNearPoint( SpaceID spaceID,
    const Vector3 & start, const Vector3 & end, const Vector3 & point,
    const StockNativeBrokenFilter & broken, StockMaterialHit & hit );

// The native f642b0 query for native owners such as PyBallisticsSimulator.
// False for no surface, a zero-length segment, a missing or cleared space and
// an interrupted or failed query; it never leaves a Python error set.
bool collideStockSegmentNative( SpaceID spaceID, const Vector3 & start,
    const Vector3 & end, WorldTriangle::Flags skipFlags,
    const StockNativeSegmentFilter & filter, StockNativeSegmentHit & hit );

// #1513 f5d160/f5d1c0 for WGSPGGunMarkerDataProvider: the nearest camera-space
// surface distance along start->end that has no 0x80 (PROJECTILENOCOLLIDE)
// flag, or a negative value. The hit point is start + normalise(end-start) * d.
float stockGunMarkerSegmentCollision( const Vector3 & start, const Vector3 & end );

// f5bf40 uses the same owner with caller supplied collision masks.
float stockGunMarkerSegmentCollision( const Vector3 & start, const Vector3 & end,
    WorldTriangle::Flags skipFlags, WorldTriangle::Flags requiredFlags );

// The native owner behind BigWorld.wg_collideWater: segment distance or -1.
float collideStockWater( const Vector3 & start, const Vector3 & end, bool edgeAlpha );

// The native owner behind BigWorld.wg_getSpaceBounds: camera-space x/z bounds.
Vector4 stockCameraSpaceBounds();

BW_END_NAMESPACE
