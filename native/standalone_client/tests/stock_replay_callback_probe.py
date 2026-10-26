"""Extract production callback operations for real CPython2/public Pickler tests.

The fixture allocates the callback owner without starting a game connection;
packet timing and teardown are independently exercised by the controller test.
"""
from pathlib import Path
ROOT=Path(__file__).resolve().parents[1]


def generate():
    source=(ROOT/'bigworld/stock_replay_controller.cpp').read_text()
    names=source[source.index('bool text('):source.index('void notify(')]
    lifecycle=source[source.index('void deliverDataCallback('):source.index('PyObject* construct(')]
    methods=source[source.index('PyObject* getDataCallback('):source.index('PyObject* autoStart(')]
    return r'''#include "cstdmf/cstdmf_windows.hpp"
#include "cstdmf/allocator.hpp"
#include "cstdmf/log_msg.hpp"
#include "pyscript/script.hpp"
#include "script/pickler.hpp"
#include <map>
#include <set>
#include <string>
#include <stdexcept>
#include <iostream>
BW_BEGIN_NAMESPACE
namespace ReplayProbe {
struct StopObserver {void stop(){}};
struct State {StopObserver controller;std::map<std::string,PyObject*> dataCallbacks;bool clearingCallbacks=false;};
constexpr std::size_t callbackCount=11;
struct PyController {PyObject_HEAD State* state;PyObject* callbacks[callbackCount];};
std::set<PyController*> owners;
// EXTRACTED_NAMES
// EXTRACTED_LIFECYCLE
// EXTRACTED_METHODS
PyObject* make(PyTypeObject* cls,PyObject* args,PyObject* kwargs){
 if(PyTuple_Size(args)||(kwargs&&PyDict_Size(kwargs))){PyErr_SetString(PyExc_TypeError,"Probe owner has no constructor arguments");return NULL;}
 auto* self=reinterpret_cast<PyController*>(cls->tp_alloc(cls,0));if(!self)return NULL;
 self->state=NULL;for(auto*& cb:self->callbacks)cb=NULL;
 try{self->state=new State;owners.insert(self);}catch(...){Py_DECREF(self);return PyErr_NoMemory();}
 return reinterpret_cast<PyObject*>(self);
}
PyObject* deliver(PyController* self,PyObject* args){
 PyObject* name=NULL;PyObject* data=NULL;if(!PyArg_ParseTuple(args,"OO",&name,&data))return NULL;
 std::string key;if(!text(name,key))return NULL;
 BW::string blob=Pickler::pickle(ScriptObject(data,ScriptObject::FROM_BORROWED_REFERENCE));
 deliverDataCallback(self,key,std::string(blob.data(),blob.size()));Py_RETURN_NONE;
}
PyObject* deliverBytes(PyController* self,PyObject* args){
 PyObject* name=NULL;PyObject* data=NULL;if(!PyArg_ParseTuple(args,"OO",&name,&data))return NULL;
 std::string key,blob;if(!text(name,key)||!text(data,blob))return NULL;
 deliverDataCallback(self,key,blob);Py_RETURN_NONE;
}
PyObject* clearOwner(PyController* self,PyObject*){clear(self);Py_RETURN_NONE;}
#define METHOD(n,f,flags) {n,reinterpret_cast<PyCFunction>(f),flags,NULL}
PyMethodDef functions[]={METHOD("setDataCallback",setDataCallback,METH_VARARGS|METH_KEYWORDS),METHOD("getCallbackHandler",getDataCallback,METH_VARARGS|METH_KEYWORDS),METHOD("deliver",deliver,METH_VARARGS),METHOD("deliverBytes",deliverBytes,METH_VARARGS),METHOD("clear",clearOwner,METH_NOARGS),{NULL,NULL,0,NULL}};
PyTypeObject type={PyObject_HEAD_INIT(NULL) 0};
bool install(){
 type.tp_name=const_cast<char*>("ReplayCallbackProbe");type.tp_basicsize=sizeof(PyController);type.tp_flags=Py_TPFLAGS_DEFAULT|Py_TPFLAGS_HAVE_GC;
 type.tp_new=make;type.tp_dealloc=reinterpret_cast<destructor>(dealloc);type.tp_traverse=reinterpret_cast<traverseproc>(traverse);type.tp_clear=reinterpret_cast<inquiry>(clear);type.tp_methods=functions;
 if(PyType_Ready(&type)<0)return false;PyObject* module=PyImport_AddModule("replay_probe");if(!module)return false;Py_INCREF(&type);return PyModule_AddObject(module,"Controller",reinterpret_cast<PyObject*>(&type))==0;
}
}
BW_END_NAMESPACE
int main(){
 BW_SYSTEMSTAGE_MAIN();BW::LogMsg::automatedTest(true);SetErrorMode(SEM_FAILCRITICALERRORS|SEM_NOGPFAULTERRORBOX);
 Py_NoSiteFlag=1;
 Py_SetPythonHome(const_cast<char*>("C:\\wot-native-dev\\engine\\source\\programming\\bigworld\\third_party\\python"));
 Py_Initialize();BW::Script::runInitTimeJobs();int result=1;
 try{
  if(!BW::Pickler::init()||!BW::ReplayProbe::install())throw std::runtime_error("Cannot initialise actual public Pickler/Python callback probe");
  if(PyRun_SimpleString(R"PY(
import gc,weakref
from replay_probe import Controller
owner=Controller()
assert owner.getCallbackHandler('missing') is None
class Event(object):
 def __init__(self):self.calls=[]
 def __call__(self,*args):self.calls.append(args)
event=Event();owner.setDataCallback('bootcamp_setContext',event)
assert owner.getCallbackHandler('bootcamp_setContext') is event
owner.deliver('bootcamp_setContext',(7,'context',{'ready':True}))
assert event.calls==[(7,'context',{'ready':True})]
original=Event();changed=Event();owner.setDataCallback('decode-reentry',original)
def replace_during_unpickle():
 owner.setDataCallback('decode-reentry',changed)
 return 'restored'
class DecodeReentry(object):
 def __reduce__(self):return (replace_during_unpickle,())
owner.deliver('decode-reentry',(DecodeReentry(),))
assert original.calls==[('restored',)] and changed.calls==[]
assert owner.getCallbackHandler('decode-reentry') is changed
owner.deliverBytes('unregistered','not a pickle')
owner.setDataCallback('bootcamp_setContext',None)
assert owner.getCallbackHandler('bootcamp_setContext') is None
owner.deliverBytes('bootcamp_setContext','not a pickle')
owner.setDataCallback(u'\u5766\u514b',event)
assert owner.getCallbackHandler('\xe5\x9d\xa6\xe5\x85\x8b') is event
owner.setDataCallback('key\x00suffix',event)
assert owner.getCallbackHandler('key\x00suffix') is event
owner.setDataCallback('arbitrary',42)
assert owner.getCallbackHandler('arbitrary')==42
for method,args in ((owner.setDataCallback,()),(owner.setDataCallback,('only',)),(owner.setDataCallback,(object(),event)),(owner.getCallbackHandler,()),(owner.getCallbackHandler,(object(),))):
 try:method(*args)
 except TypeError:pass
 else:raise AssertionError('Invalid callback ABI arguments accepted')
del method,args
replacement=Event();nested=Event();reentry=[]
class Retired(object):
 def __del__(self):
  reentry.append(owner.getCallbackHandler('replace') is replacement)
  owner.setDataCallback('replace',nested)
old=Retired();owner.setDataCallback('replace',old);del old
owner.setDataCallback('replace',replacement)
assert reentry==[True] and owner.getCallbackHandler('replace') is nested
clearing=[]
class DuringClear(object):
 def __del__(self):
  try:owner.setDataCallback('resurrect',event)
  except RuntimeError:clearing.append(True)
old=DuringClear();owner.setDataCallback('clear-me',old);del old
owner.clear();owner.clear()
assert clearing==[True] and owner.getCallbackHandler('resurrect') is None
cyclic=Controller();handler=Event();handler.owner=cyclic
reference=weakref.ref(handler);cyclic.setDataCallback('cycle',handler)
del handler,cyclic;gc.collect();assert reference() is None
owner.clear();del owner;gc.collect()
)PY")!=0)throw std::runtime_error("Production callback map/Pickler/GC contract failed");
  if(!BW::ReplayProbe::owners.empty())throw std::runtime_error("Callback probe retained a native owner");
  std::cout<<"Actual CPython2/public Pickler callback operations passed: name/None/identity/argument tuple, replacement reentry, missing-handler guard and cyclic GC"<<std::endl;result=0;
 }catch(const std::exception& e){if(PyErr_Occurred())PyErr_Print();std::cerr<<e.what()<<std::endl;}
 BW::Pickler::finalise();BW::Script::runFiniTimeJobs();Py_Finalize();BW::LogMsg::fini();return result;
}
'''.replace('// EXTRACTED_NAMES',names).replace('// EXTRACTED_LIFECYCLE',lifecycle).replace('// EXTRACTED_METHODS',methods)


if __name__=='__main__':
    import argparse
    parser=argparse.ArgumentParser(description=__doc__);parser.add_argument('output',type=Path);args=parser.parse_args();args.output.write_text(generate())
