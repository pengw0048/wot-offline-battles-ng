#pragma once

#include "wot/stock_antialiasing.hpp"
#include "moo/effect_material.hpp"
#include "moo/render_target.hpp"
#include "moo/com_object_wrap.hpp"
#include "math/matrix.hpp"
#include "math/vector4.hpp"

BW_BEGIN_NAMESPACE

// Native TemporalAASupport + VelocityBuffer. The owning deferred pipeline
// forwards its managed/default-pool device lifetime to this service.
class StockTemporalAASupport
{
public:
    StockTemporalAASupport();
    ~StockTemporalAASupport();
    void configure(const wot::StockCustomAAMode& mode);
    bool createManagedObjects();
    void deleteManagedObjects();
    void deleteUnmanagedObjects();
    bool beginFrame(bool scalarEnabled);
    void cancelFrame() { frameActive_=false; velocityPrepared_=false; }
    bool beginOpaque();
    bool prepareVelocity(DX::Surface* depth, bool skipSpeedTree, bool skipFlora);
    bool resolveHDR(const Moo::RenderTargetPtr& source, DX::BaseTexture* averageLuminance,
        float middleGray, float whitePoint);
    bool resolveLDR();
    Moo::RenderTargetPtr resolveScalar(const Moo::RenderTargetPtr& source,
        DX::Surface* depth, bool opaqueOnly);
    void endFrame();
    bool temporalEnabled() const;
    bool scalarEnabled() const { return frameActive_ && scalarEnabled_; }
    unsigned mipMapLodBias() const { return renderMode_.mipMapLodBias; }
    DX::BaseTexture* velocityTexture() const;
    DX::Surface* velocitySurface() const;
    const Vector4& parameters() const { return parameters_; }
    const Matrix& jitter() const { return jitter_; }
    const Matrix& previousViewProjection() const { return previousViewProjection_; }
    Matrix reprojection() const;
private:
    bool prepareTargets();
    bool resolveColour(const Moo::RenderTargetPtr& source);
    bool copyResult(const Moo::RenderTargetPtr& destination, DX::BaseTexture* averageLuminance,
        float middleGray, float whitePoint);
    bool draw(const Moo::EffectMaterialPtr& material);
    bool setKernel(ID3DXEffect* effect, uint width, uint height);
    wot::StockCustomAAMode requestedMode_, renderMode_;
    wot::StockTemporalAAFrame frame_;
    Moo::EffectMaterialPtr material_, velocityMaterial_;
    Moo::RenderTargetPtr colourCopy_, colourHistory_[2];
    Moo::RenderTargetPtr velocity_, velocityHistory_[2], scalarHistory_[2];
    ComObjectWrap<DX::Surface> velocitySurface_;
    Vector4 parameters_, historyDRR_[2], velocityDRR_[2], scalarDRR_[2];
    Matrix jitter_, previousViewProjection_, frameViewProjection_;
    uint width_=0, height_=0;
    uint velocityWrite_=1, velocityCurrent_=0, velocityPrevious_=1;
    uint scalarCurrent_=1, scalarPrevious_=0;
    bool dirty_=true, frameActive_=false, scalarEnabled_=false;
    bool colourHistoryValid_=false, scalarHistoryValid_=false, velocityPrepared_=false;
};

BW_END_NAMESPACE
