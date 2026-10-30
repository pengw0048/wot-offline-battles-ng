// Host-only Python 2.7 module with the client's production module name.
#include "stock_wire_codec.hpp"
#include "stock_wire_replay.hpp"
#include "stock_wire_net.hpp"
#include <vector>
PyMODINIT_FUNC initoffline_client_wire_native(void) {
    static std::vector<PyMethodDef> methods;
    for (PyMethodDef* table : {stock_wire::methods(), stock_wire_replay::methods(), stock_wire_net::methods()})
        for (PyMethodDef* method = table; method->ml_name; ++method) methods.push_back(*method);
    methods.push_back({nullptr, nullptr, 0, nullptr});
    Py_InitModule("offline_client_wire_native", methods.data());
}
