#ifndef WOT_STOCK_DEFERRED_LIGHTING_PORTABLE
#include "pch.hpp"
#include "stock_deferred_lighting.hpp"
#include "moo/effect_visual_context.hpp"
#include "moo/render_context.hpp"
#include "moo/texture_manager.hpp"
#include "resmgr/bwresource.hpp"
#include "resmgr/auto_config.hpp"
#endif
#include <cmath>

BW_BEGIN_NAMESPACE
void packStockDeferredSphericalHarmonics( const Vector3 (&source)[9], float (&packed)[28] )
{
    // #1513 PBSLightingSupport::load (a750cb..a75258) packs each colour's
    // quadratic terms, followed by the RGB sh8 term. These are already baked
    // SH coefficients; no guessed basis, normalisation or ambient scaling.
    for (unsigned colour = 0; colour < 3; ++colour)
    {
        packed[colour * 4] = source[0][colour] - source[6][colour];
        packed[colour * 4 + 1] = source[1][colour];
        packed[colour * 4 + 2] = source[2][colour];
        packed[colour * 4 + 3] = source[3][colour];
        packed[12 + colour * 4] = source[4][colour];
        packed[13 + colour * 4] = source[5][colour];
        packed[14 + colour * 4] = source[6][colour] * 3.f;
        packed[15 + colour * 4] = source[7][colour];
        packed[24 + colour] = source[8][colour];
    }
    packed[27] = 0.f;
}

void scaleStockForwardSphericalHarmonics( const float (&source)[28], const Vector3 & ambient,
    float multiplier, float (&packed)[28] )
{
    // a746f0/a7472e: forward scales only the three linear RGB vectors by
    // the current SunLight ambient colour and the authored SH multiplier.
    for (unsigned i = 0; i < 28; ++i) packed[i] = source[i];
    for (unsigned colour = 0; colour < 3; ++colour)
    {
        const float factor = ambient[colour] * multiplier;
        for (unsigned component = 0; component < 4; ++component)
            packed[colour * 4 + component] = source[colour * 4 + component] * factor;
    }
}

#ifndef WOT_STOCK_DEFERRED_LIGHTING_CORE_ONLY
namespace
{
StockDeferredLightingSettings activeSettings;
Moo::BaseTexturePtr brdfLut;
bool constantsRegistered = false;
bool sphericalHarmonicsRegistered = false;
// 5c8990 constructs exactly this AutoConfigString. a74bc0 uses its value
// only when the environment's authored prefiltered map cannot be loaded.
AutoConfigString defaultPmrem("system/PMREMTexture");

Moo::BaseTexturePtr lightingTexture(const BW::string& resource)
{
    Moo::BaseTexturePtr texture = !resource.empty() && BWResource::fileExists(resource) ?
        Moo::TextureManager::instance()->get(resource, true, false, true, "Art/File Textures/") : NULL;
    return texture && texture->pTexture() && texture->width() &&
        texture->status() == Moo::BaseTexture::STATUS_READY ? texture : NULL;
}

class SphericalHarmonicsConstant : public Moo::EffectConstantValue
{
public:
    virtual bool operator()( ID3DXEffect * effect, D3DXHANDLE handle )
    {
        // An absent environment is not a black ambient-lighting result.
        if (!activeSettings.loaded) return false;
        float packed[28];
        const float * values = activeSettings.sphericalHarmonics;
        if (activeSettings.forward)
        {
            const auto & colour = Moo::rc().effectVisualContext().sunLight().m_ambient;
            scaleStockForwardSphericalHarmonics(activeSettings.sphericalHarmonics,
                Vector3(colour.r, colour.g, colour.b), activeSettings.forwardMultiplier, packed);
            values = packed;
        }
        return SUCCEEDED(effect->SetValue(handle, values, sizeof(activeSettings.sphericalHarmonics)));
    }
};

class PbsResourcesReadyConstant : public Moo::EffectConstantValue
{
public:
    virtual bool operator()(ID3DXEffect* effect, D3DXHANDLE handle)
    {
        // #1513 9b0240 reads the active space's PBSReady flag. Shader defaults
        // are true and cannot describe legacy spaces that omit that field.
        return SUCCEEDED(effect->SetBool(handle, activeSettings.pbsResourcesReady));
    }
};

class BrdfLutConstant : public Moo::EffectConstantValue
{
public:
    virtual bool operator()( ID3DXEffect * effect, D3DXHANDLE handle )
    {
        return brdfLut && brdfLut->pTexture() && SUCCEEDED( effect->SetTexture( handle, brdfLut->pTexture() ) );
    }
};

class PmremConstant : public Moo::EffectConstantValue
{
public:
    virtual bool operator()(ID3DXEffect* effect, D3DXHANDLE handle)
    {
        return activeSettings.loaded && activeSettings.pmrem && activeSettings.pmrem->pTexture() &&
            SUCCEEDED(effect->SetTexture(handle, activeSettings.pmrem->pTexture()));
    }
};

class PmremMipsConstant : public Moo::EffectConstantValue
{
public:
    virtual bool operator()(ID3DXEffect* effect, D3DXHANDLE handle)
    {
        return activeSettings.loaded && activeSettings.pmrem && SUCCEEDED(
            effect->SetValue(handle, activeSettings.pmremMips, sizeof(activeSettings.pmremMips)));
    }
};
}

