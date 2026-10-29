#include "stock_native_bot_foliage_snapshot.hpp"
#include <Python.h>
using namespace stock_native_bot_foliage;
namespace {
Point point(PyObject*p){Point out;for(unsigned i=0;i<3;++i){PyObject*v=PySequence_GetItem(p,i);out[i]=PyFloat_AsDouble(v);Py_DECREF(v);}return out;}
PyObject* point(const Point&p){return Py_BuildValue("(ddd)",p[0],p[1],p[2]);}
PyObject* row(const native_visibility::Volume&v){
    if(!v.dynamic){PyObject*out=PyTuple_New(10);for(unsigned i=0;i<10;++i)PyTuple_SET_ITEM(out,i,PyFloat_FromDouble(v.static_row[i]));return out;}
    PyObject*axes=PyTuple_New(3);for(unsigned i=0;i<3;++i)PyTuple_SET_ITEM(axes,i,point(v.half_axes[i]));return Py_BuildValue("{s:N,s:N,s:d,s:d}","center",point(v.center),"half_axes",axes,"strength",v.strength,"radius",v.radius);
}
PyObject* snapshot(const native_visibility::FoliageSnapshot&v){
    PyObject*rows=PyList_New(v.instances.size());for(unsigned i=0;i<v.instances.size();++i)PyList_SET_ITEM(rows,i,row(v.instances[i]));
    PyObject*cells=PyDict_New();for(const auto&c:v.cells){PyObject*k=Py_BuildValue("(ii)",c.first.first,c.first.second),*members=PyList_New(c.second.size());for(unsigned i=0;i<c.second.size();++i)PyList_SET_ITEM(members,i,PyLong_FromSize_t(c.second[i]));PyDict_SetItem(cells,k,members);Py_DECREF(k);Py_DECREF(members);}
    PyObject*inactive=PyList_New(v.inactive_instances.size());unsigned i=0;for(auto index:v.inactive_instances)PyList_SET_ITEM(inactive,i++,PyLong_FromSize_t(index));return Py_BuildValue("(NNN)",rows,cells,inactive);
}
struct Poses:TreePoses {
    Point center;std::array<Point,3>axes;bool pending=false,present=true;
    bool waiting(std::uint32_t)const override{return pending;}
    bool loaded(std::uint32_t)const override{return present;}
    bool pose(Key,const Point&,const Point&,Point&c,std::array<Point,3>&a)override{c=center;a=axes;return true;}
};
PyObject* inspect(PyObject*,PyObject*battle){native_visibility::FoliageSnapshot value;if(!readFoliageSnapshot(battle,value))return nullptr;return snapshot(value);}
PyObject* update(PyObject*,PyObject*battle){offline_visibility::Update value;if(!readFoliageUpdate(battle,value))return nullptr;return Py_BuildValue("(iii)",int(value.instances.size()),int(value.cells.size()),int(value.inactive.size()));}
PyObject* trace(PyObject*,PyObject*args){
    PyObject*battle,*cases;if(!PyArg_ParseTuple(args,"OO",&battle,&cases))return nullptr;
    TreeState state;if(!readFoliageTreeState(battle,state))return nullptr;Trees trees(std::move(state));trees.activate({1,7});Poses poses;
    PyObject*out=PyList_New(PySequence_Size(cases));
    for(Py_ssize_t i=0;i<PySequence_Size(cases);++i){PyObject*c=PySequence_GetItem(cases,i),*clock=PySequence_GetItem(c,0),*center=PySequence_GetItem(c,1),*axes=PySequence_GetItem(c,2),*body=PySequence_GetItem(c,3),*waiting=PySequence_GetItem(c,4),*loaded=PySequence_GetItem(c,5);
        const double now=PyFloat_AsDouble(clock);poses.center=point(center);for(unsigned a=0;a<3;++a){PyObject*p=PySequence_GetItem(axes,a);poses.axes[a]=point(p);Py_DECREF(p);}poses.pending=PyObject_IsTrue(waiting);poses.present=PyObject_IsTrue(loaded);
        std::set<Key>bodies;if(PyObject_IsTrue(body))bodies.insert({1,7});offline_visibility::Update delta;const bool changed=trees.refresh(now,bodies,poses,delta);
        Py_DECREF(c);Py_DECREF(clock);Py_DECREF(center);Py_DECREF(axes);Py_DECREF(body);Py_DECREF(waiting);Py_DECREF(loaded);
        PyObject*signature=Py_None;Py_INCREF(signature);const auto stable=trees.state().stable.find({1,7});
        if(stable!=trees.state().stable.end()){Py_DECREF(signature);signature=PyTuple_New(12);for(unsigned n=0;n<12;++n)PyTuple_SET_ITEM(signature,n,PyFloat_FromDouble(stable->second.signature[n]));}
        PyList_SET_ITEM(out,i,Py_BuildValue("(iNiNd)",int(changed),snapshot(trees.state().foliage),int(trees.state().refreshing.size()),signature,trees.state().next_refresh));
    }return out;
}
PyMethodDef methods[]={{"inspect",inspect,METH_O,nullptr},{"update",update,METH_O,nullptr},{"trace",trace,METH_VARARGS,nullptr},{nullptr,nullptr,0,nullptr}};
}
extern "C" void initfoliage_snapshot_test(){Py_InitModule("foliage_snapshot_test",methods);}
