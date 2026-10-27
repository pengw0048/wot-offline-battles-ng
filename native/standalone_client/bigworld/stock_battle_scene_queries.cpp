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

class SegmentQuery : public CollisionCallback
{
public:
    SegmentQuery( const ClientSpacePtr & space, WorldTriangle::Flags skipFlags,
        PyObject * filter ) :
        space_( space ), nearest_( FLT_MAX ), found_( false ), interrupted_( false ),
        failed_( false )
    {
        // f61a90: an int (including bool) selects flags; only otherwise may a
        // callable filter surfaces. Any other object is ignored.
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

    int operator()( const CollisionObstacle & obstacle,
        const WorldTriangle & triangle, float distance ) override
    {
        if (failed_ || interrupted_)
            return COLLIDE_STOP;
        switch (wot::classifyStockSegmentTriangle( filter_, triangle.flags(),
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

    // Vtable 0x174d058 slot 1 (00bce4c0): terrain answers at its highest LOD.
    bool useHighestAvailableLod() override { return true; }

    bool found() const { return found_; }
    bool interrupted() const { return interrupted_; }
    bool failed() const { return failed_; }
    const StockSegmentHit & hit() const { return hit_; }

private:
    // f63ac0/f63970 read the static-item identity; a miss leaves 0/0.
    bool identity( const CollisionObstacle & obstacle, uint32 & chunkID, uint16 & itemIndex )
    {
        StockObstacleIdentity value;
        const bool known = readStockObstacleIdentity( obstacle, value );
        if (PyErr_Occurred())
        {
            failed_ = true;
            return false;
        }
        chunkID = known ? value.chunkID : 0;
        itemIndex = known ? uint16( value.itemIndex ) : 0;
        return true;
    }

    // f620a0: (matKind, collFlags, itemIndex, chunkID). Only an int-subclass
    // zero rejects; a raised error is printed and the surface is accepted.
    bool consult( const CollisionObstacle & obstacle, const WorldTriangle & triangle )
    {
        uint32 chunkID;
        uint16 itemIndex;
        if (!this->identity( obstacle, chunkID, itemIndex ))
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
        if (!this->identity( obstacle, hit.chunkID, hit.itemIndex ))
            return false;
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
    float nearest_;
    StockSegmentHit hit_;
    bool found_;
    bool interrupted_;
    bool failed_;
};
} // namespace

StockSegmentResult collideStockSegment( SpaceID spaceID, const Vector3 & start,
    const Vector3 & end, WorldTriangle::Flags skipFlags, PyObject * filter,
    StockSegmentHit & hit )
{
    // f640d0 resolves the space by ID and keeps it for the whole traversal.
    ClientSpacePtr space = SpaceManager::instance().space( spaceID );
    if (!space)
        return STOCK_SEGMENT_NO_SPACE;
    if (space->cleared())
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
PY_AUTO_MODULE_FUNCTION( RETDATA, wg_collideWater, ARG( Vector3, ARG( Vector3,
    OPTARG( bool, true, END ) ) ), BigWorld )
PY_AUTO_MODULE_FUNCTION( RETDATA, wg_getSpaceBounds, END, BigWorld )

BW_END_NAMESPACE
