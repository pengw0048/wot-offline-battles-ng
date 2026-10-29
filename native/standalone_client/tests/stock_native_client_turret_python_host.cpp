#include "stock_native_client_turret_presentation_python.hpp"
#include "stock_native_client_state.hpp"
#include "stock_native_bot_python.hpp"
namespace p=stock_native_bot_python;namespace cs=stock_native_client_state;
static PyObject*entities=nullptr;
static PyObject*descriptor=nullptr;
static std::shared_ptr<cs::State>shared;
namespace stock_native_bot {
PyObject*readNativeEntity(PyObject*,std::int64_t id){auto key=p::integer(id);auto*v=entities?PyDict_GetItem(entities,key.get()):nullptr;return p::retain(v?v:Py_None).release();}
PyObject*readNativeEntityField(PyObject*,PyObject*entity,const char*name){return p::value(entity,name).release();}
}
namespace stock_native_client_state {
std::shared_ptr<State>fromBattle(PyObject*){return ::shared;}
Actor*State::find(const std::string&key){const auto at=actors.find(key);return at==actors.end()?nullptr:at->second.get();}
const Actor*State::find(const std::string&key)const{const auto at=actors.find(key);return at==actors.end()?nullptr:at->second.get();}
}
namespace stock_native_client_query {PyObject*descriptor(const cs::Actor&){return ::descriptor;}}
struct MatrixObject {PyObject_HEAD PyObject*dictionary;};
static PyTypeObject matrix_type{};
PyObject*matrixNew(PyTypeObject*type,PyObject*,PyObject*){auto*out=reinterpret_cast<MatrixObject*>(type->tp_alloc(type,0));if(!out)return nullptr;out->dictionary=PyDict_New();if(!out->dictionary){Py_DECREF(reinterpret_cast<PyObject*>(out));return nullptr;}return reinterpret_cast<PyObject*>(out);}
void matrixFree(PyObject*self){Py_XDECREF(reinterpret_cast<MatrixObject*>(self)->dictionary);Py_TYPE(self)->tp_free(self);}
PyObject*rotate(PyObject*self,PyObject*angles){auto*d=reinterpret_cast<MatrixObject*>(self)->dictionary;auto*fail=PyDict_GetItemString(d,"fail");if(fail==Py_True){PyDict_SetItemString(d,"fail",Py_False);PyErr_SetString(PyExc_ValueError,"fixture matrix write failed");return nullptr;}if(PyDict_SetItemString(d,"angles",angles)<0)return nullptr;Py_RETURN_NONE;}
PyMethodDef matrix_methods[]={{"setRotateYPR",rotate,METH_O,nullptr},{nullptr,nullptr,0,nullptr}};
PyObject*matrix(PyObject*,PyObject*){return matrixNew(&matrix_type,nullptr,nullptr);}
PyObject*vector(PyObject*,PyObject*args){double x,y,z;if(!PyArg_ParseTuple(args,"ddd",&x,&y,&z))return nullptr;return Py_BuildValue("(ddd)",x,y,z);}
PyObject*setEntities(PyObject*,PyObject*raw){if(!PyDict_Check(raw)){PyErr_SetString(PyExc_ValueError,"entity table required");return nullptr;}Py_XINCREF(raw);Py_XDECREF(entities);entities=raw;Py_RETURN_NONE;}
PyObject*destroyEntity(PyObject*,PyObject*id){if(PyDict_GetItem(entities,id)&&PyDict_DelItem(entities,id)<0)return nullptr;Py_RETURN_NONE;}
PyObject*setupBattle(PyObject*,PyObject*battle){try{shared=std::make_shared<cs::State>();shared->local_player_id=4;auto rows=p::value(battle,"_actors");for(Py_ssize_t i=0;i<p::size(rows.get());++i){auto row=p::item(rows.get(),i);auto a=std::make_shared<cs::Actor>();auto key=p::value(row.get(),"key");a->key=p::string(key.get());a->engine_id=p::integral(row.get(),"id");a->id=a->engine_id;a->ready=a->has_current=true;a->local=p::truth(row.get(),"local");auto position=p::value(row.get(),"position");const auto point=p::numbers<3>(position.get());for(unsigned j=0;j<3;++j)a->current[j]=point[j];a->geometry=std::make_shared<int>(1);shared->actors[a->key]=a;}auto exact=p::value(battle,"_local_descriptor");descriptor=exact.get();Py_RETURN_NONE;}catch(const std::exception&e){PyErr_SetString(PyExc_ValueError,e.what());return nullptr;}}
PyObject*moveLocal(PyObject*,PyObject*args){double x,y,z;if(!PyArg_ParseTuple(args,"ddd",&x,&y,&z))return nullptr;auto*a=shared->find("player:4");a->current[0]=x;a->current[1]=y;a->current[2]=z;Py_RETURN_NONE;}
PyObject*retireState(PyObject*,PyObject*){++shared->epoch;Py_RETURN_NONE;}
PyMethodDef helpers[]={{"Matrix",matrix,METH_NOARGS,nullptr},{"Vector3",vector,METH_VARARGS,nullptr},{"entities",setEntities,METH_O,nullptr},{"destroyEntity",destroyEntity,METH_O,nullptr},{"setup_battle",setupBattle,METH_O,nullptr},{"move",moveLocal,METH_VARARGS,nullptr},{"retire_state",retireState,METH_NOARGS,nullptr},{nullptr,nullptr,0,nullptr}};
PyMODINIT_FUNC initturret_python_test(){PyObject_Init(reinterpret_cast<PyObject*>(&matrix_type),&PyType_Type);matrix_type.tp_name="turret_python_test.Matrix";matrix_type.tp_basicsize=sizeof(MatrixObject);matrix_type.tp_flags=Py_TPFLAGS_DEFAULT;matrix_type.tp_new=matrixNew;matrix_type.tp_dealloc=matrixFree;matrix_type.tp_methods=matrix_methods;matrix_type.tp_getattro=PyObject_GenericGetAttr;matrix_type.tp_setattro=PyObject_GenericSetAttr;matrix_type.tp_dictoffset=offsetof(MatrixObject,dictionary);if(PyType_Ready(&matrix_type)<0)return;auto*m=Py_InitModule("turret_python_test",stock_native_client_turret_presentation::methods());if(m)Py_InitModule("turret_python_test",helpers);}
