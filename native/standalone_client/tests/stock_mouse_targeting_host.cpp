// Actual SDK PyObjectPlus/PY_AUTO macros, bool converter and targeting bodies.
// OS cursor/focus and camera math are explicit primitive boundaries.
@PYOBJECT_SUPPORT@
#include <cmath>
#include <cstdlib>
#include <cstdarg>
#define BW_STATIC_ASSERT(expression,message) static_assert(expression,#message)
struct PyFactoryMethodLink;
#ifndef _WIN32
#include <strings.h>
#define _stricmp strcasecmp
#endif
namespace Script {
int setData(PyObject*,bool&,const char*);
PyObject* argCountError(const char*,int,int,...);
}
@SCRIPT_MACROS@
@PYOBJECT_PLUS_HEADER@
@PYOBJECT_PLUS_SOURCE@
@SCRIPT_BODIES@
struct Vector2 {float x=0,y=0;Vector2()=default;Vector2(float a,float b):x(a),y(b){}};
struct Vector3 {
    float x=0,y=0,z=0;
    Vector3()=default;Vector3(float a,float b,float c):x(a),y(b),z(c){}
    void normalise(){const float n=std::sqrt(x*x+y*y+z*z);x/=n;y/=n;z/=n;}
};
struct Matrix {
    Vector3 origin,ray,up;
    Vector3 applyVector(Vector3 value)const{return value;}
    Vector3 applyPoint(Vector3 value)const{return {value.x+origin.x,value.y+origin.y,value.z+origin.z};}
    // Targeting state is tested before the existing lookAt/invert primitive.
    void lookAt(Vector3 p,Vector3 d,Vector3 u){origin=p;ray=d;up=u;}
    void invert(){}
};
struct Camera {Vector3 nearPlanePoint(float x,float y)const{return {x,y,1};}};
namespace Moo {
struct Context {Matrix inverse;Camera lens;const Matrix& invView()const{return inverse;}const Camera& camera()const{return lens;}};
Context& rc(){static Context context;return context;}
}
struct MouseCursor {
    static bool s_inFocus_;
    Vector2 value;unsigned samples=0;
    Vector2 position(){++samples;return value;}
};
bool MouseCursor::s_inFocus_=false;
struct SimpleGUI {
    MouseCursor cursor;
    static SimpleGUI& instance(){static SimpleGUI gui;return gui;}
    MouseCursor& mouseCursor(){return cursor;}
};
class MatrixProvider:public PyObjectPlus {
    Py_Header(MatrixProvider,PyObjectPlus)
public:explicit MatrixProvider(bool,PyTypeObject* type):PyObjectPlus(type){}
};
PY_TYPEOBJECT(MatrixProvider)
PY_BEGIN_METHODS(MatrixProvider) PY_END_METHODS()
PY_BEGIN_ATTRIBUTES(MatrixProvider) PY_END_ATTRIBUTES()
@TARGET_CLASS@
@TARGET_CONSTRUCTOR@
@TARGET_MATRIX@
@TARGET_DETACH@
PY_TYPEOBJECT(MouseTargetingMatrix)
@TARGET_METHODS@
PY_BEGIN_ATTRIBUTES(MouseTargetingMatrix) PY_END_ATTRIBUTES()
static PyObject* create(PyObject*,PyObject*){return new MouseTargetingMatrix();}
static PyObject* cursor(PyObject*,PyObject* args){
    int focus;float x,y;if(!PyArg_ParseTuple(args,"iff",&focus,&x,&y))return NULL;
    MouseCursor::s_inFocus_=focus!=0;SimpleGUI::instance().cursor.value={x,y};Py_RETURN_NONE;
}
static PyObject* camera(PyObject*,PyObject* args){
    float x,y,z;if(!PyArg_ParseTuple(args,"fff",&x,&y,&z))return NULL;
    Moo::rc().inverse.origin={x,y,z};Py_RETURN_NONE;
}
static PyObject* sample(PyObject*,PyObject* args){
    PyObject* object;if(!PyArg_ParseTuple(args,"O",&object))return NULL;
    if(!MouseTargetingMatrix::Check(object)){PyErr_SetString(PyExc_TypeError,"Expected MouseTargetingMatrix");return NULL;}
    Matrix output;static_cast<MouseTargetingMatrix*>(object)->matrix(output);
    return Py_BuildValue("(fffI)",output.origin.x,output.origin.y,output.origin.z,SimpleGUI::instance().cursor.samples);
}
int main(int argc,char** argv){
    assert(argc==3);Py_Initialize();PySys_SetArgv(argc-1,argv+1);
    PyMethodDef methods[]={{"create",create,METH_NOARGS,NULL},{"cursor",cursor,METH_VARARGS,NULL},
        {"camera",camera,METH_VARARGS,NULL},{"sample",sample,METH_VARARGS,NULL},{NULL,NULL,0,NULL}};
    assert(Py_InitModule("_mouse_targeting_host",methods));FILE* input=std::fopen(argv[1],"r");assert(input);
    int result=PyRun_SimpleFileEx(input,argv[1],1);Py_Finalize();return result?1:0;
}
