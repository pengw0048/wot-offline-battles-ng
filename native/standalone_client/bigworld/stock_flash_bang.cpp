#ifndef WOT_STOCK_FLASH_BANG_PORTABLE
#include "pch.hpp"
#include "stock_flash_bang.hpp"
#include "moo/moo_utilities.hpp"
#include "moo/renderer.hpp"
#include "moo/vertex_formats.hpp"
#include "pyscript/script.hpp"
#endif
#include <algorithm>

BW_BEGIN_NAMESPACE
int StockFlashBang_token = 1;
namespace {
bool animationsOpen = true;
std::shared_ptr<StockFlashBangSupport> activeFlashBang()
{
    Renderer* renderer = Renderer::pInstance();
    IRendererPipeline* pipeline = renderer ? renderer->pipeline() : NULL;
    return pipeline ? pipeline->stockFlashBangSupport() : std::shared_ptr<StockFlashBangSupport>();
}
void clearFlashTexture(const Moo::EffectMaterialPtr& material)
{
    if (!material || !material->pEffect()) return;
    ID3DXEffect* effect = material->properties().existingStockBinding();
    if (effect) effect->SetTexture("colorTexture", NULL);
}
uint32 flashColour(const Vector4& value)
{
    // a20ff0 clamps/rounds each channel before packing the VertexTL colour.
    const auto channel = [](float v) -> uint32 {
        const float clamped = v < 0.f ? 0.f : (v < 1.f ? v : 1.f);
        return static_cast<uint32>(clamped * 255.f + .5f);
    };
    return (channel(value.w) << 24) | (channel(value.x) << 16) |
        (channel(value.y) << 8) | channel(value.z);
}
}

