#ifndef WOT_STOCK_HANGAR_PRESENTATION_PORTABLE
#include "pch.hpp"
#include "stock_hangar_presentation.hpp"
#include "stock_effect_adapter.hpp"
#include "stock_tps_scene.hpp"
#include "ashes/simple_gui.hpp"
#include "entity.hpp"
#include "py_entity.hpp"
#include "duplo/pymodel.hpp"
#include "input/input.hpp"
#include "model/model.hpp"
#include "model/super_model.hpp"
#include "moo/base_texture.hpp"
#include "moo/device_callback.hpp"
#include "moo/draw_context.hpp"
#include "moo/effect_material.hpp"
#include "moo/fullscreen_quad.hpp"
#include "moo/moo_utilities.hpp"
#include "moo/render_context.hpp"
#include "moo/render_target.hpp"
#include "moo/texture_manager.hpp"
#include "moo/visual.hpp"
#include "pyscript/script.hpp"
#include "space/entity_embodiment.hpp"
#endif
#include <algorithm>
#include <memory>

BW_BEGIN_NAMESPACE
int StockHangarPresentation_token = 1;
namespace
{
// The material's private ARIE binding is the owner its own draw consumes; a
// write before selecting it lands in the shared definition instead.
ID3DXEffect * selectBinding( const Moo::EffectMaterialPtr & material )
{
    if (!material || !material->pEffect() ||
        !material->properties().select( *material->pEffect() )) return NULL;
    return material->pEffect()->pEffect();
}

// end() returns pEffect() to the definition; the existing Properties binding
// still holds the manual texture until it is cleared here.
void clearTexture( const Moo::EffectMaterialPtr & material, const char * name )
{
    if (!material || !material->pEffect()) return;
    ID3DXEffect * binding = material->properties().existingStockBinding();
    if (binding) binding->SetTexture( name, NULL );
    ID3DXEffect * current = material->pEffect()->pEffect();
    if (current && current != binding && !wot::is_stock_effect( current ))
        current->SetTexture( name, NULL );
}

// 00599b80 WGBackgroundImage: texture +4, background.fx +8, enabled +0xc.
// The state is static in the original and outlives GUIApp's GPU resources.
bool backgroundEnabled = false;
Moo::BaseTexturePtr backgroundTexture;

class GUIBackground : public Moo::DeviceCallback
{
public:
    Moo::EffectMaterialPtr material;
    bool closed = false;

    ~GUIBackground() { close(); }
    void close()
    {
        closed = true;
        deleteManagedObjects();
    }
    virtual void createManagedObjects()
    {
        if (closed) return;
        // 006f8d90 asserts on failure; the draw then has no effect to use.
        if (!Moo::createEffect( material, "shaders/post_processing/background.fx" ))
            ERROR_MSG( "WGBackgroundImage: background effect could not be loaded\n" );
    }
    virtual void deleteManagedObjects()
    {
        clearTexture( material, "inputTexture" );
        material = NULL;
    }
    bool draw()
    {
        // 006f8e90: disabled, no texture or no effect is a legitimate no-draw.
        if (closed || !backgroundEnabled || !backgroundTexture ||
            !backgroundTexture->pTexture() || !material || !material->pEffect())
            return true;
        ID3DXEffect * effect = selectBinding( material );
        const auto & desc = Moo::rc().backBufferDesc();
        const bool drawn = effect &&
            SUCCEEDED( effect->SetTexture( "inputTexture", backgroundTexture->pTexture() ) ) &&
            Moo::rc().fsQuad().drawChecked( *material, desc.Width, desc.Height );
        // Do not let the binding retain the texture after BattleLoading replaces it.
        clearTexture( material, "inputTexture" );
        return drawn;
    }
};
std::unique_ptr<GUIBackground> background;

// 00af33d0 EdgeDrawer. Members are the entity embodiments present at the add;
// drawMode 0 is list +4, modes 1 and 2 list +0x10. Each carries its z_pass
// parameters (00af4035..00af4103).
struct EdgeMember
{
    EntityPtr entity;
    IEntityEmbodimentPtr embodiment;
    uint colorIndex;
    uint drawMode;
    bool flag;  // Stored at member +0x10 by 00af3c50; no consumer was traced.
};

class EdgeDetector : public Moo::DeviceCallback
{
public:
    // 00af34cf..00af3531: blue, red, green, white.
    BW::vector<Vector4> colors;
    std::vector<EdgeMember> members[2];
    Moo::EffectMaterialPtr transfer;
    std::shared_ptr<Moo::DrawContext::OverrideBlock> zPass;
    Moo::RenderTargetPtr target;
    bool closed = false;

