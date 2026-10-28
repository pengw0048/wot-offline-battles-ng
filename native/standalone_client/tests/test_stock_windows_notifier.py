"""Exact notifier methods over real CPython, Win32 boundaries and original PYC."""
from pathlib import Path
import itertools
import os
import shlex
import subprocess
import sys
import tempfile
import unittest

ROOT=Path(__file__).resolve().parents[1]
sys.path.insert(0,str(ROOT/'tests'))
sys.path.insert(0,str(ROOT/'bigworld'))
from test_stock_screener import engine_sources, generated_source
import patch_stock_windows_notifier as patch
import patch_stock_worker_runtime as worker
import patch_stock_runtime_services as runtime
import patch_stock_console_lifecycle as console

HOST_METHODS=r'''
static PyObject* configure(PyObject*,PyObject* args){
    int window,foreground,worker,library,procedure,themed,result,pointer;
    if(!PyArg_ParseTuple(args,"iiiiiiii",&window,&foreground,&worker,&library,&procedure,&themed,&result,&pointer))return NULL;
    notifierHost::window=window;notifierHost::foreground=foreground;notifierHost::worker=worker!=0;
    notifierHost::library=library!=0;notifierHost::procedure=procedure!=0;notifierHost::themed=themed!=0;
    notifierHost::comResult=result;notifierHost::interface=pointer!=0;Py_RETURN_NONE;
}
static PyObject* events(PyObject*,PyObject*){
    PyObject* result=PyList_New(0);for(const auto& event:notifierHost::events){
        PyObject* value=PyString_FromString(event.c_str());PyList_Append(result,value);Py_DECREF(value);
    }return result;
}
static PyObject* clearEvents(PyObject*,PyObject*){notifierHost::events.clear();Py_RETURN_NONE;}
static PyObject* closeNative(PyObject*,PyObject*){BW::finaliseStockWindowsNotifier();Py_RETURN_NONE;}
static PyObject* focus(PyObject*,PyObject* args){int gained;if(!PyArg_ParseTuple(args,"i",&gained))return NULL;
    if(gained)BW::resetStockWindowsNotifier();Py_RETURN_NONE;}
'''


def build_host(test,directory):
    source=generated_source(engine_sources(test)).replace('PyWatermarkersWrapper','WGWinNotifier')
    source=source.replace('#include "stock_screener.cpp"',
        '#include "stock_windows_notifier_host.hpp"\n#include "stock_windows_notifier.cpp"')
    source=source.replace('static PyMethodDef hostMethods[] = {',HOST_METHODS+'''
static PyMethodDef hostMethods[] = {
    {"configure",configure,METH_VARARGS,NULL},{"events",events,METH_NOARGS,NULL},
    {"clearEvents",clearEvents,METH_NOARGS,NULL},{"close",closeNative,METH_NOARGS,NULL},
    {"focus",focus,METH_VARARGS,NULL},''')
    source=source.replace('"_screener_host"','"_notifier_host"')
    for name in ('pch.hpp','app.hpp','cstdmf/stdmf.hpp','cstdmf/stock_worker_runtime.hpp',
                 'pyscript/script.hpp','pyscript/pyobject_plus.hpp','windows.h','shobjidl.h'):
        path=directory/'include'/name;path.parent.mkdir(parents=True,exist_ok=True);path.write_text('#pragma once\n')
    python=Path(os.environ.get('WOT_PYTHON27','/Users/peng/.pyenv/versions/2.7.18/bin/python2.7'))
    config=python.with_name('python2.7-config')
    if not config.exists():test.skipTest('CPython 2.7 development files required')
    flags=[flag for flag in shlex.split(subprocess.check_output([str(config),'--cflags'],text=True)) if flag!='-DNDEBUG']
    libs=shlex.split(subprocess.check_output([str(config),'--ldflags'],text=True))
    (directory/'host.cpp').write_text(source)
    binary=directory/'host'
    subprocess.run(['c++','-std=c++14','-UNDEBUG',*flags,'-Wall','-Werror','-Wno-deprecated-register',
        '-Wno-writable-strings','-Wno-unused-function','-Wno-ignored-pragmas',
        '-I',str(directory/'include'),'-I',str(ROOT/'bigworld'),'-I',str(ROOT/'tests'),
        str(directory/'host.cpp'),*libs,'-o',str(binary)],check=True)
    return binary


class StockWindowsNotifierTest(unittest.TestCase):
    def test_original_pyc_and_native_win32_owner(self):
        client=os.environ.get('WOT_0922_CLIENT')
        if not client:self.skipTest('Exact #1513 client required')
        with tempfile.TemporaryDirectory(prefix='windows-notifier-cpython-') as name:
            binary=build_host(self,Path(name))
            subprocess.run([str(binary),str(ROOT/'tests/stock_windows_notifier_consumer.py'),
                            str(Path(client)/'res/packages/scripts.pkg')],check=True)

    def test_patch_composes_with_worker_runtime_and_console_in_every_order(self):
        engine=Path(os.environ.get('WOT_BIGWORLD_ROOT','/tmp/wot-bigworld-engine-14.4.1/programming/bigworld'))
        names=set(worker.FILES)|{'client/app.hpp','lib/romp/console.hpp','lib/romp/console.cpp','client/canvas_app.cpp'}
        if not all((engine/name).is_file() for name in names):self.skipTest('Pinned engine source required')
        sources={name:(engine/name).read_bytes() for name in names}
        expected=None
        for order in itertools.permutations((patch.patch,worker.patch,runtime.patch,console.patch)):
            with tempfile.TemporaryDirectory(prefix='windows-notifier-patches-') as name:
                directory=Path(name)
                (directory/'lib/cstdmf').mkdir(parents=True)
                for path,data in sources.items():
                    destination=directory/path;destination.parent.mkdir(parents=True,exist_ok=True);destination.write_bytes(data)
                for operation in order:operation(directory)
                first={path:(directory/path).read_text() for path in sources}
                for operation in order:operation(directory)
                self.assertEqual(first,{path:(directory/path).read_text() for path in sources})
                if expected is None:expected=first
                else:self.assertEqual(expected,first)
                app=first['client/app.cpp']
                self.assertLess(app.index('finaliseStockWindowsNotifier();'),app.index('MainLoopTasks::finiAll();'))
                self.assertIn('if (state) resetStockWindowsNotifier();',app)

    def test_exact_pe_method_table_and_win32_outputs(self):
        client=os.environ.get('WOT_0922_CLIENT')
        if not client:self.skipTest('Exact #1513 client required')
        try:from stock_windows_notifier_oracle import audit
        except ImportError:self.skipTest('pefile and Unicorn required')
        audit(Path(client)/'WorldOfTanks.exe')


if __name__=='__main__':unittest.main()
