#include "pch.hpp"
#include "pyscript/script.hpp"
#include "math/vector4.hpp"

#include <windows.h>

BW_BEGIN_NAMESPACE

namespace
{
// Alienware LightFX 2.0 SDK (LFX2.h): cdecl exports, LFX_SUCCESS is 0.
struct LightFxColor { unsigned char red, green, blue, brightness; };
typedef unsigned int (*LightFxCall)();
typedef unsigned int (*LightFxCount)( unsigned int * );
typedef unsigned int (*LightFxLightCount)( unsigned int, unsigned int * );
typedef unsigned int (*LightFxDescription)( unsigned int, unsigned int, char *, unsigned int );
typedef unsigned int (*LightFxActionColor)( unsigned int, unsigned int, unsigned int,
    const LightFxColor * );
typedef unsigned int (*LightFxColorEx)( unsigned int, unsigned int, unsigned int, unsigned int );

const unsigned int LIGHT_FX_FAILURE = 1;
const unsigned int LIGHT_FX_ACTION_PULSE = 2;
const unsigned int LIGHT_FX_ACTION_COLOR = 3;

// 006fb510: each channel is clamped below at 0 and above at 255, then truncated.
unsigned char lightFxChannel( float value )
{
    const float clamped = 0.f > value ? 0.f : (value < 255.f ? value : 255.f);
    return static_cast<unsigned char>( static_cast<int>( clamped ) );
}

LightFxColor lightFxColor( const Vector4 & value )
{
    LightFxColor color = { lightFxChannel( value.x ), lightFxChannel( value.y ),
        lightFxChannel( value.z ), lightFxChannel( value.w ) };
    return color;
}

// 006fad75: LFX_ActionColorEx takes 0xBBRRGGBB.
unsigned int lightFxPackedColor( const Vector4 & value )
{
    const LightFxColor color = lightFxColor( value );
    return (unsigned int)color.brightness << 24 | (unsigned int)color.red << 16 |
        (unsigned int)color.green << 8 | color.blue;
}
}

/**
 *	#1513 BigWorld.WGLightFx (type 01a0ed90): the LightFX.dll peripheral.
 */
class WGLightFx : public PyObjectPlus
{
    Py_Header( WGLightFx, PyObjectPlus )
public:
    WGLightFx();
    ~WGLightFx();
    PY_METHOD_DECLARE( py_initialize )
    PY_METHOD_DECLARE( py_reset )
    PY_METHOD_DECLARE( py_update )
    PY_METHOD_DECLARE( py_getNumberOfDevices )
    PY_METHOD_DECLARE( py_getNumberOfLights )
    PY_METHOD_DECLARE( py_getLightDescription )
    PY_METHOD_DECLARE( py_setLightColor )
    PY_METHOD_DECLARE( py_setLightPulse )
    PY_METHOD_DECLARE( py_actionColorEx )
    PY_FACTORY_DECLARE()
private:
    PyObject * setLightAction( PyObject * args, const char * name, unsigned int action );

    LightFxCall initialize_;
    LightFxCall release_;
    LightFxCall reset_;
    LightFxCall update_;
    LightFxCount getNumDevices_;
    LightFxLightCount getNumLights_;
    LightFxDescription getLightDescription_;
    LightFxActionColor setLightActionColor_;
    LightFxColorEx actionColorEx_;
    HMODULE module_;
    bool initialized_;
};

// 006fab30: a missing LightFX.dll leaves every export unresolved.
WGLightFx::WGLightFx() : PyObjectPlus( &s_type_ ),
    initialize_( NULL ), release_( NULL ), reset_( NULL ), update_( NULL ),
    getNumDevices_( NULL ), getNumLights_( NULL ), getLightDescription_( NULL ),
    setLightActionColor_( NULL ), actionColorEx_( NULL ),
    module_( LoadLibraryW( L"LightFX.dll" ) ), initialized_( false )
{
    if (!module_)
        return;
    initialize_ = (LightFxCall)GetProcAddress( module_, "LFX_Initialize" );
    release_ = (LightFxCall)GetProcAddress( module_, "LFX_Release" );
    reset_ = (LightFxCall)GetProcAddress( module_, "LFX_Reset" );
    update_ = (LightFxCall)GetProcAddress( module_, "LFX_Update" );
    getNumDevices_ = (LightFxCount)GetProcAddress( module_, "LFX_GetNumDevices" );
    getNumLights_ = (LightFxLightCount)GetProcAddress( module_, "LFX_GetNumLights" );
    getLightDescription_ = (LightFxDescription)GetProcAddress( module_,
        "LFX_GetLightDescription" );
    setLightActionColor_ = (LightFxActionColor)GetProcAddress( module_,
        "LFX_SetLightActionColor" );
    actionColorEx_ = (LightFxColorEx)GetProcAddress( module_, "LFX_ActionColorEx" );
}

// 006fb480: release an initialized session before unloading the library.
WGLightFx::~WGLightFx()
{
    if (initialized_ && release_)
        release_();
    if (module_)
        FreeLibrary( module_ );
}

PyObject * WGLightFx::pyNew( PyObject * )
{
    return new WGLightFx();
}

PyObject * WGLightFx::py_initialize( PyObject * args )
{
    if (PyTuple_Size( args ) != 0)
        return PyErr_Format( PyExc_TypeError, "initialize() takes no arguments." );
    if (!initialize_)
        return Script::getData( false );
    initialized_ = initialize_() == 0;
    return Script::getData( initialized_ );
}

