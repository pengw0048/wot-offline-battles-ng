#pragma once

#include "camera/base_camera.hpp"

BW_BEGIN_NAMESPACE

void setStockTreeCameraTarget( MatrixProvider * target );

/**
 * #1513 SpeedTreeTargetedCamera (vtable 014d91cc) is the shared base of
 * FreeCamera and HomingCamera. The provider is retained by the camera;
 * update publishes it to the tree renderer, including a NULL provider.
 */
class SpeedTreeTargetedCamera : public BaseCamera
{
    Py_Header( SpeedTreeTargetedCamera, BaseCamera )

public:
    explicit SpeedTreeTargetedCamera( PyTypeObject * pType ) : BaseCamera( pType ) {}

    // #1513 007969b0 clears the renderer before releasing speedTreeTarget_.
    ~SpeedTreeTargetedCamera() override { setStockTreeCameraTarget( NULL ); }

    // #1513 007978e0 is called before the derived camera updates its view.
    void update( float ) override { setStockTreeCameraTarget( speedTreeTarget_.get() ); }

    PY_RW_ATTRIBUTE_DECLARE( speedTreeTarget_, speedTreeTarget )

private:
    MatrixProviderPtr speedTreeTarget_;
};

BW_END_NAMESPACE
