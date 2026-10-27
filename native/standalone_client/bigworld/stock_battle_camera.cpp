#include "pch.hpp"
#include "stock_camera_collider.hpp"
#include "stock_tree_scene.hpp"

#include "camera/base_camera.hpp"
#include "moo/render_context.hpp"
#include "pyscript/pyobject_plus.hpp"
#include "pyscript/script.hpp"
#include "pyscript/script_math.hpp"
#include "space/client_space.hpp"

BW_BEGIN_NAMESPACE

namespace
{
PyObject * setDynamicCollidersFrom( StockCameraCollider & collider, PyObject * args )
{
    if (PyTuple_Size( args ) != 1)
        return Script::argCountError( "setDynamicColliders", 1, 1,
            "CameraCollider::DynamicColliders" );
    BW::vector< StockCameraDynamicCollider > colliders;
    if (setStockCameraDynamicColliders( PyTuple_GET_ITEM( args, 0 ), colliders,
            "() argument 1" ) != 0)
        return NULL;
    collider.dynamicColliders( colliders );
    Py_RETURN_NONE;
}

PyObject * addVolumeGroupFrom( StockCameraCollider & collider, PyObject * args )
{
    if (PyTuple_Size( args ) != 1)
        return Script::argCountError( "addVolumeGroup", 1, 1, "ColliderVolumeGroup" );
    StockColliderVolumeGroup group;
    if (setStockColliderVolumeGroup( PyTuple_GET_ITEM( args, 0 ), group,
            "() argument 1" ) != 0)
        return NULL;
    collider.advanced().addVolumeGroup( group );
    Py_RETURN_NONE;
}
}


/**
 *	#1513 BigWorld.ThirdPersonProvider (type 01a137b0, factory 0079fd10): the
 *	arcade aiming cursor. It orbits a focus point beside the base provider and
 *	keeps the camera collider used by ArcadeAimingSystem.
 */
class ThirdPersonProvider : public PyObjectPlus
{
    Py_Header( ThirdPersonProvider, PyObjectPlus )

public:
    ThirdPersonProvider() : PyObjectPlus( &s_type_ ),
        heightAboveBase_( 0.f ), focusRadius_( 0.f ), up_( 0.f, 1.f, 0.f ),
        distanceFromFocus_( 0.f ), yaw_( 0.f ), pitch_( 0.f ),
        local_( Matrix::identity ), matrix_( Matrix::identity ),
        idealMatrix_( Matrix::identity ), dirty_( true ),
        positionAboveBase_( new Vector4Basic( Vector4( 0.f, 0.f, 0.f, 0.f ) ),
            SmartPointer< Vector4Basic >::STEAL_REFERENCE ),
        collider_( true ) {}

    PY_FACTORY_DECLARE()

    void update( bool collide );
    PY_AUTO_METHOD_DECLARE( RETVOID, update, OPTARG( bool, true, END ) )

    PY_METHOD_DECLARE( py_setDynamicColliders )
    PY_METHOD_DECLARE( py_addVolumeGroup )
    void initAdvancedCollider( float fovRatio, float rollbackSpeed,
        float minimalCameraDistance, float speedThreshold, float minVolume )
    {
        collider_.advanced().init( fovRatio, rollbackSpeed, minimalCameraDistance,
            speedThreshold, minVolume );
    }
    PY_AUTO_METHOD_DECLARE( RETVOID, initAdvancedCollider, ARG( float,
        ARG( float, ARG( float, ARG( float, ARG( float, END ) ) ) ) ) )

    PY_RW_ATTRIBUTE_DECLARE( base_, base )
#define STOCK_TPP_DIRTY( TYPE, NAME, MEMBER ) \
    TYPE NAME() const { return MEMBER; } \
    void NAME( TYPE value ) { MEMBER = value; dirty_ = true; } \
    PY_RW_ACCESSOR_ATTRIBUTE_DECLARE( TYPE, NAME, NAME )
    STOCK_TPP_DIRTY( float, heightAboveBase, heightAboveBase_ )
    STOCK_TPP_DIRTY( float, focusRadius, focusRadius_ )
    STOCK_TPP_DIRTY( Vector3, up, up_ )
    STOCK_TPP_DIRTY( float, distanceFromFocus, distanceFromFocus_ )
    STOCK_TPP_DIRTY( float, yaw, yaw_ )
    STOCK_TPP_DIRTY( float, pitch, pitch_ )
#undef STOCK_TPP_DIRTY
    PY_RO_ATTRIBUTE_DECLARE( matrix_, matrix )
    PY_RO_ATTRIBUTE_DECLARE( idealMatrix_, idealMatrix )
    PY_RO_ATTRIBUTE_DECLARE( positionAboveBase_, positionAboveBaseProvider )

private:
    MatrixProviderPtr base_;
    float heightAboveBase_;
    float focusRadius_;
    Vector3 up_;
    float distanceFromFocus_;
    float yaw_;
    float pitch_;
    Matrix local_;
    Matrix matrix_;
    Matrix idealMatrix_;
    bool dirty_;
    SmartPointer< Vector4Basic > positionAboveBase_;
    // #1513 constructs this collider with the advanced flag set.
    StockCameraCollider collider_;
};


