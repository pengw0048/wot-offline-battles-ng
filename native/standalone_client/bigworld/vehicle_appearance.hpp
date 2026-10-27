#pragma once

#include "cstdmf/bw_namespace.hpp"
#include "romp/resource_ref.hpp"
#include "romp/py_resource_refs.hpp"
#include "math/boundbox.hpp"
#include "pyscript/script_math.hpp"

BW_BEGIN_NAMESPACE

// Referenced by the standalone bootstrap when linking the stock appearance
// factories into a client that does not otherwise reference their classes.
extern int StockVehicleAppearance_token;

// After resources.xml and Moo are ready; release after drawing has stopped and
// before Moo's effect/texture owners are destroyed. Both calls are idempotent.
bool initialiseStockVehicleAppearanceResources();
void finiStockVehicleAppearanceResources();

// Entity prerequisite orders own this request, not the Entity. Cancellation
// drops its result and makes a late background callback harmless.
bool beginStockVehiclePrerequisites( PyObject * resources, PyObjectPtr & request );
bool pollStockVehiclePrerequisites( PyObject * request, ResourceRefs & resources );
void cancelStockVehiclePrerequisites( PyObject * request );
bool stockVehicleModelBounds( PyObject * model, BoundingBox & bounds );
bool stockVehicleCompoundContext( PyObject * compound, uint32 & spaceID,
    BoundingBox & worldBounds );
bool stockVehicleCompoundNode( PyObject * compound, const BW::string & name,
    Matrix & authoredLocal, Matrix & parentWorld, Matrix & world );
bool stockVehicleCompoundNodeLocal( PyObject * compound, const BW::string & name, Matrix & local );
// Node index and parent index (-1 for the compound root); aliases share an index.
bool stockVehicleCompoundNodeIdentity( PyObject * compound, const BW::string & name,
    int & index, int & parent );
bool writeStockVehicleCompoundNodeLocal( PyObject * compound,
    const BW::string & name, const Matrix & local );

BW_END_NAMESPACE
