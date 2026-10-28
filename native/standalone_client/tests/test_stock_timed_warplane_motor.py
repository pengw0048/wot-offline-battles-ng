"""BigWorld.PyTimedWarplaneMotor: production kernel against exact #1513 x86.

D3DX/UCRT are host references on both sides, so these tests prove the
original trajectory arithmetic and state contract, not rendered flight.
"""
from pathlib import Path
import os
import subprocess
import sys
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(Path(__file__).resolve().parent))
CXX = os.environ.get('CXX', 'clang++')
FLAGS = ['-std=c++14', '-Wall', '-Wextra', '-Werror', '-ffp-contract=off', '-O1',
         '-I', str(ROOT / 'bigworld'), '-I', str(ROOT / 'tests')]
EXPORTS = r'''
#include "stock_timed_warplane_motor_reference.hpp"
#define X extern "C" __attribute__((visibility("default")))
X void xLookAtLH(float* o, const float* e, const float* a, const float* u) { refLookAtLH(o, e, a, u); }
X void xInverse(float* m) { refInverse(m); }
X void xNormalize2(float* o, const float* v) { refNormalize2(o, v); }
X void xRotationZ(float* o, float a) { refRotationZ(o, a); }
X void xMultiply(float* o, const float* a, const float* b) { refMultiply(o, a, b); }
X float xAtan(float x) { return refAtan(x); }
'''


def client():
    return os.environ.get('WOT_0922_CLIENT')


FAKE_HEADERS = ('pch.hpp', 'cstdmf/bw_namespace.hpp', 'pyscript/script.hpp', 'pyscript/script_math.hpp',
                'app.hpp', 'duplo/motor.hpp', 'duplo/pymodel.hpp')


class StockTimedWarplaneMotorHostTest(unittest.TestCase):
    def test_real_cpython_flock_exotic_consumer(self):
        python = Path(os.environ.get('WOT_PYTHON27', '/Users/peng/.pyenv/versions/2.7.18/bin/python2.7'))
        config = python.with_name('python2.7-config')
        if not config.exists():
            self.skipTest('Pinned CPython 2.7 development files unavailable')
        import shlex
        import zipfile
        with tempfile.TemporaryDirectory(prefix='timed-warplane-cpython-') as directory:
            path = Path(directory)
            for header in FAKE_HEADERS:
                (path / 'include' / header).parent.mkdir(parents=True, exist_ok=True)
                (path / 'include' / header).write_text('#pragma once\n')
            flags = [f for f in shlex.split(subprocess.check_output([str(config), '--cflags'], text=True))
                     if f != '-DNDEBUG']
            libs = shlex.split(subprocess.check_output([str(config), '--ldflags'], text=True))
            subprocess.run(['c++', '-std=c++14', '-Wno-deprecated-register', '-Wno-writable-strings',
                            '-UNDEBUG', '-ffp-contract=off', '-I', str(path / 'include'),
                            '-I', str(ROOT / 'bigworld'), '-I', str(ROOT / 'tests'),
                            str(ROOT / 'tests/stock_timed_warplane_motor_host.cpp'),
                            *flags, *libs, '-o', str(path / 'host')], check=True)
            environment = dict(os.environ)
            if client():
                with zipfile.ZipFile(Path(client()) / 'res/packages/scripts.pkg') as archive:
                    target = path / 'FlockExotic.pyc'
                    target.write_bytes(archive.read('scripts/client/FlockExotic.pyc'))
                    environment['STOCK_FLOCK_EXOTIC_PYC'] = str(target)
            result = subprocess.run([str(path / 'host'), str(ROOT / 'tests/stock_timed_warplane_motor_consumer.py')],
                                    env=environment, capture_output=True, text=True)
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
            # The None callback is reported through the Python error printer, not raised.
            self.assertIn('Function is not callable', result.stderr)


