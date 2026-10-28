#ifndef STOCK_FLASH_DRAWABLE_IMAGES_HPP
#define STOCK_FLASH_DRAWABLE_IMAGES_HPP

#include <Render/Render_ThreadCommandQueue.h>

namespace BW { namespace ScaleformBW {

// Current BW MovieDef loading, AS3 advance and render/context retirement run on
// Moo's render thread. There is no GFx TaskManager. This queue services that real
// backend synchronously: DrawableImage immediately waits for Execute's event.
// Adding a threaded GFx producer requires a render-job owner, not a deferred list
// that could stop being pumped during Movie/context shutdown.
class StockFlashDrawableImageQueue : public Scaleform::Render::ThreadCommandQueue
{
public:
    typedef void (*Executor)(Scaleform::Render::ThreadCommand*);
    explicit StockFlashDrawableImageQueue(Executor execute) :
        execute_(execute), renderThread_(Scaleform::GetCurrentThreadId()) {}

    void configure(const Scaleform::Render::Interfaces& interfaces)
    {
        SF_ASSERT(Scaleform::GetCurrentThreadId() == renderThread_);
        interfaces_ = interfaces;
    }

    void GetRenderInterfaces(Scaleform::Render::Interfaces* interfaces) override
    {
        SF_ASSERT(Scaleform::GetCurrentThreadId() == renderThread_);
        *interfaces = interfaces_;
    }

    void PushThreadCommand(Scaleform::Render::ThreadCommand* command) override
    {
        SF_ASSERT(Scaleform::GetCurrentThreadId() == renderThread_);
        // ExecuteCommand releases its own extra reference and signals its event
        // inside Execute. Our independent lease must survive until it returns.
        Scaleform::Ptr<Scaleform::Render::ThreadCommand> retained(command);
        execute_(retained.GetPtr());
    }

private:
    StockFlashDrawableImageQueue(const StockFlashDrawableImageQueue&);
    StockFlashDrawableImageQueue& operator=(const StockFlashDrawableImageQueue&);
    Executor execute_;
    Scaleform::ThreadId renderThread_;
    Scaleform::Render::Interfaces interfaces_;
};

}} // namespace BW::ScaleformBW
#endif
