// Real SDK proxy dispatch, conversion, descriptor macros and 2D display matrix owner.
// GFx storage and Math's Python shell are explicit boundaries; no renderer is mocked as proof.
#include <Python.h>
#include <cassert>
#include <cmath>
#include <cstring>
#include <map>
#include <memory>
#include <string>
#include <vector>
#include <climits>
#define BW_GUARD
#define MF_ASSERT assert
using uint = unsigned int;
namespace BW { template<class T> using vector=std::vector<T>; using string=std::string; }
using PyTypePlus = PyTypeObject;
class ScriptObject {
public:
    enum Reference { FROM_NEW_REFERENCE, FROM_BORROWED_REFERENCE };
    ScriptObject(PyObject* p=NULL, Reference r=FROM_BORROWED_REFERENCE):p_(p) {if(r==FROM_BORROWED_REFERENCE)Py_XINCREF(p_);}
    ScriptObject(const ScriptObject& o):p_(o.p_){Py_XINCREF(p_);}
    ~ScriptObject(){Py_XDECREF(p_);}
    ScriptObject& operator=(const ScriptObject&o){Py_XINCREF(o.p_);Py_XDECREF(p_);p_=o.p_;return *this;}
    PyObject* get()const{return p_;} PyObject* newRef()const{Py_XINCREF(p_);return p_;}
    operator PyObject*()const{return p_;}
    static ScriptObject none(){return ScriptObject(Py_None);}
private: PyObject* p_;
};
class ScriptString:public ScriptObject {public: using ScriptObject::ScriptObject; const char*c_str()const{return PyString_AsString(get());}};
class PyObjectPlus:public PyObject {
public:
    explicit PyObjectPlus(PyTypeObject*t){PyObject_Init(this,t);} virtual ~PyObjectPlus(){}
    static void* operator new(size_t n){return PyObject_Malloc(n);} static void operator delete(void*p){PyObject_Free(p);}
    static PyTypeObject s_type_;
    const char*typeName()const{return Py_TYPE(this)->tp_name;}
    ScriptObject pyGetAttribute(const ScriptString&attrObj);
    bool pySetAttribute(const ScriptString&attrObj,const ScriptObject&value);
};
PyTypeObject PyObjectPlus::s_type_={PyVarObject_HEAD_INIT(&PyType_Type,0)};
@OBJECT_DISPATCH@
template<class T> class SmartPointer {
public:
    SmartPointer(T*p=NULL):p_(p){Py_XINCREF(p_);} SmartPointer(const SmartPointer&o):SmartPointer(o.p_){}
    ~SmartPointer(){Py_XDECREF(p_);} SmartPointer&operator=(T*p){Py_XINCREF(p);Py_XDECREF(p_);p_=p;return *this;}
    T*get()const{return p_;} T*operator->()const{return p_;} operator T*()const{return p_;}
private:T*p_;
};
struct Matrix {union{float m[4][4];struct{float _11,_12,_13,_14,_21,_22,_23,_24,_31,_32,_33,_34,_41,_42,_43,_44;};};
    void setIdentity(){std::memset(m,0,sizeof(m));for(int i=0;i<4;i++)m[i][i]=1;}
};
struct MatrixProvider:PyObjectPlus {
    static PyTypeObject s_type_; Matrix value;
    explicit MatrixProvider(PyTypeObject*t=&s_type_):PyObjectPlus(t){value.setIdentity();}
    static bool Check(PyObject*p){return p&&PyObject_TypeCheck(p,&s_type_);}
    void matrix(Matrix&out){out=value;} void set(const Matrix&m){value=m;}
};
PyTypeObject MatrixProvider::s_type_={PyVarObject_HEAD_INIT(&PyType_Type,0)};
using PyMatrix=MatrixProvider;
static std::string bw_wtoutf8(const wchar_t*s){std::string r;for(;*s;s++)r+=char(*s);return r;}
namespace Script {
int setData(PyObject*,Matrix&,const char*);PyObject*getData(const Matrix&);
int setData(PyObject*p,std::string&v,const char*){if(!PyString_Check(p)){PyErr_SetString(PyExc_TypeError,"string");return -1;}v=PyString_AsString(p);return 0;}
PyObject*getData(const std::string&s){return PyString_FromString(s.c_str());}
template<class T>PyObject*getReadOnlyData(const T&v){return getData(v);}
}
@MATRIX_CONVERSION@
#define Py_Header(CLASS,SUPER) public:using This=CLASS;using Super=SUPER;static PyTypeObject s_type_; \
    static This*getSelf(PyObject*p){return static_cast<This*>(static_cast<PyObjectPlus*>(p));} \
    static PyGetSetDef*s_getAttributeDefs(); \
    PyObject*pyGet___members__(){return PyList_New(0);}int pySet___members__(PyObject*){return -1;} \
    PyObject*pyGet___methods__(){return PyList_New(0);}int pySet___methods__(PyObject*){return -1;}
