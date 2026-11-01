#pragma once

#include "moo/base_texture.hpp"
#include "moo/effect_material.hpp"
#include "moo/render_target.hpp"
#include "resmgr/datasection.hpp"

BW_BEGIN_NAMESPACE
struct HDRSettings;

// 99b8e0: binds a render target with its <name>DRRParams projection.
bool setStockHdrTarget(ID3DXEffect* effect, const char* name, Moo::RenderTarget* target);
// a6f440: the original tonemapping profiles and their active index.
void loadStockToneMappings(const DataSectionPtr& hdr, HDRSettings& settings);
int stockActiveToneMapping(const HDRSettings& settings);
// a7356c: the environment's colour-correction cube strip.
Moo::BaseTexturePtr loadStockColorCorrection(const BW::string& resource);
// a729d0: the original ToneMapping pass into the current target.
bool drawStockToneMapping(const Moo::EffectMaterialPtr& material, const HDRSettings& settings,
    Moo::RenderTarget* averageLuminance, Moo::RenderTarget* hdr,
    Moo::RenderTarget* bloom, const Moo::BaseTexturePtr& colorCorrection);
BW_END_NAMESPACE
