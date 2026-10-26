#include "pch.hpp"
#include "stock_temporal_antialiasing.hpp"
#include "stock_dynamic_resolution.hpp"
#include "stock_effect_adapter.hpp"
#include "moo/effect_visual_context.hpp"
#include "moo/fullscreen_quad.hpp"
#include "moo/moo_utilities.hpp"
#include "moo/render_context.hpp"
#include <algorithm>

BW_BEGIN_NAMESPACE
namespace
{
const char* const manualTextures[] = {"g_srcBuffer", "g_historyBuffer", "g_avgLumBuffer",
    "g_downsampledVelocityBuffer", "g_prevDownsampledVelocityBuffer"};

void clearTextures(const Moo::EffectMaterialPtr& material)
{
    if (!material || !material->pEffect()) return;
    ID3DXEffect* binding = material->properties().existingStockBinding();
    ID3DXEffect* current = material->pEffect()->pEffect();
    for (const char* name : manualTextures)
    {
        if (binding) binding->SetTexture(name, NULL);
        if (current && current != binding && !wot::is_stock_effect(current))
            current->SetTexture(name, NULL);
    }
}

bool select(const Moo::EffectMaterialPtr& material, const char* technique)
{
    return material && material->setTechnique(technique) &&
        material->properties().select(*material->pEffect());
}

Vector4 projection(uint width, uint height)
{
    const auto* resolution = stockDynamicResolution();
    const auto state = resolution ? resolution->state() : wot::StockDynamicResolution();
    const auto value = state.project(width, height);
    return Vector4(value.parameters[0], value.parameters[1],
        value.parameters[2], value.parameters[3]);
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
    bool saved() const { return saved_; }
    bool bind(const Moo::RenderTargetPtr& target, DX::Surface* depth = NULL, bool padInput = false)
    {
        ComObjectWrap<DX::Surface> surface;
        if (!saved_ || !target || FAILED(target->pSurface(surface)) ||
            FAILED(Moo::rc().device()->SetDepthStencilSurface(depth)) ||
            FAILED(Moo::rc().setRenderTarget(0, surface))) return false;
        for (uint slot = 1; slot != 4; ++slot)
            if (FAILED(Moo::rc().setRenderTarget(slot, NULL))) return false;
        DX::Viewport viewport = {0, 0, target->width(), target->height(), 0.f, 1.f};
        if (padInput)
        {
            const auto* resolution = stockDynamicResolution();
            const float scale = resolution ? resolution->state().scale() : 1.f;
            viewport.Width = wot::stockTemporalInputDimension(viewport.Width, scale);
            viewport.Height = wot::stockTemporalInputDimension(viewport.Height, scale);
        }
        if (FAILED(setStockSceneViewport(&viewport, true))) return false;
        Moo::rc().screenWidth(target->width());
        Moo::rc().screenHeight(target->height());
        Moo::rc().effectVisualContext().updateSharedConstants(Moo::CONSTANTS_PER_SCREEN);
        return true;
    }
private:
    bool saved_;
};

bool createTarget(Moo::RenderTargetPtr& target, uint width, uint height,
    D3DFORMAT format, const char* name)
{
    return Moo::createRenderTarget(target, width, height, format, name, false, true) &&
        target->pTexture();
}
}

StockTemporalAASupport::StockTemporalAASupport()
    : requestedMode_(wot::stockCustomAAMode(0, true)), renderMode_(requestedMode_)
{
    parameters_.setZero();
    jitter_.setIdentity();
    previousViewProjection_.setIdentity();
    frameViewProjection_.setIdentity();
    for (uint i = 0; i != 2; ++i)
    {
        historyDRR_[i].setZero();
        velocityDRR_[i].setZero();
        scalarDRR_[i].setZero();
    }
}

StockTemporalAASupport::~StockTemporalAASupport()
{
    deleteUnmanagedObjects();
    deleteManagedObjects();
}

