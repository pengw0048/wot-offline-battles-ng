#ifndef STOCK_SCALEFORM_RENDER_THREAD_HPP
#define STOCK_SCALEFORM_RENDER_THREAD_HPP

#include "stock_d3d9_render_thread.hpp"

namespace BW { namespace ScaleformBW {

// Scaleform HAL work runs on the D3D9 render thread when one owns the device,
// otherwise inline on the caller.
struct StockRenderThread
{
    static DWORD id() { return ::wot::d3d9rt::render_thread_id(); }
    template<class F> static void runSync(F& f)
    {
        ::wot::d3d9rt::run([](void* c) { (*static_cast<F*>(c))(); }, &f);
    }
    // Waits until every job queued so far has run.
    static void drain()
    {
        struct Nothing { void operator()() const {} } nothing;
        runSync(nothing);
    }
    // Takes ownership of a heap job; the job deletes itself after running.
    template<class J> static void postJob(J* job)
    {
        ::wot::d3d9rt::post([](void* c) { J* j = static_cast<J*>(c); (*j)(); delete j; }, job);
    }
};

}} // namespace BW::ScaleformBW

#endif
