"""AvatarFilter Vector3 sub-filter channels against the original #1513 code.

The host compiles the production channel collection; Unicorn runs the
original collection, channel, interpolation and latency instructions over the
same scripted connection. Every float and double is compared bit for bit.
"""
from pathlib import Path
import math
import os
import random
import struct
import subprocess
import sys
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(Path(__file__).resolve().parent))


def f32(value):
    return struct.unpack('<f', struct.pack('<f', value))[0]


def strip_includes(text):
    return '\n'.join(line for line in text.splitlines()
                     if not line.startswith('#include') and not line.startswith('#pragma once'))


def host_source():
    header = strip_includes((ROOT / 'bigworld/stock_avatar_vector_filter.hpp').read_text())
    source = (ROOT / 'bigworld/stock_avatar_vector_filter.cpp').read_text()
    source = strip_includes(source[:source.index('\nbool stockAvatarVectorLastMessageTime(')])
    text = (ROOT / 'tests/stock_avatar_vector_filter_host.cpp').read_text()
    return text.replace('// @PRODUCTION@', header + '\n' + source)


def build_host(folder):
    source = folder / 'host.cpp'
    source.write_text(host_source())
    binary = folder / 'host'
    subprocess.run(['c++', '-std=c++14', '-O1', '-ffp-contract=off', '-Wall', '-Werror',
                    '-Wno-unused-function', str(source), '-o', str(binary)], check=True)
    return binary


def hexf(value):
    return '%08x' % struct.unpack('<I', struct.pack('<f', value))[0]


def hexd(value):
    return '%016x' % struct.unpack('<Q', struct.pack('<d', value))[0]


def host_text(command):
    name = command[0]
    if name == 'connect':
        return 'connect %d %s %s' % (int(command[1]), hexd(command[2]), hexd(command[3]))
    if name == 'sync':
        return 'sync %d %s %s' % (command[1], ' '.join(hexf(v) for v in command[2]), hexd(command[3]))
    if name == 'get':
        return 'get %d %s' % (command[1], hexd(command[2]))
    if name == 'reset':
        return 'reset %d' % command[1]
    return 'interpolation %d %d' % (command[1], command[2])


def vector(r, scale):
    return tuple(f32(r.uniform(-scale, scale)) for unused in range(3))


