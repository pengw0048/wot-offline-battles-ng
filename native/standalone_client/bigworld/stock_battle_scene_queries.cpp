#include "pch.hpp"
#include "stock_battle_scene_queries.hpp"
#include "stock_battle_scene_queries_data.hpp"
#include "stock_scene_identity.hpp"

#include "physics2/collision_callback.hpp"
#include "physics2/collision_obstacle.hpp"
#include "pyscript/script_math.hpp"
#include "romp/water.hpp"
#include "space/client_space.hpp"
#include "space/deprecated_space_helpers.hpp"
#include "space/space_manager.hpp"

#include <cfloat>
#include <cmath>

BW_BEGIN_NAMESPACE

float Water::stockWaterRayDistance( const Vector3 & start, const Vector3 & end,
    bool edgeAlpha ) const
{
    // a302e0: its false branch is the reviewed ground-effects water query.
    const float distance = this->stockGroundRayDistance( start, end );
    if (distance == -1.f || !edgeAlpha || !state_.useEdgeAlpha_)
        return distance;
    // Recompute the same accepted hit to sample wAlpha_ in water space.
    Vector3 direction = end - start;
    D3DXVec3Normalize( &direction, &direction );
    const Vector3 hit( direction.x * distance + start.x,
        direction.y * distance + start.y, direction.z * distance + start.z );
    Vector4 local;
    D3DXVec3Transform( &local, &hit, &invTransform_ );
    return wot::stockWaterEdgeAlphaAccepts( wAlpha_.empty() ? NULL : &wAlpha_[0],
        wAlpha_.size(), gridSizeX_, gridSizeZ_, state_.size_.x, state_.size_.y,
        state_.tessellation_, local.x, local.z ) ? distance : -1.f;
}

namespace
{
// 0068ece0: uint32 crosses as an int, or a long at and above 2^31.
PyObject * unsignedObject( uint32 value )
{
    return value <= 0x7fffffffu ? PyInt_FromLong( long( value ) ) :
        PyLong_FromUnsignedLong( value );
}

// f63ac0/f63970: chunkID | itemIndex16 << 32 | valid16 << 48; 0 without identity.
uint64 compositeIdentity( bool known, uint32 chunkID, uint16 itemIndex )
{
    return known ? uint64( chunkID ) | (uint64( itemIndex ) << 32) | (uint64( 1 ) << 48) : 0;
}

// ClosestCcbPy (vtable 0174d058, f620a0) and ClosestCcbNative (0174d070,
// f62400) share skip, nearest-hit and f63970 record rules; only their filter
// differs. The space stays pinned while traversal runs.
class SegmentQuery : public CollisionCallback
{
public:
    // f61a90: an int (including bool) selects flags; only otherwise may a
    // callable filter surfaces. Any other object is ignored.
    SegmentQuery( const ClientSpacePtr & space, WorldTriangle::Flags skipFlags,
        PyObject * filter ) :
        space_( space ), native_( NULL ), nearest_( FLT_MAX ), found_( false ),
        interrupted_( false ), failed_( false )
    {
        filter_.skipFlags = skipFlags;
        if (filter && PyInt_Check( filter ))
            filter_.selectMask = PyInt_AS_LONG( filter );
        else if (filter && PyCallable_Check( filter ) == 1)
        {
            filter_.callable = true;
            callable_ = filter;
        }
        hit_.distance = FLT_MAX;
    }

    // f642b0 takes a std::function by value; an empty one accepts every surface.
    SegmentQuery( const ClientSpacePtr & space, WorldTriangle::Flags skipFlags,
        const StockNativeSegmentFilter & filter ) :
        space_( space ), native_( &filter ), nearest_( FLT_MAX ), found_( false ),
        interrupted_( false ), failed_( false )
    {
        filter_.skipFlags = skipFlags;
        hit_.distance = FLT_MAX;
    }

    // Vtables 0174d058/0174d070 slot 1 (00bce4c0): terrain answers at its highest LOD.
    bool useHighestAvailableLod() override { return true; }

