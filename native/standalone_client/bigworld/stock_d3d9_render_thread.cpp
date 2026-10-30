// One render thread exclusively owns the real D3D9 device. Moo receives wrapper objects; the
// thread that created the device records calls into a ring that the render thread replays.
// The engine targets XP headers; slim reader/writer locks need Vista declarations.
#undef _WIN32_WINNT
#define _WIN32_WINNT 0x0600
#undef NTDDI_VERSION
#define NTDDI_VERSION 0x06000000
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <d3d9.h>
#include "stock_d3d9_render_thread.hpp"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cstdint>
#include <new>
#include <type_traits>
#include <unordered_map>
#include <utility>
#include <vector>

namespace {
namespace rt {
static FILE* g_log; static bool g_stats, g_retired;
static FILE* reportLog() { if (!g_log) g_log = fopen("d3d9_render_thread.log", "a"); return g_log; }
static DWORD g_main, g_renderId; static bool g_started;
static const uint32_t RING = 128u << 20;
static uint8_t* g_ring; static volatile LONG g_head, g_tail, g_sleeping, g_foreign;
static HANDLE g_work, g_idle;
static uint32_t g_sinceWake;
static volatile LONG g_framesQueued, g_framesDone;
static LARGE_INTEGER g_freq;
static long long g_frames, g_frameStart, g_frameTicks, g_syncTicks, g_paceTicks, g_bytes, g_syncCount;
static std::unordered_map<const char*, long> g_syncBy;
static inline long long now() { LARGE_INTEGER t; QueryPerformanceCounter(&t); return t.QuadPart; }
inline bool onMain() { return g_started && GetCurrentThreadId() == g_main; }
inline bool onRender() { return GetCurrentThreadId() == g_renderId; }

struct Rec { uint32_t size; void (*fn)(void*); };
static void wake() { if (InterlockedCompareExchange(&g_sleeping, 0, 1) == 1) SetEvent(g_work); }
static void* reserve(uint32_t payload, void (*fn)(void*)) {
    const uint32_t need = (sizeof(Rec) + payload + 7) & ~7u;
    for (;;) {
        const uint32_t head = g_head, tail = g_tail;
        const uint32_t used = head >= tail ? head - tail : RING - (tail - head);
        if (used + need + 64 < RING) {
            if (head + need + sizeof(Rec) > RING) {
                if (tail == 0 || tail > head) { wake(); SwitchToThread(); continue; }
                reinterpret_cast<Rec*>(g_ring + head)->fn = nullptr;   // wrap marker
                MemoryBarrier(); InterlockedExchange(&g_head, 0); continue;
            }
            Rec* r = reinterpret_cast<Rec*>(g_ring + head); r->size = need; r->fn = fn;
            return r + 1;
        }
        wake(); SwitchToThread();
    }
}
static void commit(bool flush = false) {
    const uint32_t size = reinterpret_cast<Rec*>(g_ring + g_head)->size;
    g_bytes += size; g_sinceWake += size;
    MemoryBarrier(); InterlockedExchange(&g_head, g_head + size);
    if (flush || g_sinceWake > (32u << 10)) { g_sinceWake = 0; wake(); }
}
template<class T> static void invokeAndDestroy(void* p) { T* f = static_cast<T*>(p); (*f)(); f->~T(); }
template<class F> void post(F&& f) {
    using T = typename std::decay<F>::type;
    static_assert(alignof(T) <= 8, "record alignment");
    new (reserve(sizeof(T), &invokeAndDestroy<T>)) T(std::forward<F>(f)); commit();
}
template<class T> struct WithData { T f; uint32_t n; };
template<class T> static void invokeData(void* p) { auto* h = static_cast<WithData<T>*>(p); h->f(reinterpret_cast<const uint8_t*>(h + 1)); h->~WithData<T>(); }
template<class F> void postData(const void* data, uint32_t n, F&& f) {
    using T = typename std::decay<F>::type;
    void* p = reserve(sizeof(WithData<T>) + n, &invokeData<T>);
    auto* h = new (p) WithData<T>{std::forward<F>(f), n};
    if (n) memcpy(h + 1, data, n);
    commit();
}

// Synchronous execution on the render thread, in order with everything the main thread queued.
struct SyncTask { void (*run)(void*); void* ctx; volatile LONG done; HANDLE ev; };
static void runTask(SyncTask* t) { t->run(t->ctx); InterlockedExchange(&t->done, 1); SetEvent(t->ev); }
static CRITICAL_SECTION g_foreignLock; static std::vector<SyncTask*> g_foreignTasks;
static HANDLE threadEvent() { static thread_local HANDLE ev = CreateEventA(nullptr, FALSE, FALSE, nullptr); return ev; }
void stallReport(const char* name);   // logs the render thread's stack once when a sync stalls
template<class F> auto sync(F&& f, const char* name) -> decltype(f()) {
    using R = decltype(f());
    if (!g_started || onRender()) return f();
    struct Ctx { F* f; R r; } ctx{&f, R()};
    SyncTask task{[](void* c) { auto* x = static_cast<Ctx*>(c); x->r = (*x->f)(); }, &ctx, 0, threadEvent()};
    if (onMain()) {
        long long t = now(); ++g_syncCount; ++g_syncBy[name];
        SyncTask* tp = &task; post([tp] { runTask(tp); }); g_sinceWake = 0; wake();
        for (int w = 0; !task.done; ++w) { WaitForSingleObject(task.ev, 5); if (w == 2000) stallReport(name); }
        g_syncTicks += now() - t;
    } else {
        EnterCriticalSection(&g_foreignLock); g_foreignTasks.push_back(&task); LeaveCriticalSection(&g_foreignLock);
        InterlockedExchange(&g_foreign, 1); InterlockedExchange(&g_sleeping, 0); SetEvent(g_work);
        for (int w = 0; !task.done; ++w) { WaitForSingleObject(task.ev, 5); if (w == 2000) stallReport(name); }
    }
    return ctx.r;
}
template<class F> HRESULT async(F&& f, const char* name) {
    if (onMain()) { post(std::forward<F>(f)); return D3D_OK; }
    return sync([&]() -> HRESULT { f(); return D3D_OK; }, name);
}

// Retirement: a wrapper's real object is released after every command that may still use it.
struct Retire { void (*fn)(void*); void* obj; };
static std::vector<Retire> g_deferred; static CRITICAL_SECTION g_deferredLock;
static bool g_inlineRetire;
static void retire(void (*fn)(void*), void* obj) {
    if (g_inlineRetire && onRender()) { fn(obj); return; }
    if (onMain()) { post([fn, obj] { fn(obj); }); return; }
    if (!g_started) { fn(obj); return; }
    EnterCriticalSection(&g_deferredLock); g_deferred.push_back({fn, obj}); LeaveCriticalSection(&g_deferredLock);
}
static void runDeferred() {
    std::vector<Retire> list;
    EnterCriticalSection(&g_deferredLock); list.swap(g_deferred); LeaveCriticalSection(&g_deferredLock);
    for (auto& r : list) r.fn(r.obj);
}

// Identity map real -> wrapper, so getters that return existing objects return the same wrapper.
static SRWLOCK g_mapLock = SRWLOCK_INIT; static std::unordered_map<void*, void*> g_identity;
static void (*g_onDestroyMain)(void*);
}  // namespace rt

template<class I> struct RefBase : I {
    I* real; volatile LONG refs = 1; bool ex = false;
    volatile LONG queuedUse = 0;   // set once a queued main-thread command may read the resource
    void* ext = nullptr; void (*extFree)(void*) = nullptr;   // per-wrapper cache, freed with the wrapper
    explicit RefBase(I* r) : real(r) {}
    virtual ~RefBase() {}
    ULONG STDMETHODCALLTYPE AddRef() override { return ULONG(InterlockedIncrement(&refs)); }
    ULONG STDMETHODCALLTYPE Release() override {
        LONG r = InterlockedDecrement(&refs);
        if (r == 0) {
            AcquireSRWLockExclusive(&rt::g_mapLock);
            auto it = rt::g_identity.find(real); if (it != rt::g_identity.end() && it->second == this) rt::g_identity.erase(it);
            ReleaseSRWLockExclusive(&rt::g_mapLock);
            if (rt::onMain() && rt::g_onDestroyMain) rt::g_onDestroyMain(this);
            rt::retire([](void* o) { auto* w = static_cast<RefBase<I>*>(o); if (w->extFree) w->extFree(w->ext); if (w->real) w->real->Release(); delete w; }, this);
        }
        return ULONG(r);
    }
};

struct WDevice; struct WSurface; struct WTexture; struct WCube; struct WVolTex; struct WVolume; struct WVB; struct WIB;
struct WVS; struct WPS; struct WDecl; struct WSB; struct WQuery; struct WSwap;
namespace rt {
static WDevice* g_dev;
inline IDirect3DDevice9Ex* device();
static void* g_vtbls[16]; static int g_vtblCount;
static bool isWrapper(const void* p) { void* v = *(void* const*)p; for (int i = 0; i < g_vtblCount; ++i) if (g_vtbls[i] == v) return true; return false; }
template<class T> T* unwrap(T* p) {
    if (!p) return nullptr;
    if (!isWrapper(p)) return p;
    return *reinterpret_cast<T**>(reinterpret_cast<char*>(p) + sizeof(void*));
}
IDirect3DSurface9* wrapOut(void*, IDirect3DSurface9* r); IDirect3DTexture9* wrapOut(void*, IDirect3DTexture9* r);
IDirect3DCubeTexture9* wrapOut(void*, IDirect3DCubeTexture9* r); IDirect3DVolumeTexture9* wrapOut(void*, IDirect3DVolumeTexture9* r);
IDirect3DVolume9* wrapOut(void*, IDirect3DVolume9* r); IDirect3DVertexBuffer9* wrapOut(void*, IDirect3DVertexBuffer9* r);
IDirect3DIndexBuffer9* wrapOut(void*, IDirect3DIndexBuffer9* r); IDirect3DVertexShader9* wrapOut(void*, IDirect3DVertexShader9* r);
IDirect3DPixelShader9* wrapOut(void*, IDirect3DPixelShader9* r); IDirect3DVertexDeclaration9* wrapOut(void*, IDirect3DVertexDeclaration9* r);
IDirect3DStateBlock9* wrapOut(void*, IDirect3DStateBlock9* r); IDirect3DQuery9* wrapOut(void*, IDirect3DQuery9* r);
IDirect3DSwapChain9* wrapOut(void*, IDirect3DSwapChain9* r); IDirect3DBaseTexture9* wrapOut(void*, IDirect3DBaseTexture9* r);
}

#include "stock_d3d9_render_thread_wrappers.inc"

// ------------------------------------------------------------------ cached resource descriptions
struct LevelCache { std::vector<D3DSURFACE_DESC> levels; };
// Mirrors keep dynamic buffer bytes on the main thread so DISCARD/NOOVERWRITE locks never wait.
struct Mirror { std::vector<uint8_t> data; bool pending = false, valid = false; UINT off = 0, len = 0; DWORD flags = 0; uint8_t* real = nullptr; };
static std::unordered_map<void*, LevelCache> g_levels;          // texture/cube wrapper -> level descs (written once at wrap)
static std::unordered_map<void*, D3DSURFACE_DESC> g_surfDesc;
static std::unordered_map<void*, D3DVOLUME_DESC> g_volDesc;
static std::unordered_map<void*, D3DVERTEXBUFFER_DESC> g_vbDesc;
static std::unordered_map<void*, D3DINDEXBUFFER_DESC> g_ibDesc;
static std::unordered_map<void*, std::vector<D3DVOLUME_DESC>> g_volLevels;
static SRWLOCK g_descLock = SRWLOCK_INIT;
static void freeMirror(void* p) { delete static_cast<Mirror*>(p); }

namespace rt {
template<class W, class I> I* wrapIdentity(I* r, void (*init)(W*) = nullptr) {
    AcquireSRWLockExclusive(&g_mapLock);
    auto it = g_identity.find(r);
    if (it != g_identity.end()) {
        W* w = static_cast<W*>(it->second); LONG c = w->refs;
        while (c > 0) { LONG prev = InterlockedCompareExchange(&w->refs, c + 1, c); if (prev == c) break; c = prev; }
        if (c > 0) { ReleaseSRWLockExclusive(&g_mapLock); r->Release(); return w; }
    }
    W* w = new W(r);
    bool known = false; for (int i = 0; i < g_vtblCount; ++i) known |= g_vtbls[i] == *(void**)w;
    if (!known && g_vtblCount < 16) g_vtbls[g_vtblCount++] = *(void**)w;
    g_identity[r] = w;
    ReleaseSRWLockExclusive(&g_mapLock);
    if (init) init(w);
    return w;
}
}
static void initTexture(WTexture* w) {
    LevelCache c; DWORD n = w->real->GetLevelCount(); c.levels.resize(n);
    for (DWORD i = 0; i < n; ++i) w->real->GetLevelDesc(i, &c.levels[i]);
    AcquireSRWLockExclusive(&g_descLock); g_levels[w] = std::move(c); ReleaseSRWLockExclusive(&g_descLock);
}
static void initCube(WCube* w) {
    LevelCache c; DWORD n = w->real->GetLevelCount(); c.levels.resize(n);
    for (DWORD i = 0; i < n; ++i) w->real->GetLevelDesc(i, &c.levels[i]);
    AcquireSRWLockExclusive(&g_descLock); g_levels[w] = std::move(c); ReleaseSRWLockExclusive(&g_descLock);
}
static void initVolTex(WVolTex* w) {
    std::vector<D3DVOLUME_DESC> v(w->real->GetLevelCount());
    for (DWORD i = 0; i < v.size(); ++i) w->real->GetLevelDesc(i, &v[i]);
    AcquireSRWLockExclusive(&g_descLock); g_volLevels[w] = std::move(v); ReleaseSRWLockExclusive(&g_descLock);
}
static void initSurface(WSurface* w) { D3DSURFACE_DESC d; w->real->GetDesc(&d); AcquireSRWLockExclusive(&g_descLock); g_surfDesc[w] = d; ReleaseSRWLockExclusive(&g_descLock); }
static void initVolume(WVolume* w) { D3DVOLUME_DESC d; w->real->GetDesc(&d); AcquireSRWLockExclusive(&g_descLock); g_volDesc[w] = d; ReleaseSRWLockExclusive(&g_descLock); }
static void initVB(WVB* w) { D3DVERTEXBUFFER_DESC d; w->real->GetDesc(&d); AcquireSRWLockExclusive(&g_descLock); g_vbDesc[w] = d; ReleaseSRWLockExclusive(&g_descLock); }
static void initIB(WIB* w) { D3DINDEXBUFFER_DESC d; w->real->GetDesc(&d); AcquireSRWLockExclusive(&g_descLock); g_ibDesc[w] = d; ReleaseSRWLockExclusive(&g_descLock); }
template<class M, class K> static auto lookup(M& m, K k) -> typename M::mapped_type {
    AcquireSRWLockShared(&g_descLock); auto it = m.find(k); auto v = it != m.end() ? it->second : typename M::mapped_type(); ReleaseSRWLockShared(&g_descLock); return v;
}

namespace rt {
IDirect3DSurface9* wrapOut(void*, IDirect3DSurface9* r) { return wrapIdentity<WSurface>(r, initSurface); }
IDirect3DTexture9* wrapOut(void*, IDirect3DTexture9* r) { return wrapIdentity<WTexture>(r, initTexture); }
IDirect3DCubeTexture9* wrapOut(void*, IDirect3DCubeTexture9* r) { return wrapIdentity<WCube>(r, initCube); }
IDirect3DVolumeTexture9* wrapOut(void*, IDirect3DVolumeTexture9* r) { return wrapIdentity<WVolTex>(r, initVolTex); }
IDirect3DVolume9* wrapOut(void*, IDirect3DVolume9* r) { return wrapIdentity<WVolume>(r, initVolume); }
IDirect3DVertexBuffer9* wrapOut(void*, IDirect3DVertexBuffer9* r) { return wrapIdentity<WVB>(r, initVB); }
IDirect3DIndexBuffer9* wrapOut(void*, IDirect3DIndexBuffer9* r) { return wrapIdentity<WIB>(r, initIB); }
IDirect3DVertexShader9* wrapOut(void*, IDirect3DVertexShader9* r) { return wrapIdentity<WVS>(r); }
IDirect3DPixelShader9* wrapOut(void*, IDirect3DPixelShader9* r) { return wrapIdentity<WPS>(r); }
IDirect3DVertexDeclaration9* wrapOut(void*, IDirect3DVertexDeclaration9* r) { return wrapIdentity<WDecl>(r); }
IDirect3DStateBlock9* wrapOut(void*, IDirect3DStateBlock9* r) { return wrapIdentity<WSB>(r); }
IDirect3DQuery9* wrapOut(void*, IDirect3DQuery9* r) { return wrapIdentity<WQuery>(r); }
IDirect3DSwapChain9* wrapOut(void*, IDirect3DSwapChain9* r) {
    IDirect3DSwapChain9Ex* ex = nullptr; bool isEx = SUCCEEDED(r->QueryInterface(__uuidof(IDirect3DSwapChain9Ex), (void**)&ex)); if (ex) ex->Release();
    auto* w = static_cast<WSwap*>(wrapIdentity<WSwap>(static_cast<IDirect3DSwapChain9Ex*>(r))); w->ex = isEx; return w;
}
IDirect3DBaseTexture9* wrapOut(void* s, IDirect3DBaseTexture9* r) {
    switch (r->GetType()) {
    case D3DRTYPE_TEXTURE: return wrapOut(s, static_cast<IDirect3DTexture9*>(r));
    case D3DRTYPE_CUBETEXTURE: return wrapOut(s, static_cast<IDirect3DCubeTexture9*>(r));
    case D3DRTYPE_VOLUMETEXTURE: return wrapOut(s, static_cast<IDirect3DVolumeTexture9*>(r));
    default: return r;
    }
}
}

// ------------------------------------------------------------------ main-thread shadow state
namespace sh {
static D3DVIEWPORT9 vp; static RECT scissor; static bool rtResetsScissor;
static IDirect3DSurface9* rt[4]; static IDirect3DSurface9* ds;
static IDirect3DVertexBuffer9* vb[16]; static UINT off[16], stride[16], freq[16];
static IDirect3DIndexBuffer9* ib; static IDirect3DVertexDeclaration9* decl; static DWORD fvf;
static IDirect3DVertexShader9* vs; static IDirect3DPixelShader9* ps; static float npatch;
static IDirect3DBaseTexture9* tex[264];
static bool known = true, declKnown = true, fvfKnown = true;
static DWORD fvfAfterDecl;
struct Snapshot { D3DVIEWPORT9 vp; RECT scissor; IDirect3DSurface9* ds; IDirect3DVertexBuffer9* vb[16]; UINT off[16], stride[16], freq[16];
    IDirect3DIndexBuffer9* ib; IDirect3DVertexDeclaration9* decl; DWORD fvf; IDirect3DVertexShader9* vs; IDirect3DPixelShader9* ps; float npatch;
    IDirect3DBaseTexture9* tex[264]; bool declKnown, fvfKnown; };
static void saveRaw(Snapshot& s) { s.vp = vp; s.scissor = scissor; s.ds = ds; memcpy(s.vb, vb, sizeof vb); memcpy(s.off, off, sizeof off); memcpy(s.stride, stride, sizeof stride);
    memcpy(s.freq, freq, sizeof freq); s.ib = ib; s.decl = decl; s.fvf = fvf; s.vs = vs; s.ps = ps; s.npatch = npatch; memcpy(s.tex, tex, sizeof tex); s.declKnown = declKnown; s.fvfKnown = fvfKnown; }
static void loadRaw(const Snapshot& s) { vp = s.vp; scissor = s.scissor; ds = s.ds; memcpy(vb, s.vb, sizeof vb); memcpy(off, s.off, sizeof off); memcpy(stride, s.stride, sizeof stride);
    memcpy(freq, s.freq, sizeof freq); ib = s.ib; decl = s.decl; fvf = s.fvf; vs = s.vs; ps = s.ps; npatch = s.npatch; memcpy(tex, s.tex, sizeof tex); declKnown = s.declKnown; fvfKnown = s.fvfKnown; }
template<class T> static void set(T*& slot, T* v) { if (v) v->AddRef(); T* old = slot; slot = v; if (old) old->Release(); }
template<class F> static void eachPtr(Snapshot& s, F f) { f((IUnknown*)s.ds); for (auto* v : s.vb) f((IUnknown*)v); f((IUnknown*)s.ib); f((IUnknown*)s.decl); f((IUnknown*)s.vs); f((IUnknown*)s.ps); for (auto* t : s.tex) f((IUnknown*)t); }
static void addRefs(Snapshot& s) { eachPtr(s, [](IUnknown* p) { if (p) p->AddRef(); }); }
static void releaseRefs(Snapshot& s) { eachPtr(s, [](IUnknown* p) { if (p) p->Release(); }); }
static void currentShadow(Snapshot& s) { saveRaw(s); }
// A snapshot owns references to every object it names; so does the live shadow.
static void save(Snapshot& s) { Snapshot old = s; saveRaw(s); addRefs(s); releaseRefs(old); }
static void load(const Snapshot& s) { Snapshot cur; saveRaw(cur); Snapshot n = s; addRefs(n); loadRaw(n); releaseRefs(cur); }
static void clearAll() { Snapshot cur; saveRaw(cur); Snapshot empty = cur; empty.ds = nullptr; for (auto& v : empty.vb) v = nullptr; empty.ib = nullptr; empty.decl = nullptr; empty.vs = nullptr; empty.ps = nullptr;
    for (auto& t : empty.tex) t = nullptr; loadRaw(empty); releaseRefs(cur); for (auto*& r : rt) set(r, (IDirect3DSurface9*)nullptr); }
static inline int texIndex(DWORD stage) { return stage < 16 ? int(stage) : (stage >= D3DVERTEXTEXTURESAMPLER0 && stage <= D3DVERTEXTEXTURESAMPLER3) ? int(16 + stage - D3DVERTEXTEXTURESAMPLER0) : -1; }
}
static std::unordered_map<void*, sh::Snapshot*> g_sbShadow;      // state block wrapper -> shadow captured with it (main thread)

// Pull every shadowed value from the real device; called before the render thread starts or inside sync.
template<class T> static T* keep(T* w) { return w; }
static void refreshShadows(IDirect3DDevice9* d) {
    sh::Snapshot old; sh::saveRaw(old); IDirect3DSurface9* oldRt[4]; memcpy(oldRt, sh::rt, sizeof oldRt);
    d->GetViewport(&sh::vp); d->GetScissorRect(&sh::scissor);
    for (DWORD i = 0; i < 4; ++i) { IDirect3DSurface9* s = nullptr; d->GetRenderTarget(i, &s); sh::rt[i] = s ? keep(rt::wrapOut(nullptr, s)) : nullptr; }
    { IDirect3DSurface9* s = nullptr; d->GetDepthStencilSurface(&s); sh::ds = s ? keep(rt::wrapOut(nullptr, s)) : nullptr; }
    for (UINT n = 0; n < 16; ++n) { IDirect3DVertexBuffer9* b = nullptr; d->GetStreamSource(n, &b, &sh::off[n], &sh::stride[n]); sh::vb[n] = b ? keep(rt::wrapOut(nullptr, b)) : nullptr; d->GetStreamSourceFreq(n, &sh::freq[n]); }
    { IDirect3DIndexBuffer9* b = nullptr; d->GetIndices(&b); sh::ib = b ? keep(rt::wrapOut(nullptr, b)) : nullptr; }
    { IDirect3DVertexDeclaration9* x = nullptr; d->GetVertexDeclaration(&x); sh::decl = x ? keep(rt::wrapOut(nullptr, x)) : nullptr; }
    d->GetFVF(&sh::fvf); sh::npatch = d->GetNPatchMode();
    { IDirect3DVertexShader9* x = nullptr; d->GetVertexShader(&x); sh::vs = x ? keep(rt::wrapOut(nullptr, x)) : nullptr; }
    { IDirect3DPixelShader9* x = nullptr; d->GetPixelShader(&x); sh::ps = x ? keep(rt::wrapOut(nullptr, x)) : nullptr; }
    for (DWORD s = 0; s < 264; ++s) sh::tex[s] = nullptr;
    for (DWORD s = 0; s < 16; ++s) { IDirect3DBaseTexture9* t = nullptr; d->GetTexture(s, &t); sh::tex[s] = t ? keep(rt::wrapOut(nullptr, t)) : nullptr; }
    for (DWORD s = 0; s < 4; ++s) { IDirect3DBaseTexture9* t = nullptr; d->GetTexture(D3DVERTEXTEXTURESAMPLER0 + s, &t); sh::tex[16 + s] = t ? keep(rt::wrapOut(nullptr, t)) : nullptr; }
    sh::known = sh::declKnown = sh::fvfKnown = true;
    sh::releaseRefs(old); for (auto* r : oldRt) if (r) r->Release();
}
static void ensureShadows() {
    if (sh::known) return;
    rt::sync([]() -> int { refreshShadows(reinterpret_cast<IDirect3DDevice9*>(rt::unwrap(rt::device()))); return 0; }, "shadow.refresh");
}

// ------------------------------------------------------------------ device: cached/immutable answers
static D3DCAPS9 g_caps; static D3DDEVICE_CREATION_PARAMETERS g_cparams; static IDirect3D9* g_d3d;
static volatile HRESULT g_coop = D3D_OK, g_presentHr = D3D_OK;
namespace rt { inline IDirect3DDevice9Ex* device() { return reinterpret_cast<IDirect3DDevice9Ex*>(g_dev); } }
#define REAL (this->real)
HRESULT WSwap::GetDevice(IDirect3DDevice9** o) { *o = rt::device(); rt::device()->AddRef(); return D3D_OK; }
#define RES_GETDEVICE(W) HRESULT W::GetDevice(IDirect3DDevice9** o) { *o = rt::device(); rt::device()->AddRef(); return D3D_OK; }
RES_GETDEVICE(WTexture) RES_GETDEVICE(WCube) RES_GETDEVICE(WVolTex) RES_GETDEVICE(WSurface) RES_GETDEVICE(WVolume) RES_GETDEVICE(WVB) RES_GETDEVICE(WIB)
RES_GETDEVICE(WVS) RES_GETDEVICE(WPS) RES_GETDEVICE(WDecl) RES_GETDEVICE(WSB) RES_GETDEVICE(WQuery)
HRESULT WDevice::TestCooperativeLevel() { return g_coop; }
HRESULT WDevice::CheckDeviceState(HWND) { return g_coop; }
UINT WDevice::GetAvailableTextureMem() { return REAL->GetAvailableTextureMem(); }
HRESULT WDevice::GetDeviceCaps(D3DCAPS9* c) { *c = g_caps; return D3D_OK; }
HRESULT WDevice::GetCreationParameters(D3DDEVICE_CREATION_PARAMETERS* p) { *p = g_cparams; return D3D_OK; }
HRESULT WDevice::GetDirect3D(IDirect3D9** o) { *o = g_d3d; g_d3d->AddRef(); return D3D_OK; }

// ------------------------------------------------------------------ device: recorded state
#define ASYNC(name, body) rt::async([=]() { body; }, name)
static void markQueuedUse(IDirect3DBaseTexture9* t) {
    if (t && rt::isWrapper(t) && t->GetType() == D3DRTYPE_TEXTURE) InterlockedExchange(&static_cast<WTexture*>(static_cast<IDirect3DTexture9*>(t))->queuedUse, 1);
}
HRESULT WDevice::SetRenderState(D3DRENDERSTATETYPE s, DWORD v) { auto r = REAL; return ASYNC("SetRenderState", r->SetRenderState(s, v)); }
HRESULT WDevice::SetSamplerState(DWORD st, D3DSAMPLERSTATETYPE s, DWORD v) { auto r = REAL; return ASYNC("SetSamplerState", r->SetSamplerState(st, s, v)); }
HRESULT WDevice::SetTextureStageState(DWORD st, D3DTEXTURESTAGESTATETYPE s, DWORD v) { auto r = REAL; return ASYNC("SetTextureStageState", r->SetTextureStageState(st, s, v)); }
HRESULT WDevice::SetTexture(DWORD st, IDirect3DBaseTexture9* t) {
    auto r = REAL; auto u = rt::unwrap(t); HRESULT h = ASYNC("SetTexture", r->SetTexture(st, u));
    if (rt::onMain()) { int i = sh::texIndex(st); if (i >= 0) sh::set(sh::tex[i], t); markQueuedUse(t); }
    return h;
}
HRESULT WDevice::GetTexture(DWORD st, IDirect3DBaseTexture9** o) {
    if (rt::onRender()) { HRESULT h = REAL->GetTexture(st, o); if (*o) *o = rt::wrapOut(this, *o); return h; }
    int i = sh::texIndex(st);
    if (!rt::onMain() || i < 0 || !sh::known) { ensureShadows(); if (i < 0 || !rt::onMain()) return rt::sync([&]() -> HRESULT { HRESULT h = REAL->GetTexture(st, o); if (*o) *o = rt::wrapOut(this, *o); return h; }, "GetTexture"); }
    *o = sh::tex[i]; if (*o) (*o)->AddRef(); return D3D_OK;
}
HRESULT WDevice::SetVertexShader(IDirect3DVertexShader9* s) { auto r = REAL; auto u = rt::unwrap(s); HRESULT h = ASYNC("SetVertexShader", r->SetVertexShader(u)); if (rt::onMain()) sh::set(sh::vs, s); return h; }
HRESULT WDevice::SetPixelShader(IDirect3DPixelShader9* s) { auto r = REAL; auto u = rt::unwrap(s); HRESULT h = ASYNC("SetPixelShader", r->SetPixelShader(u)); if (rt::onMain()) sh::set(sh::ps, s); return h; }
HRESULT WDevice::GetVertexShader(IDirect3DVertexShader9** o) { if (rt::onRender()) { HRESULT h = REAL->GetVertexShader(o); if (*o) *o = rt::wrapOut(this, *o); return h; } ensureShadows(); *o = sh::vs; if (*o) (*o)->AddRef(); return D3D_OK; }
HRESULT WDevice::GetPixelShader(IDirect3DPixelShader9** o) { if (rt::onRender()) { HRESULT h = REAL->GetPixelShader(o); if (*o) *o = rt::wrapOut(this, *o); return h; } ensureShadows(); *o = sh::ps; if (*o) (*o)->AddRef(); return D3D_OK; }
HRESULT WDevice::SetIndices(IDirect3DIndexBuffer9* b) { auto r = REAL; auto u = rt::unwrap(b); HRESULT h = ASYNC("SetIndices", r->SetIndices(u)); if (rt::onMain()) sh::set(sh::ib, b); return h; }
HRESULT WDevice::GetIndices(IDirect3DIndexBuffer9** o) { if (rt::onRender()) { HRESULT h = REAL->GetIndices(o); if (*o) *o = rt::wrapOut(this, *o); return h; } ensureShadows(); *o = sh::ib; if (*o) (*o)->AddRef(); return D3D_OK; }
static void freeFvf(void* p) { delete static_cast<DWORD*>(p); }
static DWORD* declFvf(IDirect3DVertexDeclaration9* d) { return d && rt::isWrapper(d) ? static_cast<DWORD*>(static_cast<WDecl*>(d)->ext) : nullptr; }
HRESULT WDevice::SetVertexDeclaration(IDirect3DVertexDeclaration9* d) {
    auto r = REAL; auto u = rt::unwrap(d); HRESULT h = ASYNC("SetVertexDeclaration", r->SetVertexDeclaration(u));
    if (rt::onMain()) { sh::set(sh::decl, d); sh::declKnown = true; DWORD* f = declFvf(d); sh::fvfKnown = f != nullptr; if (f) sh::fvf = *f; }
    return h;
}
HRESULT WDevice::GetVertexDeclaration(IDirect3DVertexDeclaration9** o) {
    if (rt::onRender()) { HRESULT h = REAL->GetVertexDeclaration(o); if (*o) *o = rt::wrapOut(this, *o); return h; }
    ensureShadows();
    if (!sh::declKnown) return rt::sync([&]() -> HRESULT { HRESULT h = REAL->GetVertexDeclaration(o); if (*o) *o = rt::wrapOut(this, *o); sh::set(sh::decl, *o); sh::declKnown = true; return h; }, "GetVertexDeclaration");
    *o = sh::decl; if (*o) (*o)->AddRef(); return D3D_OK;
}
HRESULT WDevice::SetFVF(DWORD f) { if (rt::onMain()) { sh::fvf = f; sh::fvfKnown = true; sh::declKnown = false; } auto r = REAL; return ASYNC("SetFVF", r->SetFVF(f)); }
HRESULT WDevice::GetFVF(DWORD* f) {
    if (rt::onRender()) return REAL->GetFVF(f);
    ensureShadows();
    if (!sh::fvfKnown) { rt::sync([&]() -> HRESULT { return REAL->GetFVF(&sh::fvf); }, "GetFVF"); sh::fvfKnown = true; if (sh::declKnown && rt::onMain() && sh::decl && rt::isWrapper(sh::decl)) { auto* w = static_cast<WDecl*>(sh::decl); if (!w->ext) { w->ext = new DWORD(sh::fvf); w->extFree = freeFvf; } } }
    *f = sh::fvf; return D3D_OK;
}
HRESULT WDevice::SetNPatchMode(float n) { if (rt::onMain()) sh::npatch = n; auto r = REAL; return ASYNC("SetNPatchMode", r->SetNPatchMode(n)); }
float WDevice::GetNPatchMode() { if (rt::onRender()) return REAL->GetNPatchMode(); ensureShadows(); return sh::npatch; }
HRESULT WDevice::SetSoftwareVertexProcessing(BOOL b) { auto r = REAL; return ASYNC("SetSoftwareVertexProcessing", r->SetSoftwareVertexProcessing(b)); }
#define CONSTS(Name, T, W) \
HRESULT WDevice::Name(UINT s, const T* v, UINT c) { \
    auto r = REAL; \
    if (!rt::onMain()) return rt::sync([&]() -> HRESULT { return r->Name(s, v, c); }, #Name); \
    rt::postData(v, c * W, [r, s, c](const uint8_t* d) { r->Name(s, reinterpret_cast<const T*>(d), c); }); return D3D_OK; }
CONSTS(SetVertexShaderConstantF, float, 16) CONSTS(SetVertexShaderConstantI, int, 16) CONSTS(SetVertexShaderConstantB, BOOL, 4)
CONSTS(SetPixelShaderConstantF, float, 16) CONSTS(SetPixelShaderConstantI, int, 16) CONSTS(SetPixelShaderConstantB, BOOL, 4)
HRESULT WDevice::SetStreamSource(UINT n, IDirect3DVertexBuffer9* b, UINT o, UINT s) {
    auto r = REAL; auto u = rt::unwrap(b); HRESULT h = ASYNC("SetStreamSource", r->SetStreamSource(n, u, o, s));
    if (rt::onMain() && n < 16) { sh::set(sh::vb[n], b); sh::off[n] = o; sh::stride[n] = s; }
    return h;
}
HRESULT WDevice::GetStreamSource(UINT n, IDirect3DVertexBuffer9** b, UINT* o, UINT* s) {
    if (rt::onRender()) { HRESULT h = REAL->GetStreamSource(n, b, o, s); if (*b) *b = rt::wrapOut(this, *b); return h; }
    ensureShadows(); if (n >= 16) return D3DERR_INVALIDCALL;
    *b = sh::vb[n]; if (*b) (*b)->AddRef(); *o = sh::off[n]; *s = sh::stride[n]; return D3D_OK;
}
HRESULT WDevice::SetStreamSourceFreq(UINT n, UINT f) { if (rt::onMain() && n < 16) sh::freq[n] = f; auto r = REAL; return ASYNC("SetStreamSourceFreq", r->SetStreamSourceFreq(n, f)); }
HRESULT WDevice::GetStreamSourceFreq(UINT n, UINT* f) { if (rt::onRender()) return REAL->GetStreamSourceFreq(n, f); ensureShadows(); if (n >= 16) return D3DERR_INVALIDCALL; *f = sh::freq[n]; return D3D_OK; }
HRESULT WDevice::SetRenderTarget(DWORD i, IDirect3DSurface9* s) {
    auto r = REAL; auto u = rt::unwrap(s); HRESULT h = ASYNC("SetRenderTarget", r->SetRenderTarget(i, u));
    if (rt::onMain() && i < 4) {
        sh::set(sh::rt[i], s);
        if (i == 0 && s) {
            D3DSURFACE_DESC d = lookup(g_surfDesc, (void*)s);
            sh::vp.X = sh::vp.Y = 0; sh::vp.Width = d.Width; sh::vp.Height = d.Height; sh::vp.MinZ = 0; sh::vp.MaxZ = 1;
            if (sh::rtResetsScissor) { sh::scissor.left = sh::scissor.top = 0; sh::scissor.right = LONG(d.Width); sh::scissor.bottom = LONG(d.Height); }
        }
    }
    return h;
}
HRESULT WDevice::GetRenderTarget(DWORD i, IDirect3DSurface9** o) { if (rt::onRender()) { HRESULT h = REAL->GetRenderTarget(i, o); if (*o) *o = rt::wrapOut(this, *o); return h; } ensureShadows(); if (i >= 4) return D3DERR_INVALIDCALL; *o = sh::rt[i]; if (!*o) return D3DERR_NOTFOUND; (*o)->AddRef(); return D3D_OK; }
HRESULT WDevice::SetDepthStencilSurface(IDirect3DSurface9* s) { auto r = REAL; auto u = rt::unwrap(s); HRESULT h = ASYNC("SetDepthStencilSurface", r->SetDepthStencilSurface(u)); if (rt::onMain()) sh::set(sh::ds, s); return h; }
HRESULT WDevice::GetDepthStencilSurface(IDirect3DSurface9** o) { if (rt::onRender()) { HRESULT h = REAL->GetDepthStencilSurface(o); if (*o) *o = rt::wrapOut(this, *o); return h; } ensureShadows(); *o = sh::ds; if (!*o) return D3DERR_NOTFOUND; (*o)->AddRef(); return D3D_OK; }
HRESULT WDevice::SetViewport(const D3DVIEWPORT9* v) { if (rt::onMain()) sh::vp = *v; auto r = REAL; D3DVIEWPORT9 c = *v; return ASYNC("SetViewport", r->SetViewport(&c)); }
HRESULT WDevice::GetViewport(D3DVIEWPORT9* v) { if (rt::onRender()) return REAL->GetViewport(v); ensureShadows(); *v = sh::vp; return D3D_OK; }
HRESULT WDevice::SetScissorRect(const RECT* x) { if (rt::onMain()) sh::scissor = *x; auto r = REAL; RECT c = *x; return ASYNC("SetScissorRect", r->SetScissorRect(&c)); }
HRESULT WDevice::GetScissorRect(RECT* x) { if (rt::onRender()) return REAL->GetScissorRect(x); ensureShadows(); *x = sh::scissor; return D3D_OK; }
HRESULT WDevice::SetTransform(D3DTRANSFORMSTATETYPE t, const D3DMATRIX* m) { auto r = REAL; D3DMATRIX c = *m; return ASYNC("SetTransform", r->SetTransform(t, &c)); }
HRESULT WDevice::SetClipPlane(DWORD i, const float* p) { auto r = REAL; struct P { float v[4]; } c; memcpy(c.v, p, 16); return ASYNC("SetClipPlane", r->SetClipPlane(i, c.v)); }
HRESULT WDevice::SetMaterial(const D3DMATERIAL9* m) { auto r = REAL; D3DMATERIAL9 c = *m; return ASYNC("SetMaterial", r->SetMaterial(&c)); }
HRESULT WDevice::SetLight(DWORD i, const D3DLIGHT9* l) { auto r = REAL; D3DLIGHT9 c = *l; return ASYNC("SetLight", r->SetLight(i, &c)); }
HRESULT WDevice::LightEnable(DWORD i, BOOL e) { auto r = REAL; return ASYNC("LightEnable", r->LightEnable(i, e)); }
HRESULT WDevice::Clear(DWORD n, const D3DRECT* rects, DWORD f, D3DCOLOR c, float z, DWORD s) {
    auto r = REAL; if (!rects) n = 0;
    if (!rt::onMain()) return rt::sync([&]() -> HRESULT { return r->Clear(n, rects, f, c, z, s); }, "Clear");
    rt::postData(rects, n * sizeof(D3DRECT), [r, n, f, c, z, s](const uint8_t* d) { r->Clear(n, n ? reinterpret_cast<const D3DRECT*>(d) : nullptr, f, c, z, s); });
    return D3D_OK;
}
HRESULT WDevice::BeginScene() { auto r = REAL; return ASYNC("BeginScene", r->BeginScene()); }
HRESULT WDevice::EndScene() { auto r = REAL; return ASYNC("EndScene", r->EndScene()); }
HRESULT WDevice::DrawIndexedPrimitive(D3DPRIMITIVETYPE t, INT b, UINT m, UINT n, UINT s, UINT c) { auto r = REAL; return ASYNC("DrawIndexedPrimitive", r->DrawIndexedPrimitive(t, b, m, n, s, c)); }
HRESULT WDevice::DrawPrimitive(D3DPRIMITIVETYPE t, UINT s, UINT c) { auto r = REAL; return ASYNC("DrawPrimitive", r->DrawPrimitive(t, s, c)); }
static UINT vertsFor(D3DPRIMITIVETYPE t, UINT c) {
    switch (t) { case D3DPT_POINTLIST: return c; case D3DPT_LINELIST: return c * 2; case D3DPT_LINESTRIP: return c + 1;
    case D3DPT_TRIANGLELIST: return c * 3; case D3DPT_TRIANGLESTRIP: case D3DPT_TRIANGLEFAN: return c + 2; default: return 0; }
}
HRESULT WDevice::DrawPrimitiveUP(D3DPRIMITIVETYPE t, UINT c, const void* v, UINT stride) {
    auto r = REAL;
    if (!rt::onMain()) return rt::sync([&]() -> HRESULT { return r->DrawPrimitiveUP(t, c, v, stride); }, "DrawPrimitiveUP");
    rt::postData(v, vertsFor(t, c) * stride, [r, t, c, stride](const uint8_t* d) { r->DrawPrimitiveUP(t, c, d, stride); });
    sh::set(sh::vb[0], (IDirect3DVertexBuffer9*)nullptr); sh::off[0] = 0; sh::stride[0] = 0;
    return D3D_OK;
}
HRESULT WDevice::DrawIndexedPrimitiveUP(D3DPRIMITIVETYPE t, UINT minV, UINT nV, UINT c, const void* idx, D3DFORMAT f, const void* v, UINT stride) {
    auto r = REAL;
    if (!rt::onMain()) return rt::sync([&]() -> HRESULT { return r->DrawIndexedPrimitiveUP(t, minV, nV, c, idx, f, v, stride); }, "DrawIndexedPrimitiveUP");
    const UINT ib = vertsFor(t, c) * (f == D3DFMT_INDEX32 ? 4 : 2), ibA = (ib + 7) & ~7u, vb = (minV + nV) * stride;
    std::vector<uint8_t> buf(ibA + vb); memcpy(buf.data(), idx, ib); memcpy(buf.data() + ibA, v, vb);
    rt::postData(buf.data(), UINT(buf.size()), [r, t, minV, nV, c, f, stride, ibA](const uint8_t* d) { r->DrawIndexedPrimitiveUP(t, minV, nV, c, d, f, d + ibA, stride); });
    sh::set(sh::vb[0], (IDirect3DVertexBuffer9*)nullptr); sh::off[0] = 0; sh::stride[0] = 0; sh::set(sh::ib, (IDirect3DIndexBuffer9*)nullptr);
    return D3D_OK;
}
HRESULT WDevice::StretchRect(IDirect3DSurface9* s, const RECT* sr, IDirect3DSurface9* d, const RECT* dr, D3DTEXTUREFILTERTYPE f) {
    auto r = REAL; auto us = rt::unwrap(s), ud = rt::unwrap(d); RECT a = sr ? *sr : RECT(), b = dr ? *dr : RECT(); bool hs = sr, hd = dr;
    return ASYNC("StretchRect", r->StretchRect(us, hs ? &a : nullptr, ud, hd ? &b : nullptr, f));
}
HRESULT WDevice::ColorFill(IDirect3DSurface9* s, const RECT* x, D3DCOLOR c) { auto r = REAL; auto u = rt::unwrap(s); RECT a = x ? *x : RECT(); bool h = x; return ASYNC("ColorFill", r->ColorFill(u, h ? &a : nullptr, c)); }
HRESULT WDevice::UpdateTexture(IDirect3DBaseTexture9* s, IDirect3DBaseTexture9* d) { if (rt::onMain()) { markQueuedUse(s); markQueuedUse(d); } auto r = REAL; auto us = rt::unwrap(s), ud = rt::unwrap(d); return ASYNC("UpdateTexture", r->UpdateTexture(us, ud)); }
HRESULT WDevice::UpdateSurface(IDirect3DSurface9* s, const RECT* sr, IDirect3DSurface9* d, const POINT* p) {
    auto r = REAL; auto us = rt::unwrap(s), ud = rt::unwrap(d); RECT a = sr ? *sr : RECT(); POINT b = p ? *p : POINT(); bool ha = sr, hb = p;
    return ASYNC("UpdateSurface", r->UpdateSurface(us, ha ? &a : nullptr, ud, hb ? &b : nullptr));
}

// ------------------------------------------------------------------ frames
static void flushStats();
static void framePace() {
    long long t = rt::now();
    while (rt::g_framesQueued - rt::g_framesDone > 1) { rt::wake(); WaitForSingleObject(rt::g_idle, 1); }
    rt::g_paceTicks += rt::now() - t;
    t = rt::now(); if (rt::g_frameStart) rt::g_frameTicks += t - rt::g_frameStart; rt::g_frameStart = t;
    if (++rt::g_frames >= 600) flushStats();
}
static void presented(IDirect3DDevice9Ex* r, HRESULT hr) {
    g_presentHr = hr; g_coop = r->TestCooperativeLevel(); InterlockedIncrement(&rt::g_framesDone);
}
HRESULT WDevice::Present(const RECT* s, const RECT* d, HWND w, const RGNDATA* rg) {
    auto r = REAL;
    if (!rt::onMain() || rg) return rt::sync([&]() -> HRESULT { return r->Present(s, d, w, rg); }, "Present");
    RECT a = s ? *s : RECT(), b = d ? *d : RECT(); bool ha = s, hb = d;
    InterlockedIncrement(&rt::g_framesQueued);
    rt::post([r, a, b, ha, hb, w] { presented(r, r->Present(ha ? &a : nullptr, hb ? &b : nullptr, w, nullptr)); }); rt::g_sinceWake = 0; rt::wake();
    framePace(); return g_presentHr == D3DERR_DEVICELOST ? D3DERR_DEVICELOST : D3D_OK;
}
HRESULT WDevice::PresentEx(const RECT* s, const RECT* d, HWND w, const RGNDATA* rg, DWORD f) {
    auto r = REAL;
    if (!rt::onMain() || rg) return rt::sync([&]() -> HRESULT { return r->PresentEx(s, d, w, rg, f); }, "PresentEx");
    RECT a = s ? *s : RECT(), b = d ? *d : RECT(); bool ha = s, hb = d;
    InterlockedIncrement(&rt::g_framesQueued);
    rt::post([r, a, b, ha, hb, w, f] { presented(r, r->PresentEx(ha ? &a : nullptr, hb ? &b : nullptr, w, nullptr, f)); }); rt::g_sinceWake = 0; rt::wake();
    framePace(); return g_presentHr == D3DERR_DEVICELOST ? D3DERR_DEVICELOST : D3D_OK;
}
HRESULT WSwap::Present(const RECT* s, const RECT* d, HWND w, const RGNDATA* rg, DWORD f) {
    auto r = REAL;
    if (!rt::onMain() || rg) return rt::sync([&]() -> HRESULT { return r->Present(s, d, w, rg, f); }, "Swap.Present");
    RECT a = s ? *s : RECT(), b = d ? *d : RECT(); bool ha = s, hb = d;
    InterlockedIncrement(&rt::g_framesQueued);
    rt::post([r, a, b, ha, hb, w, f] { g_presentHr = r->Present(ha ? &a : nullptr, hb ? &b : nullptr, w, nullptr, f); InterlockedIncrement(&rt::g_framesDone); }); rt::g_sinceWake = 0; rt::wake();
    framePace(); return D3D_OK;
}

// ------------------------------------------------------------------ queries: created and polled by the render thread
struct QState { volatile LONG pending = 0, ready = 0; DWORD size = 0; uint8_t data[16]; };
static std::unordered_map<void*, QState> g_qstate; static SRWLOCK g_qLock = SRWLOCK_INIT;
static std::vector<WQuery*> g_qpoll;   // render thread only
static QState& qstate(void* q) { AcquireSRWLockExclusive(&g_qLock); QState& s = g_qstate[q]; ReleaseSRWLockExclusive(&g_qLock); return s; }
static std::unordered_map<void*, D3DQUERYTYPE> g_qtype;
static DWORD qsize(D3DQUERYTYPE t) {
    switch (t) { case D3DQUERYTYPE_EVENT: return sizeof(BOOL); case D3DQUERYTYPE_OCCLUSION: return sizeof(DWORD);
    case D3DQUERYTYPE_TIMESTAMP: case D3DQUERYTYPE_TIMESTAMPFREQ: return sizeof(UINT64); case D3DQUERYTYPE_TIMESTAMPDISJOINT: return sizeof(BOOL); default: return 0; }
}
static void pollQueries() {
    for (size_t i = 0; i < g_qpoll.size();) {
        WQuery* w = g_qpoll[i]; QState& st = qstate(w); uint8_t buf[16] = {}; DWORD n = st.size;
        if (w->real && w->real->GetData(buf, n, 0) == S_OK) {
            memcpy(st.data, buf, n); MemoryBarrier(); if (!st.pending) InterlockedExchange(&st.ready, 1);
            g_qpoll[i] = g_qpoll.back(); g_qpoll.pop_back(); w->Release();
        } else ++i;
    }
}
HRESULT WDevice::CreateQuery(D3DQUERYTYPE t, IDirect3DQuery9** o) {
    auto r = REAL;
    if (!o || qsize(t) == 0 || !rt::onMain())
        return rt::sync([&]() -> HRESULT { HRESULT h = r->CreateQuery(t, o); if (o && *o) *o = rt::wrapOut(this, *o); return h; }, "CreateQuery");
    WQuery* w = new WQuery(nullptr);
    bool known = false; for (int i = 0; i < rt::g_vtblCount; ++i) known |= rt::g_vtbls[i] == *(void**)w;
    if (!known) rt::g_vtbls[rt::g_vtblCount++] = *(void**)w;
    AcquireSRWLockExclusive(&g_qLock); g_qtype[w] = t; g_qstate[w].size = qsize(t); ReleaseSRWLockExclusive(&g_qLock);
    rt::post([r, t, w] { r->CreateQuery(t, &w->real); });
    *o = w; return D3D_OK;
}
D3DQUERYTYPE WQuery::GetType() { AcquireSRWLockShared(&g_qLock); auto it = g_qtype.find(this); D3DQUERYTYPE t = it != g_qtype.end() ? it->second : D3DQUERYTYPE(0); ReleaseSRWLockShared(&g_qLock);
    if (!t) t = rt::sync([&]() -> D3DQUERYTYPE { return real->GetType(); }, "Query.GetType"); return t; }
DWORD WQuery::GetDataSize() { DWORD s = qsize(GetType()); return s ? s : rt::sync([&]() -> DWORD { return real->GetDataSize(); }, "Query.GetDataSize"); }
HRESULT WQuery::Issue(DWORD f) {
    QState& st = qstate(this); if (!st.size) st.size = qsize(GetType());
    InterlockedIncrement(&st.pending); InterlockedExchange(&st.ready, 0);
    WQuery* w = this; AddRef();
    auto run = [w, f] {
        w->real->Issue(f); QState& s = qstate(w); InterlockedDecrement(&s.pending);
        if (f & D3DISSUE_END) { bool listed = false; for (auto* q : g_qpoll) listed |= q == w; if (!listed) { w->AddRef(); g_qpoll.push_back(w); } }
        w->Release();
    };
    if (rt::onMain()) { rt::post(run); return D3D_OK; }
    return rt::sync([&]() -> HRESULT { run(); return D3D_OK; }, "Query.Issue");
}
HRESULT WQuery::GetData(void* d, DWORD s, DWORD f) {
    if (rt::onRender()) return real ? real->GetData(d, s, f) : S_FALSE;
    QState& st = qstate(this);
    if (st.ready) { if (d && s) memcpy(d, st.data, s < st.size ? s : st.size); return S_OK; }
    if (f & D3DGETDATA_FLUSH) rt::wake();
    return S_FALSE;
}

// ------------------------------------------------------------------ state blocks
HRESULT WDevice::CreateStateBlock(D3DSTATEBLOCKTYPE t, IDirect3DStateBlock9** o) {
    auto r = REAL;
    if (!rt::onMain()) return rt::sync([&]() -> HRESULT { HRESULT h = r->CreateStateBlock(t, o); if (*o) *o = rt::wrapOut(this, *o); return h; }, "CreateStateBlock");
    ensureShadows();
    WSB* w = new WSB(nullptr);
    bool known = false; for (int i = 0; i < rt::g_vtblCount; ++i) known |= rt::g_vtbls[i] == *(void**)w;
    if (!known) rt::g_vtbls[rt::g_vtblCount++] = *(void**)w;
    if (t == D3DSBT_ALL) { auto* s = new sh::Snapshot(); sh::saveRaw(*s); sh::addRefs(*s); g_sbShadow[w] = s; }
    rt::post([r, t, w] { r->CreateStateBlock(t, &w->real); });
    *o = w; return D3D_OK;
}
HRESULT WSB::Capture() {
    if (rt::onRender()) return real->Capture();
    if (!rt::onMain()) return rt::sync([&]() -> HRESULT { return real->Capture(); }, "SB.Capture");
    ensureShadows(); auto it = g_sbShadow.find(this); if (it != g_sbShadow.end()) sh::save(*it->second);
    WSB* w = this; AddRef(); rt::post([w] { w->real->Capture(); w->Release(); }); return D3D_OK;
}
HRESULT WSB::Apply() {
    if (rt::onRender()) return real->Apply();
    if (!rt::onMain()) return rt::sync([&]() -> HRESULT { return real->Apply(); }, "SB.Apply");
    auto it = g_sbShadow.find(this); if (it != g_sbShadow.end()) sh::load(*it->second); else sh::known = false;
    WSB* w = this; AddRef(); rt::post([w] { w->real->Apply(); w->Release(); }); return D3D_OK;
}

// ------------------------------------------------------------------ resources: cached descriptions, locks
D3DRESOURCETYPE WTexture::GetType() { return D3DRTYPE_TEXTURE; }
D3DRESOURCETYPE WCube::GetType() { return D3DRTYPE_CUBETEXTURE; }
D3DRESOURCETYPE WVolTex::GetType() { return D3DRTYPE_VOLUMETEXTURE; }
D3DRESOURCETYPE WSurface::GetType() { return D3DRTYPE_SURFACE; }
D3DRESOURCETYPE WVB::GetType() { return D3DRTYPE_VERTEXBUFFER; }
D3DRESOURCETYPE WIB::GetType() { return D3DRTYPE_INDEXBUFFER; }
DWORD WTexture::GetLevelCount() { return DWORD(lookup(g_levels, (void*)this).levels.size()); }
DWORD WCube::GetLevelCount() { return DWORD(lookup(g_levels, (void*)this).levels.size()); }
DWORD WVolTex::GetLevelCount() { return DWORD(lookup(g_volLevels, (void*)this).size()); }
HRESULT WTexture::GetLevelDesc(UINT l, D3DSURFACE_DESC* d) { auto c = lookup(g_levels, (void*)this); if (l >= c.levels.size()) return D3DERR_INVALIDCALL; *d = c.levels[l]; return D3D_OK; }
HRESULT WCube::GetLevelDesc(UINT l, D3DSURFACE_DESC* d) { auto c = lookup(g_levels, (void*)this); if (l >= c.levels.size()) return D3DERR_INVALIDCALL; *d = c.levels[l]; return D3D_OK; }
HRESULT WVolTex::GetLevelDesc(UINT l, D3DVOLUME_DESC* d) { auto c = lookup(g_volLevels, (void*)this); if (l >= c.size()) return D3DERR_INVALIDCALL; *d = c[l]; return D3D_OK; }
HRESULT WSurface::GetDesc(D3DSURFACE_DESC* d) { *d = lookup(g_surfDesc, (void*)this); return D3D_OK; }
HRESULT WVolume::GetDesc(D3DVOLUME_DESC* d) { *d = lookup(g_volDesc, (void*)this); return D3D_OK; }
HRESULT WVB::GetDesc(D3DVERTEXBUFFER_DESC* d) { *d = lookup(g_vbDesc, (void*)this); return D3D_OK; }
HRESULT WIB::GetDesc(D3DINDEXBUFFER_DESC* d) { *d = lookup(g_ibDesc, (void*)this); return D3D_OK; }
// Level surfaces never change for a texture; the texture wrapper keeps one reference per level.
using LevelSurfaces = std::vector<IDirect3DSurface9*>;
static void freeLevelSurfaces(void* p) { auto* v = static_cast<LevelSurfaces*>(p); for (auto* x : *v) if (x) x->Release(); delete v; }
HRESULT WTexture::GetSurfaceLevel(UINT l, IDirect3DSurface9** o) {
    if (!rt::onRender()) InterlockedExchange(&queuedUse, 1);   // level surfaces can feed queued copies
    auto* cache = static_cast<LevelSurfaces*>(ext);
    if (rt::onMain() && cache && l < cache->size() && (*cache)[l]) { *o = (*cache)[l]; (*o)->AddRef(); return D3D_OK; }
    HRESULT h = real->GetSurfaceLevel(l, o); if (*o) *o = rt::wrapOut(this, *o);
    if (rt::onMain() && SUCCEEDED(h) && *o) {
        if (!cache) { cache = new LevelSurfaces(); ext = cache; extFree = freeLevelSurfaces; }
        if (cache->size() <= l) cache->resize(l + 1);
        if (!(*cache)[l]) { (*cache)[l] = *o; (*o)->AddRef(); }
    }
    return h;
}
// Non-dynamic locks: the render thread performs the real lock after all earlier work; the caller writes directly.
// A lockable texture no queued command has read can be locked by the caller: the MULTITHREADED
// runtime serialises the call, and waiting could deadlock against Scaleform jobs on the render thread.
static bool lockDirect(WTexture* w, UINT l) {
    if (rt::onRender() || w->queuedUse) return false;
    auto c = lookup(g_levels, (void*)w); return l < c.levels.size() && c.levels[l].Pool != D3DPOOL_DEFAULT;
}
HRESULT WTexture::LockRect(UINT l, D3DLOCKED_RECT* lr, const RECT* x, DWORD f) { if (lockDirect(this, l)) return real->LockRect(l, lr, x, f); return rt::sync([&]() -> HRESULT { return real->LockRect(l, lr, x, f); }, "Texture.LockRect"); }
HRESULT WTexture::UnlockRect(UINT l) { if (lockDirect(this, l)) return real->UnlockRect(l); auto r = real; return rt::async([r, l] { r->UnlockRect(l); }, "Texture.UnlockRect"); }
HRESULT WCube::LockRect(D3DCUBEMAP_FACES face, UINT l, D3DLOCKED_RECT* lr, const RECT* x, DWORD f) { return rt::sync([&]() -> HRESULT { return real->LockRect(face, l, lr, x, f); }, "Cube.LockRect"); }
HRESULT WCube::UnlockRect(D3DCUBEMAP_FACES face, UINT l) { auto r = real; return rt::async([r, face, l] { r->UnlockRect(face, l); }, "Cube.UnlockRect"); }
HRESULT WSurface::LockRect(D3DLOCKED_RECT* lr, const RECT* x, DWORD f) { return rt::sync([&]() -> HRESULT { return real->LockRect(lr, x, f); }, "Surface.LockRect"); }
HRESULT WSurface::UnlockRect() { auto r = real; return rt::async([r] { r->UnlockRect(); }, "Surface.UnlockRect"); }
static HRESULT wrapContainer(IUnknown* c, REFIID riid, void** o) {
    if (!c) { *o = nullptr; return E_NOINTERFACE; }
    if (riid == __uuidof(IDirect3DTexture9) || riid == __uuidof(IDirect3DBaseTexture9) || riid == __uuidof(IDirect3DCubeTexture9) || riid == __uuidof(IDirect3DVolumeTexture9) || riid == __uuidof(IDirect3DResource9)) {
        *o = rt::wrapOut(nullptr, static_cast<IDirect3DBaseTexture9*>(c)); return D3D_OK;
    }
    if (riid == __uuidof(IDirect3DDevice9) || riid == __uuidof(IDirect3DDevice9Ex)) { c->Release(); *o = rt::device(); rt::device()->AddRef(); return D3D_OK; }
    if (riid == __uuidof(IDirect3DSwapChain9) || riid == __uuidof(IDirect3DSwapChain9Ex)) { *o = rt::wrapOut(nullptr, static_cast<IDirect3DSwapChain9*>(c)); return D3D_OK; }
    c->Release(); *o = nullptr; return E_NOINTERFACE;
}
HRESULT WSurface::GetContainer(REFIID riid, void** o) { return rt::sync([&]() -> HRESULT { void* c = nullptr; HRESULT h = real->GetContainer(riid, &c); if (FAILED(h)) { *o = nullptr; return h; } return wrapContainer((IUnknown*)c, riid, o); }, "Surface.GetContainer"); }
HRESULT WVolume::GetContainer(REFIID riid, void** o) { return rt::sync([&]() -> HRESULT { void* c = nullptr; HRESULT h = real->GetContainer(riid, &c); if (FAILED(h)) { *o = nullptr; return h; } return wrapContainer((IUnknown*)c, riid, o); }, "Volume.GetContainer"); }

template<class W, class Desc> static HRESULT bufferLock(W* w, UINT off, UINT len, void** out, DWORD flags, Desc desc) {
    auto* r = w->real;
    const bool fast = rt::onMain() && (flags & (D3DLOCK_DISCARD | D3DLOCK_NOOVERWRITE)) && !(flags & D3DLOCK_READONLY);
    if (fast) {
        if (!w->ext) { w->ext = new Mirror(); w->extFree = freeMirror; }
        Mirror& m = *static_cast<Mirror*>(w->ext);
        if (!m.pending && (m.valid || (flags & D3DLOCK_DISCARD))) {
            if (m.data.size() != desc.Size) m.data.assign(desc.Size, 0);
            m.valid = true;
            if (!len && !off) len = desc.Size;
            if (off + len <= desc.Size) { m.pending = true; m.off = off; m.len = len; m.flags = flags; *out = m.data.data() + off; return D3D_OK; }
        }
    }
    HRESULT h = rt::sync([&]() -> HRESULT { return r->Lock(off, len, out, flags); }, "Buffer.Lock");
    if (rt::onMain() && SUCCEEDED(h) && w->ext) { Mirror& m = *static_cast<Mirror*>(w->ext); m.real = (uint8_t*)*out; m.off = off; m.len = len ? len : desc.Size; }
    return h;
}
template<class W> static HRESULT bufferUnlock(W* w) {
    auto* r = w->real;
    if (rt::onMain()) {
        Mirror* mp = static_cast<Mirror*>(w->ext);
        if (mp && mp->pending) {
            Mirror& m = *mp; m.pending = false; const UINT off = m.off, len = m.len; const DWORD flags = m.flags;
            rt::postData(m.data.data() + off, len, [r, off, len, flags](const uint8_t* d) { void* p = nullptr; if (SUCCEEDED(r->Lock(off, len, &p, flags)) && p) { memcpy(p, d, len); r->Unlock(); } });
            return D3D_OK;
        }
        if (mp && mp->real) { Mirror& m = *mp; if (m.off + m.len <= m.data.size()) memcpy(m.data.data() + m.off, m.real, m.len); m.real = nullptr; }
        rt::post([r] { r->Unlock(); }); return D3D_OK;
    }
    return rt::sync([&]() -> HRESULT { return r->Unlock(); }, "Buffer.Unlock");
}
HRESULT WVB::Lock(UINT o, UINT s, void** p, DWORD f) { return bufferLock(this, o, s, p, f, lookup(g_vbDesc, (void*)this)); }
HRESULT WVB::Unlock() { return bufferUnlock(this); }
HRESULT WIB::Lock(UINT o, UINT s, void** p, DWORD f) { return bufferLock(this, o, s, p, f, lookup(g_ibDesc, (void*)this)); }
HRESULT WIB::Unlock() { return bufferUnlock(this); }

// Reset needs every reference to default-pool surfaces gone: drop shadow references and run pending retirements first.
static void beforeReset() { rt::g_inlineRetire = true; sh::clearAll(); rt::runDeferred(); rt::g_inlineRetire = false; }
HRESULT WDevice::Reset(D3DPRESENT_PARAMETERS* p) { return rt::sync([&]() -> HRESULT { beforeReset(); HRESULT h = REAL->Reset(p); refreshShadows(REAL); return h; }, "Reset"); }
HRESULT WDevice::ResetEx(D3DPRESENT_PARAMETERS* p, D3DDISPLAYMODEEX* m) { return rt::sync([&]() -> HRESULT { beforeReset(); HRESULT h = REAL->ResetEx(p, m); refreshShadows(REAL); return h; }, "ResetEx"); }
HRESULT WDevice::EndStateBlock(IDirect3DStateBlock9** o) { return rt::sync([&]() -> HRESULT { HRESULT h = REAL->EndStateBlock(o); if (*o) *o = rt::wrapOut(this, *o); refreshShadows(REAL); return h; }, "EndStateBlock"); }

// ------------------------------------------------------------------ render thread
void rt::stallReport(const char* name) {
    if (!rt::reportLog()) return;
    HANDLE t = OpenThread(THREAD_SUSPEND_RESUME | THREAD_GET_CONTEXT, FALSE, rt::g_renderId);
    fprintf(rt::g_log, "STALL sync=%s caller=%lu render=%lu handle=%p\n", name, GetCurrentThreadId(), rt::g_renderId, t);
    if (t && SuspendThread(t) != (DWORD)-1) {
        CONTEXT c = {}; c.ContextFlags = CONTEXT_CONTROL;
        if (GetThreadContext(t, &c)) {
            uintptr_t pcs[48]; int n = 0; pcs[n++] = c.Eip;
            // Raw stack scan: every value that points into a loaded module's code is a candidate return address.
            for (uintptr_t* sp = (uintptr_t*)c.Esp; n < 48 && !IsBadReadPtr(sp, 4) && (uintptr_t)sp < c.Esp + 64 * 1024; ++sp) {
                HMODULE m = nullptr;
                if (*sp > 0x10000 && GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT, (LPCSTR)*sp, &m) && m) pcs[n++] = *sp;
            }
            for (int i = 0; i < n; ++i) {
                HMODULE m = nullptr; char path[MAX_PATH] = "?";
                GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT, (LPCSTR)pcs[i], &m);
                if (m) GetModuleFileNameA(m, path, MAX_PATH);
                const char* base = strrchr(path, '\\');
                fprintf(rt::g_log, "  %s+%lx\n", base ? base + 1 : path, (unsigned long)(pcs[i] - (uintptr_t)m));
            }
        }
        ResumeThread(t);
    }
    if (t) CloseHandle(t);
    fflush(rt::g_log);
}
static DWORD WINAPI renderThread(void*) {
    rt::g_renderId = GetCurrentThreadId();
    SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_ABOVE_NORMAL);
    for (;;) {
        if (rt::g_foreign) {
            std::vector<rt::SyncTask*> tasks;
            EnterCriticalSection(&rt::g_foreignLock); tasks.swap(rt::g_foreignTasks); InterlockedExchange(&rt::g_foreign, 0); LeaveCriticalSection(&rt::g_foreignLock);
            for (auto* t : tasks) rt::runTask(t);
        }
        const uint32_t tail = rt::g_tail;
        if (tail != (uint32_t)rt::g_head) {
            MemoryBarrier();
            rt::Rec* r = reinterpret_cast<rt::Rec*>(rt::g_ring + tail);
            if (!r->fn) { InterlockedExchange(&rt::g_tail, 0); continue; }
            r->fn(r + 1);
            MemoryBarrier(); InterlockedExchange(&rt::g_tail, tail + r->size);
            continue;
        }
        rt::runDeferred(); pollQueries();
        SetEvent(rt::g_idle);
        int spins = 0; while (rt::g_tail == rt::g_head && !rt::g_foreign && spins++ < 4000) YieldProcessor();
        if (rt::g_tail != rt::g_head || rt::g_foreign) continue;
        InterlockedExchange(&rt::g_sleeping, 1);
        if (rt::g_tail == rt::g_head && !rt::g_foreign) WaitForSingleObject(rt::g_work, g_qpoll.empty() ? 2 : 1);
        InterlockedExchange(&rt::g_sleeping, 0);
    }
}
static void flushStats() {
    if (!rt::g_stats || !rt::reportLog()) return;
    const double f = double(rt::g_frames), ms = 1000.0 / double(rt::g_freq.QuadPart);
    fprintf(rt::g_log, "frames=%lld avg_frame_ms=%.3f syncs/frame=%.2f sync_wait_ms/frame=%.3f pace_wait_ms/frame=%.3f queue_kb/frame=%.1f\n",
        rt::g_frames, rt::g_frameTicks * ms / f, rt::g_syncCount / f, rt::g_syncTicks * ms / f, rt::g_paceTicks * ms / f, rt::g_bytes / 1024.0 / f);
    for (auto& e : rt::g_syncBy) fprintf(rt::g_log, "  sync %s per_frame=%.2f\n", e.first, e.second / f);
    fflush(rt::g_log);
    rt::g_frames = rt::g_frameTicks = rt::g_syncCount = rt::g_syncTicks = rt::g_paceTicks = rt::g_bytes = 0; rt::g_syncBy.clear();
}

