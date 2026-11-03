#include "pch.hpp"
#include "stock_customization_helper.hpp"
#include "stock_customization_selection.hpp"
#include "vehicle_appearance.hpp"
#include "ashes/simple_gui.hpp"
#include "ashes/mouse_cursor.hpp"
#include "duplo/pymodel.hpp"
#include "entity.hpp"
#include "space/entity_embodiment.hpp"
#include "py_entity.hpp"
#include "input/input.hpp"
#include "moo/base_texture.hpp"
#include "moo/draw_context.hpp"
#include "moo/effect_material.hpp"
#include "moo/effect_property.hpp"
#include "moo/fullscreen_quad.hpp"
#include "moo/managed_effect.hpp"
#include "moo/moo_utilities.hpp"
#include "moo/render_context.hpp"
#include "moo/render_target.hpp"
#include "resmgr/xml_section.hpp"
#include "pyscript/pyobject_plus.hpp"
#include <algorithm>
#include <cmath>
#include <map>
#include <vector>

BW_BEGIN_NAMESPACE
int StockCustomizationHelper_token = 1;
namespace
{
const char * const selectorEffect = "shaders/edgeDetect/tank_region_selector.fx";
bool maskActive = false;
bool maskFailed = false;
int maskPart = 0, maskMode = 0;
struct MaskKey
{
    const Moo::ComplexEffectMaterial * material;
    bool nodeless, tracks;
    bool operator<( const MaskKey & other ) const
    {
        if (material != other.material) return std::less<const void *>()( material, other.material );
        return nodeless != other.nodeless ? nodeless < other.nodeless : tracks < other.tracks;
    }
};
struct MaskMaterial
{
    Moo::ComplexEffectMaterialPtr original, material;
};
std::map<MaskKey, MaskMaterial> maskMaterials;
ID3DXEffect * binding( const Moo::EffectMaterialPtr & material )
{
    return material && material->pEffect() && material->properties().select( *material->pEffect() ) ?
        material->pEffect()->pEffect() : NULL;
}
void releaseTexture( const Moo::EffectMaterialPtr & material )
{
    if (!material || !material->pEffect()) return;
    ID3DXEffect * effect = material->properties().existingStockBinding();
    if (effect)
    { effect->SetTexture( "g_colorIdMap", NULL ); effect->SetTexture( "g_blurredMask", NULL ); }
}
}

bool stockCustomizationMaskActive() { return maskActive; }
Moo::ComplexEffectMaterialPtr stockCustomizationMaskMaterial(
    const Moo::ComplexEffectMaterialPtr & original, bool nodeless, bool tracks )
{
    const MaskKey key = { original.getObject(), nodeless, tracks };
    MaskMaterial & cached = maskMaterials[key];
    if (!cached.material)
    {
        DataSectionPtr section = new XMLSection( "material" );
        original->baseMaterial()->save( section );
        section->writeString( "fx", selectorEffect );
        cached.original = original;
        cached.material = new Moo::ComplexEffectMaterial;
        if (!cached.material->load( section ))
        {
            maskMaterials.erase( key );
            maskFailed = true;
            return NULL;
        }
        cached.material->baseMaterial()->replaceDefaults();
    }
    Moo::EffectMaterialPtr material = cached.material->baseMaterial();
    material->hTechnique( tracks ? "RENDER_MASK_TRACKS" :
        nodeless ? "RENDER_MASK" : "RENDER_MASK_SKINNED" );
    ID3DXEffect * effect = binding( material );
    if (!effect) { maskFailed = true; return NULL; }
    // a4031a..a403c5: mode 3 merges body regions, but retains gun regions;
    // mode 4 uses camouflage grouping while the turret only writes depth.
    const int mode = wot::customizationMaskMode( maskMode, maskPart );
    effect->SetInt( "g_selectionMode", mode );
    effect->SetInt( "g_partIndex", maskPart );
    effect->SetInt( "g_numParts", 4 );
    effect->SetBool( "g_depthOnly", wot::customizationDepthOnly( maskMode, maskPart, tracks ) );
    // g_debugColorIdMode retains its original unsigned-zero default.
    const char * sourceNames[] = { "colorIdMap", "metallicGlossMap" };
    const char * targetNames[] = { "g_colorIdMap", "g_metallicGlossMap" };
    for (unsigned i = 0; i != 2; ++i)
    {
        Moo::BaseTexturePtr texture;
        const Moo::EffectPropertyPtr property = original->baseMaterial()->getProperty( sourceNames[i] );
        if (property) property->getTexture( texture );
        effect->SetTexture( targetNames[i], texture ? texture->pTexture() : NULL );
    }
    return cached.material;
}