void StockTemporalAASupport::configure(const wot::StockCustomAAMode& mode)
{
    // 99ef30 resets the sample index on a real configuration change. Default
    // resources are recreated at a frame boundary, outside any active pass.
    if (requestedMode_.temporalSamples != mode.temporalSamples ||
        requestedMode_.mipMapLodBias != mode.mipMapLodBias ||
        requestedMode_.eightBitHistory != mode.eightBitHistory)
    {
        requestedMode_ = mode;
        frame_.sampleIndex = 0;
        dirty_ = true;
    }
}

bool StockTemporalAASupport::createManagedObjects()
{
    deleteManagedObjects();
    return Moo::createEffect(material_, "shaders/anti_aliasing/taa.fx") &&
        Moo::createEffect(velocityMaterial_, "shaders/post_processing/velocity_buffer.fx");
}

void StockTemporalAASupport::deleteManagedObjects()
{
    clearTextures(material_);
    clearTextures(velocityMaterial_);
    material_ = NULL;
    velocityMaterial_ = NULL;
}

void StockTemporalAASupport::deleteUnmanagedObjects()
{
    frameActive_ = false;
    colourHistoryValid_ = scalarHistoryValid_ = velocityPrepared_ = false;
    clearTextures(material_);
    clearTextures(velocityMaterial_);
    velocitySurface_ = NULL;
    velocity_ = NULL;
    colourCopy_ = NULL;
    for (uint i = 0; i != 2; ++i)
    {
        colourHistory_[i] = NULL;
        velocityHistory_[i] = NULL;
        scalarHistory_[i] = NULL;
    }
    width_ = height_ = 0;
    velocityWrite_ = 1; velocityCurrent_ = 0; velocityPrevious_ = 1;
    scalarCurrent_ = 1; scalarPrevious_ = 0;
    previousViewProjection_.setIdentity();
    dirty_ = true;
}

bool StockTemporalAASupport::prepareTargets()
{
    const auto& size = Moo::rc().backBufferDesc();
    if (size.Width < 2 || size.Height < 2) return false;
    width_ = size.Width; height_ = size.Height;
    const bool temporal = renderMode_.temporalSamples > 1;
    if (temporal)
    {
        // Original formats 2 and 0x24 map to A8R8G8B8 and A16B16G16R16F.
        const D3DFORMAT format = renderMode_.eightBitHistory ? D3DFMT_A8R8G8B8 : D3DFMT_A16B16G16R16F;
        if (!createTarget(colourCopy_, width_, height_, format, "stock/TAA input colour") ||
            !createTarget(velocity_, width_, height_, D3DFMT_G16R16F, "stock/velocity") ||
            FAILED(velocity_->pSurface(velocitySurface_))) return false;
        for (uint i = 0; i != 2; ++i)
            if (!createTarget(colourHistory_[i], width_, height_, format,
                "stock/TAA history")) return false;
    }
    if (temporal || scalarEnabled_)
    {
        for (uint i = 0; i != 2; ++i)
            if (!createTarget(velocityHistory_[i], width_/2, height_/2, D3DFMT_G16R16F,
                "stock/velocity half history")) return false;
    }
    if (scalarEnabled_)
    {
        // DepthBasedEffects constructs ScalarTAASupport(1,2): half resolution,
        // original format 2. It is independent of the main colour history.
        for (uint i = 0; i != 2; ++i)
            if (!createTarget(scalarHistory_[i], width_/2, height_/2, D3DFMT_A8R8G8B8,
                "stock/DBE temporal history")) return false;
    }
    return true;
}

bool StockTemporalAASupport::beginFrame(bool scalarEnabled)
{
    if (frameActive_) return true;
    if (!Moo::rc().device() || !material_ || !velocityMaterial_) return false;
    const auto& size = Moo::rc().backBufferDesc();
    if (dirty_ || width_ != size.Width || height_ != size.Height || scalarEnabled_ != scalarEnabled)
    {
        deleteUnmanagedObjects();
        scalarEnabled_ = scalarEnabled;
        renderMode_ = requestedMode_;
        if (!prepareTargets())
        {
            deleteUnmanagedObjects();
            return false;
        }
        dirty_ = false;
    }
    frameActive_ = true;
    velocityPrepared_ = false;
    return true;
}