static IDirect3DDevice9Ex* wrapDevice(IDirect3DDevice9* real, bool isEx) {
    WDevice* w = new WDevice(static_cast<IDirect3DDevice9Ex*>(real)); w->ex = isEx;
    rt::g_vtbls[rt::g_vtblCount++] = *(void**)w;
    rt::g_dev = w; rt::g_main = GetCurrentThreadId();
    real->GetDeviceCaps(&g_caps); real->GetCreationParameters(&g_cparams); real->GetDirect3D(&g_d3d); g_d3d->Release();
    { RECT saved, probe = {1, 2, 3, 4}, after; IDirect3DSurface9* rt0 = nullptr;
      real->GetScissorRect(&saved); real->SetScissorRect(&probe);
      if (SUCCEEDED(real->GetRenderTarget(0, &rt0)) && rt0) { real->SetRenderTarget(0, rt0); rt0->Release(); }
      real->GetScissorRect(&after); sh::rtResetsScissor = memcmp(&after, &probe, sizeof(RECT)) != 0; real->SetScissorRect(&saved); }
    { D3DVERTEXELEMENT9 el[] = {{0, 0, D3DDECLTYPE_FLOAT3, D3DDECLMETHOD_DEFAULT, D3DDECLUSAGE_POSITION, 0}, D3DDECL_END()};
      IDirect3DVertexDeclaration9* d = nullptr, *prev = nullptr; DWORD prevFvf = 0; real->GetVertexDeclaration(&prev); real->GetFVF(&prevFvf);
      if (SUCCEEDED(real->CreateVertexDeclaration(el, &d)) && d) { real->SetVertexDeclaration(d); real->GetFVF(&sh::fvfAfterDecl); d->Release(); }
      if (prev) { real->SetVertexDeclaration(prev); prev->Release(); } else if (prevFvf) real->SetFVF(prevFvf); }
    refreshShadows(real);
    InitializeCriticalSection(&rt::g_foreignLock); InitializeCriticalSection(&rt::g_deferredLock);
    rt::g_ring = static_cast<uint8_t*>(VirtualAlloc(nullptr, rt::RING, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE));
    rt::g_work = CreateEventA(nullptr, FALSE, FALSE, nullptr); rt::g_idle = CreateEventA(nullptr, FALSE, FALSE, nullptr);
    rt::g_onDestroyMain = [](void* w) {

        auto it = g_sbShadow.find(w); if (it != g_sbShadow.end()) { sh::Snapshot* sn = it->second; g_sbShadow.erase(it); sh::releaseRefs(*sn); delete sn; } };
    rt::g_started = true;
    CreateThread(nullptr, 0, renderThread, nullptr, 0, nullptr);
    return w;
}

