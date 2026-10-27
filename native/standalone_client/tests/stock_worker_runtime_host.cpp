// Host harness for patch_stock_worker_runtime.py. Each section compiles the
// actual patched (or pre-patch control) engine bodies against bounded fakes
// and prints the observed Win32/engine calls one per line.
#if defined(WORKER_HOST_PLATFORM)
#include <Python.h>
#endif
#include <cstddef>
#include <windows.h>
#include "cstdmf/stock_worker_runtime.hpp"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

static long milli(double value) { return std::lround(value * 1000.0); }
static void record(const char * name, long value = 0) { win32().record(name, value); }
static void dump(const char * scenario)
{
    std::printf("scenario %s\n", scenario);
    for (const FakeCall & call : win32().calls) std::printf("%s %ld\n", call.name.c_str(), call.value);
    win32().calls.clear();
}
static void printRole() { std::printf("role %d\n", wot::stockSimulationWorker() ? 1 : 0); }

#define BW_GUARD
#define GPU_PROFILER_SCOPE(name)
#define PROFILER_SCOPED(name)
#define DEBUG_MSG(...)
#define ERROR_MSG(...) record("ERROR_MSG")
#define CRITICAL_MSG(...) record("CRITICAL_MSG")
typedef unsigned int uint;
typedef unsigned int uint32;
typedef unsigned long long uint64;
namespace BW { using std::string; using std::vector; using std::wstring; }

struct Matrix
{
    float m[16];
    static const Matrix identity;
};
const Matrix Matrix::identity = {};

#if defined(WORKER_HOST_ROLE)
bool roleA();
bool roleB();
const void * roleAddressA();
const void * roleAddressB();

int main(int argc, char ** argv)
{
    // TU A makes the first query; TU B observes it after the environment flips.
    const bool first = roleA();
    if (argc > 1) setenv("OFFLINE_LAN_0922_CLIENT_MODE", argv[1], 1);
    else unsetenv("OFFLINE_LAN_0922_CLIENT_MODE");
    std::printf("first %d\nsecond %d\nreread %d\nsame-owner %d\n", first ? 1 : 0, roleB() ? 1 : 0,
        wot::readStockSimulationWorkerRole() ? 1 : 0, roleAddressA() == roleAddressB() ? 1 : 0);
    return 0;
}

#elif defined(WORKER_HOST_FRAME)
#define ENABLE_PROFILER 0
struct DogWatch { void start() {} void stop() {} };
struct ScopedDogWatch { explicit ScopedDogWatch(DogWatch &) {} };
static DogWatch g_watchTick, g_watchUpdate, g_watchOutput;
struct MouseCursor { static void updateMouseClipping() { record("updateMouseClipping"); } };
struct ResourceMonitor { void flushModificationMonitor() { record("flushModificationMonitor"); } };
struct BWResource { static ResourceMonitor & instance() { static ResourceMonitor monitor; return monitor; } };
struct TaskRoot
{
    void tick(float game, float render) { record("tick.game", milli(game)); record("tick.render", milli(render)); }
    void updateAnimations(float game) const { record("updateAnimations", milli(game)); }
    void draw() { record("draw"); }
    void inactiveTick(float game, float render) { record("inactiveTick.game", milli(game)); record("inactiveTick.render", milli(render)); }
};
struct MainLoopTasks { static TaskRoot & root() { static TaskRoot root; return root; } };
namespace Moo
{
struct RenderContext
{
    bool deviceOk = true;
    bool checkDevice() { record("checkDevice", deviceOk); return deviceOk; }
    void preloadDeviceResources(uint32 milliseconds) { record("preloadDeviceResources", milliseconds); }
};
inline RenderContext & rc() { static RenderContext context; return context; }
}
void tickStockAudio(const Matrix &, float elapsed) { record("tickStockAudio", milli(elapsed)); }
struct Camera { Matrix invView() const { return Matrix::identity; } };
struct ClientCamera { Camera * camera() { static Camera camera; return &camera; } };
struct CameraApp
{
    static CameraApp & instance() { static CameraApp app; return app; }
    ClientCamera & clientCamera() { static ClientCamera camera; return camera; }
};
struct FramePacing { int sleepTime(int value) { return value; } };
static uint64 frameTimerValue() { return 0; }
static uint64 frameTimerFreq() { return 1000; }
static void Sleep(int milliseconds) { record("Sleep", milliseconds); }
using std::max;
static int s_framesCounter = 0;

struct App
{
    float dGameTime_ = 0.f, dRenderTime_ = 0.f;
    uint64 minFrameTime_ = 0, lastFrameEndTime_ = 0;
    int sleepTime_ = 0;
    FramePacing framePacing_;
    void calculateFrameTime() { record("calculateFrameTime"); }
    bool updateFrame(bool active);
};