StockFlashBangSupport::StockFlashBangSupport() : colour_(0,0,0,0), open_(animationsOpen) {}
StockFlashBangSupport::~StockFlashBangSupport() { close(); deleteUnmanagedObjects(); }
bool StockFlashBangSupport::initialise()
{
    if (!material_) createManagedObjects();
    return material_ != NULL;
}
void StockFlashBangSupport::createManagedObjects()
{
    Moo::EffectMaterialPtr loaded;
    if (!Moo::createEffect(loaded, "system/materials/geometrics.fx"))
    {
        material_ = NULL;
        ERROR_MSG("FlashBang: original geometrics effect could not be loaded\n");
        return;
    }
    // Geometrics exposes doubleSided as an artist property. Begin reapplies
    // it, so clone the shared defaults before choosing this material's value.
    loaded->replaceDefaults();
    if (!loaded->setProperty("doubleSided", true, false))
    {
        material_ = NULL;
        ERROR_MSG("FlashBang: geometrics doubleSided property is unavailable\n");
        return;
    }
    material_ = loaded;
}
void StockFlashBangSupport::deleteManagedObjects()
{
    clearFlashTexture(material_);
    material_ = NULL;
}
void StockFlashBangSupport::deleteUnmanagedObjects()
{
    clearFlashTexture(material_);
    historyValid_ = false;
    history_ = NULL;
}
void StockFlashBangSupport::close()
{
    open_ = false;
    ++generation_;
    colour_ = Vector4(0,0,0,0);
    historyValid_ = false;
    std::vector<Vector4ProviderPtr> released;
    released.swap(animations_);
}
void StockFlashBangSupport::reopen() { open_ = true; }
void StockFlashBangSupport::add(Vector4ProviderPtr provider)
{
    // a1dcb0 retains every registration, including repeated identities.
    if (!open_) return;
    animations_.push_back(provider);
    ++generation_;
}
void StockFlashBangSupport::remove(Vector4ProviderPtr provider)
{
    // a1e2f0 erases only the first matching identity; absent is harmless.
    const auto found = std::find(animations_.begin(), animations_.end(), provider);
    if (found == animations_.end()) return;
    ++generation_;
    Vector4ProviderPtr released = *found;
    animations_.erase(found);
}
void StockFlashBangSupport::tick()
{
    if (!open_) return;
    const auto accepted = generation_;
    const auto selected = animations_;
    Vector4 colour(0,0,0,0);
    for (const auto& provider : selected)
    {
        // The stock consumer always supplies Vector4Animation. The original
        // converter also admits None; do not dereference that null provider.
        if (!provider) continue;
        Vector4 value;
        provider->output(value);
        if (!open_ || generation_ != accepted) return;
        // a1e3f0: independent component maxima, with its x comparison order.
        colour.x = value.x > colour.x ? value.x : colour.x;
        colour.y = colour.y > value.y ? colour.y : value.y;
        colour.z = colour.z > value.z ? colour.z : value.z;
        colour.w = colour.w > value.w ? colour.w : value.w;
    }
    colour_ = colour;
}
bool StockFlashBangSupport::draw()
{
    if (!open_ || !(colour_.w > 0.f)) { historyValid_ = false; return true; }
    if (!initialise()) return false;
    if (!history_)
    {
        if (!Moo::createRenderTarget(history_, 512, 512, D3DFMT_A8R8G8B8,
                "FlashBang", false, true) || !history_->pTexture())
        {
            history_ = NULL;
            return false;
        }
    }
    const bool previous = historyValid_;
    historyValid_ = false;
    if (!material_->setTechnique(previous ? "textured2D" : "standard2D") ||
        !material_->properties().select(*material_->pEffect())) return false;
    ID3DXEffect* effect = material_->pEffect()->pEffect();
    const D3DXVECTOR4 white(1,1,1,1);
    if (FAILED(effect->SetVector("diffuseColor", &white)) ||
        FAILED(effect->SetBool("zTestDisable", true)) ||
        FAILED(effect->SetBool("zWriteEnable", false)) ||
        FAILED(effect->SetInt("blendMode", 2)) ||
        FAILED(effect->SetBool("doubleSided", true)) ||
        FAILED(effect->SetBool("useWireframe", false)) ||
        FAILED(effect->SetInt("colorWriteAlpha", 0)) ||
        FAILED(effect->SetBool("textureIsPointSampled", false)) ||
        FAILED(effect->SetFloat("outputAlphaMultiplier", 1.f)) ||
        FAILED(effect->SetTexture("colorTexture", previous ? history_->pTexture() : NULL))) return false;
    if (previous && (FAILED(effect->SetInt("outputAlphaType", 1)) ||
            FAILED(effect->SetInt("outputColourType", 1)))) return false;

    // a1de15/a21950: an overscanned pixel-space TL quad, not a world mesh.
    const float right = Moo::rc().screenWidth() + 4.f;
    const float bottom = Moo::rc().screenHeight() + 4.f;
    const uint32 colour = flashColour(colour_);
    static_assert(sizeof(Moo::VertexTLUV) == 28, "#1513 flash quad stride");
    const Moo::VertexTLUV vertices[4] = {
        {Vector4(-2.f,-2.f,0.f,1.f), colour, Vector2(0.f,0.f)},
        {Vector4(right,-2.f,0.f,1.f), colour, Vector2(1.f,0.f)},
        {Vector4(-2.f,bottom,0.f,1.f), colour, Vector2(0.f,1.f)},
        {Vector4(right,bottom,0.f,1.f), colour, Vector2(1.f,1.f)}
    };
    bool success = material_->begin();
    if (success)
    {
        for (uint32 pass = 0; pass != material_->numPasses(); ++pass)
        {
            if (!material_->beginPass(pass)) { success = false; continue; }
            Moo::rc().setFVF(Moo::VertexTLUV::fvf());
            if (FAILED(Moo::rc().drawPrimitiveUP(D3DPT_TRIANGLESTRIP, 2, vertices, sizeof(*vertices))))
                success = false;
            if (!material_->endPass()) success = false;
        }
        if (!material_->end()) success = false;
    }
    clearFlashTexture(material_);
    Moo::rc().setTexture(0, NULL);
    if (!success) return false;

    // a1dfa2..a1e180 retains the just-composited image, scaled to 512 square.
    // Only the active viewport contributes when dynamic resolution pads its RT.
    ComObjectWrap<DX::Surface> source, destination;
    DX::Viewport viewport;
    if (FAILED(Moo::rc().device()->GetRenderTarget(0, &source)) ||
        FAILED(Moo::rc().device()->GetViewport(&viewport)) ||
        FAILED(history_->pSurface(destination))) return false;
    const RECT crop = {LONG(viewport.X), LONG(viewport.Y),
        LONG(viewport.X + viewport.Width), LONG(viewport.Y + viewport.Height)};
    historyValid_ = SUCCEEDED(Moo::rc().device()->StretchRect(source, &crop,
        destination, NULL, D3DTEXF_LINEAR));
    return historyValid_;
}

void initStockFlashBangAnimations()
{
    animationsOpen = true;
    auto owner = activeFlashBang();
    if (owner) owner->reopen();
}
void finiStockFlashBangAnimations()
{
    animationsOpen = false;
    auto owner = activeFlashBang();
    if (owner) owner->close();
}
static void flashBangAnimation(Vector4ProviderPtr provider)
{
    auto owner = activeFlashBang();
    if (owner) owner->add(provider);
}
static void removeFlashBangAnimation(Vector4ProviderPtr provider)
{
    auto owner = activeFlashBang();
    if (owner) owner->remove(provider);
}
PY_AUTO_MODULE_FUNCTION(RETVOID, flashBangAnimation, ARG(Vector4ProviderPtr, END), BigWorld)
PY_AUTO_MODULE_FUNCTION(RETVOID, removeFlashBangAnimation, ARG(Vector4ProviderPtr, END), BigWorld)
BW_END_NAMESPACE
