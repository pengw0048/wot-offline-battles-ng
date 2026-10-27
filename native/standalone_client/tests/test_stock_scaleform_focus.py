"""PyMovieView.setFocussed and setattr: the #1513 contracts on the public engine.

Real CPython 2.7 runs the engine's PY_AUTO argument macros and the extracted
pristine or patched bodies over recording GFx fakes. It proves the Python ABI,
the focus owner's event order and attribute routing, not Windows input focus.
"""
from pathlib import Path
import importlib.util
import os
import shlex
import subprocess
import tarfile
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
FILES = ('lib/scaleform/ime.hpp', 'lib/scaleform/ime.cpp', 'lib/scaleform/py_movie_view.hpp',
         'lib/scaleform/py_movie_view.cpp', 'lib/pyscript/script.hpp', 'lib/pyscript/script.cpp',
         'lib/pyscript/pyobject_plus.hpp')


def load_patch():
    spec = importlib.util.spec_from_file_location('patch_scaleform_sdk', ROOT / 'bigworld/patch_scaleform_sdk.py')
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


def between(text, begin, end, include_end=True):
    start = text.rindex('\n', 0, text.index(begin)) + 1
    stop = text.index(end, start)
    return text[start:stop + (len(end) if include_end else 0)]


class ScaleformFocusTest(unittest.TestCase):
    def setUp(self):
        archive = Path(os.environ.get('WOT_PUBLIC_ENGINE_ARCHIVE', '/tmp/wot-bigworld-engine-14.4.1.tar.gz'))
        python = Path(os.environ.get('WOT_PYTHON27', '/Users/peng/.pyenv/versions/2.7.18/bin/python2.7'))
        self.config = python.with_name('python2.7-config')
        if not archive.is_file() or not self.config.exists():
            self.skipTest('Pinned public engine source and CPython 2.7 development files required')
        self.sources = {}
        with tarfile.open(archive) as tar:
            for member in tar:
                for relative in FILES:
                    if member.name.endswith('/programming/bigworld/' + relative):
                        self.sources[relative] = tar.extractfile(member).read().decode('utf-8')
                if len(self.sources) == len(FILES):
                    break
        self.assertEqual(set(self.sources), set(FILES))

    def patched(self, directory):
        patch = load_patch()
        for relative in FILES:
            (directory / relative).parent.mkdir(parents=True, exist_ok=True)
            (directory / relative).write_text(self.sources[relative])

        def edit(path, before, after, label):
            path.write_text(patch.replace_once(path.read_text(), before, after, label))

        patch.movie_view_edits(directory, edit)
        generated = {relative: (directory / relative).read_text() for relative in FILES}
        patch.movie_view_edits(directory, edit)
        self.assertEqual(generated, {relative: (directory / relative).read_text() for relative in FILES})
        return generated

    def run_host(self, directory, sources, mode):
        script_hpp, script_cpp = sources['lib/pyscript/script.hpp'], sources['lib/pyscript/script.cpp']
        header, view = sources['lib/scaleform/py_movie_view.hpp'], sources['lib/scaleform/py_movie_view.cpp']
        replacements = {
            '@SCRIPT_BODIES@': between(script_cpp, 'int Script::setData( PyObject * pObject, BW::string & rString,',
                                       '\n}\n') +
            between(script_cpp, 'PyObject * Script::argCountError(', '\n}\n'),
            '@PYAUTO_MACROS@': between(script_hpp, '// Section: Macros: Recursion utilities',
                                       '// Section: PyModuleLink', False) +
            between(sources['lib/pyscript/pyobject_plus.hpp'], '#define PY_AUTO_METHOD_DECLARE( RET, NAME, ARGS )',
                    '\n\n'),
            '@DECLARE@': between(header, '\t\tvoid setFocussed(', '\n\n'),
            '@TEARDOWN@': between(view, '\t\tManager::instance().removeMovieView(this);',
                                  '\t\t// Release PyObjects.', False),
            '@SETFOCUSSED@': between(view, 'void PyMovieView::setFocussed(', '\n\t}\n'),
            '@SETATTR@': between(view, 'bool PyMovieView::pySetAttribute(', '\n\t}\n'),
            '@IME@': '',
        }
        if mode == 'patched':
            ime = sources['lib/scaleform/ime.cpp']
            replacements['@IME@'] = (between(ime, 'PyMovieView * IME::s_pFocussed = NULL;', '\n') +
                                     between(ime, '\t// 00aa47a0', '}\t//namespace ScaleformBW', False))
        host = (ROOT / 'tests/stock_scaleform_focus_host.cpp').read_text()
        for key, value in replacements.items():
            host = host.replace(key, value)
        source = directory / (mode + '.cpp')
        source.write_text(host)
        for fake in ('lib/scaleform/config.hpp', 'include/cstdmf/bw_string.hpp'):
            (directory / fake).parent.mkdir(parents=True, exist_ok=True)
            (directory / fake).write_text('#pragma once\n')
        flags = [flag for flag in shlex.split(subprocess.check_output([str(self.config), '--cflags'], text=True))
                 if flag != '-DNDEBUG']
        libs = shlex.split(subprocess.check_output([str(self.config), '--ldflags'], text=True))
        binary = directory / mode
        subprocess.run(['c++', '-std=c++14', '-UNDEBUG', '-Wno-deprecated-register', '-Wno-writable-strings',
                        '-DHOST_PATCHED=%d' % (mode == 'patched'), '-I', str(directory),
                        '-I', str(directory / 'include'), str(source), *flags, *libs, '-o', str(binary)],
                       check=True)
        result = subprocess.run([str(binary), str(ROOT / 'tests/stock_scaleform_focus_consumer.py'), mode],
                                capture_output=True, text=True)
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        return result.stdout.strip()

    def test_pristine_rejects_the_lobby_call_and_patched_restores_the_contract(self):
        with tempfile.TemporaryDirectory(prefix='scaleform-focus-pristine-') as directory:
            output = self.run_host(Path(directory), self.sources, 'pristine')
            self.assertIn('setFocussed takes no argument', output)
        with tempfile.TemporaryDirectory(prefix='scaleform-focus-patched-') as directory:
            output = self.run_host(Path(directory), self.patched(Path(directory)), 'patched')
            self.assertIn('native-first setattr', output)

    def test_patch_fails_closed_on_an_unknown_seam(self):
        patch = load_patch()
        with tempfile.TemporaryDirectory(prefix='scaleform-focus-seam-') as directory:
            root = Path(directory)
            for relative in FILES:
                (root / relative).parent.mkdir(parents=True, exist_ok=True)
                (root / relative).write_text(self.sources[relative])
            view = root / 'lib/scaleform/py_movie_view.cpp'
            view.write_text(view.read_text().replace('IME::setFocussedMovie(this);', 'IME::setFocussedMovie( this );'))
            changes = {}

            def edit(path, before, after, label):
                changes[path] = patch.replace_once(changes.get(path, path.read_text()), before, after, label)

            with self.assertRaises(ValueError):
                patch.movie_view_edits(root, edit)


if __name__ == '__main__':
    unittest.main()
