// Real CPython 2.7 runs the engine's PY_AUTO macros and Script::setData(uint&),
// the production GUI.wg_setGlyphCacheParams binding, and the pristine or
// patched lib/scaleform/manager.{hpp,cpp} class, constructor, draw and device
// callbacks. GlyphCacheParams/GlyphCacheConfig and GlyphCache::SetParams and
// Initialize are SDK 4.2.22 text; the HAL, renderer and movies are recording
// fixtures with the guards those bodies read.
#include <Python.h>
#include <cassert>
#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <list>
#include <memory>
#include <string>
#include <vector>

#define BW_BEGIN_NAMESPACE namespace BW {
#define BW_END_NAMESPACE }
#define BW_GUARD
#define BW_STATIC_ASSERT(expression, message) static_assert(expression, #message)
#define MF_WATCH(...)
#define MF_VERIFY(expression) do { bool ok = (expression); assert(ok); (void)ok; } while (0)
#define INFO_MSG(...)
#define TRACE_MSG(...)
#define ERROR_MSG(...)
#define ENABLE_WATCHERS 0
#define ENABLE_RESOURCE_COUNTERS 0

typedef unsigned int uint;
typedef unsigned int uint32;
typedef void * HWND;

std::vector<std::string> hostCalls;
void record(const char * format, ...)
{
    char text[256];
    va_list values;
    va_start(values, format);
    std::vsnprintf(text, sizeof(text), format, values);
    va_end(values);
    hostCalls.push_back(text);
}

namespace BW {
typedef std::string string;
template <class T> using list = std::list<T>;

struct HostLink { const char * module; const char * name; PyCFunction function; };
std::vector<HostLink> & hostLinks() { static std::vector<HostLink> links; return links; }
struct PyModuleMethodLink
{
    PyModuleMethodLink(const char * module, const char * name, PyCFunction function, const char *)
    {
        hostLinks().push_back(HostLink{module, name, function});
    }
};

namespace Script {
template <class T> struct IsValidArgType { static const bool isValid = true; };
int setData(PyObject * pObject, uint & rUint, const char * varName = "");
PyObject * argCountError(const char * fn, int optas, int allas, ...);
}

@SCRIPT_BODIES@
}

@PYAUTO_MACROS@

// ---- Scaleform: SDK 4.2.22 glyph-cache configuration over recording fixtures ----
namespace Scaleform {
template <class T> class Ptr
{
public:
    Ptr(T * object = NULL) : object_(object) {}
    Ptr & operator=(T * object) { object_.reset(object); return *this; }
    T * operator->() const { return object_.get(); }
    T * GetPtr() const { return object_.get(); }
    operator bool() const { return object_ != NULL; }
    bool operator!() const { return object_ == NULL; }
private:
    std::shared_ptr<T> object_;
};

namespace Render {
@GLYPH_CONFIG@

class HAL;
typedef void PrimitiveFillManager;
// The members SDK GlyphCache::SetParams and Initialize read.
class GlyphCache : public GlyphCacheConfig
{
public:
    GlyphCache() : pRenderer(NULL), pFillMan(NULL), initializations(0) {}
    bool SetParams(const GlyphCacheParams & params);
    const GlyphCacheParams & GetParams() const { return Param; }
    void ClearCache() {}
    void Initialize(HAL * ren, void * fillMan);
    // SDK initialize() rebuilds the textures from Param; Destroy() releases them.
    void initialize();
    void Destroy() { record("glyph.destroy"); }
    GlyphCacheParams Param;
    HAL * pRenderer;
    void * pFillMan;
    unsigned initializations;
};

// The HAL owner notifications of Renderer2DImpl::OnHALEvent for the glyph cache.
class HAL
{
public:
    HAL() : initialized(false), pGlyphCache(NULL) {}
    bool IsInitialized() const { return initialized; }
    void InitHAL() { initialized = true; pGlyphCache->Initialize(this, NULL); }
    void PrepareForReset() { record("hal.prepare"); pGlyphCache->Destroy(); }
    void RestoreAfterReset() { record("hal.restore"); pGlyphCache->Initialize(this, NULL); }
    void ShutdownHAL() { pGlyphCache->Destroy(); initialized = false; }
    bool initialized;
    GlyphCache * pGlyphCache;
};

@GLYPH_BODIES@

void GlyphCache::initialize()
{
    ++initializations;
    record("glyph.initialize %u %u %u %u vector=%d", Param.NumTextures, Param.TextureWidth,
           Param.TextureHeight, Param.MaxSlotHeight, int(Param.UseVectorOnFullCache));
}

struct ContextNotify {};
class Renderer2D
{
public:
    explicit Renderer2D(HAL * hal) { hal->pGlyphCache = &glyphCache_; glyphCache_.Initialize(hal, NULL); }
    GlyphCacheConfig * GetGlyphCacheConfig() const { return const_cast<GlyphCache *>(&glyphCache_); }
    void BeginFrame() { record("renderer.begin"); }
    void EndFrame() { record("renderer.end"); }
    ContextNotify * GetContextNotify() { return &notify_; }
    template <class H> void Display(H & handle) { record("renderer.display %s", handle.name.c_str()); }
    GlyphCache & glyphCache() { return glyphCache_; }
private:
    GlyphCache glyphCache_;
    ContextNotify notify_;
};
namespace D3D9 { typedef Render::HAL HAL; class TextureManager {}; }
}

namespace GFx {
struct System { explicit System(void *) {} };
struct Viewport {};
struct FontMap {};
struct FontLib {};
struct DrawTextManager {};
struct Movie
{
    void Capture() {}
    void SetViewport(const Viewport &) {}
};
struct MovieDisplayHandle
{
    std::string name;
    bool NextCapture(Render::ContextNotify *) { return true; }
};
}
}
using namespace Scaleform;
namespace SF = Scaleform;