bool loadStockDeferredLightingSettings( DataSectionPtr environment, StockDeferredLightingSettings & settings,
    bool forward )
{
    settings.loaded = false;
    settings.pmrem = NULL;
    settings.forward = forward;
    settings.forwardMultiplier = 1.f;
    if (!environment)
    {
        ERROR_MSG( "Deferred lighting: the original environment resource is unavailable\n" );
        return false;
    }
    // a82950 reads the PBS child; a74bc0 selects the deferred SH resource.
    // a827a0's original default path is empty, with no fabricated black SH.
    const BW::string resource = environment->readString(
        forward ? "PBS/FORWARD/REM_SH" : "PBS/DEFERRED/REM_SH" );
    DataSectionPtr source = resource.empty() ? NULL : BWResource::openSection( resource );
    if (!source)
    {
        ERROR_MSG( "Environment lighting: cannot load original %s SH '%s'\n",
            forward ? "forward" : "deferred", resource.c_str() );
        return false;
    }
    Vector3 coefficients[9];
    const char * const names[] = { "sh0", "sh1", "sh2", "sh3", "sh4", "sh5", "sh6", "sh7", "sh8" };
    for (unsigned i = 0; i < 9; ++i)
        coefficients[i] = source->readVector3( names[i], Vector3( 0.f, 0.f, 0.f ) );
    // The original reader reads then discards max_lum (a750c6/fstp).
    source->readFloat( "max_lum", 1.f );
    packStockDeferredSphericalHarmonics( coefficients, settings.sphericalHarmonics );
    if (forward)
    {
        // a82950 reads this independently; a746f0 samples it at consumption.
        settings.forwardMultiplier = environment->readFloat("PBS/FORWARD/SH_multiplier", 1.f);
        settings.loaded = true;
        return true;
    }
    // a82950/a74bc0: this is an already-prefiltered authored DDS, not a
    // generated cube or an alias for an unrelated environment texture.
    settings.pmremResource = environment->readString("PBS/DEFERRED/PMREM");
    settings.pmrem = lightingTexture(settings.pmremResource);
    if (!settings.pmrem)
    {
        settings.pmremResource = defaultPmrem.value();
        settings.pmrem = lightingTexture(settings.pmremResource);
    }
    if (!settings.pmrem)
    {
        ERROR_MSG("Deferred lighting: neither authored nor configured fallback PMREM is available\n");
        return false;
    }
    // Preserve a74bc0's binary32 log/multiply boundary before ceil. a74650
    // publishes (7, computedMipCount-7, non-A8R8G8B8, 0).
    const float logarithm = std::log(float(settings.pmrem->width()));
    volatile float projected = logarithm * 1.442695f;
    settings.pmremMips[0] = 7.f;
    settings.pmremMips[1] = float(std::ceil(double(projected))) - 9.f;
    settings.pmremMips[2] = settings.pmrem->format() != D3DFMT_A8R8G8B8 ? 1.f : 0.f;
    settings.pmremMips[3] = 0.f;
    settings.loaded = true;
    return true;
}