    EdgeDetector()
    {
        colors.push_back( Vector4( 0.f, 0.f, 1.f, 1.f ) );
        colors.push_back( Vector4( 1.f, 0.f, 0.f, 1.f ) );
        colors.push_back( Vector4( 0.f, 1.f, 0.f, 1.f ) );
        colors.push_back( Vector4( 1.f, 1.f, 1.f, 1.f ) );
    }
    ~EdgeDetector() { close(); }
    void close()
    {
        closed = true;
        members[0].clear();
        members[1].clear();
        deleteUnmanagedObjects();
        deleteManagedObjects();
    }
    virtual void createManagedObjects()
    {
        if (closed) return;
        if (!Moo::createEffect( transfer, "shaders/edgeDetect/transfer.fx" ))
            ERROR_MSG( "EdgeDrawer: transfer effect could not be loaded\n" );
        // 00af3ef3 builds each member's override from shaders/edgeDetect/z_pass.
        zPass = std::make_shared<Moo::DrawContext::OverrideBlock>(
            BW::StringRef( "shaders/edgeDetect/z_pass" ), false );
        for (unsigned index = 0; index < 2; ++index)
        {
            Moo::ComplexEffectMaterial * material = zPass->stockMaterial( index != 0 );
            if (!material || !material->baseMaterial() || !material->baseMaterial()->pEffect())
            {
                ERROR_MSG( "EdgeDrawer: %s z_pass effect could not be loaded\n", index ? "skinned" : "rigid" );
                zPass.reset();
                break;
            }
        }
    }
    virtual void deleteManagedObjects()
    {
        clearTexture( transfer, "g_edgeDetectedMap" );
        transfer = NULL;
        zPass.reset();
    }
    virtual void createUnmanagedObjects()
    {
        deleteUnmanagedObjects();
        if (closed) return;
        const auto & desc = Moo::rc().backBufferDesc();
        if (!desc.Width || !desc.Height) return;
        // The z-pass tests member depth against the scene's own depth buffer.
        if (!Moo::createRenderTarget( target, desc.Width, desc.Height, D3DFMT_A8R8G8B8,
                "stock/Edge detect", true, false ))
            ERROR_MSG( "EdgeDrawer: edge target could not be created\n" );
    }
    virtual void deleteUnmanagedObjects()
    {
        // SetTexture retains the default-pool target; release it before the target.
        clearTexture( transfer, "g_edgeDetectedMap" );
        target = NULL;
    }
    virtual bool recreateForD3DExDevice() const { return true; }

    // 00af3db1: additions are ignored until the drawer is attached and ready.
    bool ready() const { return !closed && transfer && zPass && target && target->pTexture(); }

