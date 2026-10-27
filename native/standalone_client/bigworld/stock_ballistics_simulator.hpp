#pragma once

#include "cstdmf/bw_namespace.hpp"

namespace wot { namespace stock_ballistics { class Scene; } }

BW_BEGIN_NAMESPACE

// Advances every live BigWorld.PyBallisticsSimulator once per world frame.
void tickStockBallistics();

// The scene-query owner binds its f642b0/f645e0 implementation here.
void setStockBallisticsScene( wot::stock_ballistics::Scene * scene );

BW_END_NAMESPACE
