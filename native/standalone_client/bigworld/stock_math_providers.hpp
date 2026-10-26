#pragma once
#include "math/vector3.hpp"

BW_BEGIN_NAMESPACE
// The original providers update once per native MatrixProvider tick. Their
// smoothing coefficient is distance-based and does not consume elapsed time.
Vector3 stockSmoothTranslation( const Vector3 & current, const Vector3 & target );
BW_END_NAMESPACE
