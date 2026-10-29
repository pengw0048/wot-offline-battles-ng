#include "pch.hpp"
#include "stock_bsp_collision_model.hpp"

#include "duplo/pymodel.hpp"
#include "math/boundbox.hpp"
#include "math/matrix.hpp"
#include "model/model.hpp"
#include "model/super_model.hpp"
#include "physics2/bsp.hpp"
#include "physics2/collision_callback.hpp"
#include "physics2/collision_obstacle.hpp"
#include "physics2/sweep_helpers.hpp"
#include "physics2/worldtri.hpp"
#include "pyscript/pyobject_plus.hpp"
#include "pyscript/script.hpp"
#include "pyscript/script_math.hpp"

#include <set>

BW_BEGIN_NAMESPACE
namespace
{
wot::stock_bsp::Point toPoint( const Vector3 & value )
{
    const wot::stock_bsp::Point result = { value.x, value.y, value.z };
    return result;
}

Vector3 toVector3( const wot::stock_bsp::Point & value )
{
    return Vector3( value.x, value.y, value.z );
}

// 01179cd0: a BSPSweepVisitor over the segment inside one obstacle.
bool collideSegment( const BSPTree & tree, const Vector3 & start, const Vector3 & end,
    const Matrix & transform, const Matrix & inverse, CollisionState & state )
{
    const CollisionObstacle obstacle( &transform, &inverse, SceneObject() );
    SweepHelpers::BSPSweepVisitor visitor( obstacle, state );
    float distance = 1.f;
    tree.intersects( start, end, distance, NULL, &visitor );
    return visitor.shouldStop();
}

// 01179d70: sweep the triangle to extent, measured from its first vertex.
bool sweepTriangle( const BSPTree & tree, const WorldTriangle & triangle,
    const Vector3 & extent, const Matrix & transform, const Matrix & inverse,
    CollisionState & state )
{
    const CollisionObstacle obstacle( &transform, &inverse, SceneObject() );
    SweepHelpers::BSPSweepVisitor visitor( obstacle, state );
    tree.intersects( triangle, extent - triangle.v0(), &visitor );
    return visitor.shouldStop();
}

typedef std::set< wot::stock_bsp::SegmentHit, wot::stock_bsp::SegmentHitOrder > SegmentHits;

// 00f43ee0 (vtable 0174a540).
class CollideSegmentTriangleCollector : public CollisionCallback
{
public:
    CollideSegmentTriangleCollector( const Vector3 & start, const Vector3 & direction,
        bool stopAtFirst ) :
        start_( start ), direction_( direction ), stopAtFirst_( stopAtFirst ) {}

    const SegmentHits & hits() const { return hits_; }

private:
    virtual int operator()( const CollisionObstacle &, const WorldTriangle & triangle, float )
    {
        if (triangle.flags() & (TRIANGLE_NOCOLLIDE | TRIANGLE_WATER))
            return COLLIDE_ALL;
        float distance = 1e10f;
        if (!triangle.intersects( start_, direction_, distance ))
            return COLLIDE_ALL;
        const wot::stock_bsp::SegmentHit hit = {
            static_cast< uint32 >( triangle.materialKind() ), distance,
            wot::stock_bsp::rawNormal( toPoint( triangle.v0() ), toPoint( triangle.v1() ),
                toPoint( triangle.v2() ) ) };
        hits_.insert( hit );
        return stopAtFirst_ ? COLLIDE_STOP : COLLIDE_ALL;
    }

    SegmentHits hits_;
    Vector3 start_;
    Vector3 direction_;
    bool stopAtFirst_;
};

// 00f43fd0 (vtable 0174a558).
class CollideSphereTriangleCollector : public CollisionCallback
{
public:
    CollideSphereTriangleCollector( const Vector3 & centre, float radius, bool outsideOnly ) :
        centre_( toPoint( centre ) ), radiusSquared_( radius * radius ),
        outsideOnly_( outsideOnly ) {}