def scenario(seed):
    """Producer-shaped and adversarial calls across channels and time."""
    r = random.Random(seed)
    commands = []
    # Without a connection no shared delay accrues, so these read exact boundaries:
    # a 0.01 s gap (below 0.010000000000000002) drops the earlier sample, angles
    # sit exactly at +-pi, a slerp lies just outside the opposite-yaw band, NaN times.
    pi = f32(math.pi)
    commands += [('connect', False, 0.05, 0.05), ('sync', 6, (1.0, 2.0, 3.0), 0.01),
                 ('sync', 6, (4.0, 5.0, 6.0), 0.02), ('get', 6, 0.015),
                 ('interpolation', 7, 2), ('sync', 7, (pi, -pi, pi), 0.5),
                 ('sync', 7, (-pi, pi, 0.0), 0.6), ('get', 7, 0.55),
                 ('interpolation', 8, 1), ('sync', 8, (0.0, 0.5, 1.0), 0.5),
                 ('sync', 8, (f32(math.sin(-2.69)), 0.2, f32(math.cos(-2.69))), 0.6),
                 ('get', 8, 0.58), ('sync', 8, (f32(math.sin(-2.79)), 0.2, f32(math.cos(-2.79))), 0.7),
                 ('get', 8, 0.68), ('get', 7, float('nan')), ('sync', 9, (1.0, 1.0, 1.0), float('nan')),
                 ('get', 9, 0.5)]
    commands.append(('connect', True, 100.0, 100.05))
    # The observer consumer: LINEAR on CAMERA_SHOT_POINT, reset, remote syncs, reads.
    commands += [('interpolation', 0, 0), ('reset', 0), ('get', 0, 100.0)]
    now = 100.0
    for frame in range(400):
        now += r.choice([0.005, 0.016, 0.033, 0.1, 1.2, 5.0])
        server = now + r.choice([0.0, 0.02, 0.3])
        connected = r.random() > 0.05
        commands.append(('connect', connected, now, server))
        channel = r.choice([0, 0, 0, 1, 2, 3, 9])
        kind = r.random()
        if kind < 0.45:
            # #1513 dereferences a missing connection for time 0; never script that.
            stamps = [now, now - r.uniform(0.0, 0.5), now + 0.004] + ([0.0] if connected else [])
            stamp = r.choice(stamps)
            scale = r.choice([1.0, 50.0, 7.0])
            commands.append(('sync', channel, vector(r, scale), stamp))
        elif kind < 0.85:
            commands.append(('get', channel, r.choice([server, now - 0.2, now + 1.0, 0.0])))
        elif kind < 0.92:
            commands.append(('interpolation', channel, r.choice([0, 1, 2, 3, 7, -1])))
        else:
            commands.append(('reset', channel))
    # Identifiers outside 0..9 never create channels; get yields the zero vector.
    commands += [('sync', 10, (1.0, 2.0, 3.0), 200.0), ('get', 10, 200.0), ('reset', -1),
                 ('interpolation', 12, 1), ('get', -3, 0.0)]
    # Angles wrap across pi and the slerp's opposite-yaw fallback.
    for kind in (1, 2, 3):
        commands.append(('interpolation', 4, kind))
        base = 300.0 + kind
        for step, value in enumerate([(3.0, -3.0, 2.9), (-3.0, 3.1, -2.9), (10.0, -10.0, 0.5),
                                      (-1.0, 0.2, -1.0), (1.0, 0.25, 1.0), (0.0, 0.0, 0.0)]):
            commands += [('connect', True, base + step, base + step),
                         ('sync', 4, tuple(f32(v) for v in value), base + step * 0.2)]
            commands.append(('get', 4, base + step * 0.2 + 0.1))
    # More than 24 samples keep the newest eight.
    commands.append(('connect', True, 500.0, 500.0))
    for step in range(30):
        commands.append(('sync', 5, (float(step), 0.0, 0.0), 500.0 + step * 0.05))
    commands.append(('get', 5, 501.0))
    return commands


class AvatarVectorFilterOracleTests(unittest.TestCase):
    def test_channels_match_original_instructions(self):
        client = os.environ.get('WOT_0922_CLIENT')
        if not client:
            self.skipTest('WOT_0922_CLIENT is required for the original instruction oracle')
        try:
            import stock_avatar_vector_filter_oracle as oracle
        except ImportError:
            self.skipTest('pefile and Unicorn are required for the original instruction oracle')
        commands = scenario(1513) + scenario(1514)
        with tempfile.TemporaryDirectory(prefix='avatar-vectors-') as folder:
            binary = build_host(Path(folder))
            text = '\n'.join(host_text(c) for c in commands) + '\n'
            produced = subprocess.run([str(binary)], input=text, capture_output=True, text=True,
                                      check=True).stdout.splitlines()
        reached = set()
        original = oracle.Scenario(Path(client) / 'WorldOfTanks.exe', reached).run(commands)
        self.assertEqual(len(produced), len(original))
        for index, (mine, theirs) in enumerate(zip(produced, original)):
            self.assertEqual(mine, theirs, 'command %d %r' % (index, commands[index]))
        self.assertEqual(sorted(n for a, n in oracle.BRANCHES.items() if a not in reached), [])
        gets = sum(1 for c in commands if c[0] == 'get')
        print('Vector3 channels matched the original x86: %d commands, %d reads, %d branches'
              % (len(commands), gets, len(oracle.BRANCHES)))


if __name__ == '__main__':
    unittest.main()


