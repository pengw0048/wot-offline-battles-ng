#pragma once

#include <windows.h>

#include <string>

namespace wot
{
namespace stock_lobby_input
{
// #1513 006f4820 for one LCID: LOCALE_SABBREVLANGNAME is converted to UTF-8 and
// truncated to its first two bytes ("ENU" -> "EN"). A failed UTF-8 conversion
// yields "" as in the original; a failed locale query, where the original reads
// an unfilled buffer, is contained to "" as well.
inline std::string localeLanguageCode(LCID locale)
{
    const int size = GetLocaleInfoW(locale, LOCALE_SABBREVLANGNAME, NULL, 0);
    if (size <= 0)
        return std::string();
    std::wstring name(static_cast<size_t>(size), L'\0');
    if (GetLocaleInfoW(locale, LOCALE_SABBREVLANGNAME, &name[0], size) <= 0)
        return std::string();

    // 00687780 (bw_wtoutf8): a failed conversion leaves the result empty.
    std::string utf8;
    const int bytes = WideCharToMultiByte(CP_UTF8, 0, name.c_str(), -1, NULL, 0, NULL, NULL);
    if (bytes > 0)
    {
        utf8.resize(static_cast<size_t>(bytes));
        const int written = WideCharToMultiByte(CP_UTF8, 0, name.c_str(), -1, &utf8[0], bytes,
            NULL, NULL);
        utf8.resize(written > 0 ? static_cast<size_t>(written - 1) : 0);
    }
    return utf8.substr(0, 2);
}

// The calling thread's active input language, read live on every call; the
// LCID is LOWORD(GetKeyboardLayout(0)).
inline std::string languageCode()
{
    return localeLanguageCode(LOWORD(reinterpret_cast<ULONG_PTR>(GetKeyboardLayout(0))));
}
}
}
