// Isolated Win32 probe: real embedded CPython, windows and first-chance SEH.
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <Python.h>
#include <cstdio>
#include <cstring>
#include <string>
#include <stdexcept>

static HANDLE writeEntered, writeRelease, finiDone;
static volatile LONG pauseWrite = 0;
static volatile LONG nestedException = 0;
static BOOL WINAPI observedWriteFile(HANDLE file, LPCVOID bytes, DWORD length,
    LPDWORD written, LPOVERLAPPED overlapped)
{
    if (InterlockedCompareExchange(&nestedException, 0, 1) == 1) {
        __try { RaiseException(0xc0000094UL, 0, 0, NULL); }
        __except(EXCEPTION_EXECUTE_HANDLER) {}
    }
    if (InterlockedCompareExchange(&pauseWrite, 0, 1) == 1) {
        SetEvent(writeEntered);
        if (WaitForSingleObject(writeRelease, 10000) != WAIT_OBJECT_0) return FALSE;
    }
    return WriteFile(file, bytes, length, written, overlapped);
}
// One instrumented IO boundary lets the test hold an actual active VEH while
// terminal shutdown runs. All recording, TLS and file IO remain production.
#define WriteFile observedWriteFile
#include "stock_offline_platform.cpp"
#undef WriteFile

static int failed = 0;
#define CHECK(value) do { if (!(value)) { \
    std::fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #value); \
    ++failed; } } while (0)
static PyObject *module = NULL;
static bool reenterHide = false;
static long nestedResult = 0;
static bool markerPublished = false;

static long call(const char *name)
{
    PyObject *result = PyObject_CallMethod(module, const_cast<char *>(name), NULL);
    if (!result) { PyErr_Print(); CHECK(result != NULL); return -9999; }
    CHECK(PyInt_Check(result));
    const long value = PyInt_AsLong(result);
    Py_DECREF(result);
    return value;
}

static LRESULT CALLBACK windowProcedure(HWND window, UINT message, WPARAM w, LPARAM l)
{
    if (message == WM_SHOWWINDOW && !w && reenterHide) {
        reenterHide = false;
        markerPublished = GetPropW(window, WINDOW_GENERATION) != NULL;
        nestedResult = call("hide_process_windows");
    }
    return DefWindowProcW(window, message, w, l);
}

static HWND makeWindow()
{
    HWND window = CreateWindowExW(WS_EX_NOACTIVATE | WS_EX_TOOLWINDOW,
        L"WotOfflinePlatformProbe", L"Platform probe", WS_OVERLAPPEDWINDOW,
        -10000, -10000, 100, 100, NULL, NULL, GetModuleHandleW(NULL), NULL);
    CHECK(window != NULL);
    if (window) ShowWindow(window, SW_SHOWNOACTIVATE);
    return window;
}

static void checkWindows()
{
    WNDCLASSW cls = {};
    cls.hInstance = GetModuleHandleW(NULL);
    cls.lpfnWndProc = windowProcedure;
    cls.lpszClassName = L"WotOfflinePlatformProbe";
    CHECK(RegisterClassW(&cls) != 0);
    const HWND foreground = GetForegroundWindow();
    HWND first = makeWindow();
    HWND hidden = makeWindow();
    ShowWindow(hidden, SW_HIDE);
    reenterHide = true;
    CHECK(call("hide_process_windows") == 1);
    CHECK(markerPublished && nestedResult == -ERROR_BUSY);
    CHECK(!IsWindowVisible(first) && !IsWindowVisible(hidden));
    CHECK(call("hide_process_windows") == 0);
    CHECK(call("show_process_windows") == 1);
    CHECK(IsWindowVisible(first) && !IsWindowVisible(hidden));
    CHECK(call("show_process_windows") == 0);
    CHECK(GetPropW(first, WINDOW_GENERATION) == NULL);

    // A generation replacement, even on the same live HWND and same PID,
    // cannot restore somebody else's later window identity or remove its mark.
    CHECK(call("hide_process_windows") == 1);
    const HANDLE later = reinterpret_cast<HANDLE>(0x12345678);
    CHECK(SetPropW(first, WINDOW_GENERATION, later));
    CHECK(call("show_process_windows") == 0);
    CHECK(!IsWindowVisible(first) && GetPropW(first, WINDOW_GENERATION) == later);
    RemovePropW(first, WINDOW_GENERATION);
    ShowWindow(first, SW_SHOWNOACTIVATE);
    CHECK(call("hide_process_windows") == 1);
    CHECK(DestroyWindow(first));
    CHECK(call("show_process_windows") == 0);
    CHECK(DestroyWindow(hidden));

    HWND windows[MAX_HIDDEN_WINDOWS + 1];
    for (unsigned int i = 0; i < MAX_HIDDEN_WINDOWS + 1; ++i) windows[i] = makeWindow();
    CHECK(call("hide_process_windows") == -ERROR_INSUFFICIENT_BUFFER);
    CHECK(g_hidden_window_count == 0);
    for (unsigned int i = 0; i < MAX_HIDDEN_WINDOWS + 1; ++i) {
        CHECK(IsWindowVisible(windows[i]));
        CHECK(GetPropW(windows[i], WINDOW_GENERATION) == NULL);
        CHECK(DestroyWindow(windows[i]));
    }
    CHECK(GetForegroundWindow() == foreground);
    std::puts("Actual Win32 windows: no-activation / reentry / generation / destroyed identity / rollback PASS");
}

