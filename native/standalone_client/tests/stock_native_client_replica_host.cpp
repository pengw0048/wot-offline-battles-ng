#define WOT_CLIENT_REPLICA_HOST_BUILD
#include "../bigworld/stock_native_client_replica.cpp"
namespace stock_native_client_replica {namespace {
PyObject* nativeVector(const Point&v){return p::tupleNumbers(Point{float(v[0]),float(v[1]),float(v[2])}).release();}
Point nativeXYZ(PyObject*v){return p::numbers<3>(v);}
void nativeMatrix(PyObject*o,const Point&v,const Point&r,bool rotate){p::set(o,"translation",p::tupleNumbers(v));if(rotate)p::set(o,"rotation",p::tupleNumbers(r));}
bool engineOwns(std::int64_t,PyObject*o){return p::truth(o,"owned",true);}
}}
namespace {
using namespace stock_native_client_replica;
PyMethodDef methods[]={{"create",create,METH_VARARGS,""},{"ingest",ingest,METH_VARARGS,""},{"advance",advance,METH_VARARGS,""},{"manages",manages,METH_VARARGS,""},{"close",close,METH_VARARGS,""},{nullptr,nullptr,0,nullptr}};
}
PyMODINIT_FUNC initclient_replica_test(){Py_InitModule("client_replica_test",methods);}
