// Bounded Win32 model for worker-runtime host tests: process environment,
// top-level windows with visibility/properties/placement, and the cursor.
#pragma once
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <map>
#include <string>
#include <vector>

typedef unsigned long DWORD;
typedef int BOOL;
typedef unsigned int UINT;
typedef long LONG;
typedef intptr_t LPARAM;
typedef uintptr_t WPARAM;
typedef void * HANDLE;
typedef void * HINSTANCE;
typedef void * HCURSOR;
typedef const wchar_t * LPCWSTR;
#define CALLBACK
#define TRUE 1
#define FALSE 0
#define ERROR_SUCCESS 0L
#define ERROR_GEN_FAILURE 31L
#define ERROR_NOT_SUPPORTED 50L
#define ERROR_INSUFFICIENT_BUFFER 122L
#define ERROR_BUSY 170L
#define ERROR_ALREADY_EXISTS 183L
#define ERROR_OPERATION_ABORTED 995L
#define ERROR_INVALID_WINDOW_HANDLE 1400L
#define ERROR_INVALID_STATE 5023L
#define SW_HIDE 0
#define SW_SHOWNORMAL 1
#define SWP_NOSIZE 0x0001
#define SWP_NOZORDER 0x0004
#define SWP_NOACTIVATE 0x0010
#define SWP_SHOWWINDOW 0x0040
#define GWL_STYLE (-16)
#define WS_POPUP 0x80000000L
#define WS_VISIBLE 0x10000000L
#define WS_OVERLAPPEDWINDOW 0x00CF0000L
#define NOTIFY_FOR_THIS_SESSION 0

struct RECT { LONG left, top, right, bottom; };
struct POINT { LONG x, y; };
struct WINDOWPLACEMENT { UINT length; UINT flags; UINT showCmd; RECT rcNormalPosition; };

struct FakeWindow
{
    DWORD pid;
    bool alive;
    bool visible;
    LONG style;
    UINT showCmd;
    std::map<std::wstring, HANDLE> properties;
};
typedef FakeWindow * HWND;
#define HWND_NOTOPMOST ((HWND)-2)

struct FakeCall { std::string name; long value; };

struct FakeWin32
{
    DWORD pid = 1000;
    DWORD lastError = 0;
    std::vector<FakeWindow *> windows;
    std::vector<FakeCall> calls;
    HWND foreground = nullptr;
    bool failSetProp = false;

    static FakeWin32 & instance() { static FakeWin32 state; return state; }
    void record(const char * name, long value = 0) { calls.push_back({name, value}); }
    long count(const std::string & name) const
    {
        long total = 0;
        for (const FakeCall & call : calls) total += call.name == name;
        return total;
    }
    HWND create(DWORD owner, bool shown, LONG style = WS_OVERLAPPEDWINDOW)
    {
        windows.push_back(new FakeWindow{owner, true, shown, style, SW_SHOWNORMAL, {}});
        return windows.back();
    }
    void destroy(HWND window)
    {
        window->alive = false;
        window->visible = false;
        window->properties.clear();
    }
};
inline FakeWin32 & win32() { return FakeWin32::instance(); }

inline DWORD GetEnvironmentVariableW(LPCWSTR name, wchar_t * buffer, DWORD size)
{
    std::string narrow;
    for (const wchar_t * c = name; *c; ++c) narrow += static_cast<char>(*c);
    const char * value = std::getenv(narrow.c_str());
    if (!value) return 0;
    const DWORD length = static_cast<DWORD>(std::strlen(value));
    if (length + 1 > size) return length + 1;
    for (DWORD index = 0; index <= length; ++index) buffer[index] = static_cast<unsigned char>(value[index]);
    return length;
}
inline DWORD GetCurrentProcessId() { return win32().pid; }
inline DWORD GetLastError() { return win32().lastError; }
inline void SetLastError(DWORD error) { win32().lastError = error; }
inline LONG InterlockedCompareExchange(LONG * target, LONG value, LONG comparand)
{
    const LONG old = *target;
    if (old == comparand) *target = value;
    return old;
}
inline LONG InterlockedExchange(LONG * target, LONG value) { const LONG old = *target; *target = value; return old; }
inline LONG InterlockedIncrement(LONG * target) { return ++*target; }

