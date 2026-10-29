"""GUI.wg_setGlyphCacheParams and the Scaleform manager glyph-cache owner: the #1513 chain.

Real CPython 2.7 runs the engine's PY_AUTO macros and Script::setData(uint&),
the production binding and the pristine or patched manager class, constructor,
draw and device callbacks over SDK 4.2.22 GlyphCacheParams/SetParams text,
driven by the original MonitorSettings, InterfaceScaleSetting,
InterfaceScaleManager and SFApplication PYC. The Unicorn oracle runs the exact
#1513 setter. Real SDK textures and a D3D9 device are covered by the Win32 probe.
"""
from pathlib import Path
import difflib
import importlib.util
import os
import shlex
import subprocess
import sys
import tarfile
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(Path(__file__).resolve().parent))
ENGINE_FILES = ('lib/scaleform/manager.hpp', 'lib/scaleform/manager.cpp', 'lib/pyscript/script.hpp',
                'lib/pyscript/script.cpp')
SDK_REVISION = '7d92be560bf1b2e4ecae9ac69e340d4cae9a557e'
SDK_FILES = ('Src/Render/Render_GlyphCacheConfig.h', 'Src/Render/Render_GlyphCache.cpp')


def load_patch():
    spec = importlib.util.spec_from_file_location('patch_stock_glyph_cache',
                                                  ROOT / 'bigworld/patch_stock_glyph_cache.py')
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


def between(text, begin, end, include_end=True):
    start = text.rindex('\n', 0, text.index(begin)) + 1
    stop = text.index(end, start)
    return text[start:stop + (len(end) if include_end else 0)]


def pinned_sources(test):
    archive = Path(os.environ.get('WOT_PUBLIC_ENGINE_ARCHIVE', '/tmp/wot-bigworld-engine-14.4.1.tar.gz'))
    checkout = Path(os.environ.get('WOT_SCALEFORM_SDK_GIT', '/Users/peng/.cache/wot-independent-client/scaleform-sdk-4.2'))
    if not archive.is_file():
        test.skipTest('Pinned public engine source archive required')
    sources = {}
    with tarfile.open(archive) as tar:
        for member in tar:
            for relative in ENGINE_FILES:
                if member.name.endswith('/programming/bigworld/' + relative):
                    sources[relative] = tar.extractfile(member).read().decode('utf-8')
            if len(sources) == len(ENGINE_FILES):
                break
    try:
        for name in SDK_FILES:
            sources[name] = subprocess.run(['git', '-C', str(checkout), 'show', SDK_REVISION + ':' + name],
                                           check=True, capture_output=True).stdout.decode('utf-8').replace('\r\n', '\n')
    except (OSError, subprocess.CalledProcessError):
        test.skipTest('Pinned Scaleform SDK checkout unavailable')
    test.assertEqual(set(sources), set(ENGINE_FILES + SDK_FILES))
    return sources


def patched_sources(sources):
    patch = load_patch()
    result = dict(sources)
    for relative, unused in patch.FILES:
        result[relative] = patch.patch_source(relative, sources[relative])
    return result


def function(source, signature):
    return between(source, signature, '\n\t}\n')


def generated_source(template, sources, manager):
    script_hpp, script_cpp = sources['lib/pyscript/script.hpp'], sources['lib/pyscript/script.cpp']
    config, cache = sources['Src/Render/Render_GlyphCacheConfig.h'], sources['Src/Render/Render_GlyphCache.cpp']
    header, body = manager['lib/scaleform/manager.hpp'], manager['lib/scaleform/manager.cpp']
    bodies = [function(body, '\tManager::Manager() :'), function(body, '\tvoid Manager::draw()'),
              function(body, '\tvoid Manager::deleteUnmanagedObjects()'),
              function(body, '\tvoid Manager::createUnmanagedObjects()')]
    if 'void Manager::setGlyphCacheParams(' in body:
        bodies.append(function(body, '\tvoid Manager::setGlyphCacheParams('))
    replacements = {
        '@SCRIPT_BODIES@': between(script_cpp, 'int Script::setData( PyObject * pObject, uint & rUint,', '\n}\n') +
        between(script_cpp, 'PyObject * Script::argCountError(', '\n}\n'),
        '@PYAUTO_MACROS@': between(script_hpp, '// Section: Macros: Module Links', '// Section: Macros: Enums',
                                   False) +
        between(script_hpp, '// Section: Macros: Recursion utilities', '// Section: PyModuleLink', False),
        '@GLYPH_CONFIG@': between(config, 'struct GlyphCacheParams', '\n};\n', False) + '\n};\n' +
        between(config, 'class GlyphCacheConfig', '\n};\n'),
        '@GLYPH_BODIES@': between(cache, 'bool GlyphCache::SetParams(', '\n}\n') +
        between(cache, 'void GlyphCache::Initialize(HAL* ren', '\n}\n'),
        '@MANAGER_CLASS@': between(header, '\tclass Manager : public Singleton< Manager >', '\n\t}', ),
        '@MANAGER_BODIES@': '\n'.join(bodies),
    }
    text = (ROOT / 'tests' / template).read_text()
    for key, value in replacements.items():
        text = text.replace(key, value)
    return text