bool StockTemporalAASupport::temporalEnabled() const
{
    return frameActive_ && renderMode_.temporalSamples > 1;
}

DX::BaseTexture* StockTemporalAASupport::velocityTexture() const
{
    return temporalEnabled() && velocity_ ? velocity_->pTexture() : NULL;
}

DX::Surface* StockTemporalAASupport::velocitySurface() const
{
    return temporalEnabled() ? velocitySurface_.pComObject() : NULL;
}

bool StockTemporalAASupport::beginOpaque()
{
    if (!frameActive_) return false;
    DX::Viewport viewport;
    if (FAILED(Moo::rc().getViewport(&viewport)) || !viewport.Width || !viewport.Height) return false;
    // 9a00a0 normalises against the actual current viewport, separately from
    // the physical texture allocation used by the nine-tap history kernel.
    const float width=static_cast<float>(viewport.Width),height=static_cast<float>(viewport.Height);
    parameters_ = Vector4(frame_.jitterX/width, frame_.jitterY/height, 1.f/width, 1.f/height);
    jitter_.setTranslate((frame_.jitterX+frame_.jitterX)/width,
        (frame_.jitterY*-2.f)/height, 0.f);
    frameViewProjection_ = Moo::rc().viewProjection();
    // Reuse the public LastViewProjection provider, backed by the actual main
    // view history. Reflection/shadow view changes never advance this owner.
    Moo::rc().lastViewProjection(previousViewProjection_);
    // WorldApp publishes CONSTANTS_ALL before beginOpaqueDraw. The native
    // viewport producer above changes TAAParams and its current velocity SRV
    // afterwards, so refresh their per-frame block as well as view history.
    Moo::rc().effectVisualContext().updateSharedConstants(
        Moo::CONSTANTS_PER_FRAME | Moo::CONSTANTS_PER_VIEW);
    return true;
}

Matrix StockTemporalAASupport::reprojection() const
{
    // 9d6cd0: previous unjittered VP, current jitter, then original clip-to-UV.
    Matrix result = previousViewProjection_;
    if (temporalEnabled()) result.postMultiply(jitter_);
    Matrix clipToUV;
    clipToUV.setIdentity();
    clipToUV(0,0) = .5f; clipToUV(1,1) = -.5f;
    clipToUV(3,0) = .5f; clipToUV(3,1) = .5f;
    result.postMultiply(clipToUV);
    return result;
}

bool StockTemporalAASupport::draw(const Moo::EffectMaterialPtr& material)
{
    return Moo::rc().fsQuad().drawChecked(*material,
        static_cast<uint>(Moo::rc().screenWidth()), static_cast<uint>(Moo::rc().screenHeight()));
}

bool StockTemporalAASupport::prepareVelocity(DX::Surface* depth, bool skipSpeedTree, bool skipFlora)
{
    if (!frameActive_) return false;
    if (!temporalEnabled() && !scalarEnabled_) return true;
    struct Complete
    {
        bool& prepared;
        bool& colourValid;
        bool& scalarValid;
        ~Complete() { if (!prepared) colourValid = scalarValid = false; }
    } complete = {velocityPrepared_, colourHistoryValid_, scalarHistoryValid_};
    const uint current = velocityWrite_;
    if (temporalEnabled())
    {
        TargetScope target;
        if (!target.bind(velocity_, depth) || !select(velocityMaterial_, "fillVelocityBuffer")) return false;
        ID3DXEffect* effect = velocityMaterial_->pEffect()->pEffect();
        if (FAILED(effect->SetBool("g_skipSpeedtree", skipSpeedTree)) ||
            FAILED(effect->SetBool("g_skipFlora", skipFlora)) ||
            !draw(velocityMaterial_)) return false;
    }
    {
        TargetScope target;
        if (!target.bind(velocityHistory_[current]) ||
            !select(velocityMaterial_, temporalEnabled() ? "downsampleVelocity" : "approximateVelocity")) return false;
        ID3DXEffect* effect = velocityMaterial_->pEffect()->pEffect();
        if (FAILED(effect->SetInt("g_shaderIndex", 0)) ||
            !draw(velocityMaterial_)) return false;
    }
    velocityDRR_[current] = projection(width_/2, height_/2);
    velocityPrevious_ = velocityCurrent_;
    velocityCurrent_ = current;
    velocityWrite_ = 1-current;
    velocityPrepared_ = true;
    return true;
}

