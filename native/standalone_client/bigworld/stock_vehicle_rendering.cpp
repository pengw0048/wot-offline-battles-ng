#include "pch.hpp"
#include "pyscript/script.hpp"
#include "pyscript/script_math.hpp"

BW_BEGIN_NAMESPACE
int StockVehicleRendering_token = 1;

// #1513 00658600/640/650/620/630: these are Python ints, not callables.
PY_MODULE_ATTRIBUTE( BigWorld, ColorPassBit, PyInt_FromLong( 1 ) )
PY_MODULE_ATTRIBUTE( BigWorld, ReflectionPassBit, PyInt_FromLong( 2 ) )
PY_MODULE_ATTRIBUTE( BigWorld, ShadowPassBit, PyInt_FromLong( 4 ) )
PY_MODULE_ATTRIBUTE( BigWorld, DepthPassBit, PyInt_FromLong( 8 ) )
PY_MODULE_ATTRIBUTE( BigWorld, DrawAll, PyInt_FromLong( 15 ) )

static PyObject * py_wg_createHKAttachment( PyObject * args )
{
    if (PyTuple_Size( args ) != 2)
        return Script::argCountError( "wg_createHKAttachment", 2, 2,
            "MatrixProviderPtr", "WGBspCollisionModelPtr" );
    MatrixProviderPtr node;
    if (Script::setData( PyTuple_GET_ITEM( args, 0 ), node, "() argument 1" ))
        return NULL;
    // 00653540 validates only the node provider (including None). This build
    // never reads the second object's BSP data and wraps a NULL attachment via
    // 0065d1e0 -> 010368e0, which returns None. Do not fabricate a Havok body.
    Py_RETURN_NONE;
}
PY_MODULE_FUNCTION( wg_createHKAttachment, BigWorld )

static PyObject * py_wg_havokExplosion( PyObject * args )
{
    if (PyTuple_Size( args ) != 3)
        return Script::argCountError( "wg_havokExplosion", 3, 3,
            "Vector3", "float", "float" );
    Vector3 position;
    float strength, radius;
    if (Script::setData( PyTuple_GET_ITEM( args, 0 ), position, "() argument 1" ) ||
        Script::setData( PyTuple_GET_ITEM( args, 1 ), strength, "() argument 2" ) ||
        Script::setData( PyTuple_GET_ITEM( args, 2 ), radius, "() argument 3" )) return NULL;
    // #1513 00653ce0 ends after those conversions. It dispatches no Havok
    // work, even when called by Avatar or delayed AreaDestructibles effects.
    Py_RETURN_NONE;
}
PY_MODULE_FUNCTION( wg_havokExplosion, BigWorld )
BW_END_NAMESPACE