namespace BW {
template <class T> class Singleton
{
public:
    Singleton() { assert(s_pInstance == NULL); s_pInstance = static_cast<T *>(this); }
    ~Singleton() { assert(s_pInstance == this); s_pInstance = NULL; }
    static T & instance() { assert(s_pInstance); return *s_pInstance; }
    static T * pInstance() { return s_pInstance; }
private:
    static T * s_pInstance;
};
template <class T> T * Singleton<T>::s_pInstance = NULL;

namespace Moo {
struct DeviceCallback { virtual ~DeviceCallback() {} };
struct RenderContext
{
    RenderContext() : deviceGood(true) {}
    bool checkDevice() { return deviceGood; }
    void initRenderStates() {}
    bool deviceGood;
};
RenderContext & rc() { static RenderContext context; return context; }
}
struct KeyEvent {};
namespace ScaleformBW {
struct SysAlloc { static SysAlloc & instance() { static SysAlloc alloc; return alloc; } };
struct Loader {};
struct Log {};
class PyMovieView
{
public:
    explicit PyMovieView(const char * name) { handle_.name = name; }
    GFx::Movie * pMovieView() { return &movie_; }
    GFx::MovieDisplayHandle & movieDisplayHandle() { return handle_; }
private:
    GFx::Movie movie_;
    GFx::MovieDisplayHandle handle_;
};
void fullScreenViewport(GFx::Viewport &) {}

// The fixture reads the manager's stored parameters directly.
#define private public
@MANAGER_CLASS@;
#undef private

// Host BW::Manager::doInit/doFini: the public HAL and renderer ownership.
bool Manager::doInit()
{
    pRenderHAL_ = new Render::D3D9::HAL();
    pRenderer_ = new Render::Renderer2D(pRenderHAL_.GetPtr());
    pRenderHAL_->InitHAL();
    return true;
}
bool Manager::doFini()
{
    pRenderer_ = NULL;
    pRenderHAL_ = NULL;
    return true;
}
Manager::~Manager() { doFini(); }
void Manager::addMovieView(PyMovieView * obj) { movies_.push_back(obj); }
void Manager::removeMovieView(PyMovieView * obj) { movies_.remove(obj); }

@MANAGER_BODIES@
}
}

#if HOST_PATCHED
#include "stock_glyph_cache.cpp"
#endif