    const std::vector< wot::stock_bsp::SphereHit > & hits() const { return hits_; }

private:
    virtual int operator()( const CollisionObstacle &, const WorldTriangle & triangle, float )
    {
        if (triangle.flags() & (TRIANGLE_NOCOLLIDE | TRIANGLE_WATER))
            return COLLIDE_ALL;
        const wot::stock_bsp::Point v0 = toPoint( triangle.v0() );
        const wot::stock_bsp::Point v1 = toPoint( triangle.v1() );
        const wot::stock_bsp::Point v2 = toPoint( triangle.v2() );
        if (outsideOnly_ && wot::stock_bsp::sphereCentreBehind( centre_, v0, v1, v2 ))
            return COLLIDE_ALL;
        wot::stock_bsp::collectSphereHit( hits_, centre_, radiusSquared_, v0, v1, v2,
            static_cast< uint32 >( triangle.materialKind() ) );
        return COLLIDE_ALL;
    }

    std::vector< wot::stock_bsp::SphereHit > hits_;
    wot::stock_bsp::Point centre_;
    float radiusSquared_;
    bool outsideOnly_;
};

// The original dereferences a NULL tree here (and in collideSphere and
// collidesWithTriangle); contain that state as a Python error.
PyObject * missingTree( const char * method )
{
    PyErr_Format( PyExc_RuntimeError,
        "WGBspCollisionModel.%s: the loaded model has no collision BSP", method );
    return NULL;
}
}

std::vector<StockBspSegmentHit> stockBspSegment(const BSPTree & tree,
    const Vector3 & start,const Vector3 & end)
{
    Vector3 direction=end-start;direction.normalise();
    CollideSegmentTriangleCollector collector(start,direction,false);
    CollisionState state(collector);
    collideSegment(tree,start,end,Matrix::identity,Matrix::identity,state);
    std::vector<StockBspSegmentHit> result;
    result.reserve(collector.hits().size());
    for(const auto & raw:collector.hits()) {
        Vector3 normal=toVector3(raw.normal);normal.normalise();
        StockBspSegmentHit hit;hit.distance=raw.distance;hit.material=raw.materialKind;
        hit.normal=toPoint(normal);
        hit.angleCosine=wot::stock_bsp::facedCosine(hit.normal,toPoint(direction));
        result.push_back(hit);
    }
    return result;
}

/**
 *	#1513 WGBspCollisionModel: the BSP of one cached Model, queried in that
 *	model's own coordinates. Callers transform world points before querying.
 */
class WGBspCollisionModel : public PyObjectPlus
{
    Py_Header( WGBspCollisionModel, PyObjectPlus )
public:
    WGBspCollisionModel() : PyObjectPlus( &s_type_ ), transform_( Matrix::identity ),
        inverse_( Matrix::identity ), tree_( NULL ) {}

    const BSPTree * tree() const { return tree_; }
    Model * model() const { return model_.get(); }

    PY_FACTORY_DECLARE()
    PY_METHOD_DECLARE( py_setModelName )
    PY_METHOD_DECLARE( py_setDestructibleModelName )
    PY_METHOD_DECLARE( py_setModel )
    PY_METHOD_DECLARE( py_collideSegment )
    PY_METHOD_DECLARE( py_collideSegmentAny )
    PY_METHOD_DECLARE( py_collideSphere )
    PY_METHOD_DECLARE( py_collidesWithTriangle )
    PY_METHOD_DECLARE( py_getBoundingBox )

private:
    bool assign( ModelPtr model );

    // 00f442f0 initialises both obstacle matrices to identity; nothing sets them.
    Matrix transform_;
    Matrix inverse_;
    const BSPTree * tree_;
    ModelPtr model_;
};

