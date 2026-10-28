// Explicit Win32/COM boundaries for the production taskbar owner. CPython,
// public argument macros and the original PYC execute outside these doubles.
#include <cstdint>
#define WINAPI
using BOOL=int;
using DWORD=std::uint32_t;
using ULONG=unsigned long;
using UINT=unsigned;
using HWND=std::uint32_t;
using HMODULE=void*;
using FARPROC=void(*)();
using HRESULT=int;
using ULONGLONG=unsigned long long;
enum TBPFLAG {TBPF_NOPROGRESS=0,TBPF_ERROR=4,TBPF_PAUSED=8};
const DWORD FLASHW_STOP=0,FLASHW_TRAY=2,FLASHW_TIMER=4,CLSCTX_ALL=0x17;
const HRESULT S_OK=0;
const int CLSID_TaskbarList=1,IID_ITaskbarList3=2;
struct FLASHWINFO {UINT cbSize;HWND hwnd;DWORD dwFlags;UINT uCount;DWORD dwTimeout;};
namespace notifierHost {
std::vector<std::string> events;
HWND window=123,foreground=123;
bool worker=false,themed=true,library=true,procedure=true,interface=true;
HRESULT comResult=S_OK;
void event(const std::string& value){events.push_back(value);}
}
HWND GetForegroundWindow(){return notifierHost::foreground;}
BOOL hostIsAppThemed(){return notifierHost::themed;}
HMODULE LoadLibraryW(const wchar_t* name){assert(std::wstring(name)==L"UxTheme.dll");notifierHost::event("load");return notifierHost::library?reinterpret_cast<HMODULE>(7):NULL;}
BOOL FreeLibrary(HMODULE value){assert(value==reinterpret_cast<HMODULE>(7));notifierHost::event("free");return 1;}
FARPROC GetProcAddress(HMODULE value,const char* name){assert(value==reinterpret_cast<HMODULE>(7)&&std::string(name)=="IsAppThemed");return notifierHost::procedure?reinterpret_cast<FARPROC>(&hostIsAppThemed):NULL;}
struct ITaskbarList3 {
    ULONG Release(){notifierHost::event("release");return 0;}
    HRESULT SetProgressValue(HWND window,ULONGLONG done,ULONGLONG total){notifierHost::event("progress:"+std::to_string(window)+":"+std::to_string(done)+":"+std::to_string(total));return 0;}
    HRESULT SetProgressState(HWND window,TBPFLAG value){notifierHost::event("state:"+std::to_string(window)+":"+std::to_string(value));return 0;}
};
HRESULT CoCreateInstance(int clsid,void* outer,DWORD context,int iid,void** output){
    assert(clsid==CLSID_TaskbarList&&outer==NULL&&context==CLSCTX_ALL&&iid==IID_ITaskbarList3);
    notifierHost::event("create");static ITaskbarList3 value;
    *output=notifierHost::interface?&value:NULL;return notifierHost::comResult;
}
BOOL FlashWindowEx(FLASHWINFO* value){assert(value->cbSize==20);notifierHost::event("flash:"+std::to_string(value->hwnd)+":"+std::to_string(value->dwFlags)+":"+std::to_string(value->uCount)+":"+std::to_string(value->dwTimeout));return 0;}
namespace wot {bool stockSimulationWorker(){return notifierHost::worker;}}
namespace BW {
struct App {static App* pInstance(){static App app;return &app;}HWND hWnd(){return notifierHost::window;}};
}
