"""Shared helpers for the battle marker canvas, minimap and Flash bridge tests."""
from pathlib import Path
import json
import os
import shlex
import shutil
import struct
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]
TESTS = ROOT / 'tests'


def f32(value):
    return struct.unpack('<f', struct.pack('<f', value))[0]


class Executable:
    """Minimal x86 PE reader for the exact #1513 WorldOfTanks.exe."""

    def __init__(self, path):
        self.data = Path(path).read_bytes()
        pe = struct.unpack_from('<I', self.data, 0x3c)[0]
        assert self.data[pe:pe + 4] == b'PE\0\0'
        machine, count, _, _, _, optional_size, _ = struct.unpack_from('<HHIIIHH', self.data, pe + 4)
        assert machine == 0x14c
        optional = pe + 24
        self.base = struct.unpack_from('<I', self.data, optional + 28)[0]
        self.sections = []
        table = optional + optional_size
        for i in range(count):
            _, virtual_size, address, raw_size, raw = struct.unpack_from('<8sIIII', self.data, table + 40 * i)
            self.sections.append((address, max(virtual_size, raw_size), raw))

    def offset(self, va):
        rva = va - self.base
        for address, size, raw in self.sections:
            if address <= rva < address + size:
                return raw + rva - address
        raise KeyError(hex(va))

    def word(self, va):
        return struct.unpack_from('<I', self.data, self.offset(va))[0]

    def float(self, va):
        return struct.unpack_from('<f', self.data, self.offset(va))[0]

    def cstr(self, va):
        start = self.offset(va)
        return self.data[start:self.data.index(b'\0', start)].decode('latin-1')

    def code(self, va, size):
        start = self.offset(va)
        return self.data[start:start + size]

    def method_table(self, va):
        methods = []
        while self.word(va):
            methods.append((self.cstr(self.word(va)), self.word(va + 4), self.word(va + 8)))
            va += 16
        return methods

    def type_object(self, va):
        return {'name': self.cstr(self.word(va + 12)), 'basicsize': self.word(va + 16),
                'methods': self.word(va + 116)}


def client_executable(test):
    client = os.environ.get('WOT_0922_CLIENT')
    if not client:
        test.skipTest('WOT_0922_CLIENT is required for the exact #1513 executable')
    path = Path(client) / 'WorldOfTanks.exe'
    if not path.is_file():
        test.skipTest('WorldOfTanks.exe is missing from WOT_0922_CLIENT')
    return Executable(path)


def client_package(test):
    client = os.environ.get('WOT_0922_CLIENT')
    package = Path(client) / 'res/packages/scripts.pkg' if client else None
    if not package or not package.is_file():
        test.skipTest('WOT_0922_CLIENT is required for the original PYC')
    return package


def python27(test):
    pinned = Path.home() / '.pyenv/versions/2.7.18/bin/python2.7'
    python = os.environ.get('WOT_PY27') or (str(pinned) if pinned.is_file() else shutil.which('python2.7'))
    if not python or not Path(python + '-config').exists():
        test.skipTest('CPython 2.7 with development files is required')
    return python


def compiler(test):
    cxx = os.environ.get('CXX') or shutil.which('clang++') or shutil.which('c++')
    if not cxx:
        test.skipTest('A host C++ compiler is required')
    return cxx


def run_host(test, source):
    """Compile tests/<source> against include/ and return its JSON output."""
    cxx = compiler(test)
    with tempfile.TemporaryDirectory(prefix='stock-battle-gui-') as directory:
        binary = Path(directory) / 'host'
        subprocess.run([cxx, '-std=c++14', '-Wall', '-Wextra', '-Werror', '-ffp-contract=off',
                        '-I', str(ROOT / 'include'), '-I', str(TESTS), str(TESTS / source),
                        '-o', str(binary)], check=True)
        output = subprocess.run([str(binary)], check=True, stdout=subprocess.PIPE, text=True).stdout
    # Every printed number is a float32 value written with enough digits.
    return json.loads(output, parse_float=lambda text: f32(float(text)))


HOST_HEADERS = ('scaleform/flash_gui_component.hpp', 'cstdmf/bw_string.hpp', 'moo/render_context.hpp',
                'pyscript/pyobject_plus.hpp', 'pyscript/script.hpp', 'pyscript/script_math.hpp',
                'romp/py_texture_provider.hpp')


def build_probe(test, python, directory):
    """Compile the production battle bindings and the probe into a CPython 2.7 module."""
    cxx = compiler(test)
    directory = Path(directory)
    stubs = directory / 'include'
    for header in HOST_HEADERS:
        (stubs / header).parent.mkdir(parents=True, exist_ok=True)
        (stubs / header).write_text('#pragma once\n')
    (stubs / 'pch.hpp').write_text('#include "stock_battle_host_engine.hpp"\n')
    includes = []
    for flag in shlex.split(subprocess.check_output([python + '-config', '--includes'], text=True)):
        includes.extend(['-isystem', flag[2:]] if flag.startswith('-I') else [flag])
    sources = [TESTS / 'stock_battle_probe_module.cpp', ROOT / 'bigworld/stock_battle_flash.cpp',
               ROOT / 'bigworld/stock_battle_marker_canvas.cpp', ROOT / 'bigworld/stock_battle_minimap.cpp']
    objects = []
    for source in sources:
        target = directory / (source.stem + '.o')
        subprocess.run([cxx, '-std=c++14', '-Wall', '-Wextra', '-Werror', '-Wno-missing-field-initializers',
                        '-Wno-unused-parameter', '-Wno-deprecated-register', '-Wno-register',
                        '-ffp-contract=off', '-fPIC', *includes, '-I', str(stubs), '-I', str(TESTS),
                        '-I', str(ROOT / 'include'), '-I', str(ROOT / 'bigworld'), '-c', str(source),
                        '-o', str(target)], check=True)
        objects.append(str(target))
    link = ['-dynamiclib', '-undefined', 'dynamic_lookup'] if os.uname().sysname == 'Darwin' else ['-shared']
    subprocess.run([cxx, *link, *objects, '-o', str(directory / 'battle_gui_probe.so')], check=True)
