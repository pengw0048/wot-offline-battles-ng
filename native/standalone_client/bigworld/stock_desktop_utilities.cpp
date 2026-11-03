#ifndef WOT_DESKTOP_UTILITIES_HOST
#include "pch.hpp"
#include "pyscript/script.hpp"
#include <windows.h>
#endif
#include <cstring>
#include <limits>

BW_BEGIN_NAMESPACE
int StockDesktopUtilities_token = 1;

bool wg_copyToClipboard( const BW::wstring & text )
{
    // #1513 006f53d0 accepts one wide string and returns a Python bool;
    // 006f5780 publishes the NUL-terminated string as CF_UNICODETEXT.
    if (!OpenClipboard( NULL )) return false;
    bool copied = false;
    if (text.size() < (std::numeric_limits<size_t>::max)() / sizeof(wchar_t) &&
        EmptyClipboard())
    {
        const size_t bytes = (text.size() + 1) * sizeof(wchar_t);
        // Clipboard ownership transfers only after SetClipboardData succeeds.
        // Use movable storage and contain allocation/locking failures locally.
        HGLOBAL allocation = GlobalAlloc( GMEM_MOVEABLE | GMEM_DDESHARE, bytes );
        if (allocation)
        {
            void * target = GlobalLock( allocation );
            if (target)
            {
                std::memcpy( target, text.c_str(), bytes );
                GlobalUnlock( allocation );
                copied = SetClipboardData( CF_UNICODETEXT, allocation ) != NULL;
            }
            if (!copied) GlobalFree( allocation );
        }
    }
    CloseClipboard();
    return copied;
}
PY_AUTO_MODULE_FUNCTION( RETDATA, wg_copyToClipboard, ARG( BW::wstring, END ), BigWorld )

int wg_firstDayOfWeek()
{
    // #1513 0096f293 requests a numeric DWORD, not locale text. Preserve its
    // raw value and -1 failure sentinel; UtilsManager owns the AS3 +1 mapping.
    int firstDay = -1;
    const int copied = GetLocaleInfoW( LOCALE_USER_DEFAULT,
        LOCALE_IFIRSTDAYOFWEEK | LOCALE_RETURN_NUMBER,
        reinterpret_cast<LPWSTR>( &firstDay ), 2 );
    return copied == 2 ? firstDay : -1;
}
PY_AUTO_MODULE_FUNCTION( RETDATA, wg_firstDayOfWeek, END, BigWorld )
BW_END_NAMESPACE