class PyCustomizationHelper : public PyObjectPlus, private Moo::DeviceCallback
{
    Py_Header( PyCustomizationHelper, PyObjectPlus )
public:
    PyCustomizationHelper( const EntityPtr & entity, int mode, PyObject * callback ) :
        PyObjectPlus( &s_type_ ), entity_( entity ), callback_( callback ), mode_( mode )
    { instances().push_back( this ); }
    virtual ~PyCustomizationHelper()
    {
        auto & owners = instances();
        owners.erase( std::remove( owners.begin(), owners.end(), this ), owners.end() );
        close();
        if (owners.empty()) maskMaterials.clear();
    }
    static std::vector<PyCustomizationHelper *> & instances()
    { static std::vector<PyCustomizationHelper *> owners; return owners; }
    void close()
    {
        deleteUnmanagedObjects();
        deleteManagedObjects();
        entity_ = NULL;
        callback_ = NULL;
    }
    virtual void deleteUnmanagedObjects()
    {
        releaseTexture( transfer_ );
        mask_ = NULL;
        readback_ = NULL;
        blurred_ = NULL;
        systemSurface_ = NULL;
    }
    virtual void deleteManagedObjects()
    { releaseTexture( transfer_ ); transfer_ = NULL; maskMaterials.clear(); }
    virtual bool recreateForD3DExDevice() const { return true; }
    bool resources()
    {
        if (!transfer_ && !Moo::createEffect( transfer_, selectorEffect )) return false;
        const auto & desc = Moo::rc().backBufferDesc();
        if (!desc.Width || !desc.Height) return false;
        if (mask_ && (width_ != desc.Width || height_ != desc.Height)) deleteUnmanagedObjects();
        width_ = desc.Width; height_ = desc.Height;
        if (!mask_ && !Moo::createRenderTarget( mask_, width_, height_, D3DFMT_A8R8G8B8,
            "stock/Customization mask", false, false )) return false;
        if (!readback_ && !Moo::createRenderTarget( readback_, 1, 1, D3DFMT_A8R8G8B8,
            "stock/Customization readback", false, false )) return false;
        if (!blurred_ && !Moo::createRenderTarget( blurred_, width_, height_, D3DFMT_A8R8G8B8,
            "stock/Customization selected blur", false, true )) return false;
        if (!systemSurface_ && FAILED( Moo::rc().device()->CreateOffscreenPlainSurface(
            1, 1, D3DFMT_A8R8G8B8, D3DPOOL_SYSTEMMEM, &systemSurface_, NULL ) )) return false;
        return true;
    }
    bool fullScreen( const char * technique, const Vector4 & cursor )
    {
        if (!transfer_->hTechnique( technique )) return false;
        ID3DXEffect * effect = binding( transfer_ );
        const Vector4 selected( selectedRaw_ / 255.f, 0.f, 0.f, 0.f );
        // a3e250..a3e2be: authored selector stripe, blur and edge palette.
        const Vector4 stripe( released_ ? .65f : .86f, released_ ? .45f : .6f,
            released_ ? .23f : .31f, 0.f );
        const Vector4 blur( .86f, .6f, .31f, 0.f );
        const Vector4 edge( .98f, .549f, .274f, 0.f );
        if (!effect || FAILED( effect->SetTexture( "g_colorIdMap", mask_->pTexture() ) ) ||
            FAILED( effect->SetVector( "g_cursorPos", &cursor ) ) ||
            FAILED( effect->SetVector( "g_selectedRegion", &selected ) ) ||
            FAILED( effect->SetVector( "g_stripeColor", &stripe ) ) ||
            FAILED( effect->SetVector( "g_blurColor", &blur ) ) ||
            FAILED( effect->SetVector( "g_selectedEdgeColor", &edge ) ) ||
            FAILED( effect->SetTexture( "g_blurredMask",
                technique == BW::string( "SELECTED_REGION" ) ? blurred_->pTexture() : NULL ) )) return false;
        return Moo::rc().fsQuad().drawChecked( *transfer_,
            technique == BW::string( "READBACK_SELECTION" ) ? 1 : width_,
            technique == BW::string( "READBACK_SELECTION" ) ? 1 : height_ );
    }
    bool draw()
    {
        if (!entity_ || entity_->isDestroyed() || !entity_->isInWorld()) return true;
        IEntityEmbodiment * embodiment = entity_->pPrimaryEmbodiment();
        if (!embodiment) return true;
        if (!resources()) return false;
        // The mask needs its own depth, not stale main-scene depth. All four
        // parts participate, including depth-only tracks and skipped regions.
        bool success = mask_->push();
        if (!success) return false;
        success = SUCCEEDED( Moo::rc().device()->Clear( 0, NULL,
            D3DCLEAR_TARGET | D3DCLEAR_ZBUFFER, 0, 1.f, 0 ) );
        if (success)
        {
            Moo::DrawContext context( Moo::RENDERING_PASS_COLOR );
            context.begin( Moo::DrawContext::ALL_CHANNELS_MASK );
            context.pushImmediateMode();
            maskMode = mode_;
            maskFailed = false;
            maskActive = true;
            success = stockVisitVehicleCompoundParts( embodiment->scriptObject().get(),
                [&context]( PyModel * model, const Matrix & world, int part )
                { maskPart = part; model->draw( context, world ); } );
            maskActive = false;
            success = success && !maskFailed;
            context.popImmediateMode();
            context.end( Moo::DrawContext::ALL_CHANNELS_MASK );
            context.flush( Moo::DrawContext::ALL_CHANNELS_MASK );
        }
        mask_->pop();
        if (!success) return false;
        const Vector2 position = SimpleGUI::instance().mouseCursor().position();
        const Vector4 cursor( (position.x + 1.f) * 0.5f, 1.f - (position.y + 1.f) * 0.5f, 0.f, 0.f );
        unsigned raw = 0;
        if (selecting_)
        {
            if (!readback_->push()) return false;
            success = fullScreen( "READBACK_SELECTION", cursor );
            readback_->pop();
            ComObjectWrap<DX::Surface> surface;
            if (success) success = SUCCEEDED( readback_->pSurface( surface ) ) &&
                SUCCEEDED( Moo::rc().device()->GetRenderTargetData( surface.pComObject(), systemSurface_.pComObject() ) );
            D3DLOCKED_RECT locked;
            if (success) success = SUCCEEDED( systemSurface_->LockRect( &locked, NULL, D3DLOCK_READONLY ) );
            if (success)
            { raw = static_cast<const unsigned char *>( locked.pBits )[3]; systemSurface_->UnlockRect(); }
            if (!success) return false;
        }
        const bool released = !InputDevices::isKeyDown( KeyCode::KEY_LEFTMOUSE );
        // 8a8b61..8a8c24: select on the press edge, suppress camera drags,
        // notify hover changes separately, and never report a guessed region.
        PyObjectPtr event;
        if (selecting_ && released_ != released && !dragging_ && released_)
        {
            // a407b0(1) preserves the existing selected region while the
            // purchase window disables highlighting; it cannot install a new
            // selection behind that window.
            if (highlighting_) selectedRaw_ = raw;
            resultSelected_ = selectedRaw_ != 0;
            const wot::CustomizationRegion decoded = wot::customizationRegion( raw );
            area_ = decoded.area;
            region_ = decoded.region;
            if (area_ == -1 && region_ == -1 && !resultSelected_) event = Py_None;
            else event = PyObjectPtr( Py_BuildValue( "(iiOO)", area_, region_,
                resultSelected_ ? Py_True : Py_False, Py_False ), PyObjectPtr::STEAL_REFERENCE );
        }
        if (selecting_ && raw && raw != hoveredRaw_ && !event)
            event = PyObjectPtr( Py_BuildValue( "(iiOO)", -1, -1,
                Py_False, Py_True ), PyObjectPtr::STEAL_REFERENCE );
        released_ = released; hoveredRaw_ = raw;
        if (highlighting_)
        {
            if (!fullScreen( "EDGE_DETECT", cursor )) success = false;
            if (selectedRaw_)
            {
                if (!blurred_->push()) success = false;
                else
                {
                    // BLUR_MASK reads the identity mask, not its own target.
                    if (!fullScreen( "BLUR_MASK", cursor )) success = false;
                    blurred_->pop();
                    if (!fullScreen( "SELECTED_REGION", cursor )) success = false;
                }
            }
        }
        releaseTexture( transfer_ );
        // Publish only after the GPU pass is complete; this callback may close
        // the view, destroy its compound, and drop the service's last reference.
        if (event && callback_ && callback_.get() != Py_None)
        {
            PyObjectPtr callback = callback_;
            PyObjectPtr result( PyObject_CallFunctionObjArgs( callback.get(), event.get(), NULL ),
                PyObjectPtr::STEAL_REFERENCE );
            if (!result) PyErr_Print();
        }
        return success;
    }
    PY_FACTORY_DECLARE()
    PY_METHOD_DECLARE( py_getSelectionResult )
    PY_METHOD_DECLARE( py_setSelectionCallback )
    PY_METHOD_DECLARE( py_setSelectionMode )
    PY_METHOD_DECLARE( py_setSelectingEnabled )
    PY_METHOD_DECLARE( py_setHighlightingEnabled )
    PY_METHOD_DECLARE( py_setCursorDragging )
private:
    EntityPtr entity_;
    PyObjectPtr callback_;
    Moo::EffectMaterialPtr transfer_;
    Moo::RenderTargetPtr mask_, readback_, blurred_;
    ComObjectWrap<DX::Surface> systemSurface_;
    unsigned width_ = 0, height_ = 0, selectedRaw_ = 0, hoveredRaw_ = 0;
    int mode_, area_ = -1, region_ = -1;
    bool selecting_ = true, highlighting_ = true, dragging_ = false, released_ = false;
    bool resultSelected_ = false;
};