@ATTRIBUTE_MACROS@
@ATTRIBUTE_DEFINITIONS@
namespace SF {namespace Render {template<class T>struct Matrix2x4 {T M[2][4];Matrix2x4()=default;
    Matrix2x4(T a,T b,T c,T d,T e,T f,T g,T h):M{{a,b,c,d},{e,f,g,h}}{} };}}
namespace GFx {
struct State;
class Value {
public:
    enum ValueType {VT_Undefined,VT_Null,VT_Boolean,VT_Number,VT_Int,VT_UInt,VT_String,VT_StringW,VT_Array,VT_Object,VT_DisplayObject};
    ValueType type=VT_Undefined; double number=0;std::string text;std::shared_ptr<State>state;
    ValueType GetType()const{return type;}bool GetBool()const{return number!=0;}double GetNumber()const{return number;}
    int GetInt()const{return int(number);}uint GetUInt()const{return uint(number);}const char*GetString()const{return text.c_str();}
    const wchar_t*GetStringW()const{return L"";}void SetUndefined(){type=VT_Undefined;}void SetNull(){type=VT_Null;}
    void SetBoolean(bool v){type=VT_Boolean;number=v;}void SetInt(long v){type=VT_Int;number=v;}
    void SetNumber(double v){type=VT_Number;number=v;}void SetString(const char*v){type=VT_String;text=v;}
    bool GetMember(const char*,Value*)const;bool SetMember(const char*,const Value&);
    uint GetArraySize()const;bool GetElement(uint,Value*)const;void SetElement(uint,const Value&);
    bool GetDisplayMatrix(SF::Render::Matrix2x4<float>*)const;bool SetDisplayMatrix(const SF::Render::Matrix2x4<float>&);
    bool GetText(Value*)const;void SetText(const char*);
    struct ObjectVisitor{virtual ~ObjectVisitor(){}virtual void Visit(const char*,const Value&)=0;};
    void VisitMembers(ObjectVisitor*);
};
struct State {std::map<std::string,Value>members;std::vector<Value>array;SF::Render::Matrix2x4<float>matrix{1,0,0,3,0,1,0,5};
    bool matrixReadable=true,acceptMembers=true;int matrixWrites=0,memberWrites=0;double x=3,y=5,alpha=100;std::string text="initial";};
void Value::VisitMembers(ObjectVisitor*v){for(const auto&item:state->members)v->Visit(item.first.c_str(),item.second);}
bool Value::GetMember(const char*n,Value*out)const{if(!state)return false;auto it=state->members.find(n);if(it==state->members.end())return false;*out=it->second;return true;}
bool Value::SetMember(const char*n,const Value&v){if(!state||!state->acceptMembers)return false;state->members[n]=v;state->memberWrites++;return true;}
uint Value::GetArraySize()const{return state?state->array.size():0;}bool Value::GetElement(uint i,Value*out)const{if(i>=GetArraySize())return false;*out=state->array[i];return true;}
void Value::SetElement(uint i,const Value&v){if(i>=GetArraySize())state->array.resize(i+1);state->array[i]=v;}
bool Value::GetDisplayMatrix(SF::Render::Matrix2x4<float>*out)const{if(!state||!state->matrixReadable)return false;*out=state->matrix;return true;}
bool Value::SetDisplayMatrix(const SF::Render::Matrix2x4<float>&m){if(!state)return false;state->matrix=m;state->matrixWrites++;return true;}
bool Value::GetText(Value*out)const{out->SetString(state->text.c_str());return true;}void Value::SetText(const char*s){state->text=s;}
struct Movie {void CreateArray(Value*v){v->type=Value::VT_Array;v->state=std::make_shared<State>();}void CreateObject(Value*v){v->type=Value::VT_Object;v->state=std::make_shared<State>();}};
}
struct PyMovieView {GFx::Movie movie;GFx::Movie*pMovieView(){return &movie;}};
class PyGFxValue:public PyObjectPlus {
    Py_Header(PyGFxValue,PyObjectPlus)
public:
    using Children_t=std::map<std::string,SmartPointer<PyGFxValue>>;
    GFx::Value gfxValue_;PyMovieView*pyMovieView_;ScriptObject pyScript_;Children_t children_;bool needResyncSelf_=false,hasCollectedChildren_=false;
    std::string name_="fixture";int resyncs=0;
    explicit PyGFxValue(PyTypeObject*t=&s_type_):PyObjectPlus(t){static PyMovieView movie;pyMovieView_=&movie;}
    ScriptObject pyGetAttribute(const ScriptString&);bool pySetAttribute(const ScriptString&,const ScriptObject&);
    static GFx::Value convertValueToGFx(PyObject*,PyMovieView*);
    static PyObject*convertValueToPython(GFx::Value&,PyMovieView*,PyGFxValue*);
    static PyObject*convertValueToPython(GFx::Value&v,PyMovieView*m){return convertValueToPython(v,m,NULL);}
    PyObject*convertValueToPython(){return convertValueToPython(gfxValue_,pyMovieView_,this);}
    static PyGFxValue*create(const char*n,PyGFxValue*p){GFx::Value v;if(!p->gfxValue_.GetMember(n,&v))return NULL;auto*r=new PyGFxValue();r->gfxValue_=v;return r;}
    static PyGFxValue*create(GFx::Value&v,PyMovieView*){auto*r=new PyGFxValue();r->gfxValue_=v;return r;}
    bool bindToScript(PyObject*p){pyScript_=ScriptObject(p);return true;}void resyncSelf(){needResyncSelf_=false;}
    void resync();void collectChildValues();
};
class PyGFxDisplayObject:public PyGFxValue {
    Py_Header(PyGFxDisplayObject,PyGFxValue)
public:
    PyGFxDisplayObject():PyGFxValue(&s_type_){gfxValue_.type=GFx::Value::VT_DisplayObject;gfxValue_.state=std::make_shared<GFx::State>();}
    Matrix displayMatrix();void displayMatrix(const Matrix&);
    @DISPLAY_MATRIX_DECLARATION@
    BW::string text();void text(const BW::string&);
    PY_RW_ACCESSOR_ATTRIBUTE_DECLARE(BW::string,text,text)
};
PyTypeObject PyGFxValue::s_type_={PyVarObject_HEAD_INIT(&PyType_Type,0)};
PyTypeObject PyGFxDisplayObject::s_type_={PyVarObject_HEAD_INIT(&PyType_Type,0)};
PY_BEGIN_ATTRIBUTES(PyGFxDisplayObject)
    PY_ATTRIBUTE(displayMatrix)
    PY_ATTRIBUTE(text)
