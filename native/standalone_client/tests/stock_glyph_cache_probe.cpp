// Win32 probe: the production GlyphCacheParams helper against the real SDK 4.2.22
// Renderer2D, GlyphCache and D3D9 HAL on a real Direct3D 9 device. It drives
// the calls the patched manager makes: SetParams before or after InitHAL, before
// a frame, repeatedly, across a device reset and after ShutdownHAL.
#ifdef _MSC_VER
#pragma warning(push)
#pragma warning(disable: 4244 4100 4189 4458 4267)
#endif
#include <GFx/GFx_Loader.h>
#include <Render/Renderer2D.h>
#include <Render/Render_GlyphCache.h>
#include <Render/D3D9/D3D9_HAL.h>
#ifdef _MSC_VER
#pragma warning(pop)
#endif
#include "stock_glyph_cache.hpp"
#include <cstdio>
#include <windows.h>

using namespace Scaleform;

static int failures = 0;
#define CHECK(expression) do { if (expression) std::printf("ok   %s\n", #expression); \
    else { std::printf("FAIL %s (line %d)\n", #expression, __LINE__); ++failures; } } while (0)

static Render::GlyphCache * cacheOf(Render::Renderer2D * renderer)
{
    return static_cast<Render::GlyphCache *>(renderer->GetGlyphCacheConfig());
}

static void describe(const char * when, Render::GlyphCache * cache)
{
    const Render::GlyphCacheParams & p = cache->GetParams();
    std::printf("%s: params %u %u %u %u vector=%d fit=%d fence=%d; textures=%u scaleU=%g scaleV=%g\n", when,
        p.NumTextures, p.TextureWidth, p.TextureHeight, p.MaxSlotHeight, int(p.UseVectorOnFullCache),
        int(p.UseAutoFit), int(p.FenceWaitOnFullCache), cache->GetNumTextures(), cache->GetScaleU(),
        cache->GetScaleV());
    std::fflush(stdout);
}

// Textures are the actual glyph textures the cache created, all at the rounded size.
static bool textures(Render::GlyphCache * cache, unsigned count, unsigned width, unsigned height)
{
    if (cache->GetNumTextures() != count)
        return false;
    for (unsigned i = 0; i < count; ++i)
    {
        Render::Image * image = cache->GetImage(i);
        if (!image || image->GetSize() != Render::ImageSize(width, height))
            return false;
    }
    return cache->GetScaleU() == 1.0f / float(width) && cache->GetScaleV() == 1.0f / float(height);
}

static bool same(const Render::GlyphCacheParams & a, const Render::GlyphCacheParams & b)
{
    return a.TextureWidth == b.TextureWidth && a.TextureHeight == b.TextureHeight &&
        a.NumTextures == b.NumTextures && a.MaxSlotHeight == b.MaxSlotHeight && a.SlotPadding == b.SlotPadding &&
        a.TexUpdWidth == b.TexUpdWidth && a.TexUpdHeight == b.TexUpdHeight &&
        a.MaxRasterScale == b.MaxRasterScale && a.MaxVectorCacheSize == b.MaxVectorCacheSize &&
        a.FauxItalicAngle == b.FauxItalicAngle && a.FauxBoldRatio == b.FauxBoldRatio &&
        a.OutlineRatio == b.OutlineRatio && a.ShadowQuality == b.ShadowQuality && a.UseAutoFit == b.UseAutoFit &&
        a.UseVectorOnFullCache == b.UseVectorOnFullCache && a.FenceWaitOnFullCache == b.FenceWaitOnFullCache;
}

static void frame(Render::Renderer2D * renderer, IDirect3DDevice9 * device)
{
    device->BeginScene();
    renderer->BeginFrame();
    renderer->EndFrame();
    device->EndScene();
}

int main()
{
    HWND window = CreateWindowExW(0, L"STATIC", L"glyph-cache-probe", WS_POPUP, 0, 0, 64, 64, NULL, NULL,
        GetModuleHandleW(NULL), NULL);
    IDirect3D9 * d3d = Direct3DCreate9(D3D_SDK_VERSION);
    D3DPRESENT_PARAMETERS pp = {};
    pp.Windowed = TRUE;
    pp.SwapEffect = D3DSWAPEFFECT_DISCARD;
    pp.BackBufferFormat = D3DFMT_X8R8G8B8;
    pp.BackBufferWidth = pp.BackBufferHeight = 64;
    pp.hDeviceWindow = window;
    IDirect3DDevice9 * device = NULL;
    const HRESULT created = d3d ? d3d->CreateDevice(D3DADAPTER_DEFAULT, D3DDEVTYPE_HAL, window,
        D3DCREATE_HARDWARE_VERTEXPROCESSING | D3DCREATE_FPU_PRESERVE, &pp, &device) : E_FAIL;
    std::printf("device create hr=0x%08lx\n", unsigned long(created));
    if (FAILED(created))
        return 2;
    D3DCAPS9 caps;
    device->GetDeviceCaps(&caps);
    std::printf("max texture %lux%lu\n", caps.MaxTextureWidth, caps.MaxTextureHeight);
    {
        GFx::System system;
        Ptr<Render::D3D9::HAL> hal = *SF_NEW Render::D3D9::HAL();
        Ptr<Render::Renderer2D> renderer = *SF_NEW Render::Renderer2D(hal.GetPtr());
        Render::GlyphCache * cache = cacheOf(renderer);

        describe("constructed", cache);
        // SDK 4.2.22 default; #1513 4.6.34 defaults UseVectorOnFullCache to true.
        CHECK(!cache->GetParams().UseVectorOnFullCache && cache->GetParams().TextureWidth == 1024);

        // afterCreate before a device: SetParams stores only.
        const Render::GlyphCacheParams first = wot::stock_glyph_cache::glyphCacheParams(1, 1024, 1024, 48);
        CHECK(renderer->GetGlyphCacheConfig()->SetParams(first));
        CHECK(same(cache->GetParams(), first) && cache->GetParams().UseVectorOnFullCache);
        CHECK(cache->GetNumTextures() == 0);

        CHECK(hal->InitHAL(Render::D3D9::HALInitParams(device, pp, Render::D3D9::HALConfig_NoSceneCalls)));
        describe("InitHAL", cache);
        CHECK(textures(cache, 1, 1024, 1024));
        CHECK(cache->GetMaxGlyphHeight() == 48);
        frame(renderer, device);

        // Interface scale 2.0 and 4.0 on an initialised HAL re-create the textures.
        CHECK(renderer->GetGlyphCacheConfig()->SetParams(wot::stock_glyph_cache::glyphCacheParams(1, 2048, 2048, 48)));
        describe("scale 2", cache);
        CHECK(textures(cache, 1, 2048, 2048));
        frame(renderer, device);
        CHECK(renderer->GetGlyphCacheConfig()->SetParams(wot::stock_glyph_cache::glyphCacheParams(1, 4096, 4096, 48)));
        describe("scale 4", cache);
        CHECK(textures(cache, 1, 4096, 4096));
        frame(renderer, device);

        // SDK sizing of other legal arguments: power-of-two rounding, the 64 minimum, the 32-texture pool.
        CHECK(renderer->GetGlyphCacheConfig()->SetParams(wot::stock_glyph_cache::glyphCacheParams(2, 1000, 300, 64)));
        describe("1000x300", cache);
        CHECK(textures(cache, 2, 1024, 512) && cache->GetMaxGlyphHeight() == 64);
        CHECK(renderer->GetGlyphCacheConfig()->SetParams(wot::stock_glyph_cache::glyphCacheParams(40, 32, 1, 16)));
        describe("40 x 32x1", cache);
        CHECK(textures(cache, 32, 64, 64) && cache->GetParams().NumTextures == 40);
        CHECK(renderer->GetGlyphCacheConfig()->SetParams(wot::stock_glyph_cache::glyphCacheParams(0, 1024, 1024, 48)));
        describe("0 textures", cache);
        CHECK(cache->GetNumTextures() == 0);
        frame(renderer, device);

        // Device reset: PrepareForReset destroys, RestoreAfterReset re-creates from the stored values.
        const Render::GlyphCacheParams last = wot::stock_glyph_cache::glyphCacheParams(1, 2048, 2048, 48);
        CHECK(renderer->GetGlyphCacheConfig()->SetParams(last));
        hal->PrepareForReset();
        describe("PrepareForReset", cache);
        CHECK(cache->GetNumTextures() == 0);
        const HRESULT reset = device->Reset(&pp);
        std::printf("device reset hr=0x%08lx\n", unsigned long(reset));
        CHECK(SUCCEEDED(reset));
        hal->RestoreAfterReset();
        describe("RestoreAfterReset", cache);
        CHECK(same(cache->GetParams(), last) && textures(cache, 1, 2048, 2048));
        frame(renderer, device);

        // After ShutdownHAL the values are stored and applied by the next InitHAL.
        hal->ShutdownHAL();
        describe("ShutdownHAL", cache);
        CHECK(cache->GetNumTextures() == 0);
        CHECK(renderer->GetGlyphCacheConfig()->SetParams(wot::stock_glyph_cache::glyphCacheParams(1, 1024, 1024, 48)));
        CHECK(cache->GetNumTextures() == 0);
        CHECK(hal->InitHAL(Render::D3D9::HALInitParams(device, pp, Render::D3D9::HALConfig_NoSceneCalls)));
        describe("InitHAL again", cache);
        CHECK(textures(cache, 1, 1024, 1024));
        frame(renderer, device);
        hal->ShutdownHAL();
    }
    device->Release();
    d3d->Release();
    DestroyWindow(window);
    std::printf("failures=%d\n", failures);
    return failures ? 1 : 0;
}
