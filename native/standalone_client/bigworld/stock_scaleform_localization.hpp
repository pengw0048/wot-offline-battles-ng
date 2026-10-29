#pragma once

#include "cstdmf/bw_namespace.hpp"
#include "scaleform/config.hpp"

class FxDelegate;

BW_BEGIN_NAMESPACE
namespace ScaleformBW
{
// #1513 00621af0: one loader-level WGGFxStringTranslator over helpers.i18n.makeString.
void installStockTranslator( GFx::Loader & loader );

// #1513 00aa4cc5: each movie's FxDelegate gets WGExtensions' numeric/date
// formatters and WG.getFormattedLocalizedString over helpers.i18n.makeStringJSON.
void registerStockLocalization( FxDelegate & delegate );
} // namespace ScaleformBW
BW_END_NAMESPACE