class StockTimedWarplaneMotorOracleTest(unittest.TestCase):
    def test_original_trajectory_matches_production(self):
        if not client():
            self.skipTest('WOT_0922_CLIENT is required for the original instruction oracle')
        try:
            import stock_timed_warplane_motor_oracle as oracle
        except ImportError:
            self.skipTest('pefile and Unicorn are required for the original instruction oracle')
        base = oracle.base
        with tempfile.TemporaryDirectory(prefix='timed-warplane-oracle-') as directory:
            path = Path(directory)
            (path / 'exports.cpp').write_text(EXPORTS)
            subprocess.run([CXX, *FLAGS, '-shared', '-fPIC', str(path / 'exports.cpp'),
                            '-o', str(path / 'reference.dylib')], check=True)
            subprocess.run([CXX, *FLAGS, str(ROOT / 'tests/stock_timed_warplane_motor_probe.cpp'),
                            '-o', str(path / 'probe')], check=True)
            machine = oracle.Machine(Path(client()) / 'WorldOfTanks.exe',
                                     oracle.Reference(path / 'reference.dylib'))
            original = oracle.OriginalWarplane(machine)
            counts = {'revs': 0, 'accepted': 0, 'rejected': 0, 'synthetic': 0, 'callbacks': 0,
                      'tail': 0, 'banked': 0, 'held': 0}

            def point_words(point):
                return [base.bits(v) for v in list(point[0]) + list(point[1]) + [point[2]]]

            for number, scenario in enumerate(oracle.scenarios()):
                lines = ['init %d %d %s %s' % (base.bits(scenario['roll']), base.bits(scenario['now']),
                                                ' '.join(map(str, point_words(scenario['first']))),
                                                ' '.join(map(str, point_words(scenario['second']))))]
                for op in scenario['ops']:
                    if op[0] == 'add':
                        lines.append('add ' + ' '.join(map(str, point_words(op[1]))))
                    else:
                        lines.append('rev %d %d' % (base.bits(op[1]), base.bits(op[2])))
                output = subprocess.run([str(path / 'probe')], input='\n'.join(lines) + '\n', text=True,
                                        capture_output=True, check=True).stdout.splitlines()
                original.create(scenario['roll'], list(range(1, 60)))
                original.init(scenario['now'], scenario['first'], scenario['second'])
                self.assertFalse(machine.thrown)
                self.assertEqual(list(map(int, output[0].split())), original.state(), (number, 'init'))
                for index, (op, line) in enumerate(zip(scenario['ops'], output[1:])):
                    words = list(map(int, line.split()))
                    where = (number, index, op[0])
                    if op[0] == 'add':
                        before = (machine.word(oracle.STATE + 0x74) - machine.word(oracle.STATE + 0x70)) // 0x1c
                        original.add(op[1])
                        after = (machine.word(oracle.STATE + 0x74) - machine.word(oracle.STATE + 0x70)) // 0x1c
                        accepted = words[0]
                        self.assertEqual(accepted == -1, after == before, where)
                        if accepted != -1:
                            self.assertEqual(accepted, after - 1, where)
                            counts['accepted'] += 1
                            counts['synthetic'] += after - before == 2
                        else:
                            counts['rejected'] += 1
                        self.assertEqual(words[1:], original.state(), where)
                    else:
                        calls = original.advance(op[1], op[2])
                        count = words[0]
                        produced = [words[1 + 7 * i:8 + 7 * i] for i in range(count)]
                        self.assertEqual(produced, calls, where)
                        self.assertEqual(words[1 + 7 * count:], original.state(), where)
                        state = original.state()
                        counts['revs'] += 1
                        counts['callbacks'] += count
                        counts['tail'] += state[25] == state[27] - 1
                        counts['banked'] += state[0] != 0
                        counts['held'] += op[2] == 0.
            for name, value in counts.items():
                self.assertGreater(value, 0, counts)
            print('Original 00734c90/00734230/007348e0 matched production:', counts)


if __name__ == '__main__':
    unittest.main()