PyObject * ThirdPersonProvider::pyNew( PyObject * )
{
    // #1513 0079fd10 ignores its arguments.
    return new ThirdPersonProvider();
}


void ThirdPersonProvider::update( bool collide )
{
    // #1513 007a0960.
    if (dirty_)
    {
        D3DXMatrixTranslation( &local_, 0.f, 0.f, -distanceFromFocus_ );
        Matrix rotation;
        D3DXMatrixRotationX( &rotation, -pitch_ );
        D3DXMatrixMultiply( &local_, &local_, &rotation );
        D3DXMatrixRotationY( &rotation, yaw_ );
        D3DXMatrixMultiply( &local_, &local_, &rotation );
        const float cosine = cosf( yaw_ );
        const float sine = sinf( yaw_ );
        local_.m[3][0] = local_.m[3][0] + focusRadius_ * sine;
        local_.m[3][2] = local_.m[3][2] + focusRadius_ * cosine;
        dirty_ = false;
    }

    Matrix base = Matrix::identity;
    if (base_)
        base_->matrix( base );
    idealMatrix_ = local_;
    matrix_ = local_;
    const Vector3 height( 0.f, heightAboveBase_, 0.f );
    D3DXVECTOR4 transformed;
    D3DXVec3Transform( &transformed, &height, &base );
    Vector3 position( transformed.x, transformed.y, transformed.z );
    idealMatrix_.m[3][0] = idealMatrix_.m[3][0] + position.x;
    idealMatrix_.m[3][1] = position.y + idealMatrix_.m[3][1];
    idealMatrix_.m[3][2] = position.z + idealMatrix_.m[3][2];

    if (collide)
    {
        // Keep the pivot on this side of any static obstacle above the base.
        Vector3 direction( position.x - base.m[3][0], position.y - base.m[3][1],
            position.z - base.m[3][2] );
        D3DXVec3Normalize( &direction, &direction );
        const Vector3 start( base.m[3][0] + direction.x * 2.f,
            base.m[3][1] + direction.y * 2.f, base.m[3][2] + direction.z * 2.f );
        const float distance = collideStockCameraSegment( start, position );
        if (distance >= 0.f)
        {
            const float advance = distance - 0.35f;
            position = 0.f >= advance ? start :
                Vector3( start.x + direction.x * advance,
                    direction.y * advance + start.y, start.z + direction.z * advance );
        }
    }

    positionAboveBase_->set( Vector4( position.x, position.y, position.z, 1.f ) );
    matrix_.m[3][0] = matrix_.m[3][0] + position.x;
    matrix_.m[3][1] = position.y + matrix_.m[3][1];
    matrix_.m[3][2] = position.z + matrix_.m[3][2];
    if (!collide)
        return;

    const Vector3 original( matrix_.m[3][0], matrix_.m[3][1], matrix_.m[3][2] );
    Vector3 moved = original;
    const Vector3 direction( matrix_.m[2][0], matrix_.m[2][1], matrix_.m[2][2] );
    if (collider_.collide( moved, position, direction, up_, true ))
    {
        matrix_.m[3][0] = (moved.x - original.x) + matrix_.m[3][0];
        matrix_.m[3][1] = matrix_.m[3][1] + (moved.y - original.y);
        matrix_.m[3][2] = matrix_.m[3][2] + (moved.z - original.z);
    }
}


PyObject * ThirdPersonProvider::py_setDynamicColliders( PyObject * args )
{
    return setDynamicCollidersFrom( collider_, args );
}


PyObject * ThirdPersonProvider::py_addVolumeGroup( PyObject * args )
{
    return addVolumeGroupFrom( collider_, args );
}


PY_TYPEOBJECT( ThirdPersonProvider )

PY_BEGIN_METHODS( ThirdPersonProvider )
    PY_METHOD( update )
    PY_METHOD( setDynamicColliders )
    PY_METHOD( initAdvancedCollider )
    PY_METHOD( addVolumeGroup )
PY_END_METHODS()

