#pragma once

#include "math/vector3.hpp"

BW_BEGIN_NAMESPACE

// Nearest Ccb_(0x80, 0) camera-space hit distance along start->end, negative on
// a miss (#1513 f5d160/f5d1c0). The scene-query owner provides it.
float stockGunMarkerSegmentCollision( const Vector3 & start, const Vector3 & end );

BW_END_NAMESPACE