def binding_source(test, directory):
    import shlex
    import importlib.util
    import test_stock_math_oscillators as oscillators
    sources = oscillators.engine_sources(test)
    script_hpp, script_cpp = sources['lib/pyscript/script.hpp'], sources['lib/pyscript/script.cpp']
    bodies = ''.join(oscillators.between(script_cpp, 'int Script::setData( PyObject * pObject, ' + name, '\n}\n')
                     for name in ('int & rInt,', 'double & rDouble,'))
    bodies += oscillators.between(script_cpp, 'PyObject * Script::argCountError(', '\n}\n')
    # The declarations come from the patcher itself, applied to its anchors.
    spec = importlib.util.spec_from_file_location('vector_patch', ROOT / 'bigworld/patch_stock_avatar_filter_vectors.py')
    patcher = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(patcher)
    engine = directory / 'engine/client'
    engine.mkdir(parents=True)
    (engine / 'avatar_filter.hpp').write_text('#include "network/basictypes.hpp"\n'
                                             '\tvoid addCallback( AvatarFilterCallback * pCallback );\n'
                                             '\tListNode callbackListHead_;\n')
    (engine / 'py_avatar_filter.hpp').write_text('\tPY_METHOD_DECLARE( py_debugMatrixes );\n')
    (engine / 'py_avatar_filter.cpp').write_text('\tPY_METHOD( debugMatrixes )\nPY_END_METHODS()')
    patcher.patch_engine(directory / 'engine')
    declarations = (engine / 'py_avatar_filter.hpp').read_text().split('\n', 1)[1]
    methods = (engine / 'py_avatar_filter.cpp').read_text()
    assert methods.index('syncVector3') < methods.index('getVector3') < methods.index('resetVector3') < \
        methods.index('setInterpolationType')
    production = (ROOT / 'bigworld/stock_avatar_vector_filter.cpp').read_text()
    channels = strip_includes((ROOT / 'bigworld/stock_avatar_vector_filter.hpp').read_text()) + '\n' + \
        strip_includes(production[:production.index('\nbool stockAvatarVectorLastMessageTime(')])
    channels = channels.replace('BW_BEGIN_NAMESPACE', '').replace('BW_END_NAMESPACE', '')
    python = production[production.index('namespace\n{\n// #1513 logs MF_ASSERT_DEV'):production.rindex('BW_END_NAMESPACE')]
    text = (ROOT / 'tests/stock_avatar_vector_filter_host_py.cpp').read_text()
    for key, value in (('@SCRIPT_BODIES@', bodies),
                       ('@PYAUTO_MACROS@', oscillators.between(script_hpp, '// Section: Macros: Module Links',
                                                               '// Section: Macros: Enums', False) +
                        oscillators.between(script_hpp, '// Section: Macros: Recursion utilities',
                                            '// Section: PyModuleLink', False)),
                       ('@PYOBJECT_PLUS_MACROS@', oscillators.macro(sources['lib/pyscript/pyobject_plus.hpp'],
                                                                    'PY_AUTO_METHOD_DECLARE')),
                       ('// @CHANNELS@', channels), ('// @DECLARATIONS@', declarations),
                       ('// @METHODS@', python)):
        text = text.replace(key, value)
    return text


class AvatarVectorFilterBindingTests(unittest.TestCase):
    def test_original_observer_consumers_over_the_bindings(self):
        import shlex
        client = os.environ.get('WOT_0922_CLIENT')
        if not client:
            self.skipTest('WOT_0922_CLIENT is required for the original PYC consumers')
        python = Path(os.environ.get('WOT_PYTHON27', '/Users/peng/.pyenv/versions/2.7.18/bin/python2.7'))
        config = python.with_name('python2.7-config')
        if not config.exists():
            self.skipTest('Pinned CPython 2.7 development files unavailable')
        with tempfile.TemporaryDirectory(prefix='avatar-vectors-py-') as folder:
            directory = Path(folder)
            source = directory / 'host.cpp'
            source.write_text(binding_source(self, directory))
            flags = [flag for flag in shlex.split(subprocess.check_output([str(config), '--cflags'], text=True))
                     if flag != '-DNDEBUG' and not flag.startswith('-O')]
            libs = shlex.split(subprocess.check_output([str(config), '--ldflags'], text=True))
            binary = directory / 'host'
            subprocess.run(['c++', '-std=c++14', '-O0', '-ffp-contract=off', '-UNDEBUG', *flags, '-Wall', '-Werror',
                            '-Wno-deprecated-register', '-Wno-writable-strings', '-Wno-unused-function',
                            '-Wno-unused-variable', str(source), *libs, '-o', str(binary)], check=True)
            result = subprocess.run([str(binary), str(ROOT / 'tests/stock_avatar_vector_filter_consumer.py'),
                                     str(Path(client) / 'res/packages/scripts.pkg')],
                                    capture_output=True, text=True)
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        print(result.stdout.strip())
