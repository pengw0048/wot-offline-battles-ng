#include "pch.hpp"
#include "stock_windows_notifier.hpp"
#include "app.hpp"
#include "cstdmf/stock_worker_runtime.hpp"
#include "pyscript/pyobject_plus.hpp"
#include "pyscript/script.hpp"
#include <windows.h>
#include <shobjidl.h>

#ifdef _MSC_VER
#pragma comment(lib, "ole32.lib")
#pragma comment(lib, "uuid.lib")
#endif

BW_BEGIN_NAMESPACE
namespace
{
class WindowsNotifierRuntime
{
public:
    ~WindowsNotifierRuntime() { close(); }

    void activate() { active_ = true; }

    void notify( TBPFLAG state )
    {
        if (!active_ || wot::stockSimulationWorker()) return;
        App * app = App::pInstance();
        if (!app || !app->hWnd()) return;
        // 00624456/006244d6 compare GetForegroundWindow with App's real HWND.
        if (GetForegroundWindow() != app->hWnd()) update( app->hWnd(), state );
    }

    void reset()
    {
        if (!active_ || wot::stockSimulationWorker()) return;
        App * app = App::pInstance();
        if (app && app->hWnd()) update( app->hWnd(), TBPF_NOPROGRESS );
    }

    void close()
    {
        active_ = false;
        releaseTaskbar();
        isAppThemed_ = NULL;
        if (theme_) FreeLibrary( theme_ );
        theme_ = NULL;
    }

private:
    typedef BOOL (WINAPI * IsAppThemedFunction)();
    bool active_ = false;
    HMODULE theme_ = NULL;
    IsAppThemedFunction isAppThemed_ = NULL;
    ITaskbarList3 * taskbar_ = NULL;

    void releaseTaskbar()
    {
        ITaskbarList3 * previous = taskbar_;
        taskbar_ = NULL;
        if (previous) previous->Release();
    }

    void update( HWND window, TBPFLAG state )
    {
        // 00624800 resolves IsAppThemed dynamically. Retain one library
        // reference and release it at native shutdown, rather than leaking a
        // LoadLibrary reference on every notification as the original does.
        if (!theme_)
        {
            theme_ = LoadLibraryW( L"UxTheme.dll" );
            if (theme_) isAppThemed_ = reinterpret_cast<IsAppThemedFunction>(
                GetProcAddress( theme_, "IsAppThemed" ) );
        }
        if (isAppThemed_ && isAppThemed_())
        {
            releaseTaskbar();
            // Original 0062485f: no private COM apartment and no HrInit call.
            // COM belongs to the application. If unavailable, flashing below
            // still runs; this optional OS presentation cannot abort battle.
            const HRESULT result = CoCreateInstance( CLSID_TaskbarList, NULL,
                CLSCTX_ALL, IID_ITaskbarList3, reinterpret_cast<void **>( &taskbar_ ) );
            if (result == S_OK && taskbar_)
            {
                const ULONGLONG progress = state == TBPF_NOPROGRESS ? 0 : 1;
                taskbar_->SetProgressValue( window, progress, progress );
                taskbar_->SetProgressState( window, state );
            }
        }
        FLASHWINFO info = {};
        info.cbSize = sizeof( info );
        info.hwnd = window;
        info.dwFlags = state == TBPF_NOPROGRESS ? FLASHW_STOP : FLASHW_TRAY | FLASHW_TIMER;
        // 006248a0: count and timeout are zero, including FLASHW_STOP.
        FlashWindowEx( &info );
    }
};

WindowsNotifierRuntime & notifier()
{
    static WindowsNotifierRuntime runtime;
    return runtime;
}
}

class WGWinNotifier : public PyObjectPlus
{
    Py_Header( WGWinNotifier, PyObjectPlus )
public:
    WGWinNotifier() : PyObjectPlus( &s_type_ ) { notifier().activate(); }
    void onBattleBeginning() { notifier().notify( TBPF_ERROR ); }
    void onInvitation() { notifier().notify( TBPF_PAUSED ); }
    void resetTaskbar() { notifier().reset(); }
    PY_AUTO_METHOD_DECLARE( RETVOID, onBattleBeginning, END )
    PY_AUTO_METHOD_DECLARE( RETVOID, onInvitation, END )
    PY_AUTO_METHOD_DECLARE( RETVOID, resetTaskbar, END )
};

PY_TYPEOBJECT( WGWinNotifier )
PY_BEGIN_METHODS( WGWinNotifier )
    PY_METHOD( onBattleBeginning )
    PY_METHOD( onInvitation )
    PY_METHOD( resetTaskbar )
PY_END_METHODS()
PY_BEGIN_ATTRIBUTES( WGWinNotifier )
PY_END_ATTRIBUTES()
// 00597e50/00624710 exports the singleton object, not a Python constructor.
PY_MODULE_ATTRIBUTE( BigWorld, WGWindowsNotifier, new WGWinNotifier() )

void resetStockWindowsNotifier() { notifier().reset(); }
void finaliseStockWindowsNotifier() { notifier().close(); }
BW_END_NAMESPACE
