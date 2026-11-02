#pragma once
#include "cstdmf/bw_namespace.hpp"
#include "moo/complex_effect_material.hpp"
BW_BEGIN_NAMESPACE
extern int StockCustomizationHelper_token;
bool stockCustomizationMaskActive();
Moo::ComplexEffectMaterialPtr stockCustomizationMaskMaterial(
    const Moo::ComplexEffectMaterialPtr & original, bool nodeless, bool tracks );
bool drawStockCustomization();
void finiStockCustomization();
BW_END_NAMESPACE