bool StockTemporalAASupport::setKernel(ID3DXEffect* effect, uint width, uint height)
{
    const auto kernel = wot::stockTemporalAAKernel(static_cast<float>(width),
        static_cast<float>(height), frame_.jitterX, frame_.jitterY);
    return SUCCEEDED(effect->SetFloatArray("g_neighborhoodSampleOffsets", kernel.offsets.data(), 20)) &&
        SUCCEEDED(effect->SetFloatArray("g_neighborhoodSampleWeights", kernel.weights.data(), 12)) &&
        SUCCEEDED(effect->SetFloatArray("g_neighborhoodLowPassKernelWeights", kernel.lowPassWeights.data(), 12));
}

bool StockTemporalAASupport::resolveColour(const Moo::RenderTargetPtr& source)
{
    const bool valid = colourHistoryValid_;
    colourHistoryValid_ = false;
    const uint current = frame_.currentHistory, previous = frame_.previousHistory;
    if (!source || !velocityPrepared_) return false;
    if (!valid)
    {
        ComObjectWrap<DX::Surface> surface;
        if (FAILED(source->pSurface(surface)) || !Moo::copySurface(colourHistory_[current].getObject(), surface)) return false;
    }
    else
    {
        TargetScope target;
        if (!target.bind(colourHistory_[current]) || !select(material_, "taaColor")) return false;
        ID3DXEffect* effect = material_->pEffect()->pEffect();
        if (!setKernel(effect, source->width(), source->height()) ||
            FAILED(effect->SetVector("g_historyDRRParams", &historyDRR_[previous])) ||
            FAILED(effect->SetVector("g_prevDownsampledVelocityDRRParams", &velocityDRR_[velocityPrevious_])) ||
            FAILED(effect->SetInt("g_debugMode", 0)) ||
            FAILED(effect->SetBool("g_lowQuality", renderMode_.eightBitHistory)) ||
            FAILED(effect->SetTexture("g_srcBuffer", source->pTexture())) ||
            FAILED(effect->SetTexture("g_historyBuffer", colourHistory_[previous]->pTexture())) ||
            FAILED(effect->SetTexture("g_prevDownsampledVelocityBuffer", velocityHistory_[velocityPrevious_]->pTexture())) ||
            !draw(material_)) return false;
    }
    historyDRR_[current] = projection(width_, height_);
    colourHistoryValid_ = true;
    return true;
}

bool StockTemporalAASupport::copyResult(const Moo::RenderTargetPtr& destination,
    DX::BaseTexture* averageLuminance, float middleGray, float whitePoint)
{
    // A null destination is the existing LDR scene render target. HDR resolves
    // bind the actual HDR source again, matching 99f90e before copyResult.
    TargetScope target;
    if (!target.saved()) return false;
    if (destination && !target.bind(destination)) return false;
    if (!select(material_, "copyResult")) return false;
    ID3DXEffect* effect = material_->pEffect()->pEffect();
    if (averageLuminance)
    {
        const Vector4 toneMapper(middleGray, whitePoint, 0.f, 0.f);
        if (FAILED(effect->SetVector("g_toneMapperParams", &toneMapper)) ||
            FAILED(effect->SetTexture("g_avgLumBuffer", averageLuminance))) return false;
    }
    return SUCCEEDED(effect->SetBool("g_lowQuality", averageLuminance == NULL)) &&
        SUCCEEDED(effect->SetTexture("g_srcBuffer", colourHistory_[frame_.currentHistory]->pTexture())) &&
        draw(material_);
}