// 00f455d0: reassigning the current model keeps its tree and reports whether
// a model is held; another model is admitted only with a rooted BSP.
bool WGBspCollisionModel::assign( ModelPtr model )
{
    if (model == model_)
        return model_.hasObject();
    model_ = model;
    tree_ = NULL;
    if (!model_)
        return false;
    const BSPTree * tree = model_->decompose();
    if (!tree || tree->numNodes() == 0)
        return false;
    tree_ = tree;
    return true;
}

// 00f442f0 ignores its arguments.
PyObject * WGBspCollisionModel::pyNew( PyObject * )
{
    return new WGBspCollisionModel();
}

// 00f44e70: Model::get( name ), then 00f455d0.
PyObject * WGBspCollisionModel::py_setModelName( PyObject * args )
{
    if (PyTuple_Size( args ) != 1)
        return Script::argCountError( "setModelName", 1, 1, "BW::string" );
    BW::string name;
    if (Script::setData( PyTuple_GET_ITEM( args, 0 ), name, "() argument 1" ) != 0)
        return NULL;
    return Script::getData( this->assign( Model::get( name ) ) );
}

// 00f44ba0 also stores index and (key & 0xffff) | 0x20000 at +0x90/+0x94; no
// recovered reader uses them, so only the load is observable.
PyObject * WGBspCollisionModel::py_setDestructibleModelName( PyObject * args )
{
    if (PyTuple_Size( args ) != 3)
        return Script::argCountError( "setDestructibleModelName", 3, 3,
            "BW::string", "uint32", "uint32" );
    BW::string name;
    uint32 index = 0;
    uint32 key = 0;
    if (Script::setData( PyTuple_GET_ITEM( args, 0 ), name, "() argument 1" ) != 0 ||
        Script::setData( PyTuple_GET_ITEM( args, 1 ), index, "() argument 2" ) != 0 ||
        Script::setData( PyTuple_GET_ITEM( args, 2 ), key, "() argument 3" ) != 0)
        return NULL;
    return Script::getData( this->assign( Model::get( name ) ) );
}

// 00f44d20: the PyModel's first top model, only while its SuperModel is OK.
PyObject * WGBspCollisionModel::py_setModel( PyObject * args )
{
    if (PyTuple_Size( args ) != 1)
        return Script::argCountError( "setModel", 1, 1, "PyModelPtr" );
    SmartPointer< PyModel > pyModel;
    if (Script::setData( PyTuple_GET_ITEM( args, 0 ), pyModel, "() argument 1" ) != 0)
        return NULL;
    SuperModel * superModel = pyModel ? pyModel->pSuperModel() : NULL;
    if (!superModel || !superModel->isOK() || superModel->nModels() == 0)
        return Script::getData( false );
    return Script::getData( this->assign( superModel->topModel( 0 ) ) );
}

// 00f443f0 -> 00f44fe0: every hit as (distance, normal, hitAngleCos, materialKind).
PyObject * WGBspCollisionModel::py_collideSegment( PyObject * args )
{
    if (PyTuple_Size( args ) != 2)
        return Script::argCountError( "collideSegment", 2, 2, "Vector3", "Vector3" );
    Vector3 start, end;
    if (Script::setData( PyTuple_GET_ITEM( args, 0 ), start, "() argument 1" ) != 0 ||
        Script::setData( PyTuple_GET_ITEM( args, 1 ), end, "() argument 2" ) != 0)
        return NULL;
    if (!model_)
        Py_RETURN_NONE;
    if (!tree_)
        return missingTree( "collideSegment" );
    Vector3 direction = end - start;
    direction.normalise();
    CollideSegmentTriangleCollector collector( start, direction, false );
    CollisionState state( collector );
    collideSegment( *tree_, start, end, transform_, inverse_, state );
    const SegmentHits & hits = collector.hits();
    if (hits.empty())
        Py_RETURN_NONE;
    PyObject * result = PyList_New( Py_ssize_t( hits.size() ) );
    Py_ssize_t index = 0;
    for (SegmentHits::const_iterator it = hits.begin(); it != hits.end(); ++it, ++index)
    {
        Vector3 normal = toVector3( it->normal );
        normal.normalise();
        wot::stock_bsp::Point faced = toPoint( normal );
        const float cosine = wot::stock_bsp::facedCosine( faced, toPoint( direction ) );
        PyObject * hit = PyTuple_New( 4 );
        PyTuple_SET_ITEM( hit, 0, Script::getData( it->distance ) );
        PyTuple_SET_ITEM( hit, 1, Script::getData( toVector3( faced ) ) );
        PyTuple_SET_ITEM( hit, 2, Script::getData( cosine ) );
        PyTuple_SET_ITEM( hit, 3, Script::getData( int( it->materialKind ) ) );
        PyList_SET_ITEM( result, index, hit );
    }
    return result;
}

