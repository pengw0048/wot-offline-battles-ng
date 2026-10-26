// Win32 platform hooks rebuilt against the actual embedded Python C API.
#ifndef WOT_STOCK_OFFLINE_PLATFORM_HPP
#define WOT_STOCK_OFFLINE_PLATFORM_HPP
#include <Python.h>

// The central guard module owns registration; this table ends with a sentinel.
const PyMethodDef * stockOfflinePlatformMethods();
// Terminal application shutdown, after Python consumers stop using the hooks.
void finiStockOfflinePlatform();
#endif
