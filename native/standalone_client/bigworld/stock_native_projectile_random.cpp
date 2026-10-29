#include <Python.h>
#include "stock_native_projectile_random.hpp"
#include <cmath>
#include <stdexcept>
namespace stock_native_projectile {
struct Random::State {
    PyObject* instance=nullptr;
    PyObject* dictionary=nullptr;
    PyCFunction random=nullptr;
    ~State(){Py_XDECREF(dictionary);Py_XDECREF(instance);}
};
Random::Random()=default;
Random::~Random()=default;
bool Random::install(PyObject* instance){
    PyObject* method=PyObject_GetAttrString(instance,"random");if(!method)return false;
    const bool native=PyCFunction_Check(method)&&PyCFunction_GET_FLAGS(method)==METH_NOARGS&&PyCFunction_GET_SELF(method)==instance;
    if(!native){Py_DECREF(method);PyErr_SetString(PyExc_TypeError,"Projectile random source must be the standard bound native Random.random method");return false;}
    auto next=std::make_unique<State>();next->random=PyCFunction_GET_FUNCTION(method);Py_INCREF(instance);next->instance=instance;Py_DECREF(method);
    next->dictionary=PyObject_GetAttrString(instance,"__dict__");if(!next->dictionary)return false;
    if(!PyDict_CheckExact(next->dictionary)){PyErr_SetString(PyExc_TypeError,"Projectile random owner must expose its plain instance dictionary");return false;}
    auto spare=PyDict_GetItemString(next->dictionary,"gauss_next");
    if(!spare||(spare!=Py_None&&!PyFloat_CheckExact(spare))){PyErr_SetString(PyExc_TypeError,"Projectile random Gaussian spare is invalid");return false;}
    state_=std::move(next);return true;
}
double Random::uniform(){
    if(!state_)throw std::logic_error("native projectile random owner is not installed");
    PyObject* value=state_->random(state_->instance,nullptr);
    if(!value)throw std::runtime_error("native projectile random draw failed");
    if(!PyFloat_CheckExact(value)){Py_DECREF(value);throw std::runtime_error("native projectile random draw type changed");}
    const double out=PyFloat_AS_DOUBLE(value);Py_DECREF(value);
    if(!std::isfinite(out)||out<0.||out>=1.)throw std::runtime_error("native projectile random draw is invalid");return out;
}
double Random::gauss(double mean,double sigma){
    if(!state_)throw std::logic_error("native projectile random owner is not installed");
    auto spare=PyDict_GetItemString(state_->dictionary,"gauss_next");
    if(!spare||(spare!=Py_None&&!PyFloat_CheckExact(spare)))throw std::runtime_error("native projectile Gaussian spare changed type");
    const bool cached=spare!=Py_None;double z=cached?PyFloat_AS_DOUBLE(spare):0.;
    if(PyDict_SetItemString(state_->dictionary,"gauss_next",Py_None)<0)throw std::runtime_error("native projectile Gaussian spare reset failed");
    if(!cached){
        const double angle=uniform()*6.28318530717958647692,radius=std::sqrt(-2.*std::log(1.-uniform()));z=std::cos(angle)*radius;
        PyObject* next=PyFloat_FromDouble(std::sin(angle)*radius);if(!next)throw std::runtime_error("native projectile Gaussian spare allocation failed");
        const int result=PyDict_SetItemString(state_->dictionary,"gauss_next",next);Py_DECREF(next);if(result<0)throw std::runtime_error("native projectile Gaussian spare update failed");
    }
    return mean+z*sigma;
}
}
