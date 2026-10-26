"""Run the production six AA exports with Python 2 and original UI consumers."""
import os
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]

HOST = r'''
#include <Python.h>
#include "wot/stock_antialiasing.hpp"
#include <array>
#include <vector>
#define BW_BEGIN_NAMESPACE
#define BW_END_NAMESPACE
#define WOT_STOCK_ANTIALIASING_PORTABLE
#define PY_MODULE_FUNCTION(a,b)
#define SUCCEEDED(x) ((x)>=0)
#define FAILED(x) ((x)<0)
using uint=unsigned;using uint32=unsigned;using D3DFORMAT=unsigned;
using D3DMULTISAMPLE_TYPE=unsigned;using D3DXHANDLE=const char*;
const bool FALSE=false;
struct ID3DXEffect{int SetInt(const char*,int){return 0;}};
template<class T>struct ComObjectWrap{T*p=nullptr;T**operator&(){return &p;}T*operator->(){return p;}};
struct D3DDEVICE_CREATION_PARAMETERS{unsigned AdapterOrdinal=0,DeviceType=1;};
static std::vector<std::array<unsigned,5>> queries;
struct IDirect3D9{int CheckDeviceMultiSampleType(unsigned adapter,unsigned type,unsigned format,bool windowed,unsigned samples,void*quality){
 if(quality)abort();queries.push_back({{adapter,type,format,unsigned(windowed),samples}});return samples==2||samples==4||samples==8?0:-1;}};
static IDirect3D9 d3d;
struct Device{int GetDirect3D(IDirect3D9**out){*out=&d3d;return 0;}
 int GetCreationParameters(D3DDEVICE_CREATION_PARAMETERS*out){out->AdapterOrdinal=3;out->DeviceType=1;return 0;}};
struct Temporal{unsigned mipMapLodBias()const{return 0;}};
struct Owner{unsigned mode=0;Temporal taa;unsigned changes=0;
 unsigned customAAMode()const{return mode;}void customAAMode(unsigned v){mode=v<4?v:0;++changes;}
 const Temporal&temporalAA()const{return taa;}};
static Owner owner;static bool deferred=true;
Owner*stockDeferredBuffers(){return deferred?&owner:nullptr;}
namespace Moo{
struct EffectConstantValue{virtual~EffectConstantValue(){}virtual bool operator()(ID3DXEffect*,D3DXHANDLE)=0;};
struct EffectVisualContext{enum{AUTO_CONSTANT_TYPE_PER_FRAME};EffectConstantValue*constant=nullptr;
 void registerAutoConstant(int,const char*,EffectConstantValue*v){constant=v;}
 void unregisterAutoConstant(int,const char*){delete constant;constant=nullptr;}};
struct Info{struct{unsigned VendorId=0;}identifier_;struct Mode{unsigned Format=22;};
 Mode windowedDisplayMode_;std::vector<Mode>displayModes_={{23}};};
struct Context{bool ready=true,window=true;unsigned index=0;Device dev;Info info;EffectVisualContext visual;
 wot::StockMultisamplingSelection selection;
 Device*device(){return ready?&dev:nullptr;}unsigned deviceIndex()const{return index;}unsigned nDevices()const{return 1;}
 const Info&deviceInfo(unsigned)const{return info;}bool windowed()const{return window;}unsigned modeIndex()const{return 0;}
 unsigned stockMultisamplingType()const{return selection.type;}void requestStockMultisampling(unsigned v){selection.request(v);}
 EffectVisualContext&effectVisualContext(){return visual;}};
Context&rc(){static Context value;return value;}
}
namespace Script{int setData(PyObject*,uint&,const char*);PyObject*getData(uint value){
 // Python's C long is 64-bit on this host; model the production Win32 result.
 return value&0x80000000u?PyLong_FromUnsignedLong(value):PyInt_FromLong(value);}}
@CONVERTER@
@PRODUCTION@
static PyObject*control(PyObject*,PyObject*args){int ready,def,nvidia,window;
 if(!PyArg_ParseTuple(args,"iiii",&ready,&def,&nvidia,&window))return NULL;
 Moo::rc().ready=ready!=0;deferred=def!=0;Moo::rc().info.identifier_.VendorId=nvidia?0x10de:0;Moo::rc().window=window!=0;
 queries.clear();Py_RETURN_NONE;}
static PyObject*state(PyObject*,PyObject*){PyObject*q=PyList_New(0);for(auto&v:queries){
 PyObject*t=Py_BuildValue("(IIIII)",v[0],v[1],v[2],v[3],v[4]);PyList_Append(q,t);Py_DECREF(t);}
 return Py_BuildValue("(IiiN)",Moo::rc().selection.type,int(Moo::rc().selection.pending),int(owner.changes),q);}
@WRAPPERS@
static PyMethodDef methods[]={@METHODS@{"control",control,METH_VARARGS,NULL},{"state",state,METH_NOARGS,NULL},{NULL,NULL,0,NULL}};
extern "C" void initaa_probe(){Py_InitModule("aa_probe",methods);}
'''

