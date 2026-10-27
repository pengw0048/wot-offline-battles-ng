// Immutable simulation-worker process role shared by client, Moo, romp, ashes
// and the offline platform module. The patcher copies this file unchanged to
// lib/cstdmf/stock_worker_runtime.hpp.
#ifndef WOT_STOCK_WORKER_RUNTIME_HPP
#define WOT_STOCK_WORKER_RUNTIME_HPP
#include <windows.h>

namespace wot {

// Exact full value of the launcher's existing environment contract.
inline bool readStockSimulationWorkerRole()
{
    static const wchar_t expected[] = L"simulation_worker";
    const DWORD length = static_cast<DWORD>(sizeof(expected) / sizeof(expected[0]) - 1);
    wchar_t value[sizeof(expected) / sizeof(expected[0]) + 1] = {};
    // A longer value returns its required size, so a prefix never matches.
    if (GetEnvironmentVariableW(L"OFFLINE_LAN_0922_CLIENT_MODE", value,
            static_cast<DWORD>(sizeof(value) / sizeof(value[0]))) != length)
        return false;
    for (DWORD index = 0; index < length; ++index)
        if (value[index] != expected[index]) return false;
    return true;
}

// The first call caches the role for the process; BWWinMain makes it first.
inline bool stockSimulationWorker()
{
    static const bool worker = readStockSimulationWorkerRole();
    return worker;
}

// BWWinMain marks the never-shown worker window it owns with this property.
inline const wchar_t * stockWorkerWindowProperty()
{
    return L"WoTIndependent.WorkerRuntime.Window";
}

} // namespace wot
#endif
