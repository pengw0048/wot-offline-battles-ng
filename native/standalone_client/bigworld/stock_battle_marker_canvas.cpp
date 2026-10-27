#include "pch.hpp"
#include "stock_battle_flash.hpp"
#include "moo/render_context.hpp"
#include "pyscript/script.hpp"
#include "pyscript/script_math.hpp"
#include "wot/stock_battle_marker_canvas.hpp"
#include <cstring>

BW_BEGIN_NAMESPACE
namespace ScaleformBW
{
namespace
{
namespace flash = wot::stock_battle_flash;
namespace presentation = wot::stock_battle_presentation;
namespace markers = wot::stock_battle_marker_canvas;

const char CANVAS_PATH[] = "_root.vehicleMarkersCanvas";

void copyMatrix( const Matrix & source, presentation::Matrix44 & target )
{
    std::memcpy( target.m, &source, sizeof( target.m ) );
}

presentation::Camera currentCamera()
{
    Moo::RenderContext & rc = Moo::rc();
    presentation::Camera camera;
    copyMatrix( rc.view(), camera.view );
    copyMatrix( rc.viewProjection(), camera.viewProjection );
    camera.nearPlane = rc.camera().nearPlane();
    camera.farPlane = rc.camera().farPlane();
    camera.zoomFactor = rc.zoomFactor();
    camera.halfScreenWidth = rc.halfScreenWidth();
    camera.halfScreenHeight = rc.halfScreenHeight();
    return camera;
}

struct CanvasBackend
{
    typedef Scaleform::GFx::Value Clip;
    typedef MatrixProviderPtr Provider;
    typedef Scaleform::GFx::Value::DisplayInfo DisplayInfo;

    bool isObject( const Clip & clip ) const { return clip.IsObject(); }
    bool attachMovie( Clip & root, const char * symbol, const char * name, int depth,
        Clip & out )
    {
        return flash::attachMovie( root, symbol, name, depth, out );
    }
    void getMember( const Clip & clip, const char * name, Clip & out )
    {
        flash::getMember( clip, name, out );
    }
    void removeFromParent( Clip & clip ) { flash::removeFromParent( clip ); }
    void swapChildren( Clip & first, Clip & second ) { flash::swapChildren( first, second ); }
    bool matrix( const Provider & provider, presentation::Matrix44 & out )
    {
        Matrix world;
        provider->matrix( world );
        copyMatrix( world, out );
        return true;
    }
    void setMatrix3D( Clip & clip, const presentation::GfxMatrix3 & matrix )
    {
        if (!clip.IsDisplayObject())
            return;
        Scaleform::Render::Matrix3F value;
        std::memcpy( value.M, matrix.m, sizeof( matrix.m ) );
        clip.SetMatrix3D( value );
    }
    // 013e5694..013e56e6: identity view, marker projection, visible = w > 0.
    void setDisplay3D( Clip & clip, const presentation::GfxMatrix4 & projection, bool visible )
    {
        if (!clip.IsDisplayObject())
            return;
        Scaleform::Render::Matrix3F view;
        view.SetIdentity();
        Scaleform::Render::Matrix4F matrix;
        std::memcpy( matrix.M, projection.m, sizeof( projection.m ) );
        DisplayInfo info;
        info.SetViewMatrix3D( &view );
        info.SetProjectionMatrix3D( &matrix );
        info.SetVisible( visible );
        clip.SetDisplayInfo( info );
    }
    void setVisible( Clip & clip, bool visible )
    {
        if (!clip.IsDisplayObject())
            return;
        DisplayInfo info;
        info.SetVisible( visible );
        clip.SetDisplayInfo( info );
    }
    // 013e4ec0 on the "marker" child.
    void setScale( Clip & clip, double scale )
    {
        if (!clip.IsDisplayObject())
            return;
        DisplayInfo info;
        info.SetXScale( scale );
        info.SetYScale( scale );
        clip.SetDisplayInfo( info );
    }
    // 013e57e2..013e587f on the "bgShadow" child.
    void setAlpha( Clip & clip, double alpha )
    {
        if (!clip.IsDisplayObject())
            return;
        DisplayInfo info;
        info.SetAlpha( alpha );
        clip.SetDisplayInfo( info );
    }
};

Vector4 toVector( const markers::DistanceProperties & properties )
{
    return Vector4( properties.minimum, properties.maximum, properties.initial, properties.speed );
}

void fromVector( const Vector4 & value, markers::DistanceProperties & properties )
{
    properties.minimum = value.x;
    properties.maximum = value.y;
    properties.initial = value.z;
    properties.speed = value.w;
}
}

/**
 *	#1513 GUI.WGVehicleMarkersCanvasFlashAS3 (factory 013e4b20, basic size 0x1e0):
 *	the vehicle marker canvas inside the MarkersManager movie. Each marker is an
 *	attached AS3 symbol projected with Scaleform 3D matrices every frame.
 */
class WGVehicleMarkersCanvasGUIComponentAS3 : public StockSharedMovieComponent
{
    Py_Header( WGVehicleMarkersCanvasGUIComponentAS3, FlashGUIComponent )

public:
    WGVehicleMarkersCanvasGUIComponentAS3( PyMovieView * pMovieView ) :
        StockSharedMovieComponent( pMovieView, &s_type_ ),
        canvas_( backend_ )
    {
        // 013e4cee: the canvas root is optional at construction.
        if (flash::getVariable( *this->movie(), CANVAS_PATH, root_ ) && root_.IsDisplayObject())
        {
            Scaleform::GFx::Value::DisplayInfo origin( 0.0, 0.0 );
            root_.SetDisplayInfo( origin );
        }
    }