    int operator()( const CollisionObstacle & obstacle,
        const WorldTriangle & triangle, float distance ) override
    {
        if (failed_ || interrupted_)
            return COLLIDE_STOP;
        if (native_)
        {
            // f62400: skip, farther than the recorded hit, then the native filter.
            if (filter_.skipFlags & triangle.flags())
                return COLLIDE_ALL;
            if (distance > hit_.distance)
                return COLLIDE_BEFORE;
            if (*native_ && !this->consultNative( obstacle, triangle ))
                return failed_ || interrupted_ ? COLLIDE_STOP : COLLIDE_ALL;
        }
        else switch (wot::classifyStockSegmentTriangle( filter_, triangle.flags(),
            distance, nearest_ ))
        {
        case wot::StockSegmentStep::Skip:
            return COLLIDE_ALL;
        case wot::StockSegmentStep::Farther:
            return COLLIDE_BEFORE;
        case wot::StockSegmentStep::Consult:
            if (!this->consult( obstacle, triangle ))
                return failed_ || interrupted_ ? COLLIDE_STOP : COLLIDE_ALL;
            break;
        case wot::StockSegmentStep::Accept:
            break;
        }
        // f64630's WGMaterialDisabler branch cannot occur before that real type exists.
        if (hit_.distance > distance && !this->record( obstacle, triangle, distance ))
            return COLLIDE_STOP;
        nearest_ = distance;
        return COLLIDE_BEFORE;
    }

    bool found() const { return found_; }
    bool interrupted() const { return interrupted_; }
    bool failed() const { return failed_; }
    const StockSegmentHit & hit() const { return hit_; }

private:
    // The chunk-model or compiled WGDE1 identity owner; a miss is absent.
    bool identity( const CollisionObstacle & obstacle, bool & known, uint32 & chunkID,
        uint16 & itemIndex )
    {
        uint32 chunk = 0, item = 0;
        known = readStockObstacleItem( obstacle, chunk, item );
        if (PyErr_Occurred())
        {
            failed_ = true;
            return false;
        }
        chunkID = known ? chunk : 0;
        itemIndex = known ? uint16( item ) : 0;
        return true;
    }

    bool consultNative( const CollisionObstacle & obstacle, const WorldTriangle & triangle )
    {
        bool known;
        uint32 chunkID;
        uint16 itemIndex;
        if (!this->identity( obstacle, known, chunkID, itemIndex ))
            return false;
        const bool accepted = (*native_)( triangle.materialKind(),
            triangle.collisionFlags(), compositeIdentity( known, chunkID, itemIndex ) );
        if (space_->cleared())
        {
            interrupted_ = true;
            return false;
        }
        return accepted;
    }

    // f620a0: (matKind, collFlags, itemIndex, chunkID). Only an int-subclass
    // zero rejects; a raised error is printed and the surface is accepted.
    bool consult( const CollisionObstacle & obstacle, const WorldTriangle & triangle )
    {
        bool known;
        uint32 chunkID;
        uint16 itemIndex;
        if (!this->identity( obstacle, known, chunkID, itemIndex ))
            return false;
        PyObjectPtr args( PyTuple_New( 4 ), PyObjectPtr::STEAL_REFERENCE );
        if (!args)
        {
            failed_ = true;
            return false;
        }
        PyTuple_SET_ITEM( args.get(), 0, unsignedObject( triangle.materialKind() ) );
        PyTuple_SET_ITEM( args.get(), 1, unsignedObject( triangle.collisionFlags() ) );
        PyTuple_SET_ITEM( args.get(), 2, PyInt_FromLong( itemIndex ) );
        PyTuple_SET_ITEM( args.get(), 3, unsignedObject( chunkID ) );
        for (Py_ssize_t i = 0; i != 4; ++i)
        {
            if (!PyTuple_GET_ITEM( args.get(), i ))
            {
                failed_ = true;
                return false;
            }
        }
        // The callable may replace its owner or release the last space reference.
        PyObjectPtr callable = callable_;
        PyObject * result = PyObject_Call( callable.get(), args.get(), NULL );
        bool accepted = true;
        if (!result)
            PyErr_PrintEx( 0 );
        else
        {
            accepted = !(PyInt_Check( result ) && PyInt_AS_LONG( result ) == 0);
            Py_DECREF( result );
        }
        // Geometry it retired outlives this traversal; its result does not.
        if (space_->cleared())
        {
            interrupted_ = true;
            return false;
        }
        return accepted;
    }