PY_BEGIN_ATTRIBUTES( ThirdPersonProvider )
    PY_ATTRIBUTE( base )
    PY_ATTRIBUTE( heightAboveBase )
    PY_ATTRIBUTE( focusRadius )
    PY_ATTRIBUTE( up )
    PY_ATTRIBUTE( distanceFromFocus )
    PY_ATTRIBUTE( yaw )
    PY_ATTRIBUTE( pitch )
    PY_ATTRIBUTE( matrix )
    PY_ATTRIBUTE( idealMatrix )
    PY_ATTRIBUTE( positionAboveBaseProvider )
PY_END_ATTRIBUTES()

PY_FACTORY( ThirdPersonProvider, BigWorld )


/**
 *	#1513 SpeedTreeTargetedCamera (vtable 014d91cc): each update hands its
 *	target to the tree renderer slot that CursorCamera's sptHiding also uses.
 */
class SpeedTreeTargetedCamera : public BaseCamera
{
    Py_Header( SpeedTreeTargetedCamera, BaseCamera )

public:
    explicit SpeedTreeTargetedCamera( PyTypeObject * pType ) : BaseCamera( pType ) {}

    // #1513 007978e0.
    void update( float ) override { setStockTreeCameraTarget( speedTreeTarget_.get() ); }

    PY_RW_ATTRIBUTE_DECLARE( speedTreeTarget_, speedTreeTarget )

private:
    MatrixProviderPtr speedTreeTarget_;
};

PY_TYPEOBJECT( SpeedTreeTargetedCamera )

PY_BEGIN_METHODS( SpeedTreeTargetedCamera )
PY_END_METHODS()

PY_BEGIN_ATTRIBUTES( SpeedTreeTargetedCamera )
    PY_ATTRIBUTE( speedTreeTarget )
PY_END_ATTRIBUTES()


/**
 *	#1513 BigWorld.HomingCamera (type 01a138d8, factory 007a15b0): looks from
 *	the camera provider at the aim provider, offsets the aim to its clip
 *	coordinates and keeps the camera out of hulls, obstacles and water.
 */
class HomingCamera : public SpeedTreeTargetedCamera
{
    Py_Header( HomingCamera, SpeedTreeTargetedCamera )

public:
    explicit HomingCamera( bool advanced ) : SpeedTreeTargetedCamera( &s_type_ ),
        up_( 0.f, 1.f, 0.f ), aimPointClipCoords_( 0.f, 0.f ),
        collider_( advanced ) {}

    ~HomingCamera()
    {
        // #1513 007a0fa0 clears the tree renderer target unconditionally.
        setStockTreeCameraTarget( NULL );
    }

    PY_FACTORY_DECLARE()

    void set( const Matrix & ) override {}
    void update( float dTime ) override;

    PY_METHOD_DECLARE( py_setDynamicColliders )
    PY_METHOD_DECLARE( py_addVolumeGroup )
    void initAdvancedCollider( float fovRatio, float rollbackSpeed,
        float minimalCameraDistance, float speedThreshold, float minVolume )
    {
        collider_.advanced().init( fovRatio, rollbackSpeed, minimalCameraDistance,
            speedThreshold, minVolume );
    }
    PY_AUTO_METHOD_DECLARE( RETVOID, initAdvancedCollider, ARG( float,
        ARG( float, ARG( float, ARG( float, ARG( float, END ) ) ) ) ) )
    void setScrollDelta( float delta ) { collider_.advanced().scrollDelta( delta ); }
    PY_AUTO_METHOD_DECLARE( RETVOID, setScrollDelta, ARG( float, END ) )

    PY_RW_ATTRIBUTE_DECLARE( pivotPositionProvider_, pivotPositionProvider )
    PY_RW_ATTRIBUTE_DECLARE( cameraPositionProvider_, cameraPositionProvider )
    PY_RW_ATTRIBUTE_DECLARE( aimPointProvider_, aimPointProvider )
    PY_RW_ATTRIBUTE_DECLARE( up_, up )
    PY_RW_ATTRIBUTE_DECLARE( aimPointClipCoords_, aimPointClipCoords )

private:
    Vector4ProviderPtr pivotPositionProvider_;
    Vector4ProviderPtr cameraPositionProvider_;
    Vector4ProviderPtr aimPointProvider_;
    Vector3 up_;
    Vector2 aimPointClipCoords_;
    StockCameraCollider collider_;
};


PyObject * HomingCamera::pyNew( PyObject * args )
{
    // #1513 007a15b0 reads one optional bool; it ignores other arities and
    // returns a camera with the conversion error pending. Raise instead.
    bool advanced = false;
    const Py_ssize_t count = PyTuple_Size( args );
    if (count > 1)
        return Script::argCountError( "HomingCamera", 0, 1, "bool" );
    if (count == 1 && Script::setData( PyTuple_GET_ITEM( args, 0 ), advanced,
            "() argument 1" ) != 0)
        return NULL;
    return new HomingCamera( advanced );
}


