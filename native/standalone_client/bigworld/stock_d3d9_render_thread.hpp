#ifndef STOCK_D3D9_RENDER_THREAD_HPP
#define STOCK_D3D9_RENDER_THREAD_HPP

#include <d3d9.h>

namespace wot { namespace d3d9rt {

// Returns the wrapper that records calls for the render thread, or device when the thread is off.
IDirect3DDevice9* wrap_device(IDirect3DDevice9* device, bool ex);
// Zero when no render thread owns the device.
DWORD render_thread_id();
// Runs fn on the render thread after everything already queued, and waits for it.
void run(void (*fn)(void*), void* ctx);
// Queues fn after everything already queued; the caller's device bindings are restored afterwards.
void post(void (*fn)(void*), void* ctx);

}} // namespace wot::d3d9rt

#endif
