"""BigWorldIPC.Mutex: real CPython 2.7 binding, Win32 probe build, x86 oracle.

The CPython host records Win32 calls; it proves binding and control flow only.
Kernel mutex behaviour is proved only by running the Win32 probe on Windows.
"""
from pathlib import Path
import os
import shlex
import shutil
import subprocess
import sys
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(Path(__file__).resolve().parent))
FAKE_HEADERS = ('pch.hpp', 'pyscript/script.hpp', 'windows.h')


def build_host(test, path, source=None):
    python = Path(os.environ.get('WOT_PYTHON27', '/Users/peng/.pyenv/versions/2.7.18/bin/python2.7'))
    config = python.with_name('python2.7-config')
    if not config.exists():
        test.skipTest('Pinned CPython 2.7 development files unavailable')
    for header in FAKE_HEADERS:
        (path / 'include' / header).parent.mkdir(parents=True, exist_ok=True)
        (path / 'include' / header).write_text('#pragma once\n')
    flags = [f for f in shlex.split(subprocess.check_output([str(config), '--cflags'], text=True))
             if f != '-DNDEBUG']
    libs = shlex.split(subprocess.check_output([str(config), '--ldflags'], text=True))
    binary = path / 'host'
    subprocess.run(['c++', '-std=c++14', '-Wno-deprecated-register', '-Wno-writable-strings', '-UNDEBUG',
                    '-I', str(path / 'include'), '-I', str(ROOT / 'bigworld'), '-I', str(ROOT / 'tests'),
                    str(source or ROOT / 'tests/stock_bigworld_ipc_host.cpp'), *flags, *libs, '-o', str(binary)],
                   check=True)
    return binary


class StockBigWorldIpcTest(unittest.TestCase):
    def test_real_cpython_binding(self):
        with tempfile.TemporaryDirectory(prefix='bigworld-ipc-cpython-') as directory:
            binary = build_host(self, Path(directory))
            subprocess.run([str(binary), str(ROOT / 'tests/stock_bigworld_ipc_consumer.py')], check=True)

    def test_win32_probe_builds_for_i686(self):
        compiler = shutil.which(os.environ.get('WIN32_CXX', 'i686-w64-mingw32-g++'))
        if not compiler:
            self.skipTest('No i686 Windows cross compiler; build the probe on Windows instead')
        with tempfile.TemporaryDirectory(prefix='bigworld-ipc-probe-') as directory:
            target = Path(directory) / 'stock_bigworld_ipc_probe.exe'
            subprocess.run([compiler, '-std=c++14', '-O2', '-Wall', '-Wextra', '-Werror', '-static',
                            '-DWOT_STOCK_BIGWORLD_IPC_CORE_ONLY', '-I', str(ROOT / 'bigworld'),
                            str(ROOT / 'tests/stock_bigworld_ipc_probe.cpp'),
                            str(ROOT / 'bigworld/stock_bigworld_ipc.cpp'), '-o', str(target)], check=True)
            self.assertEqual(target.read_bytes()[:2], b'MZ')

    def test_original_x86_methods_match_production(self):
        client = os.environ.get('WOT_0922_CLIENT')
        if not client:
            self.skipTest('WOT_0922_CLIENT is required for the original instruction oracle')
        try:
            import stock_bigworld_ipc_oracle as oracle
        except ImportError:
            self.skipTest('pefile and Unicorn are required for the original instruction oracle')
        with tempfile.TemporaryDirectory(prefix='bigworld-ipc-oracle-') as directory:
            path = Path(directory)
            binary = build_host(self, path)
            report = oracle.compare(Path(client) / 'WorldOfTanks.exe', binary,
                                    ROOT / 'tests/stock_bigworld_ipc_trace.py', path)
        print('Original PyMutex instructions matched production:', report)


if __name__ == '__main__':
    unittest.main()
