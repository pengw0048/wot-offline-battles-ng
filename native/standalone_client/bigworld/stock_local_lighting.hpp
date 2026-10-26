#pragma once

#include "moo/device_callback.hpp"
#include "moo/light_container.hpp"
#include <memory>

BW_BEGIN_NAMESPACE
// #1513 has separate SUN/OMNI/SPOT effects and a real per-frame local-shadow
// cache. The SUN owner remains on LightsManager; this owner supplies the latter
// two effects, their volume geometry and VSM render targets.
class StockLocalLighting
{
public:
    StockLocalLighting();
    virtual ~StockLocalLighting();
    bool initialise();
    void tick();
    void gatherVisibleLights(const Moo::LightContainerPtr& lights);
    bool prepare();
    bool drawOmnis();
    bool drawSpots();
    virtual void createManagedObjects();
    virtual void deleteManagedObjects();
    virtual void createUnmanagedObjects();
    virtual void deleteUnmanagedObjects();
    virtual bool recreateForD3DExDevice() const { return true; }
private:
    struct State;
    std::unique_ptr<State> state_;
};
BW_END_NAMESPACE
