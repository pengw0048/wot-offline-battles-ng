#include "stock_native_client_input.hpp"
#include "stock_native_client_landing.hpp"
#include <vector>
extern "C" void initoffline_client_input_test(){
    static std::vector<PyMethodDef> methods;
    for(auto*method=stock_native_client_input::methods();method->ml_name;++method)methods.push_back(*method);
    for(auto*method=stock_native_client_landing::methods();method->ml_name;++method)methods.push_back(*method);
    methods.push_back({nullptr,nullptr,0,nullptr});
    Py_InitModule("offline_client_input_test",methods.data());
}
