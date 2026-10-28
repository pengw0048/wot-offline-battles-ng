"""Exercise live rendering-owner queries and original #1513 decal selection.

The host compiles the actual SDK SSAO and shadow enable methods; renderer
lookup and CPython wrapper macros are explicit harness boundaries.
"""
from pathlib import Path
import itertools
import os
import shlex
import struct
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]


def block(source, signature):
    start = source.index(signature)
    end = source.index('{', start)
    depth = 1
    while depth:
        end += 1
        depth += (source[end] == '{') - (source[end] == '}')
    return source[start:end + 1]


HOST = r'''
#include <Python.h>
#include <cassert>
#include <memory>
#define BW_GUARD
struct {bool m_enabled=false;} g_settings;
class SSAOSupport {public:bool m_enabled=false;bool enable()const;};
@SSAO@
namespace Moo {
class ShadowManager {
public:
 bool isDynamicShadowsEnabled()const;bool isSemiDynamicShadowsEnabled()const;bool isShadowsEnabled()const;
 void* m_pShadowsWrapper=nullptr;std::unique_ptr<int> m_semiDynamicShadows;bool m_enabledManager=false;
};
@SHADOWS@
}
struct IRendererPipeline {
 enum {TYPE_DEFERRED_SHADING,TYPE_FORWARD_SHADING};
 SSAOSupport* ssao=nullptr;Moo::ShadowManager* shadows=nullptr;
 SSAOSupport* ssaoSupport()const{return ssao;}
 Moo::ShadowManager* dynamicShadow()const{return shadows;}
};
struct Renderer {
 static Renderer* active;IRendererPipeline* current=nullptr;int type=0;
 static Renderer* pInstance(){return active;}
 static Renderer& instance(){assert(active);return *active;}
 IRendererPipeline* pipeline(){return current;}
 int pipelineType(){return type;}
};
Renderer* Renderer::active=nullptr;
@PRODUCTION@
PyObject* ssao(PyObject*,PyObject* args){if(!PyArg_ParseTuple(args,":isSSAOEnabled"))return NULL;return PyBool_FromLong(isSSAOEnabled());}
PyObject* shadows(PyObject*,PyObject* args){if(!PyArg_ParseTuple(args,":isShadowsEnabled"))return NULL;return PyBool_FromLong(isShadowsEnabled());}
PyObject* forward(PyObject*,PyObject* args){if(!PyArg_ParseTuple(args,":isForwardPipeline"))return NULL;return PyBool_FromLong(isForwardPipeline());}
Renderer renderer;IRendererPipeline pipeline;SSAOSupport ao;Moo::ShadowManager shadow;
PyObject* state(PyObject*,PyObject* args){
 int mode,global,local,dynamic,semi,enabled;
 if(!PyArg_ParseTuple(args,"iiiiii",&mode,&global,&local,&dynamic,&semi,&enabled))return NULL;
 Renderer::active=mode==0?nullptr:&renderer;renderer.current=mode<2?nullptr:&pipeline;
 renderer.type=mode==2?IRendererPipeline::TYPE_FORWARD_SHADING:IRendererPipeline::TYPE_DEFERRED_SHADING;
 pipeline.ssao=mode==3?&ao:nullptr;pipeline.shadows=mode==3?&shadow:nullptr;
 g_settings.m_enabled=global!=0;ao.m_enabled=local!=0;shadow.m_enabledManager=enabled!=0;
 shadow.m_pShadowsWrapper=dynamic?&renderer:nullptr;
 shadow.m_semiDynamicShadows.reset(semi?new int(1):nullptr);
 Py_RETURN_NONE;
}
PyMethodDef methods[]={{"isSSAOEnabled",ssao,METH_VARARGS,NULL},{"isShadowsEnabled",shadows,METH_VARARGS,NULL},
 {"isForwardPipeline",forward,METH_VARARGS,NULL},{"state",state,METH_VARARGS,NULL},{NULL,NULL,0,NULL}};
int main(){
 Py_Initialize();assert(Py_InitModule("BigWorld",methods));
 const char* program=R"PY(
import BigWorld,os,marshal,types,zipfile,itertools
for mode in [0,1,2]:
 BigWorld.state(mode,1,1,1,1,1)
 assert BigWorld.isSSAOEnabled() is False and BigWorld.isShadowsEnabled() is False
for global_,local,dynamic,semi,enabled in itertools.product([0,1],repeat=5):
 BigWorld.state(3,global_,local,dynamic,semi,enabled)
 assert BigWorld.isSSAOEnabled() is bool(global_ and local)
 assert BigWorld.isShadowsEnabled() is bool((dynamic or semi) and enabled)
for function in [BigWorld.isSSAOEnabled,BigWorld.isShadowsEnabled]:
 try:function(1)
 except TypeError:pass
 else:raise AssertionError('Expected zero arguments')
client=os.environ.get('WOT_0922_CLIENT')
if client:
 archive=zipfile.ZipFile(os.path.join(client,'res/packages/scripts.pkg'))
 root=marshal.loads(archive.read('scripts/client/vehicle_systems/components/vehicleDecal.pyc')[8:])
 code=next(c for c in root.co_consts if isinstance(c,types.CodeType) and c.co_name=='VehicleDecal')
 class Dependency(object):descriptor=staticmethod(lambda value:None)
 namespace={'__name__':'vehicleDecal','dependency':Dependency,'ISettingsCore':object,'BigWorld':BigWorld,'__builtins__':__builtins__}
 members=types.FunctionType(code,namespace)()
 VehicleDecal=type('VehicleDecal',(object,),members);namespace['VehicleDecal']=VehicleDecal
 for mode,global_,local,dynamic,semi,enabled in itertools.product([2,3],[0,1],[0,1],[0,1],[0,1],[0,1]):
  BigWorld.state(mode,global_,local,dynamic,semi,enabled)
  expected=VehicleDecal.DECALS_OFF
  if mode==3:
   if not ((dynamic or semi) and enabled):expected=VehicleDecal.FORWARD_DECAL
   elif global_ and local:expected=VehicleDecal.OCCLUSION_DECAL
  assert VehicleDecal.getDecalType()==expected
print('Live SSAO/shadow owner queries and original vehicleDecal consumer passed')
)PY";
 assert(!PyRun_SimpleString(program));Py_Finalize();
}
'''


