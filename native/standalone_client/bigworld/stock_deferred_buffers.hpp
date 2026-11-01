#pragma once

#include "wot/stock_deferred_buffers.hpp"
#include "moo/device_callback.hpp"
#include "moo/effect_material.hpp"
#include "moo/render_target.hpp"
#include "moo/com_object_wrap.hpp"
#include "resmgr/datasection.hpp"
#include "stock_antialiasing.hpp"
#include "stock_temporal_antialiasing.hpp"

BW_BEGIN_NAMESPACE
class IRendererPipeline;

void loadStockDeferredMaterialSettings(DataSectionPtr source,
    wot::StockDeferredMaterialSettings& settings);
void activateStockDeferredMaterialSettings(const wot::StockDeferredMaterialSettings& settings);
wot::StockDeferredMaterialSettings stockDeferredMaterialSettings();
void loadStockScreenSpaceShadowSettings(DataSectionPtr source,wot::StockScreenSpaceShadowSettings& settings);
void activateStockScreenSpaceShadowSettings(const wot::StockScreenSpaceShadowSettings& settings);
bool setStockDeferredDecalConversion(ID3DXEffect* effect);
bool bindStockDeferredLegacyDecalInputs(ID3DXEffect* effect);
void clearStockDeferredLegacyDecalInputs(const Moo::EffectMaterialPtr& material);
bool setStockDeferredTextureScale(ID3DXEffect* effect,const char* parameter,uint width,uint height);
bool drawStockDeferredSunLight(const Moo::EffectMaterialPtr& material);
// During a deferred frame the scene depth replaces the main depth buffer that
// reuseZ render targets captured at creation.
DX::Surface* stockFrameDepthSurface(DX::Surface* depth);
bool finishStockAntialiasingScene();
bool resolveStockTemporalHDR(const Moo::RenderTargetPtr& source,
    DX::BaseTexture* averageLuminance, float middleGray, float whitePoint);

// One owner belongs to the actual deferred pipeline. Its depth texture and
// linear-depth semantic are shared by scene reconstruction and decal draws.
class StockDeferredBuffers : public Moo::DeviceCallback
{
public:
    explicit StockDeferredBuffers(IRendererPipeline& pipeline);
    ~StockDeferredBuffers();
    bool initialise();
    bool begin();
    bool beginOpaqueVelocity();
    bool prepare();
    bool drawSpaceDecals();
    bool drawSpaceDecalsAO();
    bool end();
    bool ready() const;
    void setLightingQuality(int option) { lightingSelection_.quality=option;depthQuality_.lighting=option; }
    void setShadowQuality(int option) { depthQuality_.shadows=option; }
    int lightingQuality() const { return lightingSelection_.quality; }
    int shadowQuality() const { return depthQuality_.shadows; }
    int transmissionType() { return lightingSelection_.transmissionType(); }
    bool prepared() const { return prepared_; }
    DX::Texture* linearDepth() const;
    DX::BaseTexture* depthEffectsTexture() const;
    bool ambientOcclusionEnabled() const;
    bool contactShadowsEnabled() const;
    bool depthEffectsHalfResolution() const { return depthQuality_.directHalfResolution(); }
    uint32 customAAMode() const { return customAAMode_; }
    void customAAMode(uint32 mode);
    bool applyAntialiasing();
    StockTemporalAASupport& temporalAA() { return temporalAA_; }
    const StockTemporalAASupport& temporalAA() const { return temporalAA_; }
    DX::BaseTexture* objectKindCopy();
    virtual void createManagedObjects();
    virtual void deleteManagedObjects();
    virtual void createUnmanagedObjects();
    virtual void deleteUnmanagedObjects();
    virtual bool recreateForD3DExDevice() const { return true; }
private:
    bool select(const Moo::EffectMaterialPtr& material,const char* technique);
    bool draw(const Moo::EffectMaterialPtr& material);
    bool convertMaterials();
    bool fillLinearDepth();
    bool fillDepthEffects();
    bool blurDepthEffects(const Moo::RenderTargetPtr& input,const Moo::RenderTargetPtr& scratch,
        bool bilateral,bool lowQuality,bool opaqueOnly,float scale,float sharpness);
    IRendererPipeline& pipeline_;
    wot::StockDeferredLightingSelection lightingSelection_;
    wot::StockDepthEffectQuality depthQuality_;
    ComObjectWrap<DX::Texture> hardwareDepth_;
    ComObjectWrap<DX::Surface> depthSurface_;
    ComObjectWrap<DX::Surface> previousDepth_;
    ComObjectWrap<DX::Texture> linearDepth_;
    ComObjectWrap<DX::Surface> linearDepthSurface_;
    ComObjectWrap<DX::Surface> reducedDepthMip_;
    Moo::RenderTargetPtr reducedDepth_;
    Moo::RenderTargetPtr converted_[2];
    Moo::RenderTargetPtr kindCopy_;
    Moo::RenderTargetPtr lowPrecisionDepth_;
    Moo::RenderTargetPtr depthEffectsHalf_;
    Moo::RenderTargetPtr depthEffectsFull_;
    Moo::RenderTargetPtr depthEffectsScratch_;
    Moo::RenderTargetPtr depthEffectsHalfScratch_;
    Moo::EffectMaterialPtr depthMaterial_;
    Moo::EffectMaterialPtr conversionMaterial_;
    Moo::EffectMaterialPtr kindCopyMaterial_;
    Moo::EffectMaterialPtr aoMaterial_;
    Moo::EffectMaterialPtr contactShadowMaterial_;
    Moo::EffectMaterialPtr bilateralUpscaleMaterial_;
    Moo::EffectMaterialPtr bilateralBlurMaterial_;
    Moo::BaseTexturePtr aoNoise_,horizonNoise_;
    bool constantsRegistered_=false;
    friend DX::Surface* stockFrameDepthSurface(DX::Surface* depth);
    unsigned allocationRetryFrames_=0;
    bool frameActive_=false;
    bool prepared_=false;
    bool kindCopied_=false;
    StockFXAASupport fxaa_;
    StockTemporalAASupport temporalAA_;
    uint32 customAAMode_=0;
    bool aaMaterialsReady_=false;
};
StockDeferredBuffers* stockDeferredBuffers();
BW_END_NAMESPACE