    // f63970: normalised (v2-v0)x(v2-v1) in world space and the obstacle origin.
    bool record( const CollisionObstacle & obstacle, const WorldTriangle & triangle,
        float distance )
    {
        StockSegmentHit hit;
        bool known;
        if (!this->identity( obstacle, known, hit.chunkID, hit.itemIndex ))
            return false;
        hit.itemID = compositeIdentity( known, hit.chunkID, hit.itemIndex );
        const Vector3 & v0 = triangle.v0();
        const Vector3 & v1 = triangle.v1();
        const Vector3 & v2 = triangle.v2();
        const Vector3 a( v2.x - v0.x, v2.y - v0.y, v2.z - v0.z );
        const Vector3 b( v2.x - v1.x, v2.y - v1.y, v2.z - v1.z );
        const Vector3 cross( a.y * b.z - a.z * b.y, a.z * b.x - b.z * a.x,
            b.y * a.x - a.y * b.x );
        D3DXVec3TransformNormal( &hit.normal, &cross, &obstacle.transform() );
        D3DXVec3Normalize( &hit.normal, &hit.normal );
        hit.translation = obstacle.transform().applyToOrigin();
        hit.distance = distance;
        hit.materialKind = triangle.materialKind();
        hit_ = hit;
        found_ = true;
        return true;
    }

    ClientSpacePtr space_;
    wot::StockSegmentFilter filter_;
    PyObjectPtr callable_;
    const StockNativeSegmentFilter * native_;
    float nearest_;
    StockSegmentHit hit_;
    bool found_;
    bool interrupted_;
    bool failed_;
};

// The material-near-point callback (0174cf00, f610d0) visits every intersection:
// distance from the requested point, not distance from the ray origin, wins.
class MaterialNearPointQuery : public CollisionCallback
{
public:
    MaterialNearPointQuery( const ClientSpacePtr & space, const Vector3 & start,
        const Vector3 & end, const Vector3 & point, PyObjectPtr broken ) :
        space_( space ), start_( start ), direction_( end - start ), point_( point ),
        broken_( broken ), nearest_( FLT_MAX ), found_( false ), failed_( false ),
        interrupted_( false ), hitPoint_( 0.f, 0.f, 0.f ), normal_( 0.f, 0.f, 0.f ),
        kind_( 0 ), chunk_( 0 ), item_( 0 )
    {
        D3DXVec3Normalize( &direction_, &direction_ );
    }

    bool useHighestAvailableLod() override { return true; }

    int operator()( const CollisionObstacle & obstacle,
        const WorldTriangle & triangle, float distance ) override
    {
        if (failed_ || interrupted_) return COLLIDE_STOP;
        if (triangle.flags() & 0x80) return COLLIDE_ALL;
        const Vector3 hit( start_.x + direction_.x * distance,
            start_.y + direction_.y * distance, start_.z + direction_.z * distance );
        const Vector3 delta = hit - point_;
        const float separation = std::sqrt( delta.y * delta.y + delta.x * delta.x +
            delta.z * delta.z );
        if (separation >= nearest_) return COLLIDE_ALL;
        // f64630: compiled StaticModelFeature has no material fashion.
        uint32 chunk = 0, item = 0;
        readStockObstacleItem( obstacle, chunk, item );
        if (PyErr_Occurred()) { failed_ = true; return COLLIDE_STOP; }
        const uint32 kind = triangle.materialKind();
        BW::string filename;
        if (kind >= 71 && kind <= 100)
        {
            // Damaged material, or no callback: record the kind only. The caller
            // uses this even if no surface was accepted (f613ee).
            if (!broken_ || kind >= 87) { kind_ = kind; return COLLIDE_ALL; }
            readStockObstacleFilename( obstacle, filename );
            if (PyErr_Occurred()) { failed_ = true; return COLLIDE_STOP; }
            PyObjectPtr args( PyTuple_New( 4 ), PyObjectPtr::STEAL_REFERENCE );
            if (!args) { failed_ = true; return COLLIDE_STOP; }
            PyTuple_SET_ITEM( args.get(), 0, unsignedObject( chunk ) );
            PyTuple_SET_ITEM( args.get(), 1, PyInt_FromLong( uint16( item ) ) );
            PyTuple_SET_ITEM( args.get(), 2, unsignedObject( kind ) );
            // f6124e intentionally passes the previously accepted filename.
            PyTuple_SET_ITEM( args.get(), 3, PyString_FromStringAndSize(
                filename_.data(), filename_.size() ) );
            for (Py_ssize_t i = 0; i != 4; ++i)
                if (!PyTuple_GET_ITEM( args.get(), i ))
                { failed_ = true; return COLLIDE_STOP; }
            PyObjectPtr callback = broken_;
            bool reject = true;
            {
                PyObjectPtr result( PyObject_Call( callback.get(), args.get(), NULL ),
                    PyObjectPtr::STEAL_REFERENCE );
                if (!result) PyErr_PrintEx( 0 );
                else if (Script::setData( result.get(), reject, "callback result" ) < 0)
                {
                    // Preserve the conversion error without returning a tuple
                    // with an exception pending as the original wrapper does.
                    failed_ = true;
                    return COLLIDE_STOP;
                }
            } // An int subclass destructor may retire the space too.
            if (space_->cleared()) { interrupted_ = true; return COLLIDE_STOP; }
            if (reject) return COLLIDE_ALL;
        }
        if (space_->cleared()) { interrupted_ = true; return COLLIDE_STOP; }
        nearest_ = separation;
        hitPoint_ = hit;
        const Vector3 a = triangle.v2() - triangle.v0();
        const Vector3 b = triangle.v2() - triangle.v1();
        const Vector3 cross( a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z,
            a.x * b.y - a.y * b.x );
        D3DXVec3TransformNormal( &normal_, &cross, &obstacle.transform() );
        D3DXVec3Normalize( &normal_, &normal_ );
        chunk_ = chunk;
        item_ = uint16( item );
        kind_ = kind;
        filename_ = filename;
        found_ = true;
        return COLLIDE_ALL;
    }

