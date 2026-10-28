#include "pch.hpp"
#include "stock_gpu_detection.hpp"
#include "stock_cpu_detection.hpp"
#include "stock_dynamic_resolution.hpp"
#include "wot/stock_gpu_performancer.hpp"
#include "wot/stock_gpu_sampling.hpp"
#include "moo/effect_material.hpp"
#include "moo/fullscreen_quad.hpp"
#include "moo/init.hpp"
#include <dxgi.h>
#include "moo/moo_utilities.hpp"
#include "moo/render_context.hpp"
#include "moo/render_target.hpp"
#include "moo/renderer.hpp"

BW_BEGIN_NAMESPACE
namespace
{
template<class T> class ComOwner
{
public:
    ComOwner() : value_(NULL) {}
    ~ComOwner() { if (value_) value_->Release(); }
    T ** address() { return &value_; }
    T * get() const { return value_; }
private:
    ComOwner(const ComOwner&);
    ComOwner& operator=(const ComOwner&);
    T * value_;
};

// Restore the device state as well as invalidate Moo's cached state after raw
// D3D calls. RenderTarget push/pop separately restores target and viewport.
class DeviceScope
{
public:
    explicit DeviceScope(IDirect3DDevice9 * device) : device_(device), scene_(false), valid_(false), sceneResolution_(Moo::rc().stockSceneResolution())
    {
        if (FAILED(device_->CreateStateBlock(D3DSBT_ALL, state_.address())) ||
            FAILED(state_.get()->Capture())) return;
        Moo::rc().stockSceneResolution(false);
        const HRESULT result = Moo::rc().beginScene();
        scene_ = true; // beginScene increments its nesting count even on failure.
        valid_ = SUCCEEDED(result);
    }
    ~DeviceScope()
    {
        if (scene_) Moo::rc().endScene();
        if (state_.get()) state_.get()->Apply();
        Moo::rc().invalidateStateCache();
        Moo::rc().stockSceneResolution(sceneResolution_);
    }
    bool valid() const { return valid_; }
private:
    IDirect3DDevice9 * device_;
    ComOwner<IDirect3DStateBlock9> state_;
    bool scene_;
    bool valid_;
    bool sceneResolution_;
};

bool videoMemory(HMONITOR monitor, SIZE_T & video, SIZE_T & system)
{
    HMODULE library = LoadLibraryW(L"dxgi.dll");
    if (!library) return false;
    typedef HRESULT (WINAPI * CreateFactory)(REFIID, void **);
    CreateFactory create = reinterpret_cast<CreateFactory>(GetProcAddress(library, "CreateDXGIFactory"));
    IDXGIFactory * factory = NULL;
    bool found = false;
    if (create && SUCCEEDED(create(__uuidof(IDXGIFactory), reinterpret_cast<void **>(&factory))) && factory)
    {
        for (UINT i = 0; !found; ++i)
        {
            IDXGIAdapter * adapter = NULL;
            if (FAILED(factory->EnumAdapters(i, &adapter))) break;
            for (UINT j = 0; ; ++j)
            {
                IDXGIOutput * output = NULL;
                if (FAILED(adapter->EnumOutputs(j, &output))) break;
                DXGI_OUTPUT_DESC description = {};
                const bool match = SUCCEEDED(output->GetDesc(&description)) && description.Monitor == monitor;
                output->Release();
                if (match) { found = true; break; }
            }
            if (found)
            {
                DXGI_ADAPTER_DESC description = {};
                if (SUCCEEDED(adapter->GetDesc(&description)))
                {
                    video = description.DedicatedVideoMemory;
                    system = description.DedicatedSystemMemory;
                }
                else found = false;
            }
            adapter->Release();
        }
        factory->Release();
    }
    FreeLibrary(library);
    return found;
}

bool queryResult(IDirect3DQuery9 * query, UINT64 & result)
{
    const DWORD start = GetTickCount();
    for (;;)
    {
        const HRESULT hr = query->GetData(&result, sizeof(result), D3DGETDATA_FLUSH);
        if (hr == S_OK) return true;
        if (hr != S_FALSE || GetTickCount() - start >= 10000) return false;
        Sleep(0);
    }
}

bool draw(Moo::RenderTargetPtr target, Moo::EffectMaterialPtr material,
    Moo::FullscreenQuad & quad, const char * technique, unsigned size)
{
    if (!target || !target->push()) return false;
    const bool result = material->setTechnique(technique) && quad.drawChecked(*material, size, size);
    target->pop();
    return result;
}

bool sample(Moo::RenderTargetPtr target, Moo::EffectMaterialPtr material,
    Moo::FullscreenQuad & quad, const char * technique, unsigned size,
    unsigned count, double limit, IDirect3DQuery9 * frequency,
    IDirect3DQuery9 * begin, IDirect3DQuery9 * end, double & minimum)
{
    wot::StockGpuSampling policy(count, limit);
    while (policy.needsSample())
    {
        if (!target->push()) { policy.failure(); continue; }
        bool ok = material->setTechnique(technique);
        ok = SUCCEEDED(frequency->Issue(D3DISSUE_END)) && ok;
        ok = SUCCEEDED(begin->Issue(D3DISSUE_END)) && ok;
        if (ok) ok = quad.drawChecked(*material, size, size);
        ok = SUCCEEDED(end->Issue(D3DISSUE_END)) && ok;
        target->pop();
        UINT64 hz = 0, first = 0, last = 0;
        if (!ok || !queryResult(frequency, hz) || !queryResult(begin, first) ||
            !queryResult(end, last)) policy.failure();
        else policy.timestamps(hz, first, last);
    }
    minimum = policy.minimumMilliseconds();
    return policy.passed();
}
}