PyObject * PyCustomizationHelper::pyNew( PyObject * args )
{
    PyObject * entity, * callback = Py_None;
    int mode;
    if (!PyArg_ParseTuple( args, "Oi|O:PyCustomizationHelper", &entity, &mode, &callback )) return NULL;
    if (callback != Py_None && !PyCallable_Check( callback ))
    { PyErr_SetString( PyExc_TypeError, "Selection callback must be callable or None" ); return NULL; }
    if (entity == Py_None)
        return new PyCustomizationHelper( EntityPtr(), mode, callback );
    if (!PyEntity::Check( entity ))
    { PyErr_SetString( PyExc_TypeError, "PyCustomizationHelper requires an Entity" ); return NULL; }
    PyEntity * owner = static_cast<PyEntity *>( entity );
    return new PyCustomizationHelper( owner->isDestroyed() ? EntityPtr() : owner->pEntity(), mode, callback );
}
PyObject * PyCustomizationHelper::py_getSelectionResult( PyObject * args )
{
    if (!PyArg_ParseTuple( args, ":getSelectionResult" )) return NULL;
    if (area_ == -1 && region_ == -1 && !resultSelected_) Py_RETURN_NONE;
    return Py_BuildValue( "(iiOO)", area_, region_, resultSelected_ ? Py_True : Py_False, Py_False );
}
PyObject * PyCustomizationHelper::py_setSelectionCallback( PyObject * args )
{
    PyObject * callback = Py_None;
    if (!PyArg_ParseTuple( args, "|O:setSelectionCallback", &callback )) return NULL;
    if (callback != Py_None && !PyCallable_Check( callback ))
    { PyErr_SetString( PyExc_TypeError, "Selection callback must be callable or None" ); return NULL; }
    callback_ = callback;
    Py_RETURN_NONE;
}
PyObject * PyCustomizationHelper::py_setSelectionMode( PyObject * args )
{
    if (!PyArg_ParseTuple( args, "i:setSelectionMode", &mode_ )) return NULL;
    Py_RETURN_NONE;
}
#define CUSTOMIZATION_BOOL_METHOD( name, member ) \
PyObject * PyCustomizationHelper::py_##name( PyObject * args ) \
{ \
    PyObject * value; \
    if (!PyArg_ParseTuple( args, "O:" #name, &value )) return NULL; \
    const int flag = PyObject_IsTrue( value ); \
    if (flag < 0) return NULL; \
    member = flag != 0; \
    Py_RETURN_NONE; \
}
CUSTOMIZATION_BOOL_METHOD( setSelectingEnabled, selecting_ )
CUSTOMIZATION_BOOL_METHOD( setHighlightingEnabled, highlighting_ )
CUSTOMIZATION_BOOL_METHOD( setCursorDragging, dragging_ )
#undef CUSTOMIZATION_BOOL_METHOD
PY_TYPEOBJECT( PyCustomizationHelper )
PY_BEGIN_METHODS( PyCustomizationHelper )
    PY_METHOD( getSelectionResult )
    PY_METHOD( setSelectionCallback )
    PY_METHOD( setSelectionMode )
    PY_METHOD( setSelectingEnabled )
    PY_METHOD( setHighlightingEnabled )
    PY_METHOD( setCursorDragging )
PY_END_METHODS()
PY_BEGIN_ATTRIBUTES( PyCustomizationHelper )
PY_END_ATTRIBUTES()
PY_FACTORY( PyCustomizationHelper, BigWorld )

bool drawStockCustomization()
{
    // Retain a snapshot across callbacks; never retain a service-owned helper
    // globally or let an earlier callback invalidate the next raw pointer.
    std::vector<PyObjectPtr> owners;
    for (PyCustomizationHelper * helper : PyCustomizationHelper::instances()) owners.push_back( helper );
    bool success = true;
    for (const PyObjectPtr & owner : owners)
        if (!static_cast<PyCustomizationHelper *>( owner.get() )->draw()) success = false;
    return success;
}
void finiStockCustomization()
{
    for (PyCustomizationHelper * helper : PyCustomizationHelper::instances()) helper->close();
    maskMaterials.clear();
}
BW_END_NAMESPACE