bool App::updateFrame(bool active)
@BODY@

int main()
{
    printRole();
    struct Scenario { const char * name; bool active; bool device; float game; float render; };
    const Scenario scenarios[] = {
        { "active", true, true, 0.016f, 0.017f },
        { "inactive", false, true, 0.021f, 0.023f },
        { "device-lost", true, false, 0.016f, 0.017f },
        { "device-lost-inactive", false, false, 0.016f, 0.017f },
        { "no-elapsed", true, true, 0.f, 0.f },
        { "no-elapsed-inactive", false, true, 0.f, 0.f },
    };
    for (const Scenario & scenario : scenarios)
    {
        App app;
        app.dGameTime_ = scenario.game;
        app.dRenderTime_ = scenario.render;
        Moo::rc().deviceOk = scenario.device;
        record("result", app.updateFrame(scenario.active));
        dump(scenario.name);
    }
    return 0;
}

#elif defined(WORKER_HOST_PROGRESS)
#define D3DCLEAR_TARGET 1
#define D3DCLEAR_ZBUFFER 2
static uint64 g_now = 1000;
static uint64 timestamp() { return g_now; }
struct Font { void drawConsoleString(const BW::string &, int, int) { record("drawConsoleString"); } };
struct FontManager
{
    static FontManager & instance() { static FontManager manager; return manager; }
    bool begin(Font &) { return true; }
    void end() {}
};
namespace Moo
{
struct Device
{
    void Clear(DWORD, const void *, DWORD, DWORD, float, DWORD) { record("Clear"); }
    void SetSoftwareVertexProcessing(BOOL) { record("SetSoftwareVertexProcessing"); }
};
struct RenderContext
{
    bool deviceOk = true;
    Device device_;
    Device * device() { return &device_; }
    bool checkDevice() { record("checkDevice", deviceOk); return deviceOk; }
    bool mixedVertexProcessing() { return true; }
    long beginScene() { record("beginScene"); return 0; }
    long endScene() { record("endScene"); return 0; }
    long present() { record("present"); return 0; }
    void nextFrame() { record("nextFrame"); }
    void preloadDeviceResources(uint32 milliseconds) { record("preloadDeviceResources", milliseconds); }
    void push() { record("push"); }
    void pop() { record("pop"); }
    Matrix view() const { return Matrix::identity; }
    void view(const Matrix &) {}
    void world(const Matrix &) {}
    void updateViewTransforms() {}
};
inline RenderContext & rc() { static RenderContext context; return context; }
}
struct ProgressTask { float done_; float length_; };
struct ProgressNode { BW::string name; int level; ProgressTask * task; std::vector<int> children; };
struct ProgressDisplay
{
    uint64 lastDrawn_ = 0, minRedrawTime_ = 100;
    bool (*pCallback_)() = nullptr;
    Font * pFont_ = nullptr;
    std::vector<BW::string> messages_;
    std::vector<int> roots_;
    std::vector<ProgressNode> tasks_;
    void drawBar(int, float) { record("drawBar"); }
    bool draw(bool force);
};

bool ProgressDisplay::draw( bool force )
@PROGRESS_BODY@

struct PyObject {};
static PyObject g_setProgress;
static PyObject * PyObject_GetAttrString(PyObject *, const char *) { return &g_setProgress; }
static PyObject * Py_BuildValue(const char *, double value) { record("setProgress", milli(value)); return nullptr; }
namespace Script { static void call(PyObject *, PyObject *, const char *) { record("Script.call"); } }
template <class T> struct SmartPointer
{
    T * object = nullptr;
    SmartPointer() {}
    SmartPointer(T * value) : object(value) {}
    explicit operator bool() const { return object != nullptr; }
    T & operator*() const { return *object; }
    T * operator->() const { return object; }
};
struct ScriptObject { bool exists() const { return true; } PyObject * getObject() const { static PyObject o; return &o; } };
struct GUIShader { virtual ~GUIShader() {} };
struct ClipGUIShader : GUIShader { void value(float) {} };
typedef SmartPointer<GUIShader> GUIShaderPtr;
struct GUIComponent
{
    ScriptObject script_;
    ScriptObject & script() { return script_; }
    SmartPointer<GUIComponent> child(const char *) { return SmartPointer<GUIComponent>(); }
    GUIShaderPtr shader(const char *) { return GUIShaderPtr(); }
};
typedef SmartPointer<GUIComponent> SimpleGUIComponentPtr;
struct SimpleGUI
{
    static SimpleGUI & instance() { static SimpleGUI gui; return gui; }
    void update(float elapsed) { record("SimpleGUI.update", milli(elapsed)); }
    void draw() { record("SimpleGUI.draw"); }
};
static float lastTime_ = -1.f;
static float MIN_PROGRESS_STEP = 0.05f;
struct GUIProgressDisplay : ProgressDisplay
{
    GUIComponent component_;
    SimpleGUIComponentPtr gui_ = SimpleGUIComponentPtr(&component_);
    bool draw(bool force);
};

