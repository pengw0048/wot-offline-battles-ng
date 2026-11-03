#include "stock_native_bot_human_snapshot.hpp"
#include "stock_native_client_state.hpp"
#include <Python.h>
static PyObject* project(PyObject*, PyObject* args) {
    PyObject *raw,*snapshot,*descriptor;
    if (!PyArg_ParseTuple(args,"OOO",&raw,&snapshot,&descriptor)) return nullptr;
    auto input=stock_native_client_state::read(raw), effective=stock_native_client_state::read(snapshot);
    stock_native_bot::HumanDescriptor out;
    if (!stock_native_bot::readHumanProjections(input,effective,descriptor,out)) return nullptr;
    if (PyDict_GetItemString(raw,"critical")) {
        out.id=1;out.radio_maximum=100.;out.vision_maximum=100.;
        stock_native_bot::HumanIngress ingress;
        if (!stock_native_bot::readHumanInput(input,out,ingress)) return nullptr;
        const auto& p=ingress.human.perception;
        return Py_BuildValue("(ddii)",p.config.radio,p.config.relay,int(p.sample.last_effort),int(p.sample.designated));
    }
    PyObject* rows=PyList_New(0);
    for (const auto& p:out.projections) {
        PyObject* row=Py_BuildValue("(dddddii)",p.config.radio,p.config.relay,p.config.designated_duration,p.config.last_effort_duration,p.config.designated_sector,int(p.last_effort),int(p.designated));
        PyList_Append(rows,row);Py_DECREF(row);
    }
    return rows;
}
static PyMethodDef methods[]={{"project",project,METH_VARARGS,nullptr},{nullptr,nullptr,0,nullptr}};
extern "C" __attribute__((visibility("default"))) void inithuman_projection_test(){Py_InitModule("human_projection_test",methods);}
