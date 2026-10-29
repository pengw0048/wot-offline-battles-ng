#include <cstdlib>
#include <new>
namespace allocation_probe {bool active=false;std::size_t count=0;}
void* operator new(std::size_t size){void*value=std::malloc(size?size:1);if(!value)throw std::bad_alloc();if(allocation_probe::active)++allocation_probe::count;return value;}
void operator delete(void*value)noexcept{std::free(value);}
void* operator new[](std::size_t size){return ::operator new(size);}
void operator delete[](void*value)noexcept{::operator delete(value);}
#define WOT_CLIENT_REPLICA_HOST_BUILD
#include "../bigworld/stock_native_client_state.cpp"
#include "../bigworld/stock_native_client_replica.cpp"
#include "../bigworld/stock_native_client_drive_presentation_python.cpp"
namespace stock_native_client_replica {namespace {
PyObject* nativeVector(const Point&v){return p::tupleNumbers(Point{float(v[0]),float(v[1]),float(v[2])}).release();}
Point nativeXYZ(PyObject*v){return p::numbers<3>(v);}
void nativeMatrix(PyObject*o,const Point&v,const Point&r,bool rotate){p::set(o,"translation",p::tupleNumbers(v));if(rotate)p::set(o,"rotation",p::tupleNumbers(r));}
bool engineOwns(std::int64_t,PyObject*o){return p::truth(o,"owned",true);}
}}
namespace {
using namespace stock_native_client_replica;
template<class F>std::size_t allocations(F&&fn){allocation_probe::count=0;allocation_probe::active=true;try{fn();}catch(...){allocation_probe::active=false;throw;}allocation_probe::active=false;return allocation_probe::count;}
PyObject*valueContract(PyObject*,PyObject*){try{
    using V=stock_native_client_state::Value;auto out=p::dict();
    // Initialize the one process-wide empty iterator sentinel before counting.
    V empty;static_cast<void>(empty.object.begin());const V&constant=empty;
    if(!constant.object.empty()||constant.object.begin()!=constant.object.end()||constant.object.find("missing")!=constant.object.end()||constant.object.count("missing")||constant.object.size())throw p::Error("Empty object lookup changed");
    p::set(out.get(),"probe",p::integer(allocations([]{void*block=::operator new(1);::operator delete(block);} )));
    volatile double observed=0.;
    p::set(out.get(),"primitive",p::integer(allocations([&]{for(int i=0;i<128;++i){V value;value.kind=V::Kind::Number;value.number=i;V copy=value;V moved=std::move(copy);observed+=moved.numeric();}})));
    const auto version=std::make_shared<const V>(empty);
    p::set(out.get(),"shared",p::integer(allocations([&]{for(int i=0;i<128;++i){V value;value.kind=V::Kind::Shared;value.reference=version;V copy=value;V moved=std::move(copy);observed+=moved.numeric();}})));
    p::set(out.get(),"legacy_empty_map",p::integer(allocations([&]{for(int i=0;i<128;++i){std::map<std::string,V> value;observed+=value.size();}})));
    V source;source.kind=V::Kind::Object;source.object["z"].number=9.;source.object["a"].array.push_back(empty);source.object["a"].kind=V::Kind::Array;
    source.object["z"].kind=V::Kind::Number;const auto*identity=&source.object["z"];
    V moved;
    p::set(out.get(),"move",p::integer(allocations([&]{V temporary(std::move(source));moved=std::move(temporary);} )));
    if(&moved.object["z"]!=identity||source.object.size()||source.object.begin()!=source.object.end())throw p::Error("Move changed object identity");
    auto copy=moved;copy.object["z"].number=4.;copy.object["a"].array.front().kind=V::Kind::Boolean;
    if(moved.get("z").numeric()!=9.||moved.get("a").array.front().kind!=V::Kind::Null)throw p::Error("Object copy aliases source");
    auto first=moved.object.begin();if(first->first!="a"||(++first)->first!="z"||++first!=moved.object.end())throw p::Error("Object ordering changed");
    auto duplicate=moved.object.emplace("z",empty);if(duplicate.second||duplicate.first->second.numeric()!=9.)throw p::Error("Duplicate insertion overwrote data");
    moved.object["middle"]=empty;if(&moved.object["z"]!=identity)throw p::Error("Insertion invalidated object reference");
    if(moved.object.erase("absent")||moved.object.erase("middle")!=1||moved.object.size()!=2)throw p::Error("Object erase changed");
    moved.object.clear();empty.kind=V::Kind::Object;if(!moved.object.empty()||moved!=empty)throw p::Error("Cleared object changed");
    std::vector<V> values;values.reserve(1);V frozen;frozen.kind=V::Kind::Shared;frozen.reference=version;values.push_back(frozen);
    const auto relocation=allocations([&]{values.reserve(128);});if(values.front().reference!=version)throw p::Error("Vector relocation changed shared identity");
    p::set(out.get(),"vector_relocation",p::integer(relocation));return out.release();
}catch(const std::exception&e){return fail(e);}}
PyObject* clockState(PyObject*,PyObject*args){auto handle=stock_native_bot_python::Ref(stateHandle(nullptr,args));auto state=stock_native_client_state::shared(handle.get());return Py_BuildValue("(OdK)",state->server_time_ms?stock_native_bot_python::integer(*state->server_time_ms).get():Py_None,state->received_at,static_cast<unsigned long long>(state->epoch));}
PyObject* controllerCall(PyObject*self,PyObject*args){return PyObject_CallObject(PyTuple_GET_ITEM(self,0),args);}
PyMethodDef controllerMethod={"native_controller",controllerCall,METH_VARARGS,""};
PyObject* controller(PyObject*,PyObject*args){PyObject*object;const char*name;if(!PyArg_ParseTuple(args,"Os",&object,&name))return nullptr;auto fn=stock_native_bot_python::value(object,name);stock_native_bot_python::Ref context(PyTuple_Pack(1,fn.get()));return PyCFunction_New(&controllerMethod,context.get());}
PyObject*storage(PyObject*,PyObject*args){auto handle=stock_native_bot_python::Ref(stateHandle(nullptr,args));auto state=stock_native_client_state::shared(handle.get());auto out=stock_native_bot_python::dict();for(const auto&entry:state->actors){const auto&a=*entry.second;auto row=stock_native_bot_python::dict();stock_native_bot_python::set(row.get(),"current_shared",stock_native_bot_python::boolean(bool(a.state.reference)));stock_native_bot_python::set(row.get(),"sample_shared",stock_native_bot_python::boolean(a.samples.empty()||a.samples.back().state.reference==a.state.reference));stock_native_bot_python::set(row.get(),"receipt_shared",stock_native_bot_python::boolean(a.receipts.empty()||a.receipts.back().state.reference==a.state.reference));if(!a.receipts.empty())stock_native_bot_python::set(row.get(),"first_receipt",stock_native_bot_python::Ref(stock_native_client_state::write(a.receipts.front().state)));stock_native_bot_python::set(out.get(),entry.first.c_str(),row);}return out.release();}
PyMethodDef methods[]={{"value_contract",valueContract,METH_NOARGS,""},{"storage",storage,METH_VARARGS,""},{"controller",controller,METH_VARARGS,""},{"clock_state",clockState,METH_VARARGS,""},{"state",stateHandle,METH_VARARGS,""},{"snapshot",snapshot,METH_VARARGS,""},{"manifest",manifest,METH_VARARGS,""},{"refresh_pending",refreshPending,METH_VARARGS,""},{"flush_pending",flushPending,METH_VARARGS,""},{"refresh",refresh,METH_VARARGS,""},{"probes",probes,METH_VARARGS,""},{"statistics",statistics,METH_VARARGS,""},{"inspect",inspect,METH_VARARGS,""},{"create",create,METH_VARARGS,""},{"ingest",ingest,METH_VARARGS,""},{"advance",advance,METH_VARARGS,""},{"manages",manages,METH_VARARGS,""},{"close",close,METH_VARARGS,""},{nullptr,nullptr,0,nullptr}};
}
PyMODINIT_FUNC initclient_replica_test(){Py_InitModule("client_replica_test",methods);}