void HomingCamera::update( float dTime )
{
    // #1513 007a19e0. The WGReplayCameraController override (01d2a904) is not
    // restored, as for CursorCamera.
    SpeedTreeTargetedCamera::update( dTime );

    Vector4 value;
    Vector3 position( 0.f, 0.f, 0.f );
    if (cameraPositionProvider_)
    {
        cameraPositionProvider_->output( value );
        position = Vector3( value.x, value.y, value.z );
    }
    // #1513 reads an uninitialised aim without a provider; aim at the camera.
    Vector3 aim = position;
    if (aimPointProvider_)
    {
        aimPointProvider_->output( value );
        aim = Vector3( value.x, value.y, value.z );
    }
    const float dx = aim.x - position.x;
    const float dy = aim.y - position.y;
    const float dz = aim.z - position.z;
    const float length = sqrtf( (dy * dy + dx * dx) + dz * dz );
    Vector3 direction( 0.f, 0.f, 1.f );
    if (!(length > -1e-8f && 1e-8f > length))
    {
        const float inverse = 1.f / length;
        direction = Vector3( dx * inverse, dy * inverse, dz * inverse );
    }
    const Vector3 target( direction.x + position.x, direction.y + position.y,
        direction.z + position.z );
    D3DXMatrixLookAtLH( &view_, &position, &target, &up_ );

    const Moo::Camera & camera = Moo::rc().camera();
    const float clipX = -1.f > aimPointClipCoords_.x ? -1.f :
        (aimPointClipCoords_.x < 1.f ? aimPointClipCoords_.x : 1.f);
    const float clipY = -1.f > aimPointClipCoords_.y ? -1.f :
        (aimPointClipCoords_.y < 1.f ? aimPointClipCoords_.y : 1.f);
    const float halfTan = tanf( camera.fov() * 0.5f );
    Matrix rotation;
    D3DXMatrixRotationY( &rotation, atanf( (halfTan * clipX) * camera.aspectRatio() ) );
    D3DXMatrixMultiply( &view_, &view_, &rotation );
    D3DXMatrixRotationX( &rotation, -atanf( halfTan * clipY ) );
    D3DXMatrixMultiply( &view_, &view_, &rotation );
    D3DXMatrixInverse( &invView_, NULL, &view_ );

    if (!pivotPositionProvider_)
        return;
    pivotPositionProvider_->output( value );
    const Vector3 pivot( value.x, value.y, value.z );
    const Vector3 forward( invView_.m[2][0], invView_.m[2][1], invView_.m[2][2] );
    Vector3 moved = position;
    if (!collider_.collide( moved, pivot, forward, up_, true ))
        return;
    const Vector3 delta( moved.x - position.x, moved.y - position.y,
        moved.z - position.z );
    const Vector3 back( -delta.x, -delta.y, -delta.z );
    Vector3 shift;
    D3DXVec3TransformNormal( &shift, &back, &view_ );
    view_.m[3][0] = view_.m[3][0] + shift.x;
    view_.m[3][1] = shift.y + view_.m[3][1];
    view_.m[3][2] = shift.z + view_.m[3][2];
    invView_.m[3][0] = delta.x + invView_.m[3][0];
    invView_.m[3][1] = delta.y + invView_.m[3][1];
    invView_.m[3][2] = delta.z + invView_.m[3][2];
}


PyObject * HomingCamera::py_setDynamicColliders( PyObject * args )
{
    return setDynamicCollidersFrom( collider_, args );
}


PyObject * HomingCamera::py_addVolumeGroup( PyObject * args )
{
    return addVolumeGroupFrom( collider_, args );
}


PY_TYPEOBJECT( HomingCamera )

PY_BEGIN_METHODS( HomingCamera )
    PY_METHOD( setDynamicColliders )
    PY_METHOD( initAdvancedCollider )
    PY_METHOD( addVolumeGroup )
    PY_METHOD( setScrollDelta )
PY_END_METHODS()

PY_BEGIN_ATTRIBUTES( HomingCamera )
    PY_ATTRIBUTE( pivotPositionProvider )
    PY_ATTRIBUTE( cameraPositionProvider )
    PY_ATTRIBUTE( aimPointProvider )
    PY_ATTRIBUTE( up )
    PY_ATTRIBUTE( aimPointClipCoords )
PY_END_ATTRIBUTES()

PY_FACTORY( HomingCamera, BigWorld )

BW_END_NAMESPACE
