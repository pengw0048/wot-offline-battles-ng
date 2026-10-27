"""Math.PyOscillator, PyCompoundOscillator and the random-noise oscillators.

Real CPython 2.7 builds the engine's PY_AUTO, factory and accessor macros with
the production binding and runs the original DynamicCameras, cameras,
mathUtils and ArcadeCamera.__updateOscillators PYC over the exact
gui/avatar_input_handler.xml sections. Unicorn runs the exact #1513 factories,
methods, attributes and virtual laws over the same operations; every read
must match bit for bit (any NaN counts as NaN).
"""
from pathlib import Path
import json
import math
import os
import re
import shlex
import struct
import subprocess
import sys
import tarfile
import tempfile
import unittest
import zipfile

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(Path(__file__).resolve().parent))
sys.path.insert(0, str(ROOT.parents[1] / 'tools'))
ENGINE_FILES = ('lib/pyscript/script.hpp', 'lib/pyscript/script.cpp', 'lib/pyscript/pyobject_plus.hpp')
SCRIPT_BODIES = ('float & rFloat,', 'double & rDouble,')
ENGINE_MACROS = ('PY_AUTO_METHOD_DECLARE', 'PY_FACTORY_DECLARE', 'PY_AUTO_FACTORY_DECLARE',
                 'PY_FACTORY_METHOD_LINK_DECLARE', 'PY_READABLE_ATTRIBUTE_GET',
                 'PY_RW_ACCESSOR_ATTRIBUTE_DECLARE', 'PY_WRITABLE_ACCESSOR_ATTRIBUTE_SET', 'PY_ATTR_SCOPE',
                 'PY_BEGIN_METHODS', 'PY_METHOD', 'PY_END_METHODS', 'PY_ATTRIBUTE_CREATE_WRAPPER',
                 'PY_ATTRIBUTE', 'PY_END_ATTRIBUTES', 'PY_FACTORY', 'PY_FACTORY_NAMED',
                 'PY_CLASS_TOKEN_BASE', 'PY_CLASS_TOKEN')
ATTRIBUTES = ('deviation', 'velocity', 'externalForce', 'constraints')
# Exact consumers: ArcadeCamera passes False for impulse/movement, Strategic and
# Arty for both of theirs; SniperCamera and every noise section use radians.
SECTIONS = {'arcadeMode': {'impulseOscillator': False, 'movementOscillator': False,
                           'randomNoiseOscillatorSpherical': True},
            'postMortemMode': {'impulseOscillator': False, 'movementOscillator': False,
                               'randomNoiseOscillatorSpherical': True},
            'sniperMode': {'impulseOscillator': True, 'movementOscillator': True,
                           'randomNoiseOscillatorSpherical': True},
            'strategicMode': {'oscillator': False, 'randomNoiseOscillatorFlat': False},
            'artyMode': {'oscillator': False, 'randomNoiseOscillatorFlat': False}}


def between(text, begin, end, include_end=True):
    start = text.rindex('\n', 0, text.index(begin)) + 1
    stop = text.index(end, start)
    return text[start:stop + (len(end) if include_end else 0)]


def macro(text, name):
    match = re.search(r'^#define %s\b' % re.escape(name), text, re.M)
    lines = []
    for line in text[match.start():].split('\n'):
        lines.append(line)
        if not line.rstrip().endswith('\\'):
            break
    return '\n'.join(lines) + '\n'


def engine_sources(test):
    archive = Path(os.environ.get('WOT_PUBLIC_ENGINE_ARCHIVE', '/tmp/wot-bigworld-engine-14.4.1.tar.gz'))
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
    test.assertEqual(set(sources), set(ENGINE_FILES))
    return sources


def generated_source(sources):
    script_hpp, script_cpp = sources['lib/pyscript/script.hpp'], sources['lib/pyscript/script.cpp']
    plus = sources['lib/pyscript/pyobject_plus.hpp']
    bodies = ''.join(between(script_cpp, 'int Script::setData( PyObject * pObject, ' + name, '\n}\n')
                     for name in SCRIPT_BODIES)
    replacements = {
        '@SCRIPT_BODIES@': bodies + between(script_cpp, 'PyObject * Script::argCountError(', '\n}\n'),
        '@PYAUTO_MACROS@': between(script_hpp, '// Section: Macros: Module Links', '// Section: Macros: Enums',
                                   False) +
        between(script_hpp, '// Section: Macros: Recursion utilities', '// Section: PyModuleLink', False),
        '@PYOBJECT_PLUS_MACROS@': ''.join(macro(plus, name) for name in ENGINE_MACROS),
    }
    text = (ROOT / 'tests/stock_math_oscillators_host.cpp').read_text()
    for key, value in replacements.items():
        text = text.replace(key, value)
    return text


