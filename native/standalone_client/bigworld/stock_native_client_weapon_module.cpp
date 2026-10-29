#include "pch.hpp"
#include "pyscript/script.hpp"
#include "stock_native_client_weapon_python.hpp"
namespace {
class Module final:public BW::Script::InitTimeJob {
public:
    Module():BW::Script::InitTimeJob(1){}
    void init()override{auto*m=Py_InitModule("offline_client_weapon_native",stock_native_client_weapon::methods());if(!m||!stock_native_client_weapon::initialize(m)){PyErr_Print();CRITICAL_MSG("Native client weapon initialization failed\n");}}
};
Module module;
}