// 00f444c0: the first accepted hit's distance, or None.
PyObject * WGBspCollisionModel::py_collideSegmentAny( PyObject * args )
{
    if (PyTuple_Size( args ) != 2)
        return Script::argCountError( "collideSegmentAny", 2, 2, "Vector3", "Vector3" );
    Vector3 start, end;
    if (Script::setData( PyTuple_GET_ITEM( args, 0 ), start, "() argument 1" ) != 0 ||
        Script::setData( PyTuple_GET_ITEM( args, 1 ), end, "() argument 2" ) != 0)
        return NULL;
    if (!model_)
        Py_RETURN_NONE;
    if (!tree_)
        return missingTree( "collideSegmentAny" );
    Vector3 direction = end - start;
    direction.normalise();
    CollideSegmentTriangleCollector collector( start, direction, true );
    CollisionState state( collector );
    collideSegment( *tree_, start, end, transform_, inverse_, state );
    if (collector.hits().empty())
        Py_RETURN_NONE;
    return Script::getData( collector.hits().begin()->distance );
}

// 00f446b0 -> 00f452b0: [(closestPoint, materialKind)] in first-contact order.
PyObject * WGBspCollisionModel::py_collideSphere( PyObject * args )
{
    if (PyTuple_Size( args ) != 3)
        return Script::argCountError( "collideSphere", 3, 3, "Vector3", "float", "bool" );
    Vector3 centre;
    float radius = 0.f;
    bool outsideOnly = false;
    if (Script::setData( PyTuple_GET_ITEM( args, 0 ), centre, "() argument 1" ) != 0 ||
        Script::setData( PyTuple_GET_ITEM( args, 1 ), radius, "() argument 2" ) != 0 ||
        Script::setData( PyTuple_GET_ITEM( args, 2 ), outsideOnly, "() argument 3" ) != 0)
        return NULL;
    if (!tree_)
        return missingTree( "collideSphere" );
    wot::stock_bsp::Point first[3], second[3], extent;
    wot::stock_bsp::sphereSweep( toPoint( centre ), radius, first, second, extent );
    CollideSphereTriangleCollector collector( centre, radius, outsideOnly );
    CollisionState state( collector );
    sweepTriangle( *tree_, WorldTriangle( toVector3( first[0] ), toVector3( first[1] ),
        toVector3( first[2] ) ), toVector3( extent ), transform_, inverse_, state );
    sweepTriangle( *tree_, WorldTriangle( toVector3( second[0] ), toVector3( second[1] ),
        toVector3( second[2] ) ), toVector3( extent ), transform_, inverse_, state );
    const std::vector< wot::stock_bsp::SphereHit > & hits = collector.hits();
    PyObject * result = PyList_New( Py_ssize_t( hits.size() ) );
    for (size_t i = 0; i < hits.size(); ++i)
    {
        PyObject * hit = PyTuple_New( 2 );
        PyTuple_SET_ITEM( hit, 0, Script::getData( toVector3( hits[i].point ) ) );
        PyTuple_SET_ITEM( hit, 1, Script::getData( int( hits[i].materialKind ) ) );
        PyList_SET_ITEM( result, Py_ssize_t( i ), hit );
    }
    return result;
}