def build_host(test, directory, sources):
    python = Path(os.environ.get('WOT_PYTHON27', '/Users/peng/.pyenv/versions/2.7.18/bin/python2.7'))
    config = python.with_name('python2.7-config')
    if not config.exists():
        test.skipTest('Pinned CPython 2.7 development files unavailable')
    for fake in ('pch.hpp', 'math/vector3.hpp', 'pyscript/script.hpp', 'pyscript/pyobject_plus.hpp'):
        (directory / 'include' / fake).parent.mkdir(parents=True, exist_ok=True)
        (directory / 'include' / fake).write_text('#pragma once\n')
    source = directory / 'host.cpp'
    source.write_text(generated_source(sources))
    flags = [flag for flag in shlex.split(subprocess.check_output([str(config), '--cflags'], text=True))
             if flag not in ('-DNDEBUG',) and not flag.startswith('-O')]
    libs = shlex.split(subprocess.check_output([str(config), '--ldflags'], text=True))
    binary = directory / 'host'
    # Separate float32 operations and libm calls, as the SSE2 original performs them.
    subprocess.run(['c++', '-std=c++14', '-O0', '-ffp-contract=off', '-fno-fast-math', '-UNDEBUG', *flags,
                    '-Wall', '-Werror', '-Wno-deprecated-register', '-Wno-writable-strings',
                    '-Wno-unused-function', '-Wno-unused-variable', '-I', str(directory / 'include'),
                    '-I', str(ROOT / 'bigworld'), str(source), *libs, '-o', str(binary)], check=True)
    return binary


def client(test):
    path = os.environ.get('WOT_0922_CLIENT')
    if not path:
        test.skipTest('WOT_0922_CLIENT is required for the original PYC and x86 contract')
    return Path(path)


def oracle_module(test):
    try:
        import stock_math_oscillators_oracle as oracle
    except ImportError:
        test.skipTest('pefile and Unicorn are required for the original instruction oracle')
    return oracle


def comparable(value):
    if isinstance(value, list) and value and value[0] == 'get':
        return value[:3] + [['nan' if (b & 0x7f800000) == 0x7f800000 and b & 0x7fffff else b for b in value[3]]]
    return value


def gets(operation, attributes=ATTRIBUTES):
    return [['get', operation[1], name] for name in attributes]


