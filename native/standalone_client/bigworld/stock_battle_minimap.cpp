#include "pch.hpp"
#include "stock_battle_flash.hpp"
#include "pyscript/script.hpp"
#include "pyscript/script_math.hpp"
#include "romp/py_texture_provider.hpp"
#include "wot/stock_battle_minimap.hpp"
#include <cstring>

BW_BEGIN_NAMESPACE
namespace ScaleformBW
{
namespace
{
namespace flash = wot::stock_battle_flash;
namespace presentation = wot::stock_battle_presentation;
namespace minimap = wot::stock_battle_minimap;

struct MinimapBackend
{
    typedef Scaleform::GFx::Value Clip;
    typedef MatrixProviderPtr Provider;

    bool isObject( const Clip & clip ) const { return clip.IsObject(); }
    bool getMember( const Clip & clip, const char * name, Clip & out )
    {
        return flash::getMember( clip, name, out );
    }
    bool attachMovie( Clip & container, const char * symbol, const char * name, int depth,
        Clip & out )
    {
        return flash::attachMovie( container, symbol, name, depth, out );
    }
    void removeFromParent( Clip & clip ) { flash::removeFromParent( clip ); }
    void addChild( Clip & container, Clip & clip ) { flash::addChild( container, clip ); }
    bool matrix( const Provider & provider, presentation::Matrix44 & out )
    {
        Matrix world;
        provider->matrix( world );
        std::memcpy( out.m, &world, sizeof( out.m ) );
        return true;
    }
    void setDisplay( Clip & clip, const minimap::EntryDisplay & display )
    {
        if (!clip.IsDisplayObject())
            return;
        Scaleform::GFx::Value::DisplayInfo info;
        if (display.hasPosition)
        {
            info.SetX( display.x );
            info.SetY( display.y );
        }
        if (display.hasRotation)
            info.SetRotation( display.rotation );
        if (display.hasScale)
        {
            info.SetXScale( display.xScale );
            info.SetYScale( display.yScale );
        }
        info.SetVisible( display.visible );
        clip.SetDisplayInfo( info );
    }
};

void reportMissingEntry( const char * method, uint id )
{
    PyErr_Format( PyExc_KeyError, "GUI.WGMinimapFlash.%s - invalid entry id %i", method, int( id ) );
    PyErr_PrintEx( 0 );
}

// 013e9794/013e9f6d leave this TypeError pending behind a result; it is
// reported and cleared here instead.
void reportMissingContainer( const BW::string & container )
{
    PyErr_Format( PyExc_TypeError,
        "GUI.WGMinimapFlash: Runtime error: Container '%s' is not found into the movie.",
        container.c_str() );
    PyErr_PrintEx( 0 );
}
}

/**
 *	#1513 GUI.WGMinimapFlashAS3 (factory 013ea100, basic size 0x1f8): minimap
 *	entries attached into named containers below componentPath and placed from
 *	their matrices every frame. The DeviceCallback/TextureStatusHandler bases
 *	serve only the background texture, which setBackground never sets.
 */
class WGMinimapGUIComponentAS3 : public StockSharedMovieComponent
{
    Py_Header( WGMinimapGUIComponentAS3, FlashGUIComponent )

public:
    explicit WGMinimapGUIComponentAS3( PyMovieView * pMovieView ) :
        StockSharedMovieComponent( pMovieView, &s_type_ ),
        minimap_( backend_ )
    {
    }

    ~WGMinimapGUIComponentAS3()
    {
        minimap_.clear();
        flash::release( root_ );
    }

    // 013ea348: only the lookup result is checked; a non-object root is
    // contained by addEntry/moveEntry.
    bool bindRoot( const char * path )
    {
        return flash::getVariable( *this->movie(), path, root_ );
    }

    // 013e8cb0 / 013e96e0: 0 when the container or symbol cannot be used.
    uint addEntry( const BW::string & symbol, const BW::string & container,
        MatrixProviderPtr matrix, bool active, uint transformProps )
    {
        unsigned id = 0;
        switch (minimap_.addEntry( root_, symbol.c_str(), container.c_str(), matrix, active,
            transformProps, id ))
        {
        case minimap::Minimap<MinimapBackend>::ADDED:
            return id;
        case minimap::Minimap<MinimapBackend>::NO_CONTAINER:
            reportMissingContainer( container );
            return 0;
        default:
            // The original keeps an entry without a clip and dereferences it later.
            PyErr_Format( PyExc_RuntimeError,
                "GUI.WGMinimapFlash.addEntry - cannot attach symbol '%s'", symbol.c_str() );
            PyErr_PrintEx( 0 );
            return 0;
        }
    }
    PY_AUTO_METHOD_DECLARE( RETDATA, addEntry, ARG( BW::string, ARG( BW::string,
        OPTARG( MatrixProviderPtr, NULL, OPTARG( bool, false, OPTARG( uint, 0xffffffff,
        END ) ) ) ) ) )

    // 013e9a40.
    void delEntry( uint id )
    {
        if (!minimap_.delEntry( id ))
            reportMissingEntry( "delEntry", id );
    }
    PY_AUTO_METHOD_DECLARE( RETVOID, delEntry, ARG( uint, END ) )

