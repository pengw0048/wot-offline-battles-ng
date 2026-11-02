#ifndef WOT_STOCK_DIRECTIONAL_SHADOW_FXH
#define WOT_STOCK_DIRECTIONAL_SHADOW_FXH

// Solve the receiver plane in atlas coordinates. Derive world-position
// differentials before cascade selection; neighbouring pixels can select a
// different cascade, but must not contribute its matrix to this derivative.
float2 stockShadowDepthGradient(float3 dx, float3 dy)
{
    float determinant = dx.x * dy.y - dx.y * dy.x;
    if (determinant == 0.f) return float2(0.f, 0.f);
    return float2((dx.z * dy.y - dx.y * dy.z) / determinant,
                  (dx.x * dy.z - dx.z * dy.x) / determinant);
}

// The public texture matrix omits the DX9 half-texel offset: raster pixel
// (x,y) was evaluated at (x/width,y/height), while the sampler addresses its
// texel at ((x+.5)/width,(y+.5)/height). Compare the two surfaces at the
// stored raster point rather than at the requested filter UV. A constant
// split bias alone cannot remove moving bands on a sloped receiver.
float stockShadowReceiverDepth(float2 receiverUV, float receiverDepth,
    float2 gradient, float2 sampleUV, float2 atlasDimensions)
{
    float2 rasterPoint = floor(sampleUV * atlasDimensions) / atlasDimensions;
    float2 offset = rasterPoint - receiverUV;
    return receiverDepth + gradient.x * offset.x + gradient.y * offset.y;
}

// R32F stores raw depth, whereas the original D16 shadow sampler returns
// linearly filtered comparison coverage. Interpolate comparisons, never depth.
float stockShadowBilinearCoverage(float topLeft, float topRight,
    float bottomLeft, float bottomRight, float2 phase)
{
    return lerp(lerp(topLeft, topRight, phase.x),
                lerp(bottomLeft, bottomRight, phase.x), phase.y);
}

// The original ASM receiver writes static coverage to BLUE. Terrain and
// object depths are separate public atlas channels; union their occlusion at
// each PCF sample before averaging, so overlap is never counted twice.
float stockShadowStaticCoverage(float2 depths, float receiverDepth)
{
    return receiverDepth > min(depths.x, depths.y);
}

float stockShadowCombinedCoverage(float dynamicCoverage, float staticCoverage)
{
    return max(dynamicCoverage, staticCoverage);
}
#endif
