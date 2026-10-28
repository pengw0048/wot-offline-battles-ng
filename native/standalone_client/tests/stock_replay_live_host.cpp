#include <Python.h>
#include <cmath>
#include <climits>
#include <map>
#include <string>
#include <vector>
#include <cstdint>
struct Vector2 {float x=0,y=0;Vector2(){} Vector2(float a,float b):x(a),y(b){} };
struct Vector3 {float x=0,y=0,z=0;static Vector3 zero(){return Vector3();}};
namespace Script {
int setData(PyObject* o,float& v,const char*){double d=PyFloat_AsDouble(o);if(PyErr_Occurred())return -1;v=static_cast<float>(d);return 0;}
int setData(PyObject* o,int& v,const char*){long n=PyInt_AsLong(o);if(PyErr_Occurred())return -1;if(n<INT32_MIN||n>INT32_MAX){PyErr_SetString(PyExc_OverflowError,"int32");return -1;}v=static_cast<int>(n);return 0;}
int setData(PyObject* o,unsigned int& v,const char*){unsigned long n=PyLong_AsUnsignedLong(o);if(PyErr_Occurred())return -1;if(n>UINT32_MAX){PyErr_SetString(PyExc_OverflowError,"uint32");return -1;}v=static_cast<unsigned int>(n);return 0;}
int setData(PyObject* o,bool& v,const char*){int n=PyObject_IsTrue(o);if(n<0)return -1;v=n!=0;return 0;}
int setData(PyObject* o,Vector2& v,const char*){return PyArg_ParseTuple(o,"ff",&v.x,&v.y)?0:-1;}
int setData(PyObject* o,Vector3& v,const char*){return PyArg_ParseTuple(o,"fff",&v.x,&v.y,&v.z)?0:-1;}
PyObject* getData(float v){return PyFloat_FromDouble(v);} PyObject* getData(int v){return PyInt_FromLong(v);}
PyObject* getData(unsigned int v){return PyLong_FromUnsignedLong(v);} PyObject* getData(bool v){return PyBool_FromLong(v);}
PyObject* getData(Vector2 v){return Py_BuildValue("(ff)",v.x,v.y);} PyObject* getData(Vector3 v){return Py_BuildValue("(fff)",v.x,v.y,v.z);}
}
struct ScriptObject {enum Ownership {FROM_BORROWED_REFERENCE};PyObject* value;ScriptObject(PyObject* v,Ownership):value(v){}};
namespace Pickler {std::string pickle(ScriptObject data){PyObject* m=PyImport_ImportModule("cPickle");if(!m)return {};PyObject* p=PyObject_CallMethod(m,const_cast<char*>("dumps"),const_cast<char*>("Oi"),data.value,2);Py_DECREF(m);if(!p)return {};std::string result(PyString_AS_STRING(p),PyString_GET_SIZE(p));Py_DECREF(p);return result;}}
struct Controller {bool active=false;float now=0;bool playing()const{return active;}float timeMark(unsigned int)const{return now;}};
struct Entity {bool entering=false;bool stockReplayEnteringWorld(){return entering;}};
struct ConnectionControl {std::map<int,Entity> entities;static ConnectionControl* pInstance(){static ConnectionControl c;return &c;}Entity* findEntity(int id){auto it=entities.find(id);return it==entities.end()?nullptr:&it->second;}};
struct State {Controller controller;
@FIELDS@
};
struct PyController {PyObject_HEAD State* state;PyObject* callbacks[11];};
bool noArgs(PyObject* args,PyObject* kwargs){static char* names[]={NULL};return PyArg_ParseTupleAndKeywords(args,kwargs,"",names)!=0;}
@TEXT@
@METHODS@
@PROPERTIES@
PyObject* make(PyTypeObject* cls,PyObject*,PyObject*){auto* self=reinterpret_cast<PyController*>(cls->tp_alloc(cls,0));self->state=new State;for(auto*& c:self->callbacks)c=NULL;return reinterpret_cast<PyObject*>(self);}
void destroy(PyController* self){for(auto* c:self->callbacks)Py_XDECREF(c);delete self->state;Py_TYPE(self)->tp_free(reinterpret_cast<PyObject*>(self));}
PyObject* configure(PyController* self,PyObject* args){int active=0;float now=0;if(!PyArg_ParseTuple(args,"if",&active,&now))return NULL;self->state->controller.active=active!=0;self->state->controller.now=now;Py_RETURN_NONE;}
PyObject* callback(PyController* self,PyObject* args){int i=0;PyObject* c=NULL;if(!PyArg_ParseTuple(args,"iO",&i,&c))return NULL;if(i<0||i>=11){PyErr_SetString(PyExc_ValueError,"callback index");return NULL;}Py_INCREF(c);PyObject* previous=self->callbacks[i];self->callbacks[i]=c;Py_XDECREF(previous);Py_RETURN_NONE;}
PyObject* entity(PyController*,PyObject* args){int id=0,entering=0;if(!PyArg_ParseTuple(args,"ii",&id,&entering))return NULL;auto* c=ConnectionControl::pInstance();if(entering<0)c->entities.erase(id);else c->entities[id].entering=entering!=0;Py_RETURN_NONE;}
#define METHOD(n,f) {n,reinterpret_cast<PyCFunction>(f),METH_VARARGS|METH_KEYWORDS,NULL}
PyMethodDef methods[]={
@BINDINGS@
{"configure",reinterpret_cast<PyCFunction>(configure),METH_VARARGS,NULL},{"callback",reinterpret_cast<PyCFunction>(callback),METH_VARARGS,NULL},{"entity",reinterpret_cast<PyCFunction>(entity),METH_VARARGS,NULL},{NULL,NULL,0,NULL}};
#define FIELD(n,g,s,i) {const_cast<char*>(n),reinterpret_cast<getter>(g),reinterpret_cast<setter>(s),NULL,reinterpret_cast<void*>(i)}
PyGetSetDef fields[]={
FIELD("gunRotatorTargetPoint",getPresentationVector,setPresentationVector,0),FIELD("gunMarkerPosition",getPresentationVector,setPresentationVector,1),FIELD("gunMarkerDirection",getPresentationVector,setPresentationVector,2),
FIELD("gunMarkerDiameter",getPresentationReal,setPresentationReal,0),FIELD("turretYaw",getPresentationReal,setPresentationReal,1),FIELD("gunPitch",getPresentationReal,setPresentationReal,2),
FIELD("fps",getPerformance,setPerformance,0),FIELD("ping",getPerformance,setPerformance,1),FIELD("isServerAim",getPresentationBool,setPresentationBool,0),FIELD("isLaggingNow",getPresentationBool,setPresentationBool,1),
FIELD("arenaPeriod",getPeriod,setPeriod,0),FIELD("arenaLength",getDuration,setDuration,0),{NULL,NULL,NULL,NULL,NULL}};
PyTypeObject type={PyObject_HEAD_INIT(NULL) 0};
extern "C" void initreplay_live_probe(){type.tp_name=const_cast<char*>("ReplayLiveProbe");type.tp_basicsize=sizeof(PyController);type.tp_flags=Py_TPFLAGS_DEFAULT;type.tp_new=make;type.tp_dealloc=reinterpret_cast<destructor>(destroy);type.tp_methods=methods;type.tp_getset=fields;if(PyType_Ready(&type)<0)return;PyObject* module=Py_InitModule("replay_live_probe",NULL);if(module){Py_INCREF(&type);PyModule_AddObject(module,"Controller",reinterpret_cast<PyObject*>(&type));}}
