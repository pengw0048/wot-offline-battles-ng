"""Audit the recovered replay owner against fresh public-source lifecycle code."""
import importlib.util
import itertools
import os
from pathlib import Path
import shutil
import subprocess
from native_test_compile_flags import native_test_compile_flags
import tarfile
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]


def load(name):
    spec = importlib.util.spec_from_file_location(name, ROOT / 'bigworld' / ('patch_stock_' + name + '.py'))
    result = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(result)
    return result


class ReplayControllerLifecycle(unittest.TestCase):
    def test_replacing_callback_publishes_before_destructor_reentry(self):
        if not shutil.which(os.environ.get('CXX', 'clang++')):
            raise unittest.SkipTest('C++ compiler unavailable: ' + os.environ.get('CXX', 'clang++'))
        source = (ROOT / 'bigworld/stock_replay_controller.cpp').read_text()
        body = source[source.index('int setCallback('):source.index('PyObject* getFlag(')]
        probe = r'''
#include <cassert>
#include <cstddef>
#include <functional>
struct PyObject { int refs=1; std::function<void()> destroy; };
struct PyController { PyObject* callbacks[1]={}; };
PyObject* Py_None=nullptr;
PyObject* PyExc_TypeError=nullptr;
bool PyCallable_Check(PyObject*) { return true; }
void PyErr_SetString(PyObject*,const char*) { assert(false); }
void Py_INCREF(PyObject* value) { ++value->refs; }
void Py_XDECREF(PyObject* value) {
    if (value && --value->refs==0 && value->destroy) value->destroy();
}
'''
        cases = r'''
int main() {
    PyController owner;
    PyObject previous,replacement,nested;
    owner.callbacks[0]=&previous;
    previous.destroy=[&] {
        assert(owner.callbacks[0]==&replacement);
        assert(setCallback(&owner,&nested,nullptr)==0);
    };
    assert(setCallback(&owner,&replacement,nullptr)==0);
    assert(owner.callbacks[0]==&nested);
    assert(previous.refs==0 && replacement.refs==1 && nested.refs==2);
    Py_XDECREF(owner.callbacks[0]);
    assert(nested.refs==1);
}
'''
        with tempfile.TemporaryDirectory(prefix='replay-callback-reentry-') as directory:
            path = Path(directory)
            (path / 'probe.cpp').write_text(probe + body + cases)
            subprocess.run([os.environ.get('CXX', 'clang++'), *native_test_compile_flags(os.environ.get('CXX', 'clang++')), '-std=c++17',
                            '-Wall', '-Wextra', '-Werror',
                            str(path / 'probe.cpp'), '-o', str(path / 'probe')], check=True)
            subprocess.run([str(path / 'probe')], check=True)

    def test_fresh_owner_composition_and_transaction(self):
        archive = Path(os.environ.get('WOT_BIGWORLD_ARCHIVE', '/tmp/wot-bigworld-engine-14.4.1.tar.gz'))
        if not archive.exists():
            self.skipTest('Pinned public-source archive unavailable')
        paths = ['client/script_bigworld.cpp', 'client/script_app.cpp',
                 'client/connection_control.hpp', 'client/connection_control.cpp',
                 'client/device_app.cpp']
        with tarfile.open(archive) as source:
            names = source.getnames()
            original = {name: source.extractfile(next(item for item in names if item.endswith('/' + name))).read() for name in paths}
        replay, xmpp, audio = load('replay_controller'), load('xmpp'), load('audio')
        for order in itertools.permutations([replay.patch, xmpp.patch, audio.patch_engine]):
            with tempfile.TemporaryDirectory(prefix='replay-controller-lifecycle-') as directory:
                root = Path(directory)
                for name, content in original.items():
                    target = root / name
                    target.parent.mkdir(parents=True, exist_ok=True)
                    target.write_bytes(content)
                for apply in order:
                    apply(root)
                first = {name: (root / name).read_bytes() for name in paths}
                for apply in reversed(order):
                    apply(root)
                self.assertEqual(first, {name: (root / name).read_bytes() for name in paths})
                script = (root / paths[0]).read_text()
                self.assertLess(script.index('initialiseStockReplayController()'), script.index('EntityType::init('))
                app = (root / paths[1]).read_text()
                self.assertLess(app.index('finiStockReplayController();'), app.index('BigWorldClientScript::fini();'))
                connection = (root / paths[3]).read_text()
                self.assertEqual(connection.count('tickStockReplayController( dt );'), 1)
                self.assertLess(connection.index('connectionHelper_.update( pConnection_, dt );'), connection.index('tickStockReplayController( dt );'))
                self.assertIn('createStockReplayNativeConnection(', connection)
                self.assertIn('if (pConnection_ != expected || mode_ != OFFLINE)', connection)
                self.assertIn('if (pConnection_ == expected)', connection)
                for name, content in original.items():
                    (root / name).write_bytes(content)
                target = root / paths[3]
                target.write_text(target.read_text().replace('connectionHelper_.update( pConnection_, dt );', 'unknownUpdate();'))
                with self.assertRaises(RuntimeError):
                    replay.patch(root)
                self.assertEqual((root / paths[0]).read_bytes(), original[paths[0]])


if __name__ == '__main__':
    unittest.main()
