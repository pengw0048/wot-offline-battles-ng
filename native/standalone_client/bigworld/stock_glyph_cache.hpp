#pragma once

#include "scaleform/config.hpp"

namespace wot
{
namespace stock_glyph_cache
{
// #1513 00a9fb9e..00a9fc3f: the four arguments fill the GlyphCacheParams
// constructor. Scaleform 4.6.34 also defaults UseVectorOnFullCache to true
// (00be8451), where SDK 4.2.22 defaults it to false.
inline Scaleform::Render::GlyphCacheParams glyphCacheParams(unsigned numTextures,
    unsigned textureWidth, unsigned textureHeight, unsigned maxSlotHeight)
{
    Scaleform::Render::GlyphCacheParams params(numTextures, textureWidth, textureHeight,
        maxSlotHeight);
    params.UseVectorOnFullCache = true;
    return params;
}
}
}
