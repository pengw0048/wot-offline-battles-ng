#ifndef STOCK_FLASH_OVERLAY_TARGET_HPP
#define STOCK_FLASH_OVERLAY_TARGET_HPP

namespace BW { namespace ScaleformBW {

// An overlay's masks and filter returns must use one color/stencil pair.
// Scene depth (including the deferred INTZ surface) remains scene-owned.
class StockFlashOverlayTarget
{
    typedef ::Scaleform::Render::D3D9::HAL HAL;
    typedef ::Scaleform::Render::RenderTarget RenderTarget;
    ::Scaleform::Ptr<RenderTarget> target_;
    IDirect3DSurface9* color_ = NULL;
    IDirect3DSurface9* stencil_ = NULL;
    HAL* hal_ = NULL;
    D3DSURFACE_DESC description_ = {};
    bool reported_ = false;

    void fail(const char* operation, HRESULT result)
    {
        if (!reported_)
            ERROR_MSG("Scaleform: overlay %s failed (%08lx)\n", operation,
                static_cast<unsigned long>(result));
        reported_ = true;
    }

    bool prepare(HAL* hal, IDirect3DDevice9* device, IDirect3DSurface9* color)
    {
        D3DSURFACE_DESC desc = {};
        const HRESULT described = color ? color->GetDesc(&desc) : E_FAIL;
        if (FAILED(described)) { fail("color description", described); return false; }
        if (target_ && hal_ == hal && color_ == color &&
            description_.Width == desc.Width && description_.Height == desc.Height &&
            description_.MultiSampleType == desc.MultiSampleType &&
            description_.MultiSampleQuality == desc.MultiSampleQuality)
            return true;

        IDirect3DSurface9* stencil = NULL;
        const HRESULT created = device->CreateDepthStencilSurface(desc.Width, desc.Height,
            D3DFMT_D24S8, desc.MultiSampleType, desc.MultiSampleQuality, FALSE, &stencil, NULL);
        if (FAILED(created) || !stencil)
        {
            if (stencil) stencil->Release();
            fail("stencil allocation", FAILED(created) ? created : E_FAIL);
            return false;
        }
        RenderTarget* createdTarget = hal->CreateRenderTarget(color, stencil);
        if (!createdTarget)
        {
            stencil->Release();
            fail("HAL target allocation", E_FAIL);
            return false;
        }
        ::Scaleform::Ptr<RenderTarget> target = *createdTarget;
        reset();
        color->AddRef();
        color_ = color;
        stencil_ = stencil;
        hal_ = hal;
        description_ = desc;
        target_ = target;
        return true;
    }

public:
    ~StockFlashOverlayTarget() { reset(); }
    void reset()
    {
        target_.Clear();
        if (stencil_) stencil_->Release();
        if (color_) color_->Release();
        color_ = stencil_ = NULL;
        hal_ = NULL;
        reported_ = false;
    }

    class Scope
    {
        StockFlashOverlayTarget& owner_;
        HAL* hal_;
        IDirect3DDevice9* device_;
        ::Scaleform::Ptr<RenderTarget> previous_;
        IDirect3DSurface9* color_ = NULL;
        IDirect3DSurface9* depth_ = NULL;
        D3DVIEWPORT9 viewport_ = {};
        RECT scissor_ = {};
        bool changed_ = false;
        bool ready_ = false;
    public:
        Scope(StockFlashOverlayTarget& owner, HAL* hal, IDirect3DDevice9* device, bool overlay) :
            owner_(owner), hal_(hal), device_(device)
        {
            // World text retains the scene depth/stencil-2 contract unchanged.
            if (!overlay) { ready_ = true; return; }
            if (!device || !hal) { owner_.fail("device owner", E_FAIL); return; }
            previous_ = hal->GetDefaultRenderTarget();
            const HRESULT colorResult = device->GetRenderTarget(0, &color_);
            const HRESULT depthResult = device->GetDepthStencilSurface(&depth_);
            const HRESULT viewportResult = device->GetViewport(&viewport_);
            const HRESULT scissorResult = device->GetScissorRect(&scissor_);
            if (!previous_ || FAILED(colorResult) ||
                (FAILED(depthResult) && depthResult != D3DERR_NOTFOUND) ||
                FAILED(viewportResult) || FAILED(scissorResult))
            { owner_.fail("state capture", E_FAIL); return; }
            if (!owner_.prepare(hal, device, color_)) return;
            const HRESULT detached = device->SetDepthStencilSurface(NULL);
            if (FAILED(detached)) { owner_.fail("scene depth detach", detached); return; }
            changed_ = true;
            const HRESULT bound = device->SetDepthStencilSurface(owner_.stencil_);
            if (FAILED(bound)) { owner_.fail("stencil bind", bound); return; }
            // Update HAL's stack bottom too: filter PopRenderTarget must return
            // to this pair, not the pair captured at HAL initialization.
            if (!hal->SetRenderTarget(owner_.target_, false))
            { owner_.fail("HAL target bind", E_FAIL); return; }
            ready_ = true;
        }
        ~Scope()
        {
            if (changed_)
            {
                if (!hal_->SetRenderTarget(previous_, false)) owner_.fail("HAL target restore", E_FAIL);
                bool restored = SUCCEEDED(device_->SetDepthStencilSurface(NULL));
                restored = SUCCEEDED(device_->SetRenderTarget(0, color_)) && restored;
                restored = SUCCEEDED(device_->SetDepthStencilSurface(depth_)) && restored;
                restored = SUCCEEDED(device_->SetViewport(&viewport_)) && restored;
                restored = SUCCEEDED(device_->SetScissorRect(&scissor_)) && restored;
                if (!restored) owner_.fail("scene state restore", E_FAIL);
            }
            if (depth_) depth_->Release();
            if (color_) color_->Release();
        }
        explicit operator bool() const { return ready_; }
    private:
        Scope(const Scope&);
        Scope& operator=(const Scope&);
    };

private:
    StockFlashOverlayTarget(const StockFlashOverlayTarget&);
    StockFlashOverlayTarget& operator=(const StockFlashOverlayTarget&);
public:
    StockFlashOverlayTarget() {}
};

}} // namespace BW::ScaleformBW
#endif
