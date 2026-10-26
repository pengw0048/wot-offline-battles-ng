#pragma once

#include "cstdmf/bw_namespace.hpp"
#include "cstdmf/bw_vector.hpp"
#include "pyscript/script.hpp"

BW_BEGIN_NAMESPACE
class BSPTree;
class MatrixProvider;
class Vector3;

// #1513 0079eb00: keep distinct exact PyModel instances whose top model has a
// BSP. A bad argument leaves both lists unchanged; a bad item keeps the prefix.
bool setStockCursorCameraModels( PyObject * models,
    BW::vector< PyObjectPtr > & owners, BW::vector< const BSPTree * > & trees );

// #1513 0079e2d0: sweep the near plane through each nearby model hull and move
// the ideal position in front of the first surface it reaches.
bool collideStockCursorCameraModels( const BW::vector< PyObjectPtr > & owners,
    const BW::vector< const BSPTree * > & trees, Vector3 & idealPosition,
    const Vector3 & direction, const Vector3 & up );

// #1513 009c5dd0/009c6a40: the renderer targets every CursorCamera update
// publishes. Tree hiding follows sptHiding; the camera focus target does not.
void publishStockCursorCameraHiding( MatrixProvider * target, bool enabled );
MatrixProvider * stockTreeHidingTarget();
MatrixProvider * stockCameraFocusTarget();
void finiStockCursorCameraHiding();
BW_END_NAMESPACE
