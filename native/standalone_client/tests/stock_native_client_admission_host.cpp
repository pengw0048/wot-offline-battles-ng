#include "stock_native_client_admission.hpp"
#include "stock_native_client_transport.hpp"
#include "stock_native_client_input.hpp"
#include "stock_native_client_landing.hpp"
#include <vector>
namespace {
PyObject* held(PyObject*,PyObject*args){PyObject*client,*message;if(!PyArg_ParseTuple(args,"OO",&client,&message))return nullptr;return stock_native_client_admission::held(client,message);}
}
extern "C" void initclient_admission_test(){
    std::vector<PyMethodDef> methods;
    for(auto*method=stock_native_client_transport::methods();method->ml_name;++method)methods.push_back(*method);
    for(auto*method=stock_native_client_input::methods();method->ml_name;++method)methods.push_back(*method);
    for(auto*method=stock_native_client_landing::methods();method->ml_name;++method)methods.push_back(*method);
    methods.push_back({"configure_admission",stock_native_client_admission::configure,METH_VARARGS,""});
    methods.push_back({"accept",stock_native_client_admission::accept,METH_VARARGS,""});
    methods.push_back({"timing",stock_native_client_admission::timing,METH_VARARGS,""});
    methods.push_back({"held",held,METH_VARARGS,""});
    methods.push_back({nullptr,nullptr,0,nullptr});
    // Py_InitModule retains the method-definition pointers for the module life.
    auto*persistent=new std::vector<PyMethodDef>(methods);
    auto*module=Py_InitModule("client_admission_test",persistent->data());
    if(module)stock_native_client_transport::initialize(module);
}
