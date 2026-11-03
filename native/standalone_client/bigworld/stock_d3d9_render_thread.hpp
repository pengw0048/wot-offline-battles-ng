#ifndef STOCK_D3D9_RENDER_THREAD_HPP
#define STOCK_D3D9_RENDER_THREAD_HPP

#include <d3d9.h>
#include <cstdint>

namespace wot { namespace d3d9rt {

// Coarse main-loop intervals; nested scene/terrain intervals are subsets of draw.
std::uint64_t begin_frame_stage();
void end_frame_stage(unsigned stage, std::uint64_t begin);
class FrameStage {
public:
    explicit FrameStage(unsigned stage) : stage_(stage), begin_(begin_frame_stage()) {}
    ~FrameStage() { end_frame_stage(stage_, begin_); }
    FrameStage(const FrameStage&) = delete;
    FrameStage& operator=(const FrameStage&) = delete;
private:
    unsigned stage_;
    std::uint64_t begin_;
};

// Returns the wrapper that records calls for the render thread, or device when the thread is off.
IDirect3DDevice9* wrap_device(IDirect3DDevice9* device, bool ex);
// Zero when no render thread owns the device.
DWORD render_thread_id();
// Runs fn on the render thread after everything already queued, and waits for it.
void run(void (*fn)(void*), void* ctx);
// Queues fn after everything already queued; the caller's device bindings are restored afterwards.
void post(void (*fn)(void*), void* ctx);
// Queues an owned native command without reading caller device bindings or
// waiting for ring space or execution. All external callers share FIFO order,
// fenced behind the ring records published before each command's admission.
// The caller keeps ctx alive until fn runs (and may release it inside fn).
void post_command(void (*fn)(void*), void* ctx);
// Waits for a sentinel in the owned-command queue, including foreign callers'
// prior admissions. The caller must not hold an SDK Context lock.
void drain_commands();
// Called on the device owner by a native command. Preserve an already open
// real scene without touching Moo's caller-thread nesting/profiling state.
HRESULT begin_command_scene(IDirect3DDevice9* device, bool& opened);
HRESULT end_command_scene(IDirect3DDevice9* device, bool opened);

}} // namespace wot::d3d9rt

#endif
