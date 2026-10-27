#include "pch.hpp"
#include "stock_battle_flash.hpp"
#include "pyscript/pyobject_plus.hpp"
#include "wot/stock_battle_flash_invoke.hpp"
#include "wot/stock_battle_presentation.hpp"
#include <cwchar>

BW_BEGIN_NAMESPACE
namespace ScaleformBW
{
StockSharedMovieComponent::StockSharedMovieComponent( PyMovieView * pMovieView,
        PyTypePlus * pType ) :
    FlashGUIComponent( pMovieView, pType )
{
}

void StockSharedMovieComponent::update( float dTime, float, float )
{
    this->tick( dTime );
}

void StockSharedMovieComponent::drawSelf( bool, bool )
{
}

Scaleform::GFx::Movie * StockSharedMovieComponent::movie() const
{
    return pMovieView_->pMovieView();
}
} // namespace ScaleformBW
BW_END_NAMESPACE

namespace wot
{
namespace stock_battle_flash
{
namespace
{
struct GfxAdapter
{
    typedef Scaleform::GFx::Value Value;
    static void setNull( Value & value ) { value.SetNull(); }
    static void setString( Value & value, const char * text ) { value.SetString( text ); }
    static void setNumber( Value & value, double number ) { value.SetNumber( number ); }
    static void setBoolean( Value & value, bool flag ) { value.SetBoolean( flag ); }
    static bool invoke( Value & target, const char * name, const Value * args,
        std::size_t count, Value & result )
    {
        return target.IsObject() && target.Invoke( name, &result, args, count );
    }
    // 00aaf450: Int, UInt and AS3 objects have no conversion.
    static PyObject * toPython( const Value & value )
    {
        switch (value.GetType())
        {
        case Value::VT_Undefined:
        case Value::VT_Null:
            Py_RETURN_NONE;
        case Value::VT_Boolean:
            return PyBool_FromLong( value.GetBool() );
        case Value::VT_Number:
            return PyFloat_FromDouble( value.GetNumber() );
        case Value::VT_String:
            return PyString_FromString( value.GetString() );
        case Value::VT_StringW:
            return PyUnicode_FromWideChar( value.GetStringW(),
                static_cast<Py_ssize_t>( std::wcslen( value.GetStringW() ) ) );
        default:
            return NULL;
        }
    }
};
}

bool getVariable( Scaleform::GFx::Movie & movie, const char * path, Value & out )
{
    return movie.GetVariable( &out, path );
}

bool getMember( const Value & object, const char * name, Value & out )
{
    return object.IsObject() && object.GetMember( name, &out );
}

bool getParent( const Value & displayObject, Value & parent )
{
    Value result;
    if (!displayObject.IsDisplayObject() || !displayObject.GetMember( "parent", &result ) ||
            !result.IsDisplayObject())
        return false;
    parent = result;
    return true;
}

bool attachMovie( Value & container, const char * symbol, const char * instanceName,
    int depth, Value & out )
{
    return container.IsDisplayObject() &&
        container.AttachMovie( &out, symbol, instanceName, depth ) && out.IsDisplayObject();
}

bool invoke( Value & object, const char * name, const Value * args, unsigned count )
{
    return object.IsObject() && object.Invoke( name, NULL, args, count );
}

bool removeFromParent( Value & displayObject )
{
    Value parent;
    return getParent( displayObject, parent ) &&
        invoke( parent, "removeChild", &displayObject, 1 );
}

bool swapChildren( Value & first, Value & second )
{
    Value parent;
    if (!getParent( first, parent ))
        return false;
    Value args[2] = { first, second };
    return invoke( parent, "swapChildren", args, 2 );
}

bool addChild( Value & container, Value & displayObject )
{
    return invoke( container, "addChild", &displayObject, 1 );
}

BW::string instanceName( const char * symbol, unsigned id )
{
    return BW::string( stock_battle_presentation::instanceName( symbol, id ).c_str() );
}

PyObject * invokePy2As( Value & target, PyObject * signature )
{
    return wot::stock_battle_flash::invokePy2As<GfxAdapter>( target, signature );
}

void release( Value & value )
{
    value.SetUndefined();
}
} // namespace stock_battle_flash
} // namespace wot
