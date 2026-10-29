#include "pch.hpp"
#include "pyscript/script.hpp"
#include "stock_native_client_transport.hpp"
#include "stock_native_client_admission.hpp"
#include "stock_native_client_input.hpp"
#include "stock_native_client_input_runtime.hpp"
#include "stock_native_client_input_controls_python.hpp"
#include "stock_native_client_landing.hpp"
#include <vector>
namespace {
PyObject* held(PyObject*,PyObject*args){
    PyObject*client,*message;
    if(!PyArg_ParseTuple(args,"OO",&client,&message))return nullptr;
    return stock_native_client_admission::held(client,message);
}
class Module final:public BW::Script::InitTimeJob {
    std::vector<PyMethodDef> methods_;
public:
    Module():BW::Script::InitTimeJob(1){}
    void init()override{
        for(auto*method=stock_native_client_transport::methods();method->ml_name;++method)
            methods_.push_back(*method);
        for(auto*method=stock_native_client_input::methods();method->ml_name;++method)
            methods_.push_back(*method);
        for(auto*method=stock_native_client_landing::methods();method->ml_name;++method)
            methods_.push_back(*method);
        methods_.push_back({"send_current",stock_native_client_input::sendCurrentPython,METH_VARARGS,"Publish the native player's current input."});
        methods_.push_back({"control",stock_native_client_input::control,METH_VARARGS,"Apply an original player input edge."});
        methods_.push_back({"align_control",stock_native_client_input::alignControl,METH_VARARGS,"Align the player's native gun controls."});
        methods_.push_back({"assign_control",stock_native_client_input::assignControl,METH_VARARGS,"Mirror a discrete stock control edge."});
        methods_.push_back({"configure_admission",stock_native_client_admission::configure,METH_VARARGS,"Install native snapshot admission."});
        methods_.push_back({"accept",stock_native_client_admission::accept,METH_VARARGS,"Admit one ordered snapshot."});
        methods_.push_back({"timing",stock_native_client_admission::timing,METH_VARARGS,"Accept server timeline fields."});
        methods_.push_back({"held",held,METH_VARARGS,"Resolve the accepted snapshot version."});
        methods_.push_back({nullptr,nullptr,0,nullptr});
        auto*m=Py_InitModule("offline_client_transport_native",methods_.data());
        if(m)stock_native_client_transport::initialize(m);
        else {PyErr_Print();CRITICAL_MSG("Native client transport initialization failed\n");}
    }
};
Module module;
}
