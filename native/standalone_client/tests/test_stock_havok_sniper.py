"""Exact #1513 bool ABI and original sniper/input-handler teardown consumers.

The original wrapper has no Havok backend in this build. The PE oracle checks
all executed image writes; the host uses real CPython 2.7 and the public engine
macros/converter, then runs the unchanged original PYC cleanup methods.
"""
from pathlib import Path
import os
import shlex
import subprocess
import sys
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'tests'))
from test_stock_debug_queries import between, engine_sources


HOST = r'''
#include <Python.h>
#include <cassert>
#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <deque>
#include <string>
#include <strings.h>
#define BW_BEGIN_NAMESPACE namespace BW {
#define BW_END_NAMESPACE }
#define BW_STATIC_ASSERT(expression, message) static_assert(expression, #message)
#define _stricmp strcasecmp
namespace BW {
typedef std::string string;
struct Link { std::string module; PyMethodDef method; };
std::deque<Link> & links() { static std::deque<Link> value; return value; }
struct PyModuleMethodLink {
    PyModuleMethodLink(const char * module, const char * name, PyCFunction fn, const char * doc = NULL) {
        links().push_back(Link{module, PyMethodDef{name, fn, METH_VARARGS, doc}});
    }
};
namespace Script {
template<class T> struct IsValidArgType { static const bool isValid = true; };
int setData(PyObject *, bool &, const char *);
PyObject * argCountError(const char *, int, int, ...);
}
@SCRIPT_BODIES@
}
@PYAUTO_MACROS@
#include "stock_havok_sniper.cpp"
int main(int argc, char ** argv) {
    assert(argc == 3);
    Py_Initialize();
    PySys_SetArgv(argc - 1, argv + 1);
    assert(BW::links().size() == 1);
    for (BW::Link & link : BW::links()) {
        PyObject * module = PyImport_AddModule(link.module.c_str());
        PyObject * fn = PyCFunction_NewEx(&link.method, NULL, NULL);
        assert(module && fn && PyObject_SetAttrString(module, link.method.ml_name, fn) == 0);
        Py_DECREF(fn);
    }
    FILE * script = std::fopen(argv[1], "r");
    if (!script) return 2;
    const int failed = PyRun_SimpleFileEx(script, argv[1], 1);
    Py_Finalize();
    return failed ? 1 : 0;
}
'''


def build_host(test, directory):
    python = Path(os.environ.get('WOT_PYTHON27', '/Users/peng/.pyenv/versions/2.7.18/bin/python2.7'))
    config = python.with_name('python2.7-config')
    if not config.exists():
        test.skipTest('CPython 2.7 development files required')
    source = engine_sources(test)
    hpp, cpp = source['lib/pyscript/script.hpp'], source['lib/pyscript/script.cpp']
    bodies = between(cpp, 'int Script::setData( PyObject * pObject, bool & rBool,', '\n}\n')
    bodies += between(cpp, 'PyObject * Script::argCountError(', '\n}\n')
    macros = between(hpp, '// Section: Macros: Module Links', '// Section: Macros: Enums', False)
    macros += between(hpp, '// Section: Macros: Recursion utilities', '// Section: PyModuleLink', False)
    text = HOST.replace('@SCRIPT_BODIES@', bodies).replace('@PYAUTO_MACROS@', macros)
    for name in ('pch.hpp', 'pyscript/script.hpp'):
        path = directory / 'include' / name
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_text('#pragma once\n')
    (directory / 'host.cpp').write_text(text)
    flags = [flag for flag in shlex.split(subprocess.check_output([str(config), '--cflags'], text=True))
             if flag != '-DNDEBUG']
    libs = shlex.split(subprocess.check_output([str(config), '--ldflags'], text=True))
    binary = directory / 'host'
    subprocess.run(['c++', '-std=c++14', '-UNDEBUG', *flags, '-Wall', '-Werror',
                    '-Wno-deprecated-register', '-Wno-writable-strings', '-Wno-unused-function',
                    '-I', str(directory / 'include'), '-I', str(ROOT / 'bigworld'),
                    str(directory / 'host.cpp'), *libs, '-o', str(binary)], check=True)
    return binary


class StockHavokSniperTest(unittest.TestCase):
    def test_real_cpython_abi_and_original_teardown(self):
        client = os.environ.get('WOT_0922_CLIENT')
        if not client:
            self.skipTest('Exact #1513 client required')
        with tempfile.TemporaryDirectory(prefix='havok-sniper-cpython-') as name:
            binary = build_host(self, Path(name))
            subprocess.run([str(binary), str(ROOT / 'tests/stock_havok_sniper_consumer.py'),
                            str(Path(client) / 'res/packages/scripts.pkg')], check=True)

    def test_original_pe_has_no_havok_owner_or_image_state_write(self):
        client = os.environ.get('WOT_0922_CLIENT')
        if not client:
            self.skipTest('Exact #1513 client required')
        try:
            from stock_havok_sniper_oracle import audit
        except ImportError:
            self.skipTest('pefile and Unicorn required')
        audit(Path(client) / 'WorldOfTanks.exe')


if __name__ == '__main__':
    unittest.main()
