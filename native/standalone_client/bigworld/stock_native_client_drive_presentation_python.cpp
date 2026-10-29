#include "stock_native_client_drive_presentation_python.hpp"
#include "stock_native_bot_python.hpp"
namespace stock_native_client_drive {
namespace {
namespace p=stock_native_bot_python;
p::Ref field(PyObject*o,const char*n){if(!o||o==Py_None)return p::retain(Py_None);if(PyDict_Check(o))return p::value(o,n);auto key=p::text(n);auto*v=p::directAttribute(o,key.get());if(!v&&PyErr_ExceptionMatches(PyExc_AttributeError)){PyErr_Clear();return p::retain(Py_None);}return p::Ref(v);}
void assign(PyObject*o,const char*n,const p::Ref&value){if(PyDict_Check(o)){p::set(o,n,value);return;}auto key=p::text(n);if(PyObject_GenericSetAttr(o,key.get(),value.get())<0)throw p::Error("Native player visual field assignment failed");}
void builtin(PyObject*o,const char*n,PyObject*args){auto method=field(o,n);if(!PyCFunction_Check(method.get()))throw p::Error("Native player visual controller is not the reviewed builtin");p::Ref result(PyObject_CallObject(method.get(),args));}
}
void applyTrackMode(PyObject*entity,EngineMode mode){
    auto appearance=field(entity,"appearance");if(appearance.get()==Py_None)throw p::Error("Native player track appearance is unavailable");
    p::Ref value(Py_BuildValue("(ii)",mode.power,mode.flags));assign(entity,"engineMode",value);
    applyAppearanceTrackMode(appearance.get(),mode);
}
void applyAppearanceTrackMode(PyObject*appearance,EngineMode mode){
    p::Ref value(Py_BuildValue("(ii)",mode.power,mode.flags));assign(appearance,"_CompoundAppearance__engineMode",value);
    auto detailed=field(appearance,"detailedEngineState");if(detailed.get()!=Py_None)assign(detailed.get(),"mode",p::integer(mode.power));
    auto scroll=field(appearance,"_CompoundAppearance__trackScrollCtl");if(scroll.get()!=Py_None){p::Ref args(PyTuple_Pack(1,value.get()));builtin(scroll.get(),"setMode",args.get());}
}
void applyTrackScroll(PyObject*appearance,double left,double right){
    assign(appearance,"_CompoundAppearance__leftTrackScroll",p::number(left));assign(appearance,"_CompoundAppearance__rightTrackScroll",p::number(right));
    auto scroll=field(appearance,"_CompoundAppearance__trackScrollCtl");if(scroll.get()!=Py_None){p::Ref args(Py_BuildValue("(dd)",left,right));builtin(scroll.get(),"setExternal",args.get());}
}
}