def build_host(test, directory, sources, patched):
    python = Path(os.environ.get('WOT_PYTHON27', '/Users/peng/.pyenv/versions/2.7.18/bin/python2.7'))
    config = python.with_name('python2.7-config')
    if not config.exists():
        test.skipTest('Pinned CPython 2.7 development files unavailable')
    for fake in ('pch.hpp', 'pyscript/script.hpp', 'scaleform/config.hpp', 'scaleform/manager.hpp'):
        (directory / 'include' / fake).parent.mkdir(parents=True, exist_ok=True)
        (directory / 'include' / fake).write_text('#pragma once\n')
    source = directory / 'host.cpp'
    manager = patched_sources(sources) if patched else sources
    if patched:
        spec = importlib.util.spec_from_file_location(
            'gui_profiler_patch', ROOT / 'bigworld/patch_stock_gui_profiler.py')
        profiler = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(profiler)
        manager['lib/scaleform/manager.cpp'] = profiler.patch_source(
            profiler.MANAGER, manager['lib/scaleform/manager.cpp'])
    source.write_text(generated_source('stock_glyph_cache_host.cpp', sources, manager))
    flags = [flag for flag in shlex.split(subprocess.check_output([str(config), '--cflags'], text=True))
             if flag != '-DNDEBUG']
    libs = shlex.split(subprocess.check_output([str(config), '--ldflags'], text=True))
    binary = directory / 'host'
    # The public constructor initialises its members out of declaration order.
    subprocess.run(['c++', '-std=c++14', '-UNDEBUG', *flags, '-Wall', '-Werror', '-Wno-deprecated-register',
                    '-Wno-writable-strings', '-Wno-unused-function', '-Wno-reorder-ctor',
                    '-DHOST_PATCHED=%d' % int(patched), '-I', str(directory / 'include'),
                    '-I', str(ROOT / 'bigworld'), str(source), *libs, '-o', str(binary)], check=True)
    return binary


def scripts_package(test):
    client = os.environ.get('WOT_0922_CLIENT')
    if not client:
        test.skipTest('WOT_0922_CLIENT is required for the original PYC call sites')
    return Path(client) / 'res/packages/scripts.pkg'


