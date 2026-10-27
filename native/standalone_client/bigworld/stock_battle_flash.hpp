#pragma once

#include "scaleform/flash_gui_component.hpp"
#include "cstdmf/bw_string.hpp"

BW_BEGIN_NAMESPACE
namespace ScaleformBW
{
/**
 *	A battle component presenting through a movie that its owner GUI.Flash
 *	advances and draws (#1513 01347f90 drawSelf, 013e5160/013ea4c0 update).
 *	Derived classes keep Py_Header( Derived, FlashGUIComponent ).
 */
class StockSharedMovieComponent : public FlashGUIComponent
{
public:
    StockSharedMovieComponent( PyMovieView * pMovieView, PyTypePlus * pType );

    // tick( dTime ) only: no Movie::Advance and no SimpleGUIComponent::update.
    void update( float dTime, float relParentWidth, float relParentHeight );
    void drawSelf( bool reallyDraw, bool overlay );

protected:
    virtual void tick( float dTime ) = 0;
    Scaleform::GFx::Movie * movie() const;
};
} // namespace ScaleformBW
BW_END_NAMESPACE

namespace wot
{
namespace stock_battle_flash
{
typedef Scaleform::GFx::Value Value;

// Movie::GetVariable; "_level0." and "_root." prefixes are accepted.
bool getVariable( Scaleform::GFx::Movie & movie, const char * path, Value & out );
// ObjectInterface::GetMember on an object or display object.
bool getMember( const Value & object, const char * name, Value & out );
// #1513 4.6.34 ObjectInterface::GetParent as the AS3 display-object 'parent'.
bool getParent( const Value & displayObject, Value & parent );
// ObjectInterface::AttachMovie( container, out, symbol, instanceName, depth, NULL ).
bool attachMovie( Value & container, const char * symbol, const char * instanceName,
    int depth, Value & out );
// Invoke and discard the result.
bool invoke( Value & object, const char * name, const Value * args, unsigned count );
// #1513 013e4494/013e9af3: parent.removeChild( displayObject ).
bool removeFromParent( Value & displayObject );
// #1513 013e3170: parent( first ).swapChildren( first, second ).
bool swapChildren( Value & first, Value & second );
// #1513 013e9f9e: container.addChild( displayObject ).
bool addChild( Value & container, Value & displayObject );
// Scaleform "{0}{1}": symbol followed by the decimal ID.
BW::string instanceName( const char * symbol, unsigned id );
// #1513 013ed040 with the 00aaef10/00aaf450 converters; a new reference or
// NULL with a Python exception.
PyObject * invokePy2As( Value & target, PyObject * signature );
// Releases a managed AS3 reference.
void release( Value & value );
} // namespace stock_battle_flash
} // namespace wot
