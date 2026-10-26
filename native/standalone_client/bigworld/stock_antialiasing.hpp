#pragma once

#include "moo/effect_material.hpp"
#include "moo/render_target.hpp"

BW_BEGIN_NAMESPACE
extern int StockAntialiasing_token;
bool initialiseStockAntialiasing();
void finaliseStockAntialiasing();

// #1513 9c8940/9c8a50. The native FXAA mode is one luminance pass followed by
// one FXAA pass; it is independent of the public engine's LQ/MQ/HQ variants.
class StockFXAASupport
{
public:
    StockFXAASupport();
    ~StockFXAASupport();
    void enable(bool enabled) { enabled_ = enabled; }
    bool enabled() const { return enabled_; }
    bool apply();
    bool createManagedObjects();
    void deleteManagedObjects();
    void deleteUnmanagedObjects();
private:
    bool prepareTargets();
    Moo::EffectMaterialPtr material_, luminanceMaterial_;
    Moo::RenderTargetPtr copy_, luminance_;
    bool enabled_;
};

BW_END_NAMESPACE
