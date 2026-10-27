#include "pch.hpp"
#include "stock_cursor_camera.hpp"
#include "stock_tree_scene.hpp"

#include "duplo/pymodel.hpp"
#include "math/boundbox.hpp"
#include "model/model.hpp"
#include "model/super_model.hpp"
#include "moo/render_context.hpp"
#include "physics2/bsp.hpp"
#include "physics2/worldtri.hpp"
#include "pyscript/script_math.hpp"

#include <algorithm>

BW_BEGIN_NAMESPACE

namespace
{
const char * WRONG_MODELS =
    "CursorCamera::wg_addModelsToCollideWith - wrong parameter type. "
    "Must be list of PyModel.";
const char * WRONG_MODEL =
    "CursorCamera::wg_addModelsToCollideWith - wrong item type in the list. "
    "Must be PyModel instance.";

// #1513 007a41b0: the nearest hit-vertex depth along the sweep direction.
class StockModelHitDepth : public CollisionVisitor
{
public:
    StockModelHitDepth( const Vector3 & normal, float offset ) :
        depth_( -1.f ), normal_( normal ), offset_( offset ) {}

    float depth() const { return depth_; }

    virtual bool visit( const WorldTriangle & triangle, float )
    {
        float nearest = this->vertexDepth( triangle.v0() );
        const float second = this->vertexDepth( triangle.v1() );
        nearest = nearest < second ? nearest : second;
        const float third = this->vertexDepth( triangle.v2() );
        nearest = nearest < third ? nearest : third;
        if (depth_ == -1.f || depth_ > nearest)
            depth_ = nearest;
        return false;
    }

private:
    float vertexDepth( const Vector3 & v ) const
    {
        return normal_.x * v.x + normal_.y * v.y + normal_.z * v.z - offset_;
    }

    float depth_;
    Vector3 normal_;
    float offset_;
};
}


bool setStockCursorCameraModels( PyObject * models,
    BW::vector< PyObjectPtr > & owners, BW::vector< const BSPTree * > & trees )
{
    BW_GUARD;
    if (!models || !PyList_Check( models ))
    {
        PyErr_SetString( PyExc_RuntimeError, WRONG_MODELS );
        return false;
    }

    // Release the replaced models only after the new list is admitted.
    BW::vector< PyObjectPtr > previous;
    previous.swap( owners );
    trees.clear();
    for (Py_ssize_t i = 0; i < PyList_Size( models ); ++i)
    {
        PyObject * pItem = PyList_GetItem( models, i );
        if (!pItem || Py_TYPE( pItem ) != &PyModel::s_type_)
        {
            PyErr_SetString( PyExc_RuntimeError, WRONG_MODEL );
            return false;
        }
        if (std::find_if( owners.begin(), owners.end(),
                [pItem]( const PyObjectPtr & owner )
                { return owner.get() == pItem; } ) != owners.end())
            continue;

        // #1513 reads the super model and its top model without checks.
        PyModel * pModel = static_cast< PyModel * >( pItem );
        SuperModel * pSuperModel = pModel->pSuperModel();
        if (!pSuperModel || pSuperModel->nModels() == 0)
            continue;
        ModelPtr pTop = pSuperModel->topModel( 0 );
        const BSPTree * pTree = pTop ? pTop->decompose() : NULL;
        if (!pTree)
            continue;
        trees.push_back( pTree );
        owners.push_back( PyObjectPtr( pItem ) );
    }
    return true;
}


bool collideStockCursorCameraModels( const BW::vector< PyObjectPtr > & owners,
    const BW::vector< const BSPTree * > & trees, Vector3 & idealPosition,
    const Vector3 & direction, const Vector3 & up )
{
    BW_GUARD;
    if (owners.empty())
        return false;

    bool collided = false;
    const Moo::Camera & camera = Moo::rc().camera();
    const Vector3 nearPlane = camera.nearPlanePoint( 1.f, 1.f );
    const float halfWidth = nearPlane.x * 1.5f;
    const float halfHeight = nearPlane.y * 1.5f;
    for (size_t i = 0; i < owners.size(); ++i)
    {
        PyModel * pModel = static_cast< PyModel * >( owners[i].get() );
        const Matrix & world = pModel->worldTransform();
        if ((world.applyToOrigin() - idealPosition).lengthSquared() > 400.f)
            continue;

        Matrix inverse( world );
        inverse.invert();
        const Vector3 local = inverse.applyPoint( idealPosition );
        BoundingBox bounds;
        pModel->localBoundingBox( bounds, false );
        const Vector3 & low = bounds.minBounds();
        const Vector3 & high = bounds.maxBounds();
        if (!(local.x >= low.x - 0.5f) || !(local.y >= low.y - 0.5f) ||
                !(local.z >= low.z - 0.5f) || !(high.x + 0.5f > local.x) ||
                !(high.y + 0.5f > local.y) || !(high.z + 0.5f > local.z))
            continue;

        // Sweep the enlarged near plane from 20 m behind to 5 m beyond.
        const Vector3 dir = inverse.applyVector( direction );
        const Vector3 upward = inverse.applyVector( up );
        const Vector3 reach = dir * 20.f;
        const Vector3 start = local - reach;
        Vector3 xAxis = upward.crossProduct( dir );
        xAxis.normalise();
        const Vector3 yAxis = dir.crossProduct( xAxis );
        const Vector3 cornerA = start - xAxis * halfWidth + yAxis * halfHeight;
        const Vector3 cornerB = cornerA + xAxis * (halfWidth + halfWidth);
        const Vector3 cornerC = cornerA - yAxis * (halfHeight + halfHeight);
        const Vector3 cornerD = cornerC + xAxis * (halfWidth + halfWidth);
        const Vector3 sweep = reach * 1.25f;

        StockModelHitDepth hit( dir, start.dotProduct( dir ) );
        trees[i]->intersects( WorldTriangle( cornerA, cornerB, cornerD ),
            sweep, &hit );
        trees[i]->intersects( WorldTriangle( cornerA, cornerD, cornerC ),
            sweep, &hit );
        if (hit.depth() == -1.f)
            continue;

        const float advance = hit.depth() - camera.nearPlane() - 0.2f;
        const Vector3 moved = start + dir * advance;
        if (!(dir.dotProduct( moved - local ) >= 0.f))
        {
            idealPosition = world.applyPoint( moved );
            collided = true;
        }
    }
    return collided;
}


void publishStockCursorCameraHiding( MatrixProvider * target, bool enabled )
{
    setStockTreeCameraTarget( enabled ? target : NULL );
}


void initStockCursorCameraHiding()
{
    openStockTreeCameraTarget();
}


void finiStockCursorCameraHiding()
{
    closeStockTreeCameraTarget();
}

BW_END_NAMESPACE
