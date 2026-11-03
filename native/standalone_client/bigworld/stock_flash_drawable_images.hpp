#ifndef STOCK_FLASH_DRAWABLE_IMAGES_HPP
#define STOCK_FLASH_DRAWABLE_IMAGES_HPP

#include <Render/Render_ThreadCommandQueue.h>

namespace BW { namespace ScaleformBW {

// The executor retains and admits commands to the actual HAL render thread.
// Push must return while a Context lock is held; the SDK waits for completion
// after releasing that lock. Interfaces are configured before publication and
// cleared only after render/context retirement has finished.
class StockFlashDrawableImageQueue : public Scaleform::Render::ThreadCommandQueue
{
public:
    typedef void (*Executor)(Scaleform::Render::ThreadCommand*);
    explicit StockFlashDrawableImageQueue(Executor execute) :
        execute_(execute) {}

    void configure(const Scaleform::Render::Interfaces& interfaces)
    {
        interfaces_ = interfaces;
    }

    void GetRenderInterfaces(Scaleform::Render::Interfaces* interfaces) override
    {
        *interfaces = interfaces_;
    }

    void PushThreadCommand(Scaleform::Render::ThreadCommand* command) override
    {
        // ExecuteCommand releases its own extra reference and signals its event
        // inside Execute. Our independent lease must survive until it returns.
        Scaleform::Ptr<Scaleform::Render::ThreadCommand> retained(command);
        execute_(retained.GetPtr());
    }

private:
    StockFlashDrawableImageQueue(const StockFlashDrawableImageQueue&);
    StockFlashDrawableImageQueue& operator=(const StockFlashDrawableImageQueue&);
    Executor execute_;
    Scaleform::Render::Interfaces interfaces_;
};

}} // namespace BW::ScaleformBW
#endif