    static bool attached( const EdgeMember & member )
    {
        const Entity * entity = member.entity.get();
        if (!entity || entity->isDestroyed() || !entity->isInWorld()) return false;
        if (entity->pPrimaryEmbodiment() == member.embodiment.get()) return true;
        EntityEmbodiments & auxiliary = const_cast<Entity *>( entity )->auxiliaryEmbodiments();
        return std::find_if( auxiliary.begin(), auxiliary.end(), [&]( const IEntityEmbodimentPtr & item )
            { return item.get() == member.embodiment.get(); } ) != auxiliary.end();
    }
    void prune()
    {
        for (auto & list : members)
            list.erase( std::remove_if( list.begin(), list.end(),
                []( const EdgeMember & member ) { return !attached( member ); } ), list.end() );
    }
    // 00652c09 primary embodiment, then 00652c50 each auxiliary embodiment.
    static std::vector<IEntityEmbodimentPtr> embodiments( Entity & entity )
    {
        std::vector<IEntityEmbodimentPtr> result;
        if (entity.pPrimaryEmbodiment())
            result.push_back( IEntityEmbodimentPtr( entity.pPrimaryEmbodiment() ) );
        for (const IEntityEmbodimentPtr & embodiment : entity.auxiliaryEmbodiments())
            if (embodiment) result.push_back( embodiment );
        return result;
    }
    void erase( const IEntityEmbodimentPtr & embodiment )
    {
        for (auto & list : members)
            list.erase( std::remove_if( list.begin(), list.end(),
                [&]( const EdgeMember & member ) { return member.embodiment.get() == embodiment.get(); } ),
                list.end() );
    }
    // 00af3c50 replaces a key already present in either list.
    void add( const EntityPtr & entity, uint colorIndex, uint drawMode, bool flag )
    {
        if (!ready()) return;
        prune();
        for (const IEntityEmbodimentPtr & embodiment : embodiments( *entity.get() ))
        {
            erase( embodiment );
            EdgeMember member = { entity, embodiment, colorIndex, drawMode, flag };
            members[drawMode != 0].push_back( member );
        }
    }
    // 00af4480 removes each current embodiment key; an absent key is a no-op.
    void remove( Entity & entity )
    {
        for (const IEntityEmbodimentPtr & embodiment : embodiments( entity )) erase( embodiment );
        prune();
    }
    bool parameters( const EdgeMember & member )
    {
        // 00af40c0 sets component colorIndex of a zero mask. An index past 3
        // writes past the original's stack vector; here it leaves the mask zero.
        Vector4 mask( 0.f, 0.f, 0.f, 0.f );
        if (member.colorIndex < 4) mask[member.colorIndex] = 1.f;
        // 00af3ea0..00af3ecf: mode 0 -> (0, False), 1 -> (3, True), else (1, False).
        const int depthFunc = member.drawMode == 0 ? 0 : member.drawMode == 1 ? 3 : 1;
        const bool occluded = member.drawMode == 1;
        for (unsigned index = 0; index < 2; ++index)
        {
            Moo::ComplexEffectMaterial * complex = zPass->stockMaterial( index != 0 );
            Moo::EffectMaterialPtr material;
            if (complex) material = complex->baseMaterial();
            // None of these is artistEditable, so begin() does not reapply
            // them; they must be written into the binding the draw selects.
            ID3DXEffect * effect = selectBinding( material );
            if (!effect || FAILED( effect->SetVector( "g_colorMask", &mask ) ) ||
                FAILED( effect->SetInt( "depthFunc", depthFunc ) ) ||
                FAILED( effect->SetBool( "edgeDetectOccluded", occluded ) )) return false;
        }
        return true;
    }
    bool draw()
    {
        prune();
        // 00af45fb: nothing to draw without members.
        // 00af4619: without the target (e.g. a 0x0 back buffer) the pass is silently skipped.
        if (closed || (members[0].empty() && members[1].empty()) || !ready()) return true;
        if (!target->push()) return false;
        bool success = SUCCEEDED( Moo::rc().device()->Clear( 0, NULL, D3DCLEAR_TARGET, 0, 1.f, 0 ) );
        Moo::DrawContext context( Moo::RENDERING_PASS_COLOR );
        context.begin( Moo::DrawContext::ALL_CHANNELS_MASK );
        context.pushImmediateMode();
        context.pushOverrideBlock( zPass.get() );
        for (const auto & list : members)
            for (const EdgeMember & member : list)
            {
                if (!parameters( member )) { success = false; continue; }
                member.embodiment->draw( context );
            }
        context.popOverrideBlock( zPass.get() );
        context.popImmediateMode();
        context.end( Moo::DrawContext::ALL_CHANNELS_MASK );
        context.flush( Moo::DrawContext::ALL_CHANNELS_MASK );
        target->pop();
        if (!success) return false;
        // 00af45f0: the edge map and exactly four colours. Fewer than four reads
        // past the original vector; here the missing colours are zero.
        Vector4 palette[4] = { Vector4( 0.f, 0.f, 0.f, 0.f ), Vector4( 0.f, 0.f, 0.f, 0.f ),
            Vector4( 0.f, 0.f, 0.f, 0.f ), Vector4( 0.f, 0.f, 0.f, 0.f ) };
        std::copy( colors.begin(), colors.begin() + std::min<size_t>( colors.size(), 4 ), palette );
        ID3DXEffect * effect = selectBinding( transfer );
        if (!effect || FAILED( effect->SetTexture( "g_edgeDetectedMap", target->pTexture() ) ) ||
            FAILED( effect->SetVectorArray( "g_edgeColors",
                reinterpret_cast<const D3DXVECTOR4 *>( palette ), 4 ) )) return false;
        const auto & desc = Moo::rc().backBufferDesc();
        return Moo::rc().fsQuad().drawChecked( *transfer, desc.Width, desc.Height );
    }
};
std::unique_ptr<EdgeDetector> edgeDetector;

void warning( const char * message )
{
    // 00652be0/00652f57: PyErr_Format( PyExc_Warning ), then PyErr_PrintEx( 0 ).
    PyErr_SetString( PyExc_Warning, message );
    PyErr_PrintEx( 0 );
}

// 006b45a0: every material of the first top model and its parent chain.
void assignModelTexture( Model * model, const BW::string & property, const BW::string & texture )
{
    for (; model != NULL; model = model->parent())
    {
        BW::vector<Moo::Visual::PrimitiveGroup *> groups;
        model->gatherMaterials( "*", groups );
        for (Moo::Visual::PrimitiveGroup * group : groups)
        {
            if (!group || !group->material_) continue;
            Moo::EffectMaterialPtr base = group->material_->baseMaterial();
            if (!base) continue;
            Moo::EffectPropertyPtr value = base->getProperty( property );
            if (value) value->be( BW::StringRef( texture ) );
        }
    }
}
}

