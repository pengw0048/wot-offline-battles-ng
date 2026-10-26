#include "pch.hpp"
#include "stock_antialiasing.hpp"
#include "stock_dynamic_resolution.hpp"
#include "stock_effect_adapter.hpp"
#include "moo/effect_visual_context.hpp"
#include "moo/fullscreen_quad.hpp"
#include "moo/moo_utilities.hpp"
#include "moo/render_context.hpp"

BW_BEGIN_NAMESPACE
namespace
{
void clearTexture(const Moo::EffectMaterialPtr& material, const char* name)
{
    if (!material || !material->pEffect()) return;
    // end() restores the shared Definition. Only this existing Properties
    // Binding retains the manual DEFAULT-pool texture, including after reload.
    ID3DXEffect* binding = material->properties().existingStockBinding();
    if (binding) binding->SetTexture(name, NULL);
    ID3DXEffect* current = material->pEffect()->pEffect();
    if (current && current != binding && !wot::is_stock_effect(current))
        current->SetTexture(name, NULL);
}

class TargetScope
{
public:
    TargetScope() : saved_(Moo::rc().pushRenderTarget()) {}
    ~TargetScope()
    {
        if (saved_)
        {
            Moo::rc().popRenderTarget();
            Moo::rc().effectVisualContext().updateSharedConstants(Moo::CONSTANTS_PER_SCREEN);
        }
    }
    bool bind(const Moo::RenderTargetPtr& target)
    {
        ComObjectWrap<DX::Surface> surface;
        if (!saved_ || !target || FAILED(target->pSurface(surface)) ||
            FAILED(Moo::rc().device()->SetDepthStencilSurface(NULL)) ||
            FAILED(Moo::rc().setRenderTarget(0, surface))) return false;
        for (uint slot = 1; slot != 4; ++slot)
            if (FAILED(Moo::rc().setRenderTarget(slot, NULL))) return false;
        DX::Viewport viewport = {0, 0, target->width(), target->height(), 0.f, 1.f};
        if (FAILED(setStockSceneViewport(&viewport, true))) return false;
        Moo::rc().screenWidth(target->width());
        Moo::rc().screenHeight(target->height());
        Moo::rc().effectVisualContext().updateSharedConstants(Moo::CONSTANTS_PER_SCREEN);
        return true;
    }
private:
    bool saved_;
};

bool select(const Moo::EffectMaterialPtr& material)
{
    return material && material->pEffect() &&
        material->properties().select(*material->pEffect());
}

bool draw(const Moo::EffectMaterialPtr& material)
{
    return Moo::rc().fsQuad().drawChecked(*material,
        static_cast<uint>(Moo::rc().screenWidth()),
        static_cast<uint>(Moo::rc().screenHeight()));
}
}

StockFXAASupport::StockFXAASupport() : enabled_(false) {}

StockFXAASupport::~StockFXAASupport()
{
    deleteUnmanagedObjects();
    deleteManagedObjects();
}

bool StockFXAASupport::createManagedObjects()
{
    deleteManagedObjects();
    return Moo::createEffect(material_, "shaders/anti_aliasing/wg_fxaa.fx") &&
        Moo::createEffect(luminanceMaterial_, "shaders/anti_aliasing/wg_fxaa_luminance.fx");
}

void StockFXAASupport::deleteManagedObjects()
{
    clearTexture(material_, "g_bbCopyMap");
    clearTexture(luminanceMaterial_, "g_bbCopyMap");
    material_ = NULL;
    luminanceMaterial_ = NULL;
}

void StockFXAASupport::deleteUnmanagedObjects()
{
    clearTexture(material_, "g_bbCopyMap");
    clearTexture(luminanceMaterial_, "g_bbCopyMap");
    copy_ = NULL;
    luminance_ = NULL;
}

bool StockFXAASupport::prepareTargets()
{
    const auto& size = Moo::rc().backBufferDesc();
    if (!size.Width || !size.Height) return false;
    if (copy_ && luminance_ && copy_->pTexture() && luminance_->pTexture() &&
        copy_->width() == size.Width && copy_->height() == size.Height &&
        luminance_->width() == size.Width && luminance_->height() == size.Height) return true;
    deleteUnmanagedObjects();
    // 9c8d70 requests original format 2 for both pooled scratch handles.
    if (!Moo::createRenderTarget(copy_, size.Width, size.Height, D3DFMT_A8R8G8B8,
            "stock/FXAA scene copy", false, true) ||
        !Moo::createRenderTarget(luminance_, size.Width, size.Height, D3DFMT_A8R8G8B8,
            "stock/FXAA luminance", false, true) ||
        !copy_->pTexture() || !luminance_->pTexture())
    {
        deleteUnmanagedObjects();
        return false;
    }
    return true;
}

bool StockFXAASupport::apply()
{
    if (!enabled_) return true;
    if (!Moo::rc().device() || !material_ || !luminanceMaterial_ ||
        !prepareTargets() || !Moo::copyBackBuffer(copy_.getObject())) return false;
    {
        TargetScope target;
        if (!target.bind(luminance_) || !select(luminanceMaterial_)) return false;
        ID3DXEffect* effect = luminanceMaterial_->pEffect()->pEffect();
        if (FAILED(effect->SetTexture("g_bbCopyMap", copy_->pTexture())) ||
            !draw(luminanceMaterial_)) return false;
    }
    if (!select(material_)) return false;
    ID3DXEffect* effect = material_->pEffect()->pEffect();
    return SUCCEEDED(effect->SetTexture("g_bbCopyMap", luminance_->pTexture())) && draw(material_);
}

BW_END_NAMESPACE
