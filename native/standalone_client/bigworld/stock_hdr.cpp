#include "pch.hpp"
#include "stock_hdr.hpp"
#include "stock_dynamic_resolution.hpp"
#include "wot/stock_hdr_tonemapping.hpp"

#include "moo/fullscreen_quad.hpp"
#include "moo/render_context.hpp"
#include "moo/texture_manager.hpp"
#include "romp/hdr_settings.hpp"

BW_BEGIN_NAMESPACE
namespace
{
HDRSettings::ToneMapping readToneMapping(const DataSectionPtr& section)
{
    // a6f330 restores the original defaults before reading each profile.
    HDRSettings::ToneMapping profile;
    if (!section) return profile;
    profile.m_eyeDarkLimit = section->readFloat("eyeDarkLimit", profile.m_eyeDarkLimit);
    profile.m_eyeLightLimit = section->readFloat("eyeLightLimit", profile.m_eyeLightLimit);
    profile.m_middleGray = section->readFloat("middleGray", profile.m_middleGray);
    profile.m_whitePoint = section->readFloat("whitePoint", profile.m_whitePoint);
    profile.m_blackPoint = section->readFloat("blackPoint", profile.m_blackPoint);
    return profile;
}

bool setOptionalVector(ID3DXEffect* effect, const char* name, const Vector4& value)
{
    // The DX9 binary may optimise a native write out of its layout.
    D3DXHANDLE handle = effect->GetParameterByName(NULL, name);
    return !handle || SUCCEEDED(effect->SetVector(handle, &value));
}
}

bool setStockHdrTarget(ID3DXEffect* effect, const char* name, Moo::RenderTarget* target)
{
    // 998e30: (1,1,0,0) unless dynamic resolution scales the target's texture.
    Vector4 projection(1.f, 1.f, 0.f, 0.f);
    if (target && target->pTexture())
    {
        const auto* resolution = stockDynamicResolution();
        const auto value = resolution ? resolution->state().project(target->width(), target->height()) :
            wot::StockDynamicResolution().project(target->width(), target->height());
        projection.set(value.parameters[0], value.parameters[1], value.parameters[2], value.parameters[3]);
    }
    const BW::string drr = BW::string(name) + "DRRParams";
    return SUCCEEDED(effect->SetTexture(name, target ? target->pTexture() : NULL)) &&
        SUCCEEDED(effect->SetVector(drr.c_str(), &projection));
}

void loadStockToneMappings(const DataSectionPtr& hdr, HDRSettings& settings)
{
    if (DataSectionPtr list = hdr->openSection("tonemappings"))
    {
        settings.m_activeToneMapping = list->readInt("active", 1);
        BW::vector<DataSectionPtr> profiles;
        list->openSections("tonemapping", profiles);
        for (size_t i = 0; i < profiles.size() && i < 3; ++i)
            settings.m_tonemappings[i] = readToneMapping(profiles[i]);
    }
    else
    {
        settings.m_activeToneMapping = 1;
        const HDRSettings::ToneMapping profile = readToneMapping(hdr->openSection("tonemapping"));
        for (auto& entry : settings.m_tonemappings) entry = profile;
    }
    settings.m_tonemapping = settings.m_tonemappings[stockActiveToneMapping(settings)];
}

int stockActiveToneMapping(const HDRSettings& settings)
{
    return wot::stock_active_tonemapping(settings.m_activeToneMapping);
}

Moo::BaseTexturePtr loadStockColorCorrection(const BW::string& resource)
{
    return resource.empty() ? NULL : Moo::TextureManager::instance()->get(
        resource, true, true, true, "Art/File Textures/");
}

bool drawStockToneMapping(const Moo::EffectMaterialPtr& material, const HDRSettings& settings,
    Moo::RenderTarget* averageLuminance, Moo::RenderTarget* hdr,
    Moo::RenderTarget* bloom, const Moo::BaseTexturePtr& colorCorrection)
{
    if (!material || !material->pEffect() || !averageLuminance || !hdr || !bloom ||
        !colorCorrection || !colorCorrection->pTexture() ||
        !material->properties().select(*material->pEffect())) return false;
    ID3DXEffect* effect = material->pEffect()->pEffect();
    const int active = stockActiveToneMapping(settings);
    const HDRSettings::ToneMapping& source = settings.m_tonemappings[active];
    wot::StockToneMappingProfile profile;
    profile.middleGray = source.m_middleGray;
    profile.whitePoint = source.m_whitePoint;
    profile.blackPoint = source.m_blackPoint;
    const auto params = wot::stock_tonemapping_params(profile);
    const auto reinhard = wot::stock_reinhard_params(profile);
    const Vector4 paramsValue(params[0], params[1], params[2], params[3]);
    const Vector4 reinhardValue(reinhard[0], reinhard[1], reinhard[2], reinhard[3]);
    // This client has no original bloom, lens-flare, dirt or god-ray owner, so
    // a72c7c's disabled-bloom tint and both disabled flags apply.
    const Vector4 bloomTint(0.f, 0.f, 0.f, 0.f);
    return SUCCEEDED(effect->SetTexture("g_avgLumMap", averageLuminance->pTexture())) &&
        setStockHdrTarget(effect, "g_HDRMap", hdr) && setStockHdrTarget(effect, "g_bloomMap", bloom) &&
        SUCCEEDED(effect->SetBool("g_enableLensFlare", FALSE)) &&
        SUCCEEDED(effect->SetBool("g_enableGodRays", FALSE)) &&
        SUCCEEDED(effect->SetTexture("g_rgbCubeMap", colorCorrection->pTexture())) &&
        SUCCEEDED(effect->SetVector("g_params", &paramsValue)) &&
        SUCCEEDED(effect->SetVector("g_bloomTint", &bloomTint)) &&
        setOptionalVector(effect, "g_reinhardParams", reinhardValue) &&
        SUCCEEDED(effect->SetInt("g_toneMappingFilter", active)) &&
        Moo::rc().fsQuad().drawChecked(*material, static_cast<uint>(Moo::rc().screenWidth()),
            static_cast<uint>(Moo::rc().screenHeight()));
}
BW_END_NAMESPACE