bool initialiseStockHangarPresentation()
{
    if (background) return true;
    background.reset( new GUIBackground );
    background->createManagedObjects();
    // 0093fe47 creates EdgeDrawer with the renderer, before any Account event.
    edgeDetector.reset( new EdgeDetector );
    edgeDetector->createManagedObjects();
    edgeDetector->createUnmanagedObjects();
    return true;
}

void finaliseStockHangarPresentation()
{
    if (edgeDetector) edgeDetector->close();
    edgeDetector.reset();
    if (background) background->close();
    background.reset();
    backgroundTexture = NULL;
    backgroundEnabled = false;
}

bool drawStockGUIBackground()
{
    return !background || background->draw();
}

bool drawStockEdgeDetection()
{
    return !edgeDetector || edgeDetector->draw();
}

/**
 *	#1513 00652ee0: an empty list raises Warning; more than four warns and is
 *	still assigned. EdgeDetectColorController passes four on onAccountShowGUI.
 */
PyObject * wgSetEdgeDetectColors( const BW::vector<Vector4> & colors )
{
    if (colors.empty())
    {
        PyErr_Format( PyExc_Warning, "wgSetEdgeDetectColors - colors size is zero. "
            "\t\t\t\t\t\t\t\t\tUsing defaults colors.\n" );
        return NULL;
    }
    if (colors.size() > 4)
        warning( "wgSetEdgeDetectColors - colors size is great then zero. "
            "\t\t\t\t\t\t\t\t\tUsing the first three items.\n" );
    if (edgeDetector) edgeDetector->colors = colors;
    Py_RETURN_NONE;
}
PY_AUTO_MODULE_FUNCTION( RETOWN, wgSetEdgeDetectColors, ARG( BW::vector<Vector4>, END ), BigWorld )

/**
 *	#1513 00652ab0: an invalid mode or colour warns and is still added. The
 *	original dereferences a None entity when the drawer exists.
 */
PyObject * wgAddEdgeDetectEntity( PyEntityPtr pyEntity, uint colorIndex, uint drawMode, bool flag )
{
    if (edgeDetector)
    {
        if (drawMode > 2) warning( "Incorrect draw mode (0, 1, 2 are only valid).\n" );
        else if (colorIndex > 3) warning( "Incorrect color index (0-3 are only valid).\n" );
        if (!pyEntity || !pyEntity->pEntity())
        {
            PyErr_SetString( PyExc_TypeError, "wgAddEdgeDetectEntity() argument 1 must be an entity" );
            return NULL;
        }
        edgeDetector->add( pyEntity->pEntity(), colorIndex, drawMode, flag );
    }
    Py_RETURN_NONE;
}
PY_AUTO_MODULE_FUNCTION( RETOWN, wgAddEdgeDetectEntity, ARG( PyEntityPtr, ARG( uint,
    OPTARG( uint, 0, OPTARG( bool, false, END ) ) ) ), BigWorld )

/**
 *	#1513 00652cf0: removes the entity's current embodiments.
 */
PyObject * wgDelEdgeDetectEntity( PyEntityPtr pyEntity )
{
    if (edgeDetector)
    {
        if (!pyEntity || !pyEntity->pEntity())
        {
            PyErr_SetString( PyExc_TypeError, "wgDelEdgeDetectEntity() argument 1 must be an entity" );
            return NULL;
        }
        edgeDetector->remove( *pyEntity->pEntity().get() );
    }
    Py_RETURN_NONE;
}
PY_AUTO_MODULE_FUNCTION( RETOWN, wgDelEdgeDetectEntity, ARG( PyEntityPtr, END ), BigWorld )