    PyObject * result() const
    {
        if (failed_) return NULL;
        // Miss vectors/kind are uninitialised in the original constructor.
        // Deterministic zero defaults avoid exposing stack bytes; accepted hits
        // and the original damaged-kind side effect retain their exact values.
        PyObject * result = PyTuple_New( 7 );
        if (!result) return NULL;
        PyTuple_SET_ITEM( result, 0, PyBool_FromLong( found_ && !interrupted_ && !space_->cleared() ) );
        PyTuple_SET_ITEM( result, 1, Script::getData( hitPoint_ ) );
        PyTuple_SET_ITEM( result, 2, Script::getData( normal_ ) );
        PyTuple_SET_ITEM( result, 3, unsignedObject( kind_ ) );
        PyTuple_SET_ITEM( result, 4, PyString_FromStringAndSize( filename_.data(), filename_.size() ) );
        PyTuple_SET_ITEM( result, 5, PyInt_FromLong( item_ ) );
        PyTuple_SET_ITEM( result, 6, unsignedObject( chunk_ ) );
        for (Py_ssize_t i = 0; i != 7; ++i)
            if (!PyTuple_GET_ITEM( result, i )) { Py_DECREF( result ); return NULL; }
        return result;
    }

private:
    ClientSpacePtr space_;
    Vector3 start_, direction_, point_;
    PyObjectPtr broken_;
    float nearest_;
    bool found_, failed_, interrupted_;
    Vector3 hitPoint_, normal_;
    uint32 kind_, chunk_;
    uint16 item_;
    BW::string filename_;
};

// Ccb_ (vtable 0174c304, operator f5c1d0) for the SPG gun marker: a skip and
// a required collision-flag word over all 16 bits, then the nearest distance.
class GunMarkerQuery : public CollisionCallback
{
public:
    GunMarkerQuery( WorldTriangle::Flags skip, WorldTriangle::Flags required ) :
        skip_( skip ), required_( required ), distance_( -1.f ) {}

    // Slot 1 (00bce4c0): terrain answers at its highest LOD.
    bool useHighestAvailableLod() override { return true; }

    int operator()( const CollisionObstacle &, const WorldTriangle & triangle,
        float distance ) override
    {
        const WorldTriangle::Flags flags = triangle.flags();
        if ((skip_ & flags) != 0 || (required_ & flags) != required_)
            return COLLIDE_ALL;
        // f64630's WGMaterialDisabler branch cannot occur before that real type exists.
        if (0.f > distance_ || distance_ > distance)
            distance_ = distance;
        return COLLIDE_BEFORE;
    }