    ~WGVehicleMarkersCanvasGUIComponentAS3()
    {
        canvas_.clear();
        flash::release( root_ );
    }

    // 013e3bb0 / 013e41e0.
    PyObject * addMarker( MatrixProviderPtr provider, const BW::string & symbol, bool active )
    {
        unsigned id = 0;
        switch (canvas_.addMarker( root_, provider, symbol.c_str(), active, id ))
        {
        case markers::Canvas<CanvasBackend>::ADDED:
            return Script::getData( id );
        case markers::Canvas<CanvasBackend>::NO_ROOT:
            PyErr_Format( PyExc_RuntimeError, "GUI.WGVehicleMarkersCanvasFlash.addMarker - "
                "'%s' is not found in the movie", CANVAS_PATH );
            return NULL;
        default:
            PyErr_Format( PyExc_RuntimeError, "GUI.WGVehicleMarkersCanvasFlash.addMarker - "
                "cannot attach symbol '%s'", symbol.c_str() );
            return NULL;
        }
    }
    PY_AUTO_METHOD_DECLARE( RETOWN, addMarker,
        ARG( MatrixProviderPtr, ARG( BW::string, OPTARG( bool, false, END ) ) ) )

    // 013e3d30 / 013e43d0. The original dereferences a missing marker.
    bool delMarker( uint id )
    {
        if (canvas_.delMarker( id ))
            return true;
        PyErr_Format( PyExc_KeyError,
            "GUI.WGVehicleMarkersCanvasFlash.delMarker - invalid marker id %i", int( id ) );
        return false;
    }
    PY_AUTO_METHOD_DECLARE( RETOK, delMarker, ARG( uint, END ) )

    // 013e3dc0. The original dereferences a missing marker.
    PyObject * markerInvoke( uint id, PyObjectPtr signature )
    {
        markers::Canvas<CanvasBackend>::Marker * marker = canvas_.find( id );
        if (marker == NULL)
        {
            PyErr_Format( PyExc_KeyError,
                "GUI.WGVehicleMarkersCanvasFlash.markerInvoke - invalid marker id %i", int( id ) );
            return NULL;
        }
        return flash::invokePy2As( marker->clip, signature.get() );
    }
    PY_AUTO_METHOD_DECLARE( RETOWN, markerInvoke, ARG( uint, ARG( PyObjectPtr, END ) ) )

    // 013e4050.
    void markerSetMatrix( uint id, MatrixProviderPtr provider )
    {
        canvas_.setMatrix( id, provider );
    }
    PY_AUTO_METHOD_DECLARE( RETVOID, markerSetMatrix,
        ARG( uint, ARG( MatrixProviderPtr, END ) ) )

    // 013e3f20: a missing marker is reported and cleared.
    void markerSetActive( uint id, bool active )
    {
        if (canvas_.setActive( id, active ))
            return;
        PyErr_Format( PyExc_KeyError,
            "GUI.WGVehicleMarkersCanvasFlash.markerSetActive - invalid marker id %i", int( id ) );
        PyErr_PrintEx( 0 );
    }
    PY_AUTO_METHOD_DECLARE( RETVOID, markerSetActive, ARG( uint, ARG( bool, END ) ) )

    Vector4 scaleProperties() const { return toVector( canvas_.scaleProperties() ); }
    void scaleProperties( const Vector4 & value ) { fromVector( value, canvas_.scaleProperties() ); }
    PY_RW_ACCESSOR_ATTRIBUTE_DECLARE( Vector4, scaleProperties, scaleProperties )

    Vector4 alphaProperties() const { return toVector( canvas_.alphaProperties() ); }
    void alphaProperties( const Vector4 & value ) { fromVector( value, canvas_.alphaProperties() ); }
    PY_RW_ACCESSOR_ATTRIBUTE_DECLARE( Vector4, alphaProperties, alphaProperties )

    PY_FACTORY_DECLARE()

protected:
    void tick( float )
    {
        canvas_.tick( currentCamera() );
    }

private:
    CanvasBackend backend_;
    Scaleform::GFx::Value root_;
    markers::Canvas<CanvasBackend> canvas_;
};

PY_TYPEOBJECT( WGVehicleMarkersCanvasGUIComponentAS3 )

PY_BEGIN_METHODS( WGVehicleMarkersCanvasGUIComponentAS3 )
    PY_METHOD( addMarker )
    PY_METHOD( delMarker )
    PY_METHOD( markerInvoke )
    PY_METHOD( markerSetMatrix )
    PY_METHOD( markerSetActive )
PY_END_METHODS()

PY_BEGIN_ATTRIBUTES( WGVehicleMarkersCanvasGUIComponentAS3 )
    PY_ATTRIBUTE( scaleProperties )
    PY_ATTRIBUTE( alphaProperties )
PY_END_ATTRIBUTES()

// 013e4b20: exactly one PyMovieView argument.
PyObject * WGVehicleMarkersCanvasGUIComponentAS3::pyNew( PyObject * args )
{
    PyObject * pMovie = NULL;
    if (!PyArg_ParseTuple( args, "O", &pMovie ) || !PyMovieView::Check( pMovie ))
    {
        PyErr_SetString( PyExc_TypeError, "GUI.WGVehicleMarkersCanvasFlash: "
            "Argument parsing error: Expected a PyMovieView object" );
        return NULL;
    }
    return new WGVehicleMarkersCanvasGUIComponentAS3( static_cast<PyMovieView *>( pMovie ) );
}

PY_FACTORY_NAMED( WGVehicleMarkersCanvasGUIComponentAS3, "WGVehicleMarkersCanvasFlashAS3", GUI )
} // namespace ScaleformBW
BW_END_NAMESPACE