void activateStockDeferredLightingSettings( const StockDeferredLightingSettings & settings )
{
    activeSettings = settings;
}

void deactivateStockDeferredLightingSettings()
{
    activeSettings = StockDeferredLightingSettings();
}

bool initialiseStockDeferredLightingConstants()
{
    // a748e0 loads this exact resource through the texture manager. It is a
    // lookup texture, not an environment cube or an image generated at runtime.
    const char * resource = "system/maps/env_brdf_lut.dds";
    brdfLut = BWResource::fileExists( resource ) ? Moo::TextureManager::instance()->get(
        resource, true, false, true, "Art/File Textures/" ) : NULL;
    // Do not accept TextureManager's optional missing-texture placeholder as
    // a physical BRDF. The real input must have loaded successfully.
    if (!brdfLut || !brdfLut->pTexture() || brdfLut->status() != Moo::BaseTexture::STATUS_READY)
    {
        ERROR_MSG( "Deferred lighting: the original environment BRDF lookup texture is unavailable\n" );
        return false;
    }
    if (!constantsRegistered)
    {
        initialiseStockForwardLightingConstants();
        Moo::rc().effectVisualContext().registerAutoConstant(
            Moo::EffectVisualContext::AUTO_CONSTANT_TYPE_PER_FRAME, "EnvBRDFLut", new BrdfLutConstant );
        Moo::rc().effectVisualContext().registerAutoConstant(
            Moo::EffectVisualContext::AUTO_CONSTANT_TYPE_PER_FRAME, "PMREM", new PmremConstant );
        Moo::rc().effectVisualContext().registerAutoConstant(
            Moo::EffectVisualContext::AUTO_CONSTANT_TYPE_PER_FRAME, "PMREMMipsNumber", new PmremMipsConstant );
        constantsRegistered = true;
    }
    // SpeedTreeMaterials belongs to the actual SpeedTree renderer/type pool.
    // Its public producer is not replaced by an unpopulated synthetic texture.
    return true;
}

bool initialiseStockForwardLightingConstants()
{
    if (!sphericalHarmonicsRegistered)
    {
        Moo::rc().effectVisualContext().registerAutoConstant(
            Moo::EffectVisualContext::AUTO_CONSTANT_TYPE_PER_FRAME, "REM_SH9", new SphericalHarmonicsConstant);
        Moo::rc().effectVisualContext().registerAutoConstant(
            Moo::EffectVisualContext::AUTO_CONSTANT_TYPE_PER_FRAME, "PBSResourcesReady", new PbsResourcesReadyConstant);
        sphericalHarmonicsRegistered = true;
    }
    return true;
}

void finaliseStockDeferredLightingConstants()
{
    if (sphericalHarmonicsRegistered)
    {
        Moo::rc().effectVisualContext().unregisterAutoConstant(
            Moo::EffectVisualContext::AUTO_CONSTANT_TYPE_PER_FRAME, "REM_SH9" );
        Moo::rc().effectVisualContext().unregisterAutoConstant(
            Moo::EffectVisualContext::AUTO_CONSTANT_TYPE_PER_FRAME, "PBSResourcesReady" );
        sphericalHarmonicsRegistered = false;
    }
    if (constantsRegistered)
    {
        Moo::rc().effectVisualContext().unregisterAutoConstant(
            Moo::EffectVisualContext::AUTO_CONSTANT_TYPE_PER_FRAME, "EnvBRDFLut" );
        Moo::rc().effectVisualContext().unregisterAutoConstant(
            Moo::EffectVisualContext::AUTO_CONSTANT_TYPE_PER_FRAME, "PMREM" );
        Moo::rc().effectVisualContext().unregisterAutoConstant(
            Moo::EffectVisualContext::AUTO_CONSTANT_TYPE_PER_FRAME, "PMREMMipsNumber" );
        constantsRegistered = false;
    }
    brdfLut = NULL;
}
#endif
BW_END_NAMESPACE