bool GUIProgressDisplay::draw( bool force )
@GUI_PROGRESS_BODY@

static bool g_callbackResult = true;
static bool callback() { record("callback", g_callbackResult); return g_callbackResult; }

int main()
{
    printRole();
    Font font;
    ProgressTask task = { 1.f, 2.f };
    struct Scenario { const char * name; bool callback; bool device; bool force; uint64 now; };
    const Scenario scenarios[] = {
        { "draw", true, true, true, 1000 },
        { "throttled", true, true, false, 1050 },
        { "cancelled", false, true, true, 2000 },
        { "device-lost", true, false, true, 3000 },
    };
    for (const Scenario & scenario : scenarios)
    {
        ProgressDisplay display;
        display.lastDrawn_ = 1000;
        display.pCallback_ = callback;
        display.pFont_ = &font;
        display.messages_ = { "startup" };
        display.roots_ = { 0 };
        display.tasks_ = { ProgressNode{ "App Startup", 0, &task, {} } };
        g_callbackResult = scenario.callback;
        Moo::rc().deviceOk = scenario.device;
        g_now = scenario.now;
        record("result", display.draw(scenario.force));
        record("lastDrawn", static_cast<long>(display.lastDrawn_));
        dump(scenario.name);
    }
    for (bool result : { true, false })
    {
        GUIProgressDisplay display;
        display.pCallback_ = callback;
        display.roots_ = { 0 };
        display.tasks_ = { ProgressNode{ "App Startup", 0, &task, {} } };
        g_callbackResult = result;
        lastTime_ = -1.f;
        record("result", display.draw(true));
        record("lastTime", milli(lastTime_));
        dump(result ? "gui-draw" : "gui-cancelled");
    }
    return 0;
}

#elif defined(WORKER_HOST_CURSOR)
struct Vector2
{
    float x, y;
    float lengthSquared() const { return x * x + y * y; }
};
namespace Moo
{
struct Device { void SetCursorPosition(int x, int, DWORD) { record("SetCursorPosition", x); } };
struct RenderContext
{
    HWND window = nullptr;
    Device device_;
    HWND windowHandle() { return window; }
    bool windowed() { return true; }
    Device * device() { return &device_; }
    float screenWidth() { return 320.f; }
    float screenHeight() { return 240.f; }
};
inline RenderContext & rc() { static RenderContext context; return context; }
}
struct MouseCursor
{
    bool isClipped_ = false;
    Vector2 inactivePos_ = { 0.f, 0.f };
    bool isDirty_ = false;
    static bool s_inFocus_;
    bool clipped() const { return isClipped_; }
    void clipped(bool state);
    void position(const Vector2 & pos);
    void deleteUnmanagedObjects();
    static void staticSetFocus(bool focus);
    static void updateMouseClipping();
};
struct SimpleGUI
{
    MouseCursor cursor_;
    static SimpleGUI * pInstance() { return &instance(); }
    static SimpleGUI & instance() { static SimpleGUI gui; return gui; }
    MouseCursor & mouseCursor() { return cursor_; }
    void clipRangesToPixel(float x, float y, float * px, float * py) { *px = x * 160.f; *py = y * 120.f; }
    Vector2 resolutionOverride() { return Vector2{ 0.f, 0.f }; }
    HWND hwnd() { return Moo::rc().window; }
};

namespace {
void clipCursorToWindow( bool clip );
}

void MouseCursor::position( const Vector2 & pos )
@POSITION_BODY@

void MouseCursor::clipped( bool state )
@CLIPPED_BODY@

@FOCUS_DEFINITIONS@

void MouseCursor::deleteUnmanagedObjects()
@DELETE_BODY@

/*static*/ void MouseCursor::updateMouseClipping()
@UPDATE_BODY@

namespace {
void clipCursorToWindow( bool clip )
@CLIP_BODY@
}

