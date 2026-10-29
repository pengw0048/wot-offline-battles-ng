"""Execute the SDK callback and dynamic scopes across profiler state changes."""
import importlib.util
import os
from pathlib import Path
import shlex
import shutil
import subprocess
import sys
import tempfile
import unittest

HERE = Path(__file__).resolve().parent
ROOT = HERE.parent
sys.path.insert(0, str(HERE))
from test_stock_vector4_basic import block


def module(name):
    spec = importlib.util.spec_from_file_location(name, ROOT / ('bigworld/' + name + '.py'))
    value = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(value)
    return value


patcher = module('patch_stock_inactive_profiler')
metadata = module('patch_stock_script_callback_metadata')
profile_hook = module('patch_stock_python_profile_hook')

HOST = r'''
#include <Python.h>
#include <cassert>
#include <cstring>
#include <set>
#include <string>
#include <vector>
#define ENABLE_PROFILER 1
#define MF_ASSERT assert
#define MF_ASSERT_DEV assert
#define MEMTRACKER_SCOPED(...) ((void)0)
#define ERROR_MSG(...) ((void)0)
#define ARRAY_SIZE(value) (sizeof(value)/sizeof(value[0]))
namespace BW { typedef std::string string; }
int locks=0, resolutions=0, calls=0;
struct SimpleMutexHolder { explicit SimpleMutexHolder(int&) { ++locks; } };
template<class T=void> struct StringSet {
    std::set<std::string> values;
    struct iterator {
        std::set<std::string>::iterator value;
        const char* operator*() const { return value->c_str(); }
        bool operator==(const iterator& other) const { return value==other.value; }
    };
    iterator find(const char* text) { return iterator{values.find(text)}; }
    iterator end() { return iterator{values.end()}; }
    std::pair<iterator,bool> insert(const char* text) {
        auto result=values.insert(text);return {iterator{result.first},result.second};
    }
};
struct Profiler {
    enum EventCategory { CATEGORY_CPP, CATEGORY_PYTHON };
    enum EventType { EVENT_START, EVENT_END };
    volatile bool isActive_=false;
    bool isPendingActive_=false;
    int dynamicStringsMutex_=0;
    StringSet<> dynamicStrings_;
    std::vector<std::pair<EventType,std::string> > events;
    @QUERY@
    void addEntry(const char* text, EventType event, int, EventCategory) {
        if (isActive_) events.push_back({event,text});
    }
} g_profiler;
@SCOPE@;
#define PROFILER_SCOPED_DYNAMIC_STRING_CATEGORY(text, category) ScopedDynamicStringProfiler scope(text,category)
void resolvePythonModuleAndFunctionNames(PyObject*,char* output,size_t) {
    ++resolutions;std::strcpy(output,"native.callback");
}
namespace Script {
bool isFinalised() { return false; }
PyObject* ask(PyObject*,PyObject*,const char*,bool,bool);
}
@ASK@
static PyObject* callback(PyObject*,PyObject*) { ++calls;Py_RETURN_NONE; }
static PyMethodDef method={"callback",callback,METH_NOARGS,NULL};
void invoke(PyObject* callable) {
    Py_INCREF(callable);
    PyObject* result=Script::ask(callable,PyTuple_New(0),"",false,false);
    assert(result && !PyErr_Occurred());Py_DECREF(result);
}
int main() {
    Py_Initialize();
    PyObject* callable=PyCFunction_New(&method,NULL);
#ifdef WOT_INACTIVE_PROFILER_NEGATIVE
    invoke(callable);
    assert(calls==1 && locks==1 && resolutions==1 && g_profiler.events.empty());
    g_profiler.isPendingActive_=true;
    { ScopedDynamicStringProfiler scope("pending");g_profiler.isActive_=true; }
    assert(locks==2 && g_profiler.events.size()==1);
    assert(g_profiler.events[0].first==Profiler::EVENT_END);
    Py_DECREF(callable);Py_Finalize();return 0;
#else
    invoke(callable);
    assert(calls==1 && locks==0 && resolutions==0 && g_profiler.events.empty());
    g_profiler.isPendingActive_=true;
    invoke(callable);
    assert(calls==2 && locks==0 && resolutions==0);
    // A pending scope does not acquire metadata or emit END if recording
    // starts during its lifetime. Test the actual SDK scope, not a replica.
    { ScopedDynamicStringProfiler scope("pending");g_profiler.isActive_=true; }
    assert(locks==0 && g_profiler.events.empty());
    invoke(callable);
    assert(calls==3 && locks==1 && resolutions==1 && g_profiler.events.size()==2);
    assert(g_profiler.events[0].first==Profiler::EVENT_START);
    assert(g_profiler.events[1].first==Profiler::EVENT_END);
    assert(g_profiler.events[0].second=="native.callback");
    // END uses the interned name even if the caller overwrites its buffer.
    char name[]="retained";
    { ScopedDynamicStringProfiler scope(name);std::strcpy(name,"changed"); }
    assert(g_profiler.events[2].second=="retained" && g_profiler.events[3].second=="retained");
    // Deactivation preserves addEntry's existing capture-cutoff behavior.
    { ScopedDynamicStringProfiler scope("cutoff");g_profiler.isActive_=false; }
    assert(g_profiler.events.size()==5);
    { ScopedDynamicStringProfiler scope("inactive");g_profiler.isActive_=true; }
    assert(g_profiler.events.size()==5);
    // A callback failure remains a callback failure in both code paths.
    PyObject* globals=PyDict_New();PyDict_SetItemString(globals,"__builtins__",PyEval_GetBuiltins());
    PyObject* failing=PyRun_String("lambda: 1/0",Py_eval_input,globals,globals);
    for(int active=0;active!=2;++active) {
        g_profiler.isActive_=active!=0;Py_INCREF(failing);
        PyObject* result=Script::ask(failing,PyTuple_New(0),"",false,false);
        assert(!result && PyErr_ExceptionMatches(PyExc_ZeroDivisionError));PyErr_Clear();
    }
    Py_DECREF(failing);Py_DECREF(globals);Py_DECREF(callable);Py_Finalize();
#endif
}
'''


class InactiveProfilerTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        engine = Path(os.environ.get('WOT_BW_ENGINE_SOURCE', '/tmp/wot-bigworld-engine-14.4.1/programming/bigworld'))
        if not all((engine / relative).is_file() for relative in patcher.EDITS):
            if 'WOT_BW_ENGINE_SOURCE' in os.environ:
                raise AssertionError('Configured SDK source files missing')
            raise unittest.SkipTest('Pinned SDK source required')
        cls.original = {relative: (engine / relative).read_text() for relative in patcher.EDITS}
        cls.patched = {relative: patcher.patch_source(text, relative) for relative, text in cls.original.items()}

    def test_atomic_idempotent_and_preserves_line_endings(self):
        for relative, text in self.patched.items():
            self.assertEqual(patcher.patch_source(text, relative), text)
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            for relative, text in self.original.items():
                path = root / relative;path.parent.mkdir(parents=True, exist_ok=True)
                path.write_bytes(b'\xef\xbb\xbf' + text.replace('\n', '\r\n').encode())
            profiler = root / patcher.PROFILER
            intact = profiler.read_bytes()
            profiler.write_bytes(intact.replace(b'bool watcherEnabled()', b'bool changed()', 1))
            before = {p: p.read_bytes() for p in (root / r for r in patcher.EDITS)}
            with self.assertRaises(ValueError):patcher.patch(root)
            self.assertEqual(before, {p: p.read_bytes() for p in before})
            profiler.write_bytes(intact)
            patcher.patch(root)
            for relative, text in self.patched.items():
                self.assertEqual((root / relative).read_bytes(), b'\xef\xbb\xbf' + text.replace('\n', '\r\n').encode())
            patcher.patch(root)
            for relative, text in self.patched.items():
                self.assertEqual((root / relative).read_bytes(), b'\xef\xbb\xbf' + text.replace('\n', '\r\n').encode())
        for relative, edits in patcher.EDITS.items():
            for before, after in edits:
                for broken in (self.original[relative].replace(before, '', 1),
                               self.original[relative] + before,
                               self.patched[relative] + before,
                               self.patched[relative].replace(after, '', 1)):
                    with self.assertRaises(ValueError):patcher.patch_source(broken, relative)

    def test_patch_order_with_existing_callback_and_hook_patches(self):
        original = self.original[patcher.SCRIPT]
        for sibling in (metadata.patch_source, profile_hook.patch_text):
            self.assertEqual(sibling(patcher.patch_source(original, patcher.SCRIPT)),
                             patcher.patch_source(sibling(original), patcher.SCRIPT))

    def test_sdk_scope_and_real_cpython_callback_paths(self):
        compiler = os.environ.get('CXX', 'c++')
        python = Path(os.environ.get('WOT_PYTHON27', '/Users/peng/.pyenv/versions/2.7.18/bin/python2.7'))
        config = python.with_name('python2.7-config')
        if not shutil.which(compiler) or not config.is_file():
            self.skipTest('C++ compiler and CPython 2.7 development files required')
        flags = shlex.split(subprocess.check_output([str(config), '--cflags'], text=True))
        flags = [flag for flag in flags if not flag.startswith('-O') and flag != '-DNDEBUG']
        libs = shlex.split(subprocess.check_output([str(config), '--ldflags'], text=True))
        with tempfile.TemporaryDirectory(prefix='inactive-profiler-') as directory:
            for negative, sources in ((False, self.patched), (True, self.original)):
                values = {'@QUERY@': block(self.patched[patcher.PROFILER], '\tbool recordingActive() const'),
                          '@SCOPE@': block(sources[patcher.PROFILER], 'class ScopedDynamicStringProfiler\n{'),
                          '@ASK@': block(sources[patcher.SCRIPT], 'PyObject * Script::ask(')}
                host = HOST
                for token, value in values.items():host = host.replace(token, value)
                cpp = Path(directory) / 'host.cpp';cpp.write_text(host)
                binary = Path(directory) / ('negative' if negative else 'host')
                subprocess.run([compiler, '-std=c++14', '-O0', '-Wall', '-Wextra', '-Werror',
                                '-Wno-missing-field-initializers', '-Wno-deprecated-register',
                                *(['-DWOT_INACTIVE_PROFILER_NEGATIVE'] if negative else []),
                                *flags, str(cpp), *libs, '-o', str(binary)], check=True)
                subprocess.run([str(binary)], check=True, env=dict(os.environ, PYTHONDONTWRITEBYTECODE='1'))


if __name__ == '__main__':
    unittest.main()
