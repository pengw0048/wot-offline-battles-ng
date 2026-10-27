#pragma once

#include <windows.h>

#include <string>

namespace wot
{
namespace stock_lobby_input
{
// #1513 006f4820 for one LCID: LOCALE_SABBREVLANGNAME is converted to UTF-8 and
// truncated to its first two bytes ("ENU" -> "EN"). Returns false only when the
// locale query reports no text.
inline bool localeLanguageCode(LCID locale, std::string & result)
{
    const int size = GetLocaleInfoW(locale, LOCALE_SABBREVLANGNAME, NULL, 0);
    if (size <= 0)
        return false;
    std::wstring name(static_cast<size_t>(size), L'\0');
    if (GetLocaleInfoW(locale, LOCALE_SABBREVLANGNAME, &name[0], size) <= 0)
        return false;

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
    result.assign(utf8, 0, utf8.size() < 2 ? utf8.size() : 2);
    return true;
}

// The calling thread's active input language, read live on every call; the
// LCID is LOWORD(GetKeyboardLayout(0)).
inline bool languageCode(std::string & result)
{
    return localeLanguageCode(LOWORD(reinterpret_cast<ULONG_PTR>(GetKeyboardLayout(0))), result);
}
}
}