int main()
{
    printRole();
    HWND window = win32().create(win32().pid, false);
    Moo::rc().window = window;
    MouseCursor & cursor = SimpleGUI::instance().mouseCursor();
    win32().foreground = window;
    cursor.clipped(true);
    MouseCursor::updateMouseClipping();
    dump("foreground-clip");
    win32().foreground = nullptr;
    MouseCursor::updateMouseClipping();
    dump("background-release");
    win32().foreground = window;
    cursor.clipped(false);
    MouseCursor::staticSetFocus(false);
    std::printf("inFocus=%d\n", MouseCursor::s_inFocus_ ? 1 : 0);
    MouseCursor::staticSetFocus(true);
    std::printf("inFocus=%d\n", MouseCursor::s_inFocus_ ? 1 : 0);
    cursor.deleteUnmanagedObjects();
    dump("focus-and-reset");
    cursor.position(Vector2{ 0.5f, -0.5f });
    std::printf("inactivePos=%ld,%ld\n", milli(cursor.inactivePos_.x), milli(cursor.inactivePos_.y));
    dump("position");
    return 0;
}

#elif defined(WORKER_HOST_RENDER)
#define IF_NOT_MF_ASSERT_DEV(condition) if (!(condition))
#define MF_ASSERT(condition)
#define D3DDEVTYPE_HAL 1
#define D3DDEVTYPE_REF 2
#define REGISTER_SINGLETON_FUNC(type, function)
struct Vector2 { float x, y; };
struct AssetClient;
template <class T> void bw_safe_delete(T *& object) { delete object; object = nullptr; }
struct MRTSupport { void fini() {} };
struct Device { void Release() { record("Device.Release"); } };
struct DeviceInfo {};
struct DeviceCallback { static void deleteAllManaged() {} };
static HCURSOR s_OriginalMouseCursor = nullptr;
static bool s_changingMode = false;
static void stockRememberDisplayWindow() { record("stockRememberDisplayWindow"); }
static bool & isRenderThread() { static bool value = false; return value; }

struct RenderContext
{
    HWND windowHandle_ = nullptr;
    RECT windowedRect_ = {};
    LONG windowedStyle_ = WS_OVERLAPPEDWINDOW;
    bool windowed_ = true;
    uint32 modeIndex_ = 0, deviceIndex_ = 0;
    int deviceType_ = 0;
    bool usingD3DDeviceEx_ = false, hideCursor_ = false, assetProcessingOnly_ = true, stencilWanted_ = false;
    Vector2 windowedSize_ = {};
    Device * device_ = nullptr;
    MRTSupport * mrtSupport_ = nullptr;
    void * lightContainer_ = nullptr;
    void * specularLightContainer_ = nullptr;
    void * screenCopySurface_ = nullptr;
    std::vector<DeviceInfo> devices_ = std::vector<DeviceInfo>(1);

    void releaseUnmanaged(bool) {}
    void createUnmanaged(bool) {}
    void clearBindings() {}
    bool changeModePriv(uint32 modeIndex, bool windowed, bool, uint32)
    {
        record("changeModePriv.windowed", windowed);
        modeIndex_ = modeIndex;
        windowed_ = windowed;
        return true;
    }
    bool changeMode(uint32 modeIndex, bool windowed, bool testCooperative = true, uint32 backBufferWidthOverride = 0);
    void releaseDevice();
    bool createDevice(HWND hWnd, uint32 deviceIndex, uint32 modeIndex, bool windowed, bool requireStencil,
        const Vector2 & windowedSize, bool hideCursor, bool forceRef);
};

bool RenderContext::changeMode( uint32 modeIndex, bool windowed, bool testCooperative, uint32 backBufferWidthOverride )
@CHANGE_MODE_BODY@

void RenderContext::releaseDevice( void )
@RELEASE_BODY@

// Production createDevice through its device-parameter setup.
@CREATE_PREFIX@
    return true;
}

static void dumpState(const char * scenario, const RenderContext & context)
{
    record("windowed_", context.windowed_);
    record("visible", IsWindowVisible(context.windowHandle_));
    dump(scenario);
}

int main()
{
    printRole();
    RenderContext context;
    context.windowHandle_ = win32().create(win32().pid, false);
    context.createDevice(context.windowHandle_, 0, 3, false, true, Vector2{}, false, false);
    dumpState("create-fullscreen-request", context);
    context.device_ = new Device;
    context.createDevice(context.windowHandle_, 0, 4, false, true, Vector2{}, false, false);
    dumpState("recreate-fullscreen-request", context);
    context.changeMode(5, true);
    dumpState("change-to-windowed", context);
    context.changeMode(5, false);
    dumpState("change-to-fullscreen", context);
    // A device already left in fullscreen state must still not be shown.
    context.windowed_ = false;
    context.changeMode(6, true);
    dumpState("restore-from-fullscreen", context);
    context.windowed_ = false;
    context.releaseDevice();
    dumpState("release-fullscreen", context);
    return 0;
}

