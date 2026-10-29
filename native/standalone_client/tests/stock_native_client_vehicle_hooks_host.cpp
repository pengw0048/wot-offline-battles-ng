#define WOT_CLIENT_VEHICLE_HOOKS_HOST
#include "../bigworld/stock_native_client_vehicle_hooks.cpp"
PyMODINIT_FUNC initoffline_client_vehicle_hooks_native() {
    stock_native_client_vehicle_hooks::initialize();
}
