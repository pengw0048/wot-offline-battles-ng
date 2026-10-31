// The offline_client_lifecycle_native module: the LAN client poll chain and the replay recording ports that
// lan_client.py and offline_replay.py still call. The battle itself is the native Battle (stock_native_battle*).
#if !defined(WOT_CLIENT_LIFECYCLE_HOST_BUILD)
#include "pch.hpp"
#include "pyscript/script.hpp"
#endif
#include "stock_native_client_lifecycle_python.hpp"
#include <vector>

namespace stock_native_client_lifecycle {
PyMethodDef* recording_methods();
PyMethodDef* lan_poll_methods();
PyMethodDef* lifecycle_methods() {
    static std::vector<PyMethodDef> all;
    if (all.empty()) {
        for (PyMethodDef* table : {recording_methods(), lan_poll_methods()})
            for (PyMethodDef* m = table; m->ml_name; ++m) all.push_back(*m);
        all.push_back({nullptr, nullptr, 0, nullptr});
    }
    return all.data();
}
}

#if !defined(WOT_CLIENT_LIFECYCLE_HOST_BUILD)
namespace {
class Module final : public BW::Script::InitTimeJob {
public:
    Module() : BW::Script::InitTimeJob(1) {}
    void init() override {
        if (!Py_InitModule("offline_client_lifecycle_native", stock_native_client_lifecycle::lifecycle_methods())) {
            PyErr_Print();
            CRITICAL_MSG("Native LAN client and replay module initialization failed\n");
        }
    }
};
Module module;
}
#endif