#elif defined(WORKER_HOST_WINMAIN)
static int createAndShow(HINSTANCE hInstance, LPCWSTR lpClassName, int nCmdShow, HWND & hWnd)
{
    BW::wstring windowName = L"WoT";
    int xPos = 0, yPos = 0, width = 320, height = 240;
@WINMAIN_REGION@
    return 0;
}

int main(int argc, char **)
{
    printRole();
    win32().failSetProp = argc > 1;
    HWND window = NULL;
    record("createAndShow", createAndShow(nullptr, L"App", SW_SHOWNORMAL, window));
    const HANDLE mark = GetPropW(window, wot::stockWorkerWindowProperty());
    record("visible", IsWindowVisible(window));
    record("marked-self", mark == (HANDLE)window);
    dump(argc > 1 ? "create-mark-failure" : "create");
    return 0;
}

#elif defined(WORKER_HOST_PLATFORM)
typedef wchar_t WCHAR;
@PLATFORM_DECLARATIONS@
@PLATFORM_WINDOWS@

static std::vector<HWND> g_desktop;

static PyObject * desktopCreate(PyObject *, PyObject * args)
{
    int own = 0, visible = 0, mark = 0;
    if (!PyArg_ParseTuple(args, "iii", &own, &visible, &mark)) return NULL;
    HWND window = win32().create(own ? win32().pid : win32().pid + 1, visible != 0);
    // mark 1: BWWinMain's self identity; mark 2: some other value.
    if (mark) window->properties[wot::stockWorkerWindowProperty()] = mark == 1 ? (HANDLE)window : (HANDLE)1;
    g_desktop.push_back(window);
    return PyInt_FromLong((long)g_desktop.size() - 1);
}

static PyObject * desktopVisible(PyObject *, PyObject * args)
{
    int index = 0;
    if (!PyArg_ParseTuple(args, "i", &index)) return NULL;
    return PyBool_FromLong(IsWindowVisible(g_desktop[index]));
}

static PyObject * desktopSetVisible(PyObject *, PyObject * args)
{
    int index = 0, visible = 0;
    if (!PyArg_ParseTuple(args, "ii", &index, &visible)) return NULL;
    g_desktop[index]->visible = visible != 0;
    Py_RETURN_NONE;
}

static PyObject * desktopDestroy(PyObject *, PyObject * args)
{
    int index = 0;
    if (!PyArg_ParseTuple(args, "i", &index)) return NULL;
    win32().destroy(g_desktop[index]);
    Py_RETURN_NONE;
}

static PyObject * desktopShown(PyObject *, PyObject *)
{
    return PyInt_FromLong(win32().count("ShowWindow(show)") + win32().count("SetWindowPlacement"));
}

static PyObject * desktopLock(PyObject *, PyObject * args)
{
    int busy = 0, closed = 0;
    if (!PyArg_ParseTuple(args, "ii", &busy, &closed)) return NULL;
    g_window_busy = busy;
    g_closed = closed;
    Py_RETURN_NONE;
}

static PyMethodDef nativeMethods[] = {
    { "hide_process_windows", hide_process_windows, METH_NOARGS, NULL },
    { "show_process_windows", show_process_windows, METH_NOARGS, NULL },
    { "confirm_hidden_worker_window", confirm_hidden_worker_window, METH_NOARGS, NULL },
    { NULL, NULL, 0, NULL }
};

static PyMethodDef desktopMethods[] = {
    { "create", desktopCreate, METH_VARARGS, NULL },
    { "visible", desktopVisible, METH_VARARGS, NULL },
    { "set_visible", desktopSetVisible, METH_VARARGS, NULL },
    { "destroy", desktopDestroy, METH_VARARGS, NULL },
    { "shown", desktopShown, METH_NOARGS, NULL },
    { "lock", desktopLock, METH_VARARGS, NULL },
    { NULL, NULL, 0, NULL }
};

int main(int argc, char ** argv)
{
    if (argc != 2) return 2;
    printRole();
    std::fflush(stdout);
    Py_NoSiteFlag = 1;
    Py_DontWriteBytecodeFlag = 1;
    Py_Initialize();
    Py_InitModule("offline_instance_guard_native", nativeMethods);
    Py_InitModule("fake_desktop", desktopMethods);
    FILE * driver = std::fopen(argv[1], "r");
    if (!driver) return 3;
    const int result = PyRun_SimpleFileEx(driver, argv[1], 1);
    Py_Finalize();
    return result ? 1 : 0;
}
#endif
