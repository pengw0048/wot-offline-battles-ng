#define WOT_NATIVE_PRESENTATION_HOST_BUILD
#include "stock_native_bot_presentation_impl.cpp"
namespace stock_native_bot {namespace {
PyObject* presentationVector(const Point& v){return p::tupleNumbers(Point{{float(v[0]),float(v[1]),float(v[2])}}).release();}
Point presentationXYZ(PyObject* value){return p::numbers<3>(value);}
double presentationLength(const Point& value){const float x=float(value[0]),y=float(value[1]),z=float(value[2]);volatile float xx=x*x,yy=y*y,zz=z*z;volatile float sum=xx+yy;return std::sqrt(float(sum+zz));}
void presentationMatrix(PyObject* object,const Point& xyz,const Point& rotation,bool rotate){p::set(object,"translation",p::tupleNumbers(xyz));if(rotate)p::set(object,"rotation",p::tupleNumbers(rotation));}
}}
namespace {
PyObject* run(PyObject*,PyObject* args){PyObject*battle=nullptr,*states=nullptr;double now=0.;int static_aim=0;if(!PyArg_ParseTuple(args,"OOdi",&battle,&states,&now,&static_aim))return nullptr;try{stock_native_bot::NativeServices services;if(static_aim){services.entity.geometry[0].static_turret_yaw=.25;services.entity.geometry[0].static_gun_pitch=-.1;}std::vector<std::string> errors;bool result=stock_native_bot::presentNativeBotPoses(battle,states,now,services,errors);auto out=stock_native_bot_python::list();for(auto&error:errors)stock_native_bot_python::append(out.get(),stock_native_bot_python::text(error));return Py_BuildValue("NO",out.release(),result?Py_True:Py_False);}catch(const std::exception&e){if(!PyErr_Occurred())PyErr_SetString(PyExc_RuntimeError,e.what());return nullptr;}}
PyMethodDef methods[]={{"run",run,METH_VARARGS,""},{nullptr,nullptr,0,nullptr}};
}
PyMODINIT_FUNC initpresentation_test(){Py_InitModule("presentation_test",methods);}
