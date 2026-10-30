// Host-only Python 2.7 module with the client's production module name.
#include "stock_wire_codec.hpp"
PyMODINIT_FUNC initoffline_client_wire_native(void) { Py_InitModule("offline_client_wire_native", stock_wire::methods()); }
