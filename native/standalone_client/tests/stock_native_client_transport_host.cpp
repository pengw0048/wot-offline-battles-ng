#include "stock_native_client_transport.hpp"
extern "C" void initoffline_client_transport_native(){
    auto*module=Py_InitModule("offline_client_transport_native",stock_native_client_transport::methods());
    if(module)stock_native_client_transport::initialize(module);
}
