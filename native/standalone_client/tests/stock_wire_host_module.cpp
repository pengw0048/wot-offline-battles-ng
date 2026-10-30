// Host-only Python 2.7 module wrapper for stock_wire_codec tests.
#include "stock_wire_codec.hpp"
PyMODINIT_FUNC initwire_test(void) { Py_InitModule("wire_test", stock_wire::methods()); }