    // 013e9ea0.
    void moveEntry( uint id, const BW::string & container )
    {
        switch (minimap_.moveEntry( root_, id, container.c_str() ))
        {
        case minimap::Minimap<MinimapBackend>::NO_ENTRY:
            reportMissingEntry( "moveEntry", id );
            break;
        case minimap::Minimap<MinimapBackend>::MISSING_CONTAINER:
            reportMissingContainer( container );
            break;
        default:
            break;
        }
    }
    PY_AUTO_METHOD_DECLARE( RETVOID, moveEntry, ARG( uint, ARG( BW::string, END ) ) )

    // 013e8f30.
    PyObject * entryInvoke( uint id, PyObjectPtr signature )
    {
        minimap::Minimap<MinimapBackend>::Entry * entry = minimap_.find( id );
        if (entry == NULL)
        {
            reportMissingEntry( "entryInvoke", id );
            Py_RETURN_NONE;
        }
        return flash::invokePy2As( entry->clip, signature.get() );
    }
    PY_AUTO_METHOD_DECLARE( RETOWN, entryInvoke, ARG( uint, ARG( PyObjectPtr, END ) ) )

    // 013e9200: the provider is replaced unconditionally.
    void entrySetMatrix( uint id, MatrixProviderPtr matrix )
    {
        minimap::Minimap<MinimapBackend>::Entry * entry = minimap_.find( id );
        if (entry == NULL)
            reportMissingEntry( "entrySetMatrix", id );
        else
            entry->provider = matrix;
    }
    PY_AUTO_METHOD_DECLARE( RETVOID, entrySetMatrix,
        ARG( uint, ARG( MatrixProviderPtr, END ) ) )

    // 013e90d0.
    void entrySetActive( uint id, bool active )
    {
        minimap::Minimap<MinimapBackend>::Entry * entry = minimap_.find( id );
        if (entry == NULL)
            reportMissingEntry( "entrySetActive", id );
        else
            entry->active = active;
    }
    PY_AUTO_METHOD_DECLARE( RETVOID, entrySetActive, ARG( uint, ARG( bool, END ) ) )

    // 013e95e0: reports NotImplementedError and returns False.
    bool setBackground( PyTextureProviderPtr )
    {
        PyErr_Format( PyExc_NotImplementedError,
            "GUI.WGMinimapFlash.setBackground - method not implemented" );
        PyErr_PrintEx( 0 );
        return false;
    }
    PY_AUTO_METHOD_DECLARE( RETDATA, setBackground, ARG( PyTextureProviderPtr, END ) )

    // 013e94c0.
    void setArenaBB( const Vector2 & bottomLeft, const Vector2 & topRight )
    {
        minimap_.setArena( minimap::arenaTransform( bottomLeft.x, bottomLeft.y,
            topRight.x, topRight.y ) );
    }
    PY_AUTO_METHOD_DECLARE( RETVOID, setArenaBB, ARG( Vector2, ARG( Vector2, END ) ) )

    Vector2 mapSize() const { return Vector2( minimap_.mapWidth(), minimap_.mapHeight() ); }
    void mapSize( const Vector2 & size ) { minimap_.mapSize( size.x, size.y ); }
    PY_RW_ACCESSOR_ATTRIBUTE_DECLARE( Vector2, mapSize, mapSize )

    PY_FACTORY_DECLARE()

protected:
    void tick( float )
    {
        minimap_.tick();
    }

private:
    MinimapBackend backend_;
    Scaleform::GFx::Value root_;
    minimap::Minimap<MinimapBackend> minimap_;
};

PY_TYPEOBJECT( WGMinimapGUIComponentAS3 )

PY_BEGIN_METHODS( WGMinimapGUIComponentAS3 )
    PY_METHOD( addEntry )
    PY_METHOD( delEntry )
    PY_METHOD( moveEntry )
    PY_METHOD( entryInvoke )
    PY_METHOD( entrySetMatrix )
    PY_METHOD( entrySetActive )
    PY_METHOD( setBackground )
    PY_METHOD( setArenaBB )
PY_END_METHODS()

PY_BEGIN_ATTRIBUTES( WGMinimapGUIComponentAS3 )
    PY_ATTRIBUTE( mapSize )
PY_END_ATTRIBUTES()

// 013ea100: a PyMovieView and the path of the entries root.
PyObject * WGMinimapGUIComponentAS3::pyNew( PyObject * args )
{
    PyObject * pMovie = NULL;
    const char * path = NULL;
    if (!PyArg_ParseTuple( args, "Os", &pMovie, &path ))
    {
        PyErr_SetString( PyExc_TypeError, "GUI.WGMinimapFlash: "
            "Argument parsing error: Expected a PyMovieView object and String" );
        return NULL;
    }
    if (!PyMovieView::Check( pMovie ))
    {
        PyErr_SetString( PyExc_TypeError, "GUI.WGMinimapFlash: "
            "Argument parsing error: Expected a PyMovieView object" );
        return NULL;
    }
    WGMinimapGUIComponentAS3 * pMinimap =
        new WGMinimapGUIComponentAS3( static_cast<PyMovieView *>( pMovie ) );
    if (!pMinimap->bindRoot( path ))
    {
        Py_DECREF( pMinimap );
        PyErr_Format( PyExc_TypeError, "GUI.WGMinimapFlash: "
            "Argument parsing error: PyMovieView object hasn't '%s' movie clip.", path );
        return NULL;
    }
    return pMinimap;
}

PY_FACTORY_NAMED( WGMinimapGUIComponentAS3, "WGMinimapFlashAS3", GUI )
} // namespace ScaleformBW
BW_END_NAMESPACE
