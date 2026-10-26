#include "pch.hpp"
#include "stock_graphics_initialization.hpp"
#include "stock_vehicle_fashions.hpp"
#include "stock_startup_services.hpp"
#include "stock_vehicle_traces.hpp"
#include "stock_dynamic_resolution.hpp"
#include "stock_particle_quality.hpp"
#include "stock_tps_quality.hpp"
#include "stock_tps_scene.hpp"
#include "stock_graphics_settings_consumers.hpp"
#include "stock_graphics_presets.hpp"

#include "romp/hdr_support.hpp"

BW_BEGIN_NAMESPACE
void initialiseStockGraphicsConfiguration()
{
    // 009725f9 -> 00a6e5c0(1) writes the real renderer-wide enable flag,
    // including when the current forward pipeline has no HDRSupport object.
    HDRSupport::setGlobalEnabled( true );
    initialiseStockVehicleCamouflageConfig();
}

bool initialiseStockCustomGraphicsSettings()
{
    if (!initialiseStockParticleQualityProfiles()) return false;
    // 00971070 registers in this order. Registration preserves each owner's
    // cold state; preference application remains with GraphicsSetting.
    if (!initialiseStockVehicleDustSetting() ||
        !initialiseStockVehicleTracesSetting() ||
        !initialiseStockDynamicResolutionSetting() ||
        !initialiseStockSmokeSetting() ||
        !initialiseStockParticleQualitySetting( true ) ||
        !initialiseStockTPSPerformerSetting() ||
        !initialiseStockParticleQualitySetting( false )) return false;
    HDRSupport::setGlobalEnabled( true );
    if (!initialiseStockSniperFloraSetting() ||
        !initialiseStockSniperSwingSetting() ||
        !initialiseStockGraphicsPresets()) return false;
    initialiseStockVehicleCamouflageConfig();
    return initialiseStockTPSRuntime();
}

static PyObject * py_wg_initCustomSettings( PyObject * args )
{
    if (!PyArg_ParseTuple( args, ":wg_initCustomSettings" )) return NULL;
    if (!initialiseStockCustomGraphicsSettings())
    {
        if (!PyErr_Occurred()) PyErr_SetString( PyExc_RuntimeError,
            "Original custom graphics settings initialization failed" );
        return NULL;
    }
    Py_RETURN_NONE;
}
PY_MODULE_FUNCTION( wg_initCustomSettings, BigWorld )
BW_END_NAMESPACE