// ------------------------------------------------------------------ engine-facing render-thread API
// Post runs fn on the render thread in queue order, then restores every shadowed binding, so the
// main thread's view of the device stays true. Run executes fn there and waits.
static void restoreShadowed(IDirect3DDevice9Ex* r, const sh::Snapshot& s, IDirect3DSurface9* const* rts) {
    for (DWORD i = 0; i < 4 && i < g_caps.NumSimultaneousRTs; ++i) if (rts[i] || i) r->SetRenderTarget(i, rt::unwrap(rts[i]));
    r->SetDepthStencilSurface(rt::unwrap(s.ds)); r->SetViewport(&s.vp); r->SetScissorRect(&s.scissor);
    for (UINT n = 0; n < 16; ++n) { r->SetStreamSource(n, rt::unwrap(s.vb[n]), s.off[n], s.stride[n]); r->SetStreamSourceFreq(n, s.freq[n] ? s.freq[n] : 1); }
    r->SetIndices(rt::unwrap(s.ib));
    if (s.declKnown) r->SetVertexDeclaration(rt::unwrap(s.decl)); else if (s.fvfKnown) r->SetFVF(s.fvf);
    r->SetVertexShader(rt::unwrap(s.vs)); r->SetPixelShader(rt::unwrap(s.ps)); r->SetNPatchMode(s.npatch);
    for (DWORD t = 0; t < 16; ++t) r->SetTexture(t, rt::unwrap(s.tex[t]));
    for (DWORD t = 0; t < 4; ++t) r->SetTexture(D3DVERTEXTEXTURESAMPLER0 + t, rt::unwrap(s.tex[16 + t]));
}
}  // namespace