PY_END_ATTRIBUTES()
@VALUE_CONVERSIONS@
@VALUE_DISPATCH@
@DISPLAY_MATRIX_OWNER@
static PyObject*getInfo(PyObject*p,PyObject*){auto&v=PyGFxDisplayObject::getSelf(p)->gfxValue_;PyObject*r=PyModule_New("DisplayInfoBoundary");
    PyModule_AddObject(r,"x",PyFloat_FromDouble(v.state->x));PyModule_AddObject(r,"y",PyFloat_FromDouble(v.state->y));PyModule_AddObject(r,"alpha",PyFloat_FromDouble(v.state->alpha));return r;}
static PyObject*setInfo(PyObject*p,PyObject*i){auto&s=*PyGFxDisplayObject::getSelf(p)->gfxValue_.state;
    for(auto item:{std::make_pair("x",&s.x),std::make_pair("y",&s.y),std::make_pair("alpha",&s.alpha)}){ScriptObject v(PyObject_GetAttrString(i,item.first),ScriptObject::FROM_NEW_REFERENCE);if(!v.get())return NULL;*item.second=PyFloat_AsDouble(v.get());}
    Py_RETURN_NONE;}
static PyObject*stats(PyObject*,PyObject*p){auto&s=*PyGFxDisplayObject::getSelf(p)->gfxValue_.state;return Py_BuildValue("(iiff)",s.memberWrites,s.matrixWrites,s.matrix.M[0][3],s.matrix.M[1][3]);}
static PyObject*control(PyObject*,PyObject*args){PyObject*p;int readable,accept;if(!PyArg_ParseTuple(args,"Oii",&p,&readable,&accept))return NULL;auto&s=*PyGFxDisplayObject::getSelf(p)->gfxValue_.state;s.matrixReadable=readable;s.acceptMembers=accept;Py_RETURN_NONE;}
static PyObject*displayNew(PyTypeObject*,PyObject*,PyObject*){return new PyGFxDisplayObject();}
static PyObject*matrixNew(PyTypeObject*,PyObject*,PyObject*){return new PyMatrix();}
static PyObject*translation(PyObject*p,void*){auto&m=static_cast<PyMatrix*>(static_cast<PyObjectPlus*>(p))->value;PyObject*r=PyModule_New("VectorBoundary");for(int i=0;i<3;i++)PyModule_AddObject(r,i==0?"x":i==1?"y":"z",PyFloat_FromDouble(m.m[3][i]));return r;}
static PyObject*roll(PyObject*p,void*){auto&m=static_cast<PyMatrix*>(static_cast<PyObjectPlus*>(p))->value;return PyFloat_FromDouble(std::atan2(m._12,m._22));}
static PyObject*translate(PyObject*p,PyObject*args){double x,y,z;if(!PyArg_ParseTuple(args,"(ddd)",&x,&y,&z))return NULL;auto&m=static_cast<PyMatrix*>(static_cast<PyObjectPlus*>(p))->value;m.setIdentity();m._41=x;m._42=y;m._43=z;Py_RETURN_NONE;}
static PyObject*rotate(PyObject*p,PyObject*args){double r;if(!PyArg_ParseTuple(args,"d",&r))return NULL;auto&m=static_cast<PyMatrix*>(static_cast<PyObjectPlus*>(p))->value;m.setIdentity();m._11=m._22=std::cos(r);m._12=std::sin(r);m._21=-m._12;Py_RETURN_NONE;}
static PyObject*multiply(PyObject*p,PyObject*arg,bool pre){if(!MatrixProvider::Check(arg)){PyErr_SetString(PyExc_TypeError,"matrix");return NULL;}auto&v=static_cast<PyMatrix*>(static_cast<PyObjectPlus*>(p))->value;auto&o=static_cast<PyMatrix*>(static_cast<PyObjectPlus*>(arg))->value;Matrix r;const auto&a=pre?o:v;const auto&b=pre?v:o;for(int i=0;i<4;i++)for(int j=0;j<4;j++){r.m[i][j]=0;for(int k=0;k<4;k++)r.m[i][j]+=a.m[i][k]*b.m[k][j];}v=r;Py_RETURN_NONE;}
static PyObject*pre(PyObject*p,PyObject*a){return multiply(p,a,true);}static PyObject*post(PyObject*p,PyObject*a){return multiply(p,a,false);}
static void setup(PyTypeObject&t,const char*n,size_t size,PyTypeObject*base){t.tp_name=n;t.tp_basicsize=size;t.tp_flags=Py_TPFLAGS_DEFAULT;t.tp_base=base;t.tp_dealloc=[](PyObject*p){delete static_cast<PyObjectPlus*>(p);};}
int main(int argc,char**argv){assert(argc==4);Py_Initialize();setup(PyObjectPlus::s_type_,"PyObjectPlus",sizeof(PyObjectPlus),NULL);assert(PyType_Ready(&PyObjectPlus::s_type_)==0);
    setup(MatrixProvider::s_type_,"Math.Matrix",sizeof(PyMatrix),&PyObjectPlus::s_type_);MatrixProvider::s_type_.tp_new=matrixNew;
    static PyMethodDef mm[]={{"setTranslate",translate,METH_VARARGS,NULL},{"setRotateZ",rotate,METH_VARARGS,NULL},{"preMultiply",pre,METH_O,NULL},{"postMultiply",post,METH_O,NULL},{NULL,NULL,0,NULL}};
    static PyGetSetDef mg[]={{const_cast<char*>("translation"),translation,NULL,NULL,NULL},{const_cast<char*>("roll"),roll,NULL,NULL,NULL},{NULL,NULL,NULL,NULL,NULL}};MatrixProvider::s_type_.tp_methods=mm;MatrixProvider::s_type_.tp_getset=mg;assert(PyType_Ready(&MatrixProvider::s_type_)==0);
    setup(PyGFxValue::s_type_,"ScaleformBW::PyGFxValue",sizeof(PyGFxValue),&PyObjectPlus::s_type_);
    PyGFxValue::s_type_.tp_getattro=[](PyObject*p,PyObject*n){return static_cast<PyGFxValue*>(static_cast<PyObjectPlus*>(p))->pyGetAttribute(ScriptString(n,ScriptObject::FROM_BORROWED_REFERENCE)).newRef();};
    PyGFxValue::s_type_.tp_setattro=[](PyObject*p,PyObject*n,PyObject*v){return static_cast<PyGFxValue*>(static_cast<PyObjectPlus*>(p))->pySetAttribute(ScriptString(n,ScriptObject::FROM_BORROWED_REFERENCE),ScriptObject(v))?0:-1;};assert(PyType_Ready(&PyGFxValue::s_type_)==0);
    setup(PyGFxDisplayObject::s_type_,"ScaleformBW::PyGFxDisplayObject",sizeof(PyGFxDisplayObject),&PyGFxValue::s_type_);PyGFxDisplayObject::s_type_.tp_new=displayNew;PyGFxDisplayObject::s_type_.tp_getset=PyGFxDisplayObject::s_getAttributeDefs();
    static PyMethodDef dm[]={{"getDisplayInfo",getInfo,METH_NOARGS,NULL},{"setDisplayInfo",setInfo,METH_O,NULL},{NULL,NULL,0,NULL}};PyGFxDisplayObject::s_type_.tp_methods=dm;assert(PyType_Ready(&PyGFxDisplayObject::s_type_)==0);
    PyObject*math=Py_InitModule("Math",NULL);Py_INCREF(&MatrixProvider::s_type_);PyModule_AddObject(math,"Matrix",reinterpret_cast<PyObject*>(&MatrixProvider::s_type_));
    static PyMethodDef hm[]={{"stats",stats,METH_O,NULL},{"control",control,METH_VARARGS,NULL},{NULL,NULL,0,NULL}};PyObject*host=Py_InitModule("host",hm);Py_INCREF(&PyGFxDisplayObject::s_type_);PyModule_AddObject(host,"DisplayObject",reinterpret_cast<PyObject*>(&PyGFxDisplayObject::s_type_));
    PySys_SetArgv(argc-1,argv+1);FILE*f=std::fopen(argv[1],"r");assert(f);int rc=PyRun_SimpleFile(f,argv[1]);std::fclose(f);if(PyErr_Occurred())PyErr_Print();Py_Finalize();return rc?1:0;
}