// 00f447c0: does the triangle swept by hitDir touch any BSP triangle?
PyObject * WGBspCollisionModel::py_collidesWithTriangle( PyObject * args )
{
    if (PyTuple_Size( args ) != 2)
        return Script::argCountError( "collidesWithTriangle", 2, 2, "PyObjectPtr", "Vector3" );
    PyObjectPtr triangle;
    Vector3 direction;
    if (Script::setData( PyTuple_GET_ITEM( args, 0 ), triangle, "() argument 1" ) != 0 ||
        Script::setData( PyTuple_GET_ITEM( args, 1 ), direction, "() argument 2" ) != 0)
        return NULL;
    // The original leaves this ValueError pending and returns False; CPython
    // 2.7 would surface it at an unrelated later call. Raise it here.
    Vector3 vertices[3];
    PyObject * sequence = triangle.get();
    for (int i = 0; i < 3; ++i)
    {
        PyObject * item = sequence && PyTuple_Check( sequence ) &&
            i < PyTuple_GET_SIZE( sequence ) ? PyTuple_GET_ITEM( sequence, i ) : NULL;
        if (!item || Script::setData( item, vertices[i], "" ) != 0)
        {
            PyErr_SetString( PyExc_ValueError,
                "Wrong argument for conversion to WorldTriangle" );
            return NULL;
        }
    }
    if (!tree_)
        return missingTree( "collidesWithTriangle" );
    CollisionState state( CollisionCallback::s_default );
    return Script::getData( sweepTriangle( *tree_,
        WorldTriangle( vertices[0], vertices[1], vertices[2] ), vertices[0] + direction,
        transform_, inverse_, state ) );
}

// 00f44a60: (min, max, radius) of Model::boundingBox, or None without a model.
PyObject * WGBspCollisionModel::py_getBoundingBox( PyObject * args )
{
    if (PyTuple_Size( args ) != 0)
        return Script::argCountError( "getBoundingBox", 0, 0 );
    if (!model_)
        Py_RETURN_NONE;
    const BoundingBox & box = model_->boundingBox();
    PyObject * result = PyTuple_New( 3 );
    PyTuple_SET_ITEM( result, 0, Script::getData( box.minBounds() ) );
    PyTuple_SET_ITEM( result, 1, Script::getData( box.maxBounds() ) );
    PyTuple_SET_ITEM( result, 2, Script::getData( std::sqrt(
        wot::stock_bsp::boundingRadiusSquared( toPoint( box.minBounds() ),
            toPoint( box.maxBounds() ) ) ) ) );
    return result;
}

PY_TYPEOBJECT( WGBspCollisionModel )
PY_BEGIN_METHODS( WGBspCollisionModel )
    PY_METHOD( setModelName )
    PY_METHOD( setDestructibleModelName )
    PY_METHOD( setModel )
    PY_METHOD( collideSegment )
    PY_METHOD( collideSegmentAny )
    PY_METHOD( collideSphere )
    PY_METHOD( collidesWithTriangle )
    PY_METHOD( getBoundingBox )
PY_END_METHODS()
PY_BEGIN_ATTRIBUTES( WGBspCollisionModel )
PY_END_ATTRIBUTES()
PY_FACTORY_NAMED( WGBspCollisionModel, "WGBspCollisionModel", BigWorld )

const BSPTree * stockBspCollisionModelTree( PyObject * object )
{
    return WGBspCollisionModel::Check( object ) ?
        static_cast< WGBspCollisionModel * >( object )->tree() : NULL;
}

Model * stockBspCollisionModelModel( PyObject * object )
{
    return WGBspCollisionModel::Check( object ) ?
        static_cast< WGBspCollisionModel * >( object )->model() : NULL;
}
BW_END_NAMESPACE
