#ifndef WOT_STOCK_HANGAR_PRESENTATION_PORTABLE
#include "pch.hpp"
#include "stock_hangar_presentation.hpp"
#include "stock_tps_scene.hpp"
#include "ashes/simple_gui.hpp"
#include "duplo/pymodel.hpp"
#include "input/input.hpp"
#include "model/model.hpp"
#include "model/super_model.hpp"
#include "moo/base_texture.hpp"
#include "moo/device_callback.hpp"
#include "moo/effect_material.hpp"
#include "moo/fullscreen_quad.hpp"
#include "moo/moo_utilities.hpp"
#include "moo/render_context.hpp"
#include "moo/texture_manager.hpp"
#include "moo/visual.hpp"
#include "pyscript/script.hpp"
#endif
#include <memory>

BW_BEGIN_NAMESPACE
int StockHangarPresentation_token = 1;
namespace
{
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
    virtual void deleteManagedObjects() { material = NULL; }
    bool draw()
    {
        // 006f8e90: disabled, no texture or no effect is a legitimate no-draw.
        if (closed || !backgroundEnabled || !backgroundTexture ||
            !backgroundTexture->pTexture() || !material || !material->pEffect())
            return true;
        ID3DXEffect * effect = material->pEffect()->pEffect();
        if (!effect || FAILED( effect->SetTexture( "inputTexture", backgroundTexture->pTexture() ) ))
            return false;
        const auto & desc = Moo::rc().backBufferDesc();
        const bool drawn = Moo::rc().fsQuad().drawChecked( *material, desc.Width, desc.Height );
        // Do not let the effect retain the texture after BattleLoading replaces it.
        effect->SetTexture( "inputTexture", NULL );
        return drawn;
    }
};
std::unique_ptr<GUIBackground> background;

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
    return true;
}

void finaliseStockHangarPresentation()
{
    if (background) background->close();
    background.reset();
    backgroundTexture = NULL;
    backgroundEnabled = false;
}

bool drawStockGUIBackground()
{
    return !background || background->draw();
}

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