class StockGlyphCacheTest(unittest.TestCase):
    def test_patch_adds_only_the_pending_parameters_and_their_frame_consumer(self):
        patch = load_patch()
        sources = pinned_sources(self)
        for relative, edits in patch.FILES:
            pristine = sources[relative]
            patched = patch.patch_source(relative, pristine)
            self.assertEqual(patch.patch_source(relative, patched), patched)
            # Only insertions: every anchor survives and reverting each edit restores the original.
            diff = difflib.ndiff(pristine.splitlines(), patched.splitlines())
            self.assertFalse([line for line in diff if line.startswith('- ')], relative)
            reverted = patched
            for before, after in edits:
                self.assertTrue(after.startswith(before) or after.endswith(before), relative)
                reverted = reverted.replace(after, before, 1)
            self.assertEqual(reverted, pristine, relative)
        draw = function(patched_sources(sources)['lib/scaleform/manager.cpp'], '\tvoid Manager::draw()')
        # The consumer follows the renderer and device guards and precedes BeginFrame.
        self.assertLess(draw.index('if (!Moo::rc().checkDevice())'), draw.index('if (glyphCacheParamsPending_)'))
        self.assertLess(draw.index('SetParams( glyphCacheParams_ )'), draw.index('pRenderer_->BeginFrame();'))

    def test_patch_rejects_partial_duplicate_or_mixed_states_without_writing(self):
        patch = load_patch()
        sources = pinned_sources(self)
        with tempfile.TemporaryDirectory(prefix='glyph-cache-patch-') as directory:
            root = Path(directory)
            for relative, unused in patch.FILES:
                (root / relative).parent.mkdir(parents=True, exist_ok=True)
            final = patched_sources(sources)
            checked = 0
            for relative, edits in patch.FILES:
                pristine, patched = sources[relative], final[relative]
                mutations = {'pristine with one final edit %d' % i: pristine.replace(before, after, 1)
                             for i, (before, after) in enumerate(edits)}
                for index, (before, after) in enumerate(edits):
                    mutations['deleted anchor %d' % index] = pristine.replace(before, '', 1)
                    mutations['duplicated anchor %d' % index] = pristine + before
                    mutations['reverted final %d' % index] = patched.replace(after, before, 1)
                    mutations['duplicated final %d' % index] = patched + after
                    # A retained anchor repeated in place (a second BeginFrame) or elsewhere.
                    mutations['final with anchor %d repeated in place' % index] = patched.replace(
                        before, before + before, 1)
                    mutations['final with anchor %d appended' % index] = patched + before
                for token, unused in patch.TOKENS[relative]:
                    mutations['extra ' + token] = patched + '// ' + token + '\n'
                    mutations['pristine with ' + token] = pristine + '// ' + token + '\n'
                other = [name for name, unused in patch.FILES if name != relative][0]
                for name, source in mutations.items():
                    self.assertNotIn(source, (pristine, patched), name)
                    with self.assertRaises(ValueError, msg=name):
                        patch.patch_source(relative, source)
                    # A damaged file stops both writes, whether the other file is pristine or final.
                    for companion in (sources[other], final[other]):
                        (root / relative).write_text(source, encoding='utf-8')
                        (root / other).write_text(companion, encoding='utf-8')
                        with self.assertRaises(ValueError, msg=name):
                            patch.patch(root)
                        self.assertEqual((root / relative).read_text(encoding='utf-8'), source, name)
                        self.assertEqual((root / other).read_text(encoding='utf-8'), companion, name)
                        checked += 1
            # Every valid pair (each file pristine or final) reaches the final pair and stays there.
            for states in ((sources, sources), (final, final), (sources, final), (final, sources)):
                for (relative, unused), state in zip(patch.FILES, states):
                    (root / relative).write_text(state[relative], encoding='utf-8')
                patch.patch(root)
                for relative, unused in patch.FILES:
                    self.assertEqual((root / relative).read_text(encoding='utf-8'), final[relative])
                patch.patch(root)
                for relative, unused in patch.FILES:
                    self.assertEqual((root / relative).read_text(encoding='utf-8'), final[relative])
            print('Rejected glyph-cache patch states:', checked)

    def test_original_call_sites_reach_the_glyph_cache_through_the_real_binding(self):
        package = scripts_package(self)
        sources = pinned_sources(self)
        for patched, expected in ((False, 'pristine:'), (True, 'patched:')):
            with tempfile.TemporaryDirectory(prefix='glyph-cache-host-') as directory:
                binary = build_host(self, Path(directory), sources, patched)
                result = subprocess.run([str(binary), str(ROOT / 'tests/stock_glyph_cache_consumer.py'),
                                         'patched' if patched else 'pristine', str(package)],
                                        capture_output=True, text=True)
                self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
                self.assertIn(expected, result.stdout)
                print(result.stdout.strip())

    def test_original_instructions_match_production(self):
        client = os.environ.get('WOT_0922_CLIENT')
        if not client:
            self.skipTest('WOT_0922_CLIENT is required for the original instruction oracle')
        try:
            import stock_glyph_cache_oracle as oracle
        except ImportError:
            self.skipTest('pefile and Unicorn are required for the original instruction oracle')
        sources = pinned_sources(self)
        with tempfile.TemporaryDirectory(prefix='glyph-cache-oracle-') as directory:
            path = Path(directory)
            binary = build_host(self, path, sources, True)
            report = oracle.compare(Path(client) / 'WorldOfTanks.exe', binary,
                                    ROOT / 'tests/stock_glyph_cache_trace.py', path)
        print('Original glyph-cache setter instructions matched production:', report)


if __name__ == '__main__':
    unittest.main()