RUNNER = r'''
import marshal,sys,types,zipfile
sys.path.insert(0,sys.argv[1]);import aa_probe as p
def raises(exc,call):
 try:call()
 except exc:return
 raise AssertionError('Expected '+exc.__name__)
p.control(0,0,0,1)
assert p.getSupportedMultisamplingTypes() is None
assert p.getMultisamplingType()==0 and p.getSupportedCustomAAModes()==[0]
assert p.setMultisamplingType(8) is None and p.state()[0:2]==(0,0)
assert p.setCustomAAMode(3) is None and p.getCustomAAMode()==0
for setter in (p.setMultisamplingType,p.setCustomAAMode):
 for value in (-1, -1L, 1L<<40, '4', None):raises(TypeError,lambda:setter(value))
 for args in ((),(1,2)):raises(TypeError,lambda:setter(*args))
for getter in (p.getSupportedMultisamplingTypes,p.getMultisamplingType,p.getSupportedCustomAAModes,p.getCustomAAMode):
 raises(TypeError,lambda:getter(1))
p.control(1,1,0,1)
assert p.getSupportedMultisamplingTypes()==[2,4,8]
assert all(q[0:4]==(3,1,22,0)for q in p.state()[3])
mutable=p.getSupportedMultisamplingTypes();mutable.insert(0,0)
assert p.getSupportedMultisamplingTypes()==[2,4,8]
assert p.getSupportedCustomAAModes()==[0,1,2,3]
for value in (1,2L,3.,4,0xffffffffL,-1.25):
 p.setCustomAAMode(value);assert p.getCustomAAMode()==(int(value) if 0<=value<4 else 0)
p.setMultisamplingType(8.9);assert p.getMultisamplingType()==8 and p.state()[1]
p.setMultisamplingType(-1.25);assert p.getMultisamplingType()==0xffffffffL and type(p.getMultisamplingType()) is long
p.control(1,1,1,0)
assert p.getSupportedMultisamplingTypes()==[2,4,0x408,0x808,0x410,0x810]
assert all(q[2:4]==(23,0)for q in p.state()[3])
assert not any(q[4]in(8,12,16,32,0x408,0x808,0x410,0x810)for q in p.state()[3])
archive=zipfile.ZipFile(sys.argv[2]+'/res/packages/scripts.pkg')
root=marshal.loads(archive.read('scripts/client/account_helpers/settings_core/options.pyc')[8:])
def find(code,name):
 if code.co_name==name:return code
 for v in code.co_consts:
  if isinstance(v,types.CodeType):
   result=find(v,name)
   if result:return result
class SettingAbstract(object):
 def __init__(self,preview=False):self.preview=preview
errors=[]
scope={'__name__':'original_options','SettingAbstract':SettingAbstract,'BigWorld':p,'LOG_ERROR':lambda *a:errors.append(a)}
for name in ('MultisamplingSetting','CustomAASetting'):
 cls=type(name,(SettingAbstract,),eval(find(root,name),scope));scope[name]=cls
p.control(1,1,0,1);p.setMultisamplingType(4)
ms=scope['MultisamplingSetting']();assert ms._get()==2
assert ms._getOptions()==['#settings:multisamplingType/type%s'%i for i in (0,2,4,8)]
ms._set(3);assert p.getMultisamplingType()==8
before=p.state()[0];ms._set(-1);ms._set(4);assert p.state()[0]==before and len(errors)==2
p.setCustomAAMode(2);aa=scope['CustomAASetting']();assert aa._get()==2
assert aa._getOptions()==['#settings:customAAMode/mode%s'%i for i in range(4)]
aa._set(3);assert p.getCustomAAMode()==3
aa._set(4);aa._set(-1);assert p.getCustomAAMode()==3 and len(errors)==4
p.control(1,0,0,1);aa=scope['CustomAASetting']();assert aa._get()==0 and aa._getOptions()==['#settings:customAAMode/mode0']
print('Production six AA APIs and exact original Python 2 settings consumers passed')
'''


class AntialiasingAPITests(unittest.TestCase):
    def test_production_exports_and_exact_python2_consumers(self):
        python=Path(os.environ.get('WOT_PY27',str(Path.home()/'.pyenv/versions/2.7.18/bin/python2.7')))
        client=Path(os.environ.get('WOT_0922_CLIENT','/Users/peng/Downloads/World_of_Tanks_0.09.22.00.01_CH_1513_HD'))
        public=Path(os.environ.get('WOT_PUBLIC_BIGWORLD','/tmp/wot-bigworld-engine-14.4.1/programming/bigworld'))
        if sys.platform!='darwin' or not python.exists() or not(client/'WorldOfTanks.exe').exists():
            self.skipTest('Pinned Python 2.7, exact client and Darwin extension linker required')
        from test_stock_antialiasing_patch import function
        converter=function((public/'lib/pyscript/script.cpp').read_text(),'int Script::setData( PyObject * pObject, uint & rUint,')
        production=(ROOT/'bigworld/stock_antialiasing_runtime.cpp').read_text()
        names=('getSupportedMultisamplingTypes','getSupportedCustomAAModes','getMultisamplingType','getCustomAAMode','setMultisamplingType','setCustomAAMode')
        wrappers='\n'.join('static PyObject*wrap_'+n+'(PyObject*,PyObject*a){return py_'+n+'(a);}'for n in names)
        methods=''.join('{"'+n+'",wrap_'+n+',METH_VARARGS,NULL},'for n in names)
        source=HOST.replace('@CONVERTER@',converter).replace('@PRODUCTION@',production).replace('@WRAPPERS@',wrappers).replace('@METHODS@',methods)
        with tempfile.TemporaryDirectory(prefix='native-aa-api-')as folder:
            root=Path(folder);(root/'probe.cpp').write_text(source)
            result=subprocess.run(['c++','-std=c++14','-Wno-deprecated-register','-Wno-missing-field-initializers','-fPIC','-shared','-undefined','dynamic_lookup','-I'+str(python.parents[1]/'include/python2.7'),'-I'+str(ROOT/'include'),str(root/'probe.cpp'),'-o',str(root/'aa_probe.so')],capture_output=True,text=True)
            self.assertEqual(result.returncode,0,result.stderr)
            result=subprocess.run([str(python),'-c',RUNNER,str(root),str(client)],capture_output=True,text=True,env=dict(os.environ,PYTHONDONTWRITEBYTECODE='1'))
            self.assertEqual(result.returncode,0,result.stdout+result.stderr)
