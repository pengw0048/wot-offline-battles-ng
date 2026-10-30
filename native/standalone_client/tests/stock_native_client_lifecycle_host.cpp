// Host Python 2.7 module for the lifecycle differential tests.
#define WOT_CLIENT_LIFECYCLE_HOST_BUILD
#include "../bigworld/stock_native_client_lifecycle.cpp"
#include "../bigworld/stock_native_client_events.cpp"
PyMODINIT_FUNC initoffline_client_lifecycle_native(void) {
    Py_InitModule("offline_client_lifecycle_native", stock_native_client_lifecycle::lifecycle_methods());
}
