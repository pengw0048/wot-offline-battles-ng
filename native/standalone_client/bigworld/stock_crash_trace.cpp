// First-chance access violations log EIP and the EBP-chain return addresses before the crash handler runs.
#include "pch.hpp"
#include <windows.h>

namespace {
LONG CALLBACK traceAccessViolation(EXCEPTION_POINTERS* info) {
    static volatile LONG reported = 0;
    if (info->ExceptionRecord->ExceptionCode != EXCEPTION_ACCESS_VIOLATION || InterlockedIncrement(&reported) > 3)
        return EXCEPTION_CONTINUE_SEARCH;
    const CONTEXT* c = info->ContextRecord;
    const DWORD image = DWORD(GetModuleHandle(nullptr));
    char line[1024];
    int used = _snprintf_s(line, sizeof(line), _TRUNCATE, "CRASH_TRACE base=%08lx eip=%08lx esp=%08lx ebp=%08lx frames=", image, c->Eip, c->Esp, c->Ebp);
    DWORD frame = c->Ebp;
    for (int i = 0; i < 32 && frame && !(frame & 3) && used > 0 && used < int(sizeof(line)) - 12; ++i) {
        if (IsBadReadPtr(reinterpret_cast<const void*>(frame), 8)) break;
        const DWORD* slots = reinterpret_cast<const DWORD*>(frame);
        used += _snprintf_s(line + used, sizeof(line) - used, _TRUNCATE, "%08lx ", slots[1]);
        if (slots[0] <= frame) break;
        frame = slots[0];
    }
    ERROR_MSG("%s\n", line);
    // FPO frames break the EBP chain: also list stack words that point into this image.
    const auto* base = reinterpret_cast<const BYTE*>(GetModuleHandle(nullptr));
    const auto* nt = reinterpret_cast<const IMAGE_NT_HEADERS*>(base + reinterpret_cast<const IMAGE_DOS_HEADER*>(base)->e_lfanew);
    const DWORD low = DWORD(base), high = low + nt->OptionalHeader.SizeOfImage;
    used = _snprintf_s(line, sizeof(line), _TRUNCATE, "CRASH_STACK ");
    for (DWORD at = c->Esp; at < c->Esp + 0x800 && used > 0 && used < int(sizeof(line)) - 12; at += 4) {
        if (IsBadReadPtr(reinterpret_cast<const void*>(at), 4)) break;
        const DWORD value = *reinterpret_cast<const DWORD*>(at);
        if (value > low && value < high) used += _snprintf_s(line + used, sizeof(line) - used, _TRUNCATE, "%08lx ", value);
    }
    ERROR_MSG("%s\n", line);
    return EXCEPTION_CONTINUE_SEARCH;
}
struct Install {
    Install() { AddVectoredExceptionHandler(1, traceAccessViolation); }
} install;
}