/**
 *	#1513 00633330 -> 006f9160: only the first flag is stored; BattleLoading
 *	passes (True, False), (False, True) or (False, False).
 */
void wg_enableGUIBackground( bool enable, bool )
{
    backgroundEnabled = enable;
}
PY_AUTO_MODULE_FUNCTION( RETVOID, wg_enableGUIBackground, ARG( bool, ARG( bool, END ) ), BigWorld )

/**
 *	#1513 006333e0 -> 006f9090: replaces the retained texture reference.
 */
void wg_setGUIBackground( const BW::string & name )
{
    backgroundTexture = Moo::TextureManager::instance()->get( name, true, true, true,
        "Art/File Textures/" );
}
PY_AUTO_MODULE_FUNCTION( RETVOID, wg_setGUIBackground, ARG( BW::string, END ), BigWorld )

/**
 *	#1513 00aeb660: stores the flag, then pauses and hides Influx with !flag.
 */
void SetDrawInflux( bool enable )
{
    setStockTPSDrawEnabled( enable );
}
PY_AUTO_MODULE_FUNCTION( RETVOID, SetDrawInflux, ARG( bool, END ), BigWorld )

/**
 *	#1513 006b3b30: assigns a texture by resource name to the named material
 *	property and returns True. The original dereferences a None model.
 */
PyObject * wg_setPyModelTexture( PyModelPtr model, const BW::string & property,
    const BW::string & texture )
{
    if (!model)
    {
        PyErr_SetString( PyExc_TypeError, "wg_setPyModelTexture() argument 1 must be a PyModel" );
        return NULL;
    }
    SuperModel * superModel = model->pSuperModel();
    if (superModel && superModel->nModels() > 0)
        assignModelTexture( superModel->topModel( 0 ).get(), property, texture );
    return Script::getData( true );
}
PY_AUTO_MODULE_FUNCTION( RETOWN, wg_setPyModelTexture,
    ARG( BW::PyModelPtr, ARG( BW::string, ARG( BW::string, END ) ) ), BigWorld )

/**
 *	#1513 006503c0/006522b0: the primary RenderSystem::SwapChain gamma (+0x1b0,
 *	default 1.0). Its public owner is Moo::RenderContext::gammaCorrection_,
 *	which createDevice and every reset apply as the device gamma ramp.
 */
float getGammaCorrection()
{
    return Moo::rc().gammaCorrection();
}
PY_AUTO_MODULE_FUNCTION( RETDATA, getGammaCorrection, END, BigWorld )

void setGammaCorrection( float gamma )
{
    Moo::rc().gammaCorrection( gamma );
}
PY_AUTO_MODULE_FUNCTION( RETVOID, setGammaCorrection, ARG( float, END ), BigWorld )

/**
 *	#1513 00650140/00652020 read and write ColorGrading +0x1c through the
 *	pipeline's vtable +0x94 owner, returning 0.0/None when it is absent. This
 *	client's deferred pipeline has no ColorGrading (wg_color_grading.fx) owner,
 *	so it takes that absent branch: the argument is still converted, and
 *	nothing is stored.
 */
float getColorGradingStrength()
{
    return 0.f;
}
PY_AUTO_MODULE_FUNCTION( RETDATA, getColorGradingStrength, END, BigWorld )

void setColorGradingStrength( float )
{
}
PY_AUTO_MODULE_FUNCTION( RETVOID, setColorGradingStrength, ARG( float, END ), BigWorld )

/**
 * #1513 00654bc0 accepts no arguments and calls 00a20670 only when the
 * pipeline's ColorGrading owner exists. Like the strength accessors above,
 * this pipeline takes its absent-owner branch; it has no LUT owner to update.
 */
void wg_updateColorGrading()
{
}
PY_AUTO_MODULE_FUNCTION( RETVOID, wg_updateColorGrading, END, BigWorld )

/**
 *	#1513 00aa8290: a zero-delta mouse event at the given cursor position,
 *	dispatched through SimpleGUI::handleMouseEvent (01d1fee0 vtable +0x10).
 */
void syncMousePosition( float x, float y )
{
    SimpleGUI * gui = SimpleGUI::pInstance();
    if (gui == NULL) return;
    MouseEvent event;
    event.fill( 0, 0, 0, Vector2( x, y ) );
    gui->handleMouseEvent( event );
}
PY_AUTO_MODULE_FUNCTION( RETVOID, syncMousePosition, ARG( float, ARG( float, END ) ), GUI )

BW_END_NAMESPACE