namespace {
BW::ScaleformBW::Manager * manager = NULL;
std::list<BW::ScaleformBW::PyMovieView> movies;

PyObject * createManager(PyObject *, PyObject *)
{
    assert(!manager);
    manager = new BW::ScaleformBW::Manager();
    Py_RETURN_NONE;
}

PyObject * destroyManager(PyObject *, PyObject *)
{
    for (std::list<BW::ScaleformBW::PyMovieView>::iterator it = movies.begin(); it != movies.end(); ++it)
        manager->removeMovieView(&*it);
    movies.clear();
    delete manager;
    manager = NULL;
    Py_RETURN_NONE;
}

PyObject * addMovie(PyObject *, PyObject * args)
{
    const char * name;
    if (!PyArg_ParseTuple(args, "s", &name))
        return NULL;
    movies.push_back(BW::ScaleformBW::PyMovieView(name));
    manager->addMovieView(&movies.back());
    Py_RETURN_NONE;
}

PyObject * draw(PyObject *, PyObject *)
{
    manager->draw();
    Py_RETURN_NONE;
}

PyObject * setDevice(PyObject *, PyObject * args)
{
    int good;
    if (!PyArg_ParseTuple(args, "i", &good))
        return NULL;
    BW::Moo::rc().deviceGood = good != 0;
    Py_RETURN_NONE;
}

PyObject * resetDevice(PyObject *, PyObject *)
{
    manager->deleteUnmanagedObjects();
    manager->createUnmanagedObjects();
    Py_RETURN_NONE;
}

PyObject * applied(PyObject *, PyObject *)
{
    // The renderer glyph cache's current parameters and initialisation count.
    const Render::GlyphCache & cache = manager->pRenderer()->glyphCache();
    const Render::GlyphCacheParams & p = cache.GetParams();
    return Py_BuildValue("(IIIIIIIfIffffiiiI)", p.TextureWidth, p.TextureHeight, p.NumTextures,
                         p.MaxSlotHeight, p.SlotPadding, p.TexUpdWidth, p.TexUpdHeight, double(p.MaxRasterScale),
                         p.MaxVectorCacheSize, double(p.FauxItalicAngle), double(p.FauxBoldRatio),
                         double(p.OutlineRatio), double(p.ShadowQuality), int(p.UseAutoFit),
                         int(p.UseVectorOnFullCache), int(p.FenceWaitOnFullCache), cache.initializations);
}

#if HOST_PATCHED
PyObject * pending(PyObject *, PyObject *)
{
    // The stored bytes as #1513 lays them out at Manager+0xa8..0xe3.
    static_assert(sizeof(Render::GlyphCacheParams) == 0x38, "#1513 copies 0x38 bytes");
    return Py_BuildValue("(is#)", int(manager->glyphCacheParamsPending_),
                         reinterpret_cast<const char *>(&manager->glyphCacheParams_), 0x37);
}
#endif

PyObject * calls(PyObject *, PyObject *)
{
    PyObject * result = PyList_New(0);
    for (size_t i = 0; i < hostCalls.size(); ++i)
    {
        PyObject * item = PyString_FromString(hostCalls[i].c_str());
        PyList_Append(result, item);
        Py_DECREF(item);
    }
    hostCalls.clear();
    return result;
}

PyMethodDef hostMethods[] = {
    {"createManager", createManager, METH_NOARGS, NULL},
    {"destroyManager", destroyManager, METH_NOARGS, NULL},
    {"addMovie", addMovie, METH_VARARGS, NULL},
    {"draw", draw, METH_NOARGS, NULL},
    {"setDevice", setDevice, METH_VARARGS, NULL},
    {"resetDevice", resetDevice, METH_NOARGS, NULL},
    {"applied", applied, METH_NOARGS, NULL},
#if HOST_PATCHED
    {"pending", pending, METH_NOARGS, NULL},
#endif
    {"calls", calls, METH_NOARGS, NULL},
    {NULL, NULL, 0, NULL}};
}

int main(int argc, char ** argv)
{
    assert(argc >= 2);
    Py_Initialize();
    static std::vector<PyMethodDef> gui;
    for (const BW::HostLink & link : BW::hostLinks())
    {
        assert(std::strcmp(link.module, "GUI") == 0);
        gui.push_back(PyMethodDef{link.name, link.function, METH_VARARGS, NULL});
    }
    gui.push_back(PyMethodDef{NULL, NULL, 0, NULL});
    Py_InitModule("GUI", gui.data());
    Py_InitModule("_host", hostMethods);
    PySys_SetArgv(argc - 1, argv + 1);
    FILE * script = std::fopen(argv[1], "r");
    assert(script);
    const int failed = PyRun_SimpleFileEx(script, argv[1], 1);
    Py_Finalize();
    return failed ? 1 : 0;
}