    float distance() const { return distance_; }

private:
    WorldTriangle::Flags skip_;
    WorldTriangle::Flags required_;
    float distance_;
};

template <class Filter>
StockSegmentResult collideSegment( SpaceID spaceID, const Vector3 & start,
    const Vector3 & end, WorldTriangle::Flags skipFlags, const Filter & filter,
    StockSegmentHit & hit )
{
    // f640d0/f642b0 resolve the space by ID and keep it for the whole traversal.
    ClientSpacePtr space = SpaceManager::instance().space( spaceID );
    if (!space)
        return STOCK_SEGMENT_NO_SPACE;
    // 01172190 rejects |(end-start) x e2 . e1| < 1e-6 before any position test,
    // so #1513 reports no surface for a zero-length segment, present or not.
    if (space->cleared() || start == end)
        return STOCK_SEGMENT_MISS;
    SegmentQuery query( space, skipFlags, filter );
    space->collide( start, end, query );
    if (query.failed())
        return STOCK_SEGMENT_FAILED;
    // A filter that tore the space down ends the query without a surface.
    if (query.interrupted() || !query.found())
        return STOCK_SEGMENT_MISS;
    hit = query.hit();
    Vector3 direction = end - start;
    D3DXVec3Normalize( &direction, &direction );
    hit.point = Vector3( start.x + hit.distance * direction.x,
        start.y + hit.distance * direction.y, start.z + hit.distance * direction.z );
    return STOCK_SEGMENT_HIT;
}
} // namespace

StockSegmentResult collideStockSegment( SpaceID spaceID, const Vector3 & start,
    const Vector3 & end, WorldTriangle::Flags skipFlags, PyObject * filter,
    StockSegmentHit & hit )
{
    return collideSegment( spaceID, start, end, skipFlags, filter, hit );
}

StockSegmentResult collideStockSegmentFiltered( SpaceID spaceID,
    const Vector3 & start, const Vector3 & end, WorldTriangle::Flags skipFlags,
    const StockNativeSegmentFilter & filter, StockSegmentHit & hit )
{
    return collideSegment( spaceID, start, end, skipFlags, filter, hit );
}

bool collideStockSegmentNative( SpaceID spaceID, const Vector3 & start,
    const Vector3 & end, WorldTriangle::Flags skipFlags,
    const StockNativeSegmentFilter & filter, StockNativeSegmentHit & hit )
{
    StockSegmentHit found;
    const StockSegmentResult result =
        collideSegment( spaceID, start, end, skipFlags, filter, found );
    if (result == STOCK_SEGMENT_FAILED)
        PyErr_PrintEx( 0 );    // f642b0 has no Python error channel.
    if (result != STOCK_SEGMENT_HIT)
        return false;
    // f642b0 result +4 point, +10 normal, +1c matKind, +20 origin, +30 identity.
    hit.point = found.point;
    hit.normal = found.normal;
    hit.materialKind = found.materialKind;
    hit.origin = found.translation;
    hit.identity = found.itemID;
    return true;
}

float stockGunMarkerSegmentCollision( const Vector3 & start, const Vector3 & end )
{
    return stockGunMarkerSegmentCollision( start, end, 0x80, 0 );
}

float stockGunMarkerSegmentCollision( const Vector3 & start, const Vector3 & end,
    WorldTriangle::Flags skipFlags, WorldTriangle::Flags requiredFlags )
{
    // f5d160 queries the camera space; f5d1c0 builds Ccb_(0x80, 0), whose
    // distance stays -1 without an accepted surface.
    ClientSpacePtr space = DeprecatedSpaceHelpers::cameraSpace();
    if (!space || start == end)
        return -1.f;
    GunMarkerQuery query( skipFlags, requiredFlags );
    space->collide( start, end, query );
    return query.distance();
}

float collideStockWater( const Vector3 & start, const Vector3 & end, bool edgeAlpha )
{
    // f629d0: the first Water with a positive distance wins.
    const Waters & waters = Waters::instance();
    for (Waters::const_iterator it = waters.begin(); it != waters.end(); ++it)
    {
        const float distance = (*it)->stockWaterRayDistance( start, end, edgeAlpha );
        if (distance > 0.f)
            return distance;
    }
    return -1.f;
}