namespace wot { namespace d3d9rt {
IDirect3DDevice9* wrap_device(IDirect3DDevice9* device, bool ex) {
    if (!device) return device;
    // The thread owns one device for the process; a recreated device stays direct.
    if (rt::g_dev) { rt::g_retired = true; return device; }
    const char* mode = getenv("WOT_D3D9_RENDER_THREAD");
    if (mode && !strcmp(mode, "0")) return device;
    rt::g_stats = getenv("WOT_D3D9_RENDER_THREAD_STATS") != nullptr;
    QueryPerformanceFrequency(&rt::g_freq);
    return wrapDevice(device, ex);
}
DWORD render_thread_id() { return rt::g_started && !rt::g_retired ? rt::g_renderId : 0; }
void run(void (*fn)(void*), void* ctx) {
    if (!render_thread_id()) { fn(ctx); return; }
    rt::sync([&]() -> int { fn(ctx); return 0; }, "d3d9rt::run");
}
void post(void (*fn)(void*), void* ctx) {
    if (!render_thread_id()) { fn(ctx); return; }
    if (!rt::onMain()) { run(fn, ctx); return; }
    ensureShadows();
    auto* snap = new sh::Snapshot(); sh::saveRaw(*snap); sh::addRefs(*snap);
    auto* rts = new IDirect3DSurface9*[4]; for (int i = 0; i < 4; ++i) { rts[i] = sh::rt[i]; if (rts[i]) rts[i]->AddRef(); }
    auto* r = reinterpret_cast<WDevice*>(rt::g_dev)->real;
    rt::post([fn, ctx, snap, rts, r] {
        fn(ctx);
        restoreShadowed(r, *snap, rts);
        sh::releaseRefs(*snap); delete snap; for (int i = 0; i < 4; ++i) if (rts[i]) rts[i]->Release(); delete[] rts;
    });
}
}}
