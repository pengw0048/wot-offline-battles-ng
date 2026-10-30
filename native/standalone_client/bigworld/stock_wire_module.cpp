#include "pch.hpp"
#include "pyscript/script.hpp"
#include "stock_wire_codec.hpp"
#include "stock_wire_replay.hpp"
#include "stock_wire_net.hpp"
#include <vector>
namespace {
class Module final : public BW::Script::InitTimeJob {
    std::vector<PyMethodDef> methods_;
public:
    Module() : BW::Script::InitTimeJob(1) {}
    void init() override {
        for (PyMethodDef* table : {stock_wire::methods(), stock_wire_replay::methods(), stock_wire_net::methods()})
            for (PyMethodDef* method = table; method->ml_name; ++method) methods_.push_back(*method);
        methods_.push_back({nullptr, nullptr, 0, nullptr});
        if (!Py_InitModule("offline_client_wire_native", methods_.data())) {
            PyErr_Print();
            CRITICAL_MSG("Native battle wire initialization failed\n");
        }
    }
};
Module module;
}