Vector4 stockCameraSpaceBounds()
{
    // 006b3050: no camera space is a zero Vector4; otherwise min/max x and z.
    Vector4 result( 0.f, 0.f, 0.f, 0.f );
    ClientSpacePtr space = DeprecatedSpaceHelpers::cameraSpace();
    if (space)
    {
        const AABB bounds = space->bounds();
        result = Vector4( bounds.minBounds().x, bounds.minBounds().z,
            bounds.maxBounds().x, bounds.maxBounds().z );
    }
    return result;
}

namespace
{
PyObject * wg_collideSegment( SpaceID spaceID, const Vector3 & start,
    const Vector3 & end, WorldTriangle::Flags flags, PyObjectPtr filter )
{
    StockSegmentHit hit;
    switch (collideStockSegment( spaceID, start, end, flags, filter.get(), hit ))
    {
    case STOCK_SEGMENT_FAILED:
        return NULL;
    case STOCK_SEGMENT_NO_SPACE:
        // The original returns None with this error still pending; report it here.
        PyErr_Format( PyExc_ValueError, "No space ID %d", spaceID );
        PyErr_PrintEx( 0 );
        Py_RETURN_NONE;
    case STOCK_SEGMENT_MISS:
        Py_RETURN_NONE;
    case STOCK_SEGMENT_HIT:
        break;
    }
    PyObject * result = PyTuple_New( 6 );
    if (!result)
        return NULL;
    PyTuple_SET_ITEM( result, 0, Script::getData( hit.point ) );
    PyTuple_SET_ITEM( result, 1, Script::getData( hit.normal ) );
    PyTuple_SET_ITEM( result, 2, unsignedObject( hit.materialKind ) );
    PyTuple_SET_ITEM( result, 3, Script::getData( hit.translation ) );
    PyTuple_SET_ITEM( result, 4, PyInt_FromLong( hit.itemIndex ) );
    PyTuple_SET_ITEM( result, 5, unsignedObject( hit.chunkID ) );
    for (Py_ssize_t i = 0; i != 6; ++i)
    {
        if (!PyTuple_GET_ITEM( result, i ))
        {
            Py_DECREF( result );
            return NULL;
        }
    }
    return result;
}

PyObject * wg_getMatInfoNearPoint( SpaceID spaceID, const Vector3 & start,
    const Vector3 & end, const Vector3 & point, PyObjectPtr broken )
{
    ClientSpacePtr space = SpaceManager::instance().space( spaceID );
    MaterialNearPointQuery query( space, start, end, point, broken );
    if (!space)
    {
        PyErr_Format( PyExc_ValueError, "No space ID %d", spaceID );
        return NULL;
    }
    if (!space->cleared() && !(start == end)) space->collide( start, end, query );
    return query.result();
}

float wg_collideWater( const Vector3 & start, const Vector3 & end, bool edgeAlpha )
{
    return collideStockWater( start, end, edgeAlpha );
}

Vector4 wg_getSpaceBounds()
{
    return stockCameraSpaceBounds();
}
} // namespace

// #1513 declares the same argument and return converters (68e100, f63ca0).
PY_AUTO_MODULE_FUNCTION( RETOWN, wg_collideSegment, ARG( SpaceID, ARG( Vector3,
    ARG( Vector3, ARG( WorldTriangle::Flags, OPTARG( PyObjectPtr, NULL, END ) ) ) ) ),
    BigWorld )
PY_AUTO_MODULE_FUNCTION( RETOWN, wg_getMatInfoNearPoint, ARG( SpaceID, ARG( Vector3,
    ARG( Vector3, ARG( Vector3, OPTARG( PyObjectPtr, NULL, END ) ) ) ) ), BigWorld )
PY_AUTO_MODULE_FUNCTION( RETDATA, wg_collideWater, ARG( Vector3, ARG( Vector3,
    OPTARG( bool, true, END ) ) ), BigWorld )
PY_AUTO_MODULE_FUNCTION( RETDATA, wg_getSpaceBounds, END, BigWorld )

bool stockSegmentQueryCallable( PyObject * object )
{
    return object && PyCFunction_Check( object ) &&
        PyCFunction_GET_FUNCTION( object ) == _py_wg_collideSegment;
}

BW_END_NAMESPACE