def law_scenario():
    """Varied legal and boundary operations over every selected type."""
    radians = [math.radians(10.0)] * 3
    vector = lambda *v: [float(x) for x in v]
    operations = []

    def add(operation, read=True):
        operations.append(operation)
        if read and operation[0] != 'new':
            operations.extend(gets(operation))
        elif operation[0] == 'new':
            operations.extend(gets(['get', operation[1]]))

    simple = {'arcadeImpulse': [1.0, vector(90, 90, 90), vector(12, 12, 12), vector(10, 0, 10)],
              'arcadeMovement': [1.2, vector(125, 125, 125), vector(19, 19, 19), vector(40, 40, 40)],
              'sniperImpulse': [1.5, vector(90, 90, 90), vector(12, 12, 12), radians],
              'sniperAim': [1.0, vector(0, 0, 15), vector(0, 0, 3.5), vector(0.0, 0.0, 0.0)],
              'returning': [1.0, vector(0, 15, 15), vector(0, 3.5, 3.5), vector(0, 0.2, 0)],
              'negative': [0.5, vector(3, 30, 300), vector(1, 0.5, 60), vector(-1, 2, 0.001)]}
    steps = [0.016, 0.0, -0.01, 0.033, 0.05, 0.051, 0.1, 0.5, 1e-6, 0.0166, 0.0167, float('nan'), 0.008]
    for ident, args in simple.items():
        add(['new', ident, 'PyOscillator', args])
        add(['impulse', ident, vector(3, -4, 5)])
        for index in range(60):
            add(['update', ident, steps[index % len(steps)]])
            if index == 10:
                add(['set', ident, 'externalForce', vector(7, -2, 0.5)])
            if index == 20:
                add(['set', ident, 'deviation', vector(50, -50, 0.0001)])
            if index == 30:
                add(['set', ident, 'velocity', vector(-20, 3, 0)])
                add(['set', ident, 'constraints', vector(0.5, 0.5, 0.5)])
            if index == 40:
                add(['impulse', ident, vector(0.0, 100.0, -0.25)])
            if index == 50:
                add(['reset', ident])
                add(['impulse', ident, vector(1, 1, 1)])

    add(['new', 'inner', 'PyOscillator', [1.0, vector(50, 50, 50), vector(20, 20, 20), vector(0.01, 0, 0.01)]])
    add(['new', 'compound', 'PyCompoundOscillator', [{'ref': 'arcadeMovement'}, {'ref': 'inner'}]])
    add(['new', 'nested', 'PyCompoundOscillator', [{'ref': 'compound'}, {'ref': 'returning'}]])
    for ident in ('compound', 'nested'):
        add(['set', ident, 'externalForce', vector(30, -10, 5)])
        add(['set', ident, 'deviation', vector(0.5, 0.25, -0.5)])
        add(['set', ident, 'constraints', vector(9, 9, 9)])
        add(['set', ident, 'velocity', vector(1, 2, 3)])
        for index in range(40):
            add(['update', ident, steps[index % len(steps)]])
            if index == 15:
                add(['impulse', ident, vector(-6, 2, 1)])
            if index == 30:
                add(['reset', ident])

    noise = {'flat': ['PyRandomNoiseOscillatorFlat', [1.0, 8600.0, 33.0]],
             'flatThreshold': ['PyRandomNoiseOscillatorFlat', [1.0, 8600.0, 33.0, 0.5]],
             'spherical': ['PyRandomNoiseOscillatorSpherical', [0.7, 8600.0, 33.0, vector(1, 1, 0.4)]],
             'sphericalDefault': ['PyRandomNoiseOscillatorSpherical', [0.8, 8600.0, 33.0]],
             'sphericalThreshold': ['PyRandomNoiseOscillatorSpherical', [0.8, 8600.0, 33.0, vector(2, 0.5, 1), 0.0]],
             'overdamped': ['PyRandomNoiseOscillatorFlat', [1.0, 1.0, 33.0]]}
    noise_steps = [0.016, 0.0, 0.004, 0.033, 0.0021, 0.5, 0.0005, 0.016, -0.003, 0.07, 2.0, 0.016]
    for ident, (kind, args) in noise.items():
        add(['new', ident, kind, args])
        add(['impulse', ident, vector(0.3, 0.2, 0.1)])
        for index in range(150):
            add(['update', ident, noise_steps[index % len(noise_steps)]])
            if index % 37 == 5:
                add(['impulse', ident, vector(index * 0.01, -0.5, 0.25)])
            if index == 70:
                add(['set', ident, 'externalForce', vector(1, 2, 3)])
                add(['set', ident, 'constraints', vector(4, 5, 6)])
                add(['set', ident, 'velocity', vector(-1, 0, 1)])
            if index == 100:
                add(['reset', ident])
            if index == 110:
                add(['set', ident, 'deviation', vector(0.001, 0.0, 0.0)])
                add(['impulse', ident, vector(2.0, 0.0, 0.0)])
    add(['new', 'noiseFirst', 'PyCompoundOscillator', [{'ref': 'flat'}, {'ref': 'sniperAim'}]])
    for index in range(30):
        add(['update', 'noiseFirst', noise_steps[index % len(noise_steps)]])
        if index == 3:
            add(['impulse', 'noiseFirst', vector(0.5, 0.5, 0.5)])
    return operations


def exact_sections(path):
    import packed_xml
    archive = zipfile.ZipFile(path / 'res/packages/gui.pkg')
    root = packed_xml.read_packed_xml(archive.read('gui/avatar_input_handler.xml'))

    def child(element, name):
        for key, value in element.children:
            if (key.decode() if isinstance(key, bytes) else key) == name:
                return value.value if value.value_type == packed_xml.TYPE_ELEMENT else value
        raise KeyError(name)

    sections = {}
    for mode, oscillators in SECTIONS.items():
        dynamics = child(child(child(root, mode), 'camera'), 'dynamics')
        for name, as_angle in oscillators.items():
            values = {}
            for key, value in child(dynamics, name).children:
                raw = value.value.value if value.value_type == packed_xml.TYPE_ELEMENT else value.value
                values[key.decode()] = raw.decode('latin1').strip()
            sections['/%s/camera/dynamics/%s' % (mode, name)] = [name, values, as_angle]
    return sections


class StockMathOscillatorsTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.directory = tempfile.TemporaryDirectory(prefix='oscillators-cpython-')
        cls.binary = None

    @classmethod
    def tearDownClass(cls):
        cls.directory.cleanup()

    def host(self, mode, *inputs):
        if StockMathOscillatorsTest.binary is None:
            StockMathOscillatorsTest.binary = build_host(self, Path(self.directory.name), engine_sources(self))
        output = Path(self.directory.name) / ('%s.json' % mode)
        paths = []
        for index, value in enumerate(inputs):
            if isinstance(value, Path):
                paths.append(str(value))
                continue
            path = Path(self.directory.name) / ('%s-input-%d.json' % (mode, index))
            path.write_text(json.dumps(value))
            paths.append(str(path))
        result = subprocess.run([str(self.binary), str(ROOT / 'tests/stock_math_oscillators_consumer.py'),
                                 mode, *paths, str(output)], capture_output=True, text=True)
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        return json.loads(output.read_text())

    def test_laws_match_original_x86(self):
        path, oracle = client(self), oracle_module(self)
        operations = law_scenario()
        production = self.host('scenario', operations)['result']
        original = oracle.run_scenario(path / 'WorldOfTanks.exe', operations)
        self.assertEqual(len(production), len(original))
        for index, (mine, theirs) in enumerate(zip(production, original)):
            self.assertEqual(comparable(mine), comparable(theirs), 'read %d' % index)
        print('Oscillator laws matched the original x86 over %d operations and %d reads' %
              (len(operations), len(original)))

    def test_binding_contract_matches_original(self):
        path, oracle = client(self), oracle_module(self)
        original = oracle.run_contract(path / 'WorldOfTanks.exe')
        cases = oracle.contract_cases()
        arg_counts = [next((c for c in calls if c[0] == 'argCountError'), ['argCountError', 'none', 0, 0])
                      for _, _, _, _, _, calls in original[:len(cases)]]
        host = self.host('contract', {'cases': cases, 'argCountErrors': arg_counts})['result']
        report, expected = host['report'], host['expected']
        for index, case in enumerate(cases):
            mine, theirs = report[index], original[index]
            self.assertEqual(mine[:5], theirs[:5], case)
            calls = theirs[5]
            if calls and calls[-1][0] == 'argCountError':
                self.assertEqual(mine[5], expected[index], case)
            elif mine[3] is not None:
                kind, name = calls[-1][1], calls[-1][2]
                self.assertEqual(calls[-1][0], 'setData', case)
                suffix = 'a float' if kind == 'float' else 'a Vector3 or a tuple of 3 floats'
                self.assertEqual(mine[5], 'TypeError: %s must be set to %s' % (name, suffix), case)
        for mine, theirs in zip(report[len(cases):len(cases) + 4], original[len(cases):]):
            self.assertEqual(mine[:5], theirs[:5])
            self.assertEqual(theirs[5], [['setData', 'Vector3', mine[1]]])
            self.assertEqual(mine[5], 'TypeError: %s must be set to a Vector3 or a tuple of 3 floats' % mine[1])
        self.assertEqual(report[-2:], [['compound-type', '() argument 1 must be set to a PyOscillator'],
                                       ['compound-type', '() argument 2 must be set to a PyOscillator']])
        print('Oscillator bindings matched the original argument contract: %d cases' % len(original))

    def test_original_pyc_consumers_replay_on_original_x86(self):
        path, oracle = client(self), oracle_module(self)
        sections = exact_sections(path)
        result = self.host('consumers', path / 'res/packages/scripts.pkg', sections)['result']
        kinds = result['kinds']
        for name, kind in kinds.items():
            self.assertEqual(kind, {'randomNoiseOscillatorFlat': 'PyRandomNoiseOscillatorFlat',
                                    'randomNoiseOscillatorSpherical': 'PyRandomNoiseOscillatorSpherical'}.get(
                                        name.split('/')[-1], 'PyOscillator'), name)
        trace = result['trace']
        replay = oracle.run_scenario(path / 'WorldOfTanks.exe', [t[:3] if t[0] == 'get' else t for t in trace])
        production = [t for t in trace if t[0] in ('get', 'new')]
        production = [t if t[0] == 'get' else ['new', t[1], True] for t in production]
        self.assertEqual([comparable(t) for t in production], [comparable(t) for t in replay])
        moving = [frame for frame in result['arcade'] if any(frame[0]) or any(frame[1])]
        self.assertGreater(len(moving), 100)
        print('Original camera PYC drove %d oscillators; %d traced reads replayed on the x86' %
              (len(kinds), len([t for t in trace if t[0] == 'get'])))


if __name__ == '__main__':
    unittest.main()