inline BOOL IsWindow(HWND window) { return window && window->alive; }
inline BOOL IsWindowVisible(HWND window) { return window && window->alive && window->visible; }
inline DWORD GetWindowThreadProcessId(HWND window, DWORD * pid)
{
    if (!IsWindow(window)) { *pid = 0; return 0; }
    *pid = window->pid;
    return 1;
}
inline HANDLE GetPropW(HWND window, LPCWSTR name)
{
    if (!IsWindow(window)) return nullptr;
    auto found = window->properties.find(name);
    return found == window->properties.end() ? nullptr : found->second;
}
inline BOOL SetPropW(HWND window, LPCWSTR name, HANDLE value)
{
    win32().record("SetPropW");
    if (!IsWindow(window) || win32().failSetProp) return FALSE;
    window->properties[name] = value;
    return TRUE;
}
inline HANDLE RemovePropW(HWND window, LPCWSTR name)
{
    HANDLE value = GetPropW(window, name);
    if (value) window->properties.erase(name);
    return value;
}
inline BOOL GetWindowPlacement(HWND window, WINDOWPLACEMENT * placement)
{
    if (!IsWindow(window)) return FALSE;
    placement->showCmd = window->showCmd;
    return TRUE;
}
inline BOOL SetWindowPlacement(HWND window, const WINDOWPLACEMENT *)
{
    win32().record("SetWindowPlacement");
    return IsWindow(window);
}
inline BOOL ShowWindow(HWND window, int command)
{
    win32().record(command == SW_HIDE ? "ShowWindow(hide)" : "ShowWindow(show)", command);
    if (!IsWindow(window)) return FALSE;
    const BOOL previous = window->visible;
    window->visible = command != SW_HIDE;
    return previous;
}
inline BOOL UpdateWindow(HWND) { win32().record("UpdateWindow"); return TRUE; }
typedef BOOL (CALLBACK * WNDENUMPROC)(HWND, LPARAM);
inline BOOL EnumWindows(WNDENUMPROC callback, LPARAM parameter)
{
    const std::vector<FakeWindow *> snapshot = win32().windows;
    for (FakeWindow * window : snapshot)
        if (window->alive && !callback(window, parameter)) return FALSE;
    return TRUE;
}
inline LONG GetWindowLong(HWND window, int) { return window ? window->style : 0; }
inline LONG SetWindowLong(HWND window, int, LONG style)
{
    win32().record("SetWindowLong", style);
    const LONG old = window->style;
    window->style = style;
    return old;
}
inline BOOL GetWindowRect(HWND, RECT * rect) { *rect = RECT{10, 20, 330, 260}; return TRUE; }
inline BOOL GetClientRect(HWND, RECT * rect) { *rect = RECT{0, 0, 320, 240}; return TRUE; }
inline BOOL ClientToScreen(HWND, POINT * point) { point->x += 10; point->y += 20; return TRUE; }
inline BOOL SetWindowPos(HWND window, HWND, int, int, int, int, UINT flags)
{
    win32().record("SetWindowPos", static_cast<long>(flags));
    if (window && (flags & SWP_SHOWWINDOW)) window->visible = true;
    return TRUE;
}
inline HWND GetForegroundWindow() { return win32().foreground; }
inline BOOL ClipCursor(const RECT * rect) { win32().record(rect ? "ClipCursor(rect)" : "ClipCursor(NULL)"); return TRUE; }
inline BOOL SetCursorPos(int x, int) { win32().record("SetCursorPos", x); return TRUE; }
inline int ShowCursor(BOOL show) { win32().record("ShowCursor", show); return 0; }
inline HCURSOR SetCursor(HCURSOR) { win32().record("SetCursor"); return nullptr; }
inline BOOL WTSRegisterSessionNotification(HWND, DWORD) { win32().record("WTSRegisterSessionNotification"); return TRUE; }
inline HWND CreateWindowW(LPCWSTR, LPCWSTR, LONG style, int, int, int, int, HWND, void *, HINSTANCE, void *)
{
    win32().record("CreateWindow", style);
    return win32().create(win32().pid, (style & WS_VISIBLE) != 0, style);
}
#define CreateWindow CreateWindowW
