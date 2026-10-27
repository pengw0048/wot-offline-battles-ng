#pragma once

#include "cstdmf/bw_namespace.hpp"

namespace wot { namespace stock_ballistics { class Scene; } }

BW_BEGIN_NAMESPACE

// The battle scene-query owner's native f642b0 and f645e0 (false mode), as
// the scene every PyBallisticsSimulator is constructed with.
wot::stock_ballistics::Scene & stockBallisticsScene();

BW_END_NAMESPACE