bool StockTemporalAASupport::resolveHDR(const Moo::RenderTargetPtr& source,
    DX::BaseTexture* averageLuminance, float middleGray, float whitePoint)
{
    if (!temporalEnabled() || renderMode_.eightBitHistory) return true;
    if (!source || !averageLuminance) return false;
    {
        TargetScope target;
        if (!target.bind(colourCopy_, NULL, true) || !select(material_, "fillInputColor")) return false;
        ID3DXEffect* effect = material_->pEffect()->pEffect();
        const Vector4 toneMapper(middleGray, whitePoint, 0.f, 0.f);
        if (FAILED(effect->SetVector("g_toneMapperParams", &toneMapper)) ||
            FAILED(effect->SetTexture("g_avgLumBuffer", averageLuminance)) ||
            FAILED(effect->SetTexture("g_srcBuffer", source->pTexture())) ||
            !draw(material_)) return false;
    }
    return resolveColour(colourCopy_) && copyResult(source, averageLuminance, middleGray, whitePoint);
}

bool StockTemporalAASupport::resolveLDR()
{
    if (!temporalEnabled() || !renderMode_.eightBitHistory) return true;
    const bool valid = colourHistoryValid_;
    if (!Moo::copyBackBuffer(colourCopy_.getObject()) || !resolveColour(colourCopy_)) return false;
    // 99f990 leaves the first displayed frame unchanged after seeding history.
    return !valid || copyResult(Moo::RenderTargetPtr(), NULL, 0.f, 0.f);
}

Moo::RenderTargetPtr StockTemporalAASupport::resolveScalar(
    const Moo::RenderTargetPtr& source, DX::Surface* depth, bool opaqueOnly)
{
    if (!scalarEnabled()) return source;
    const bool valid = scalarHistoryValid_;
    scalarHistoryValid_ = false;
    const uint current = scalarCurrent_, previous = scalarPrevious_;
    if (!source || !velocityPrepared_ || (opaqueOnly && !depth)) return Moo::RenderTargetPtr();
    if (!valid)
    {
        ComObjectWrap<DX::Surface> surface;
        if (FAILED(source->pSurface(surface)) || !Moo::copySurface(scalarHistory_[current].getObject(), surface)) return Moo::RenderTargetPtr();
    }
    else
    {
        TargetScope target;
        if (!target.bind(scalarHistory_[current], depth) || !select(material_, "taaScalar")) return Moo::RenderTargetPtr();
        ID3DXEffect* effect = material_->pEffect()->pEffect();
        if (!setKernel(effect, source->width(), source->height()) ||
            FAILED(effect->SetBool("g_opaqueOnly", opaqueOnly)) ||
            FAILED(effect->SetInt("g_scalarDownsampleLevel", 1)) ||
            FAILED(effect->SetVector("g_historyDRRParams", &scalarDRR_[previous])) ||
            FAILED(effect->SetVector("g_downsampledVelocityDRRParams", &velocityDRR_[velocityCurrent_])) ||
            FAILED(effect->SetVector("g_prevDownsampledVelocityDRRParams", &velocityDRR_[velocityPrevious_])) ||
            FAILED(effect->SetTexture("g_srcBuffer", source->pTexture())) ||
            FAILED(effect->SetTexture("g_historyBuffer", scalarHistory_[previous]->pTexture())) ||
            FAILED(effect->SetTexture("g_downsampledVelocityBuffer", velocityHistory_[velocityCurrent_]->pTexture())) ||
            FAILED(effect->SetTexture("g_prevDownsampledVelocityBuffer", velocityHistory_[velocityPrevious_]->pTexture())) ||
            !draw(material_)) return Moo::RenderTargetPtr();
    }
    scalarDRR_[current] = projection(source->width(), source->height());
    scalarHistoryValid_ = true;
    scalarPrevious_ = current;
    scalarCurrent_ = 1-current;
    return scalarHistory_[current];
}

void StockTemporalAASupport::endFrame()
{
    if (!frameActive_) return;
    if (velocityPrepared_) previousViewProjection_ = frameViewProjection_;
    frame_.advance(renderMode_.temporalSamples, true, false);
    frameActive_ = false;
}

BW_END_NAMESPACE
