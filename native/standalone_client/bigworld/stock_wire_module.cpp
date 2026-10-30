#include "pch.hpp"
#include "pyscript/script.hpp"
#include "stock_wire_codec.hpp"
namespace {
class Module final : public BW::Script::InitTimeJob {
public:
    Module() : BW::Script::InitTimeJob(1) {}
    void init() override {
        if (!Py_InitModule("offline_client_wire_native", stock_wire::methods())) {
            PyErr_Print();
            CRITICAL_MSG("Native battle wire initialization failed\n");
        }
    }
};
Module module;
}