static void raiseRecorded()
{
    __try { RaiseException(0xc0000094UL, 0, 0, NULL); }
    __except(EXCEPTION_EXECUTE_HANDLER) {}
}

static void raiseIgnored()
{
    __try { RaiseException(0xe1234567UL, 0, 0, NULL); }
    __except(EXCEPTION_EXECUTE_HANDLER) {}
}

static void raiseAlternating(unsigned int index)
{
    __try { RaiseException(index & 1 ? 0xc0000094UL : 0xc0000006UL, 0, 0, NULL); }
    __except(EXCEPTION_EXECUTE_HANDLER) {}
}

static void raiseOwnedCxx()
{
    try { throw std::runtime_error("platform-owned-cxx"); }
    catch (const std::runtime_error &) {}
}

static DWORD WINAPI exceptionThread(LPVOID)
{
    SetLastError(0x789abc);
    raiseRecorded();
    CHECK(GetLastError() == 0x789abc);
    return 0;
}

static DWORD WINAPI finiThread(LPVOID)
{
    finiStockOfflinePlatform();
    SetEvent(finiDone);
    return 0;
}

static std::string readLog(const WCHAR *path)
{
    HANDLE file = CreateFileW(path, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE |
        FILE_SHARE_DELETE, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    CHECK(file != INVALID_HANDLE_VALUE);
    if (file == INVALID_HANDLE_VALUE) return std::string();
    std::string text;
    char chunk[4096]; DWORD count = 0;
    while (ReadFile(file, chunk, sizeof(chunk), &count, NULL) && count) text.append(chunk, count);
    CloseHandle(file);
    return text;
}

static void checkTrail(const WCHAR *path)
{
    SetEnvironmentVariableW(EXCEPTION_TRAIL_PATH_ENV, NULL);
    CHECK(call("install_exception_trail") == TRAIL_STATUS_NOT_CONFIGURED);
    SetEnvironmentVariableW(EXCEPTION_TRAIL_PATH_ENV, L"Z:\\no-such-volume\\missing\\trace.log");
    CHECK(call("install_exception_trail") == TRAIL_STATUS_PATH_INVALID);
    DeleteFileW(path);
    CHECK(SetEnvironmentVariableW(EXCEPTION_TRAIL_PATH_ENV, path));
    CHECK(call("install_exception_trail") == 0);
    CHECK(call("install_exception_trail") == 0);
    SetLastError(0x456789);
    raiseRecorded();
    CHECK(GetLastError() == 0x456789);
    raiseRecorded();
    std::string text = readLog(path);
    CHECK(text.find("code=0xC0000094") != std::string::npos);
    CHECK(text.find("repeats=1") != std::string::npos);
    CHECK(text.find(" reg eip=") != std::string::npos);
    CHECK(text.find("EXC session") != std::string::npos);
    CHECK(text.find("EXC end seq=1") != std::string::npos);

    const std::string recorded = readLog(path);
    raiseIgnored();
    CHECK(readLog(path) == recorded);
    InterlockedExchange(&nestedException, 1);
    raiseRecorded();
    CHECK(readLog(path).find("repeats=2") != std::string::npos);
    raiseOwnedCxx();
    CHECK(readLog(path).find("code=0xE06D7363") != std::string::npos);
    for (unsigned int i = 0; i < 520; ++i) raiseAlternating(i);
    const std::string bounded = readLog(path);
    CHECK(g_trail_sequence == TRAIL_MAX_RECORDS);
    CHECK(bounded.find("EXC end seq=512") != std::string::npos);
    CHECK(bounded.find("EXC seq=513 ") == std::string::npos);
    CHECK(bounded.size() <= (TRAIL_MAX_RECORDS + 1) * TRAIL_BUFFER_BYTES);

    // Cross-thread recording uses no Python API or GIL. Hold one real handler
    // inside its write to prove shutdown cannot close file/TLS under it.
    HWND retiringWindow = makeWindow();
    CHECK(call("hide_process_windows") == 1);
    CHECK(!IsWindowVisible(retiringWindow));
    CHECK(GetPropW(retiringWindow, WINDOW_GENERATION) != NULL);
    writeEntered = CreateEventW(NULL, TRUE, FALSE, NULL);
    writeRelease = CreateEventW(NULL, TRUE, FALSE, NULL);
    finiDone = CreateEventW(NULL, TRUE, FALSE, NULL);
    CHECK(writeEntered && writeRelease && finiDone);
    InterlockedExchange(&pauseWrite, 1);
    HANDLE worker = CreateThread(NULL, 0, exceptionThread, NULL, 0, NULL);
    CHECK(WaitForSingleObject(writeEntered, 10000) == WAIT_OBJECT_0);
    HANDLE finisher = CreateThread(NULL, 0, finiThread, NULL, 0, NULL);
    DWORD start = GetTickCount();
    while (InterlockedCompareExchange(&g_trail_enabled, 0, 0) && GetTickCount() - start < 10000)
        SwitchToThread();
    CHECK(!InterlockedCompareExchange(&g_trail_enabled, 0, 0));
    CHECK(WaitForSingleObject(finiDone, 0) == WAIT_TIMEOUT);
    CHECK(g_trail_file != INVALID_HANDLE_VALUE && g_trail_slot != TLS_OUT_OF_INDEXES);
    SetEvent(writeRelease);
    CHECK(WaitForSingleObject(worker, 10000) == WAIT_OBJECT_0);
    CHECK(WaitForSingleObject(finisher, 10000) == WAIT_OBJECT_0);
    CHECK(g_trail_file == INVALID_HANDLE_VALUE && g_trail_slot == TLS_OUT_OF_INDEXES);
    CHECK(!IsWindowVisible(retiringWindow));
    CHECK(GetPropW(retiringWindow, WINDOW_GENERATION) == NULL);
    CHECK(g_hidden_window_count == 0);
    CHECK(DestroyWindow(retiringWindow));
    const std::string before = readLog(path);
    raiseRecorded();
    CHECK(readLog(path) == before);
    CHECK(call("install_exception_trail") == TRAIL_STATUS_HANDLER_REFUSED);
    CHECK(call("hide_process_windows") == -ERROR_OPERATION_ABORTED);
    CHECK(call("show_process_windows") == 0);
    finiStockOfflinePlatform();
    CloseHandle(worker); CloseHandle(finisher); CloseHandle(writeEntered);
    CloseHandle(writeRelease); CloseHandle(finiDone);
    std::puts("Actual Win32 VEH: first-chance / ignored / C++ / repeat / recursion / 512 cap / thread / LastError / active-handler shutdown PASS");
}

int main(int argc, char **argv)
{
    if (argc != 2) return 2;
    SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX);
    Py_NoSiteFlag = 1; Py_DontWriteBytecodeFlag = 1;
    Py_SetPythonHome(argv[1]);
    Py_Initialize();
    CHECK(std::strncmp(Py_GetVersion(), "2.7.7", 5) == 0);
    module = Py_InitModule("offline_platform_probe", const_cast<PyMethodDef *>(stockOfflinePlatformMethods()));
    CHECK(module != NULL);
    unsigned int methodCount = 0;
    for (const PyMethodDef *method = stockOfflinePlatformMethods(); method->ml_name; ++method) {
        CHECK(method->ml_flags == METH_NOARGS); ++methodCount;
        PyObject *result = PyObject_CallMethod(module, const_cast<char *>(method->ml_name),
            const_cast<char *>("i"), 1);
        CHECK(result == NULL && PyErr_ExceptionMatches(PyExc_TypeError));
        Py_XDECREF(result); PyErr_Clear();
    }
    CHECK(methodCount == 3);
    checkWindows();
    WCHAR folder[MAX_PATH], path[MAX_PATH];
    CHECK(GetTempPathW(MAX_PATH, folder) != 0);
    CHECK(swprintf_s(path, L"%sWoT-platform-\x58f0\x97f3-%lu.log", folder, GetCurrentProcessId()) > 0);
    checkTrail(path);
    CHECK(UnregisterClassW(L"WotOfflinePlatformProbe", GetModuleHandleW(NULL)));
    Py_Finalize();
    std::printf("Actual embedded CPython 2.7.7 platform probe: %d failures\n", failed);
    return failed ? 1 : 0;
}
