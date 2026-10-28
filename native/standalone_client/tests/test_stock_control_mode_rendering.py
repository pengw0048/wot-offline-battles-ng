"""Control-mode render owners, original PE gates, and unmodified PYC consumers."""
from pathlib import Path
import os
import shlex
import subprocess
import sys
import tarfile
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'tests'))
sys.path.insert(0, str(ROOT / 'bigworld'))
import patch_stock_control_mode_rendering as patch
from test_stock_havok_sniper import HOST
from test_stock_debug_queries import between

FILES = ('lib/pyscript/script.hpp', 'lib/pyscript/script.cpp', 'lib/romp/flora.hpp',
         'lib/romp/flora.cpp', 'lib/scene/scene_draw_context.hpp', 'client/world_app.cpp')


def sources(test):
    archive = Path(os.environ.get('WOT_PUBLIC_ENGINE_ARCHIVE', '/tmp/wot-bigworld-engine-14.4.1.tar.gz'))
    if not archive.is_file():
        test.skipTest('Pinned public engine archive required')
    result = {}
    with tarfile.open(archive) as source:
        for member in source:
            for name in FILES:
                if member.name.endswith('/programming/bigworld/' + name):
                    result[name] = source.extractfile(member).read().decode('utf-8')
            if len(result) == len(FILES):
                break
    test.assertEqual(set(result), set(FILES))
    return result


def build_host(test, directory, source):
    python = Path(os.environ.get('WOT_PYTHON27', '/Users/peng/.pyenv/versions/2.7.18/bin/python2.7'))
    config = python.with_name('python2.7-config')
    if not config.exists():
        test.skipTest('CPython 2.7 development files required')
    hpp, cpp = source['lib/pyscript/script.hpp'], source['lib/pyscript/script.cpp']
    bodies = between(cpp, 'int Script::setData( PyObject * pObject, bool & rBool,', '\n}\n')
    bodies += between(cpp, 'PyObject * Script::argCountError(', '\n}\n')
    macros = between(hpp, '// Section: Macros: Module Links', '// Section: Macros: Enums', False)
    macros += between(hpp, '// Section: Macros: Recursion utilities', '// Section: PyModuleLink', False)
    flora = '\n'.join(line for line in source['lib/romp/flora.hpp'].splitlines()
                      if 'static void enabled(' in line or 'static bool enabled()' in line)
    test.assertIn('s_enabled_ = state', flora)
    test.assertIn('return s_enabled_', flora)
    flora = 'class Flora { public:\n' + flora + '\nprivate:static bool s_enabled_; };\n' \
            'bool Flora::s_enabled_ = true;\n'
    text = HOST.replace('@SCRIPT_BODIES@', bodies).replace('@PYAUTO_MACROS@', macros)
    text = text.replace('namespace Script {', flora + 'namespace Script {')
    text = text.replace('#include "stock_havok_sniper.cpp"', r'''
#include "stock_havok_sniper.cpp"
#include "stock_control_mode_rendering.cpp"
static PyObject * low(PyObject*, PyObject*) { return PyBool_FromLong(BW::stockLowDetailedMode()); }
static PyObject * flora(PyObject*, PyObject*) { return PyBool_FromLong(BW::Flora::enabled()); }
static PyMethodDef hostMethods[] = {
    {"lowDetailed", low, METH_NOARGS, NULL},
    {"floraEnabled", flora, METH_NOARGS, NULL},
    {NULL, NULL, 0, NULL},
};
''')
    text = text.replace('BW::links().size() == 1', 'BW::links().size() == 3')
    text = text.replace('FILE * script =', 'Py_InitModule("_control_render_host", hostMethods);\n    FILE * script =')
    for name in ('pch.hpp', 'pyscript/script.hpp', 'romp/flora.hpp', 'cstdmf/stdmf.hpp'):
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


