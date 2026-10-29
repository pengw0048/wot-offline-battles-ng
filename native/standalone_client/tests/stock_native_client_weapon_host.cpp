#include "stock_native_client_weapon_python.hpp"
extern "C" void initoffline_client_weapon_native(){
    auto*module=Py_InitModule("offline_client_weapon_native",stock_native_client_weapon::methods());
    if(module)stock_native_client_weapon::initialize(module);
}
