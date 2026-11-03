#pragma once

#include "cstdmf/bw_namespace.hpp"

BW_BEGIN_NAMESPACE
class Matrix;
class MatrixProvider;
class Vector3;

// #1513 9c5dd0 -> 96ea70 -> 9cd1d0 publishes the camera focus even when
// SpeedTree hiding is disabled. The script lifetime owns this reference.
void setStockDirectionalShadowFocus( MatrixProvider * target );
void openStockDirectionalShadowFocus();
void closeStockDirectionalShadowFocus();

// Retain the provider across its callback and reject a re-entrant replacement.
bool stockDirectionalShadowFocus( Matrix & result );

// #1513 9d9c80. Input is the camera split VP, or the inverse focus-box
// transform. The nearest cascade uses this tight light-space rectangle.
bool stockDirectionalShadowTightMatrices( Matrix & view, Matrix & projection,
    Matrix & texture, Matrix & cullViewProjection,
    const Matrix & splitViewProjection, const Vector3 & lightDirection,
    float lightDistance );
BW_END_NAMESPACE