class StockRenderingQueriesTest(unittest.TestCase):
    def test_native_owner_queries_and_exact_decal_consumer(self):
        python = Path(os.environ.get('WOT_PYTHON27', '/Users/peng/.pyenv/versions/2.7.18/bin/python2.7'))
        config = python.with_name('python2.7-config')
        engine = Path(os.environ.get('WOT_BIGWORLD_ROOT', '/tmp/wot-bigworld-engine-14.4.1/programming/bigworld'))
        if not config.exists() or not (engine / 'lib/romp/ssao_support.cpp').exists():
            self.skipTest('Pinned SDK and CPython 2.7 development files required')
        production = (ROOT / 'bigworld/stock_decal_resources.cpp').read_text()
        ssao_source = (engine / 'lib/romp/ssao_support.cpp').read_text()
        shadow_source = (engine / 'lib/moo/shadow_manager.cpp').read_text()
        host = HOST.replace('@SSAO@', block(ssao_source, 'bool SSAOSupport::enable() const'))
        host = host.replace('@SHADOWS@', '\n'.join(block(shadow_source, signature) for signature in (
            'bool ShadowManager::isDynamicShadowsEnabled() const',
            'bool ShadowManager::isSemiDynamicShadowsEnabled() const',
            'bool ShadowManager::isShadowsEnabled() const')))
        host = host.replace('@PRODUCTION@', '\n'.join(block(production, 'bool ' + name + '()') for name in (
            'isSSAOEnabled', 'isShadowsEnabled', 'isForwardPipeline')))
        with tempfile.TemporaryDirectory(prefix='stock-rendering-queries-') as directory:
            directory = Path(directory)
            cpp, binary = directory / 'host.cpp', directory / 'host'
            cpp.write_text(host)
            flags = [value for value in shlex.split(subprocess.check_output([str(config), '--cflags'], text=True)) if value != '-DNDEBUG']
            libs = shlex.split(subprocess.check_output([str(config), '--ldflags'], text=True))
            subprocess.run(['c++', '-std=c++14', '-UNDEBUG', '-Wno-deprecated-register',
                            *flags, str(cpp), *libs, '-o', str(binary)], check=True)
            subprocess.run([str(binary)], check=True)

    def test_original_query_wrappers_and_native_enable_instructions(self):
        client = os.environ.get('WOT_0922_CLIENT')
        if not client:
            self.skipTest('WOT_0922_CLIENT required for original instructions')
        try:
            from stock_tree_lod_oracle import OriginalTreeInstructionOracle
            from unicorn import UC_HOOK_CODE
            from unicorn.x86_const import UC_X86_REG_EAX, UC_X86_REG_EIP, UC_X86_REG_ESP
        except ImportError:
            self.skipTest('pefile and Unicorn required for original instructions')
        oracle = OriginalTreeInstructionOracle(Path(client) / 'WorldOfTanks.exe')
        emulator = oracle.emulator
        pipeline, table, descriptor, ao, shadow, switches = [oracle.allocate(size) for size in (32, 0x100, 64, 32, 64, 3)]
        emulator.mem_write(pipeline, struct.pack('<I', table))
        emulator.mem_write(table + 0xc8, struct.pack('<I', 0x3000e000))
        emulator.mem_write(table + 0xa0, struct.pack('<I', 0x3000e010))
        state = {'count': 0, 'ao_present': True, 'shadow_present': True, 'error': False}

        def boundary(uc, address, size, context):
            stack = uc.reg_read(UC_X86_REG_ESP)
            if address == 0xfb3610:
                result = state['count']
            elif address == 0x6cbfd0:
                result = pipeline
            elif address == 0x3000e000:
                result = descriptor
            elif address == 0x3000e010:
                result = shadow if state['shadow_present'] else 0
            elif address == 0x68ee00:
                result = int(bool(oracle.word(stack + 4) & 0xff))
            elif address in (0x9cbd20, 0x9cdd40, 0x98a2b0):
                result = switches + (0x9cbd20, 0x9cdd40, 0x98a2b0).index(address)
            else:
                self.assertEqual(address, 0x68e100)
                state['error'], result = True, 0
            uc.reg_write(UC_X86_REG_EAX, result)
            uc.reg_write(UC_X86_REG_EIP, oracle.word(stack))
            uc.reg_write(UC_X86_REG_ESP, stack + 4)

        for address in (0xfb3610, 0x6cbfd0, 0x3000e000, 0x3000e010, 0x68ee00, 0x9cbd20, 0x9cdd40, 0x98a2b0, 0x68e100):
            emulator.hook_add(UC_HOOK_CODE, boundary, begin=address, end=address)
        for present, global_enabled, local in itertools.product((False, True), repeat=3):
            emulator.mem_write(descriptor + 0x1c, struct.pack('<I', ao if present else 0))
            emulator.mem_write(0x1d57434, bytes([global_enabled]))
            emulator.mem_write(ao + 4, bytes([local]))
            oracle.call(0x6509c0, (0, 0xdead0000))
            self.assertEqual(emulator.reg_read(UC_X86_REG_EAX), int(present and global_enabled and local))
        for present, first, second, third, manager in itertools.product((False, True), repeat=5):
            state['shadow_present'] = present
            emulator.mem_write(switches, bytes([first, second, third]))
            emulator.mem_write(shadow + 0x39, bytes([manager]))
            oracle.call(0x650a30, (0, 0xdead0000))
            self.assertEqual(emulator.reg_read(UC_X86_REG_EAX), int(present and (first or second or third) and manager))
        for address in (0x6509c0, 0x650a30):
            state['count'], state['error'] = 1, False
            oracle.call(address, (0, 0xdead0000))
            self.assertTrue(state['error'])
            self.assertEqual(emulator.reg_read(UC_X86_REG_EAX), 0)


if __name__ == '__main__':
    unittest.main()