class StockControlModeRenderingTest(unittest.TestCase):
    def test_primary_context_patch_is_exact_idempotent_and_has_false_default(self):
        source = sources(self)
        with tempfile.TemporaryDirectory(prefix='control-mode-render-patch-') as name:
            directory = Path(name)
            for relative in ('lib/scene/scene_draw_context.hpp', 'client/world_app.cpp'):
                path = directory / relative
                path.parent.mkdir(parents=True, exist_ok=True)
                path.write_text(source[relative])
            patch.patch(directory)
            first = {relative: (directory / relative).read_text()
                     for relative in ('lib/scene/scene_draw_context.hpp', 'client/world_app.cpp')}
            patch.patch(directory)
            self.assertEqual(first, {relative: (directory / relative).read_text() for relative in first})
            self.assertEqual(first['client/world_app.cpp'].count(patch.SAMPLE), 1)
            self.assertIn('bool stockLowDetailedMode_ = false;', first['lib/scene/scene_draw_context.hpp'])
            # An unexpected owner layout cannot silently leave the mode unread.
            for relative in first:
                (directory / relative).write_text(source[relative])
            path = directory / 'client/world_app.cpp'
            path.write_text(source['client/world_app.cpp'].replace('sceneDrawContext( scene, colourDrawContext )',
                                                                  'differentContext( scene, colourDrawContext )'))
            before = {relative: (directory / relative).read_text() for relative in first}
            with self.assertRaisesRegex(RuntimeError, 'primary draw context sample'):
                patch.patch(directory)
            self.assertEqual(before, {relative: (directory / relative).read_text() for relative in first})

    def test_original_control_mode_pyc_over_the_native_owners(self):
        client = os.environ.get('WOT_0922_CLIENT')
        if not client:
            self.skipTest('Exact #1513 client required')
        with tempfile.TemporaryDirectory(prefix='control-mode-render-cpython-') as name:
            binary = build_host(self, Path(name), sources(self))
            subprocess.run([str(binary), str(ROOT / 'tests/stock_control_mode_rendering_consumer.py'),
                            str(Path(client) / 'res/packages/scripts.pkg')], check=True)

    def test_original_pe_setters_context_sample_and_both_authored_flags(self):
        client = os.environ.get('WOT_0922_CLIENT')
        if not client:
            self.skipTest('Exact #1513 client required')
        try:
            from stock_control_mode_rendering_oracle import audit
        except ImportError:
            self.skipTest('pefile and Unicorn required')
        audit(Path(client) / 'WorldOfTanks.exe')

    def test_production_tree_filter_matches_original_flag_gate(self):
        source = (ROOT / 'bigworld/stock_tree_scene.cpp').read_text()
        start = source.index('const uint32 flags = instance->definition.flags;', source.index('class TreeDrawHandler'))
        stop = source.index(' continue;', start) + len(' continue;')
        gate = source[start:stop]
        driver = r'''
#include <cassert>
#include <cstdint>
using uint32=std::uint32_t;
namespace Moo { enum Pass {RENDERING_PASS_COLOR,RENDERING_PASS_REFLECTION,RENDERING_PASS_SHADOWS}; }
struct Context { bool low; bool stockLowDetailedMode() const { return low; } };
struct Instance { struct {uint32 flags;} definition; };
bool visible(uint32 bits, bool low, Moo::Pass pass, int decorLevel) {
    Context context{low}; Instance value{{bits}}; const Instance* instance=&value;
    for (int n=0;n<1;++n) {
        @GATE@
        return true;
    }
    return false;
}
int main() {
    for (uint32 bits=0;bits<512;++bits) for (bool low:{false,true})
        for (int pass=0;pass<3;++pass) for (int decor=0;decor<3;++decor) {
            const auto p=static_cast<Moo::Pass>(pass);
            const unsigned required=bits&0x20?2:((bits>>4)&1);
            const bool baseline=!(bits&8)&&(!required||int(required)<=decor)&&
                (p!=Moo::RENDERING_PASS_REFLECTION||(bits&2))&&
                (p!=Moo::RENDERING_PASS_SHADOWS||(bits&1));
            assert(visible(bits,low,p,decor)==(baseline&&!(low&&(bits&0x10))));
        }
}
'''.replace('@GATE@', gate).replace('#include <cstdint>', '#include <cstdint>\n#include <initializer_list>')
        with tempfile.TemporaryDirectory(prefix='control-mode-tree-gate-') as name:
            directory = Path(name)
            (directory / 'gate.cpp').write_text(driver)
            subprocess.run(['c++', '-std=c++14', '-Wall', '-Wextra', '-Werror', str(directory / 'gate.cpp'),
                            '-o', str(directory / 'gate')], check=True)
            subprocess.run([str(directory / 'gate')], check=True)


if __name__ == '__main__':
    unittest.main()