int stockMeasureGpuPerformance()
{
    if (!Moo::isInitialised() || !Moo::rc().device()) return 1;
    IDirect3DDevice9 * device = Moo::rc().device();
    ComOwner<IDirect3DQuery9> frequency, begin, end;
    if (FAILED(device->CreateQuery(D3DQUERYTYPE_TIMESTAMPFREQ, frequency.address())) ||
        FAILED(device->CreateQuery(D3DQUERYTYPE_TIMESTAMP, begin.address())) ||
        FAILED(device->CreateQuery(D3DQUERYTYPE_TIMESTAMP, end.address()))) return 1;
    DeviceScope scope(device);
    if (!scope.valid()) return 1;
    Moo::EffectMaterialPtr material;
    if (!Moo::createEffect(material, "shaders/std_effects/wg_gpu_performancer.fx")) return 1;
    Moo::RenderTargetPtr targets[6];
    for (unsigned i = 0; i < 6; ++i)
    {
        const unsigned size = i < 3 ? 2048 : 128;
        if (!Moo::createRenderTarget(targets[i], size, size, D3DFMT_A16B16G16R16F,
            "GPU performance test", false, true)) return 1;
    }
    Moo::FullscreenQuad quad;
    quad.createManagedObjects();
    if (!draw(targets[1], material, quad, "GenRandTex", 2048) ||
        !draw(targets[2], material, quad, "GenRandTex", 2048) ||
        !draw(targets[4], material, quad, "GenRandTex", 128) ||
        !draw(targets[5], material, quad, "GenRandTex", 128)) return 1;
    if (!material->setProperty("g_texture0", Moo::BaseTexturePtr(targets[1]), true) ||
        !material->setProperty("g_texture1", Moo::BaseTexturePtr(targets[2]), true)) return 1;
    const char * techniques[] = { "Julia", "RandTex" };
    const unsigned counts[] = { 60, 30 };
    const double limits[] = { 500., 1000. };
    double times[2] = {0., 0.};
    for (unsigned i = 0; i < 2; ++i)
    {
        double light = 0.;
        sample(targets[3], material, quad, techniques[i], 128, counts[i], limits[i],
            frequency.get(), begin.get(), end.get(), light);
        if (!(light < 7.) || !sample(targets[0], material, quad, techniques[i], 2048,
            counts[i], limits[i], frequency.get(), begin.get(), end.get(), times[i])) return 2;
    }
    return wot::stockGpuPerformanceScore(times[0], times[1]);
}
void stockAdjustDetectedResolution(int status)
{
    if (status != 2 || !Moo::isInitialised() || !Moo::rc().device() ||
        Renderer::instance().pipelineType() != IRendererPipeline::TYPE_FORWARD_SHADING) return;
    const Moo::DeviceInfo & info = Moo::rc().deviceInfo(Moo::rc().deviceIndex());
    const D3DDISPLAYMODE desktop = info.windowedDisplayMode_;
    if (desktop.Width != 1920 || desktop.Height != 1080) return;
    StockDynamicResolutionSupport * resolution = stockDynamicResolution();
    if (resolution) resolution->state().set_scale(0.65f);
    const unsigned current = Moo::rc().modeIndex();
    if (current >= info.displayModes_.size()) return;
    const D3DDISPLAYMODE mode = info.displayModes_[current];
    // 91efdd and 91efe9 each branch to the exit; both must differ.
    if (mode.Width == 1920 || mode.Height == 1080) return;
    for (unsigned i = 0; i < info.displayModes_.size(); ++i)
    {
        const D3DDISPLAYMODE candidate = info.displayModes_[i];
        if (candidate.Width == desktop.Width && candidate.Height == desktop.Height &&
            candidate.RefreshRate == desktop.RefreshRate &&
            (candidate.Format == D3DFMT_X8R8G8B8 || candidate.Format == D3DFMT_A8B8G8R8))
        {
            Moo::rc().changeMode(i, Moo::rc().windowed());
            break;
        }
    }
}

bool stockMeasureGraphicsHardware(int64 (&scores)[5])
{
    for (unsigned i = 0; i < 5; ++i) scores[i] = 0;
    if (!Moo::isInitialised() || !Moo::rc().device()) return false;
    const Moo::DeviceInfo & info = Moo::rc().deviceInfo(Moo::rc().deviceIndex());
    IDirect3D9 * d3d = Direct3DCreate9(D3D_SDK_VERSION);
    if (d3d)
    {
        SIZE_T video = 0, system = 0;
        HMONITOR monitor = d3d->GetAdapterMonitor(info.adapterID_);
        if (videoMemory(monitor, video, system))
        {
            // The retail x86 detector adds dedicated system memory for Intel.
            if (info.identifier_.VendorId == 0x8086) video += system;
            scores[0] = video >> 20;
        }
        d3d->Release();
    }
    scores[1] = stockMeasureGpuPerformance();
    MEMORYSTATUSEX memory = {};
    memory.dwLength = sizeof(memory);
    if (GlobalMemoryStatusEx(&memory))
    {
        scores[2] = memory.ullTotalPhys >> 20;
        scores[3] = memory.ullAvailVirtual >> 20;
    }
    scores[4] = stockMeasureCpuFrequency();
    return true;
}
BW_END_NAMESPACE