PyObject * WGLightFx::py_reset( PyObject * args )
{
    if (PyTuple_Size( args ) != 0)
        return PyErr_Format( PyExc_TypeError, "reset() takes no arguments." );
    return Script::getData( reset_ && initialized_ ? reset_() : LIGHT_FX_FAILURE );
}

PyObject * WGLightFx::py_update( PyObject * args )
{
    if (PyTuple_Size( args ) != 0)
        return PyErr_Format( PyExc_TypeError, "update() takes no arguments." );
    return Script::getData( update_ && initialized_ ? update_() : LIGHT_FX_FAILURE );
}

PyObject * WGLightFx::py_getNumberOfDevices( PyObject * args )
{
    if (PyTuple_Size( args ) != 0)
        return PyErr_Format( PyExc_TypeError, "getNumberOfDevices() takes no arguments." );
    unsigned int count = 0;
    if (!getNumDevices_ || !initialized_ || getNumDevices_( &count ) != 0)
        count = 0;
    return Script::getData( count );
}

PyObject * WGLightFx::py_getNumberOfLights( PyObject * args )
{
    if (PyTuple_Size( args ) != 1)
        return PyErr_Format( PyExc_TypeError,
            "getNumberOfLights() expects 1 argument of type UINT" );
    UINT device = 0;
    if (Script::setData( PyTuple_GET_ITEM( args, 0 ), device, "() argument 1" ) != 0)
        return NULL;
    unsigned int count = 0;
    if (!getNumLights_ || !initialized_ || getNumLights_( device, &count ) != 0)
        count = 0;
    return Script::getData( count );
}

PyObject * WGLightFx::py_getLightDescription( PyObject * args )
{
    if (PyTuple_Size( args ) != 2)
        return PyErr_Format( PyExc_TypeError,
            "getLightDescription() expects 2 arguments of types UINT, UINT" );
    UINT device = 0, light = 0;
    if (Script::setData( PyTuple_GET_ITEM( args, 0 ), device, "() argument 1" ) != 0 ||
        Script::setData( PyTuple_GET_ITEM( args, 1 ), light, "() argument 2" ) != 0)
        return NULL;
    // The original passes 255 to a 256-byte stack buffer; keep its terminator.
    char description[256] = {};
    BW::string result;
    if (getLightDescription_ && initialized_ &&
        getLightDescription_( device, light, description, 0xff ) == 0)
        result = description;
    return Script::getData( result );
}

PyObject * WGLightFx::setLightAction( PyObject * args, const char * name, unsigned int action )
{
    if (PyTuple_Size( args ) != 3)
        return PyErr_Format( PyExc_TypeError,
            "%s() expects 3 arguments of types UINT, UINT, Vector4", name );
    UINT device = 0, light = 0;
    Vector4 value;
    if (Script::setData( PyTuple_GET_ITEM( args, 0 ), device, "() argument 1" ) != 0 ||
        Script::setData( PyTuple_GET_ITEM( args, 1 ), light, "() argument 2" ) != 0 ||
        Script::setData( PyTuple_GET_ITEM( args, 2 ), value, "() argument 3" ) != 0)
        return NULL;
    if (!setLightActionColor_ || !initialized_)
        return Script::getData( LIGHT_FX_FAILURE );
    const LightFxColor color = lightFxColor( value );
    return Script::getData( setLightActionColor_( device, light, action, &color ) );
}

PyObject * WGLightFx::py_setLightColor( PyObject * args )
{
    return this->setLightAction( args, "setLightColor", LIGHT_FX_ACTION_COLOR );
}

PyObject * WGLightFx::py_setLightPulse( PyObject * args )
{
    return this->setLightAction( args, "setLightPulse", LIGHT_FX_ACTION_PULSE );
}

PyObject * WGLightFx::py_actionColorEx( PyObject * args )
{
    if (PyTuple_Size( args ) != 4)
        return PyErr_Format( PyExc_TypeError,
            "actionColorEx() expects 4 arguments of types UINT, UINT, Vector4, Vector4" );
    UINT mask = 0, action = 0;
    Vector4 primary, secondary;
    if (Script::setData( PyTuple_GET_ITEM( args, 0 ), mask, "() argument 1" ) != 0 ||
        Script::setData( PyTuple_GET_ITEM( args, 1 ), action, "() argument 2" ) != 0 ||
        Script::setData( PyTuple_GET_ITEM( args, 2 ), primary, "() argument 3" ) != 0 ||
        Script::setData( PyTuple_GET_ITEM( args, 3 ), secondary, "() argument 4" ) != 0)
        return NULL;
    if (!actionColorEx_ || !initialized_)
        return Script::getData( LIGHT_FX_FAILURE );
    return Script::getData( actionColorEx_( mask, action, lightFxPackedColor( primary ),
        lightFxPackedColor( secondary ) ) );
}

PY_TYPEOBJECT( WGLightFx )
PY_BEGIN_METHODS( WGLightFx )
    PY_METHOD( initialize )
    PY_METHOD( reset )
    PY_METHOD( update )
    PY_METHOD( getNumberOfDevices )
    PY_METHOD( getNumberOfLights )
    PY_METHOD( getLightDescription )
    PY_METHOD( setLightColor )
    PY_METHOD( setLightPulse )
    PY_METHOD( actionColorEx )
PY_END_METHODS()
PY_BEGIN_ATTRIBUTES( WGLightFx )
PY_END_ATTRIBUTES()
PY_FACTORY_NAMED( WGLightFx, "WGLightFx", BigWorld )

BW_END_NAMESPACE
