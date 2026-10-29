#pragma once
#include <Python.h>
#ifdef hypot
#undef hypot
#endif
#include <array>
#include <cmath>
#include <cstdint>
#include <limits>
#include <stdexcept>
#include <string>
#include <vector>

namespace stock_native_bot_python {
struct Error:std::runtime_error {using std::runtime_error::runtime_error;};
inline PyObject* checked(PyObject*p){if(!p)throw Error("Native Bot Python boundary failed");return p;}
class Ref {
    PyObject*p_;
public:
    explicit Ref(PyObject*p):p_(checked(p)){}
    ~Ref(){Py_XDECREF(p_);}
    Ref(const Ref&)=delete;Ref&operator=(const Ref&)=delete;
    Ref(Ref&&r)noexcept:p_(r.p_){r.p_=nullptr;}
    PyObject*get()const{return p_;}
    PyObject*release(){auto*p=p_;p_=nullptr;return p;}
};
inline Ref retain(PyObject*p){checked(p);Py_INCREF(p);return Ref(p);}
inline Ref dict(){return Ref(PyDict_New());}
inline Ref list(){return Ref(PyList_New(0));}
inline Ref text(const std::string&s){
#if PY_MAJOR_VERSION>=3
return Ref(PyUnicode_FromStringAndSize(s.data(),Py_ssize_t(s.size())));
#else
return Ref(PyString_FromStringAndSize(s.data(),Py_ssize_t(s.size())));
#endif
}
inline Ref integer(std::int64_t v){return Ref(PyLong_FromLongLong(v));}
inline Ref number(double v){if(!std::isfinite(v))throw Error("Non-finite native Bot publication");return Ref(PyFloat_FromDouble(v));}
inline Ref boolean(bool v){return retain(v?Py_True:Py_False);}
inline void set(PyObject*d,const char*k,const Ref&v){if(PyDict_SetItemString(d,k,v.get())<0)throw Error("Native Bot publication allocation failed");}
inline void append(PyObject*d,const Ref&v){if(PyList_Append(d,v.get())<0)throw Error("Native Bot publication append failed");}
inline PyObject* field(PyObject*p,const char*k){if(!PyDict_Check(p))throw Error("Native Bot event is not a dictionary");return PyDict_GetItemString(p,k);}
inline Ref value(PyObject*p,const char*k){
    if(PyDict_Check(p)){auto*q=PyDict_GetItemString(p,k);return retain(q?q:Py_None);}
    auto*q=PyObject_GetAttrString(p,k);if(!q&&PyErr_ExceptionMatches(PyExc_AttributeError)){PyErr_Clear();return retain(Py_None);}return Ref(q);
}
inline Ref item(PyObject*p,Py_ssize_t n){return Ref(PySequence_GetItem(p,n));}
inline Py_ssize_t size(PyObject*p){if(!PyList_Check(p)&&!PyTuple_Check(p))throw Error("Native Bot event is not a sequence");return PySequence_Size(p);}
inline double scalar(PyObject*p){if(!p||p==Py_None||PyBool_Check(p))throw Error("Native Bot number missing");double v=PyFloat_AsDouble(p);if(PyErr_Occurred()||!std::isfinite(v))throw Error("Native Bot number invalid");return v;}
inline std::int64_t wide(PyObject*p){
    if(!p||p==Py_None||PyBool_Check(p))throw Error("Native Bot integer missing");
#if PY_MAJOR_VERSION<3
    if(PyInt_Check(p))return PyInt_AS_LONG(p);
#endif
    if(PyLong_Check(p)){auto n=PyLong_AsLongLong(p);if(PyErr_Occurred())throw Error("Native Bot integer overflow");return n;}
    const double v=scalar(p);if(v<-9223372036854775808.||v>=9223372036854775808.||std::floor(v)!=v)throw Error("Native Bot integer invalid");return std::int64_t(v);
}
inline bool flag(PyObject*p){if(!p||p==Py_None)return false;int v=PyObject_IsTrue(p);if(v<0)throw Error("Native Bot boolean invalid");return v!=0;}
inline std::string string(PyObject*p){
    if(!p||p==Py_None)return {};
#if PY_MAJOR_VERSION>=3
    auto*q=PyUnicode_AsUTF8(p);if(!q)throw Error("Native Bot text invalid");return q;
#else
    if(PyString_Check(p))return std::string(PyString_AS_STRING(p),PyString_GET_SIZE(p));
    Ref bytes(PyUnicode_AsUTF8String(p));return std::string(PyString_AS_STRING(bytes.get()),PyString_GET_SIZE(bytes.get()));
#endif
}
inline double numeric(PyObject*p,const char*k,double fallback=0.){auto v=value(p,k);return v.get()==Py_None?fallback:scalar(v.get());}
inline std::int64_t integral(PyObject*p,const char*k,std::int64_t fallback=0){auto v=value(p,k);return v.get()==Py_None?fallback:wide(v.get());}
inline bool truth(PyObject*p,const char*k,bool fallback=false){auto v=value(p,k);return v.get()==Py_None?fallback:flag(v.get());}
inline Ref call(PyObject*p,const char*k,PyObject*args){auto fn=value(p,k);return Ref(PyObject_CallObject(fn.get(),args));}
template<std::size_t N>std::array<double,N> numbers(PyObject*p){if(size(p)!=N)throw Error("Native Bot vector width mismatch");std::array<double,N> out;for(std::size_t i=0;i<N;++i){auto v=item(p,Py_ssize_t(i));out[i]=scalar(v.get());}return out;}
template<class Sequence>Ref numbers(const Sequence&values){auto out=list();for(const auto&v:values)append(out.get(),number(v));return out;}
template<class Sequence>Ref tupleNumbers(const Sequence&values){Ref out(PyTuple_New(Py_ssize_t(values.size())));Py_ssize_t i=0;for(const auto&v:values)PyTuple_SET_ITEM(out.get(),i++,number(v).release());return out;}
template<class Sequence>Ref integers(const Sequence&values){auto out=list();for(const auto&v:values)append(out.get(),integer(v));return out;}
template<class Sequence>Ref strings(const Sequence&values){auto out=list();for(const auto&v:values)append(out.get(),text(v));return out;}
inline const char* kind(bool human){return human?"human":"bot";}
}
