"""Math/cmath special-value boundaries against original #1513 instructions."""
import importlib.util
import os
from pathlib import Path
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
spec = importlib.util.spec_from_file_location('math_patch', ROOT/'bigworld/patch_stock_python_math_specials.py')
patcher = importlib.util.module_from_spec(spec)
spec.loader.exec_module(patcher)
PATHS = [patcher.ROOT/name for name in patcher.SITES]
INF, NAN = 0x7ff0000000000000, 0xfff8000000000000


def oracle_or_skip(test, source=None):
    client = os.environ.get('WOT_0922_CLIENT')
    if not client: test.skipTest('The pinned original PE is required')
    try:
        from stock_python_math_specials_oracle import OriginalPythonMathOracle
        return OriginalPythonMathOracle(Path(client)/'WorldOfTanks.exe', source)
    except ImportError:
        test.skipTest('pefile and Unicorn are required for original instructions')


def engine_source_or_skip(test):
    source = os.environ.get('WOT_BW_ENGINE_SOURCE')
    if not source: test.skipTest('The pinned public engine source is required')
    return Path(source)


class MathPatchTests(unittest.TestCase):
    def setUp(self):
        source = engine_source_or_skip(self)
        self.temp = tempfile.TemporaryDirectory(prefix='wot-python-math-')
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)
        for relative in PATHS:
            target = self.root/relative
            target.parent.mkdir(parents=True, exist_ok=True)
            target.write_bytes((source/relative).read_bytes())

    def test_only_special_value_sites_change_and_repeat_preserves_mtime(self):
        before = {p: (self.root/p).read_bytes() for p in PATHS}
        self.assertTrue(patcher.patch_engine(self.root))
        times = {}
        for relative, sites in zip(PATHS, ((6, 7), (1, 2))):
            data = (self.root/relative).read_text()
            self.assertEqual(data.count(patcher.BLOCK), 1)
            tail = data.partition(patcher.BLOCK)[2]
            self.assertEqual((patcher.count(tail, '_Py_1513_HUGE_VAL'),
                              patcher.count(tail, '_Py_1513_NAN')), sites)
            self.assertEqual((patcher.count(tail, 'Py_HUGE_VAL'), patcher.count(tail, 'Py_NAN')), (0, 0))
            self.assertEqual(patcher.unwrap(data).encode(), before[relative])
            times[relative] = (self.root/relative).stat().st_mtime_ns
        self.assertFalse(patcher.patch_engine(self.root))
        self.assertEqual(times, {p: (self.root/p).stat().st_mtime_ns for p in PATHS})

    def test_crlf_and_late_failure_are_transactional(self):
        for relative in PATHS:
            path = self.root/relative
            path.write_bytes(path.read_bytes().replace(b'\n', b'\r\n'))
        self.assertTrue(patcher.patch_engine(self.root))
        for relative in PATHS:
            self.assertNotIn(b'\n', (self.root/relative).read_bytes().replace(b'\r\n', b''))
        first = (self.root/PATHS[0]).read_bytes()
        second = self.root/PATHS[1]
        second.write_bytes(second.read_bytes().replace(b'#define N _Py_1513_NAN', b'#define N 0.'))
        with self.assertRaises(ValueError): patcher.patch_engine(self.root)
        self.assertEqual((self.root/PATHS[0]).read_bytes(), first)

    def test_fresh_late_failure_does_not_publish_first_file(self):
        first = (self.root/PATHS[0]).read_bytes()
        second = self.root/PATHS[1]
        second.write_bytes(second.read_bytes().replace(
            b'        return Py_NAN;\n    if (Py_IS_INFINITY(z.imag))',
            b'        return 0.;\n    if (Py_IS_INFINITY(z.imag))'))
        with self.assertRaises(ValueError): patcher.patch_engine(self.root)
        self.assertEqual((self.root/PATHS[0]).read_bytes(), first)

    def test_unaudited_producer_or_damaged_boundary_is_rejected(self):
        path = self.root/PATHS[0]
        original = path.read_bytes()
        path.write_bytes(original.replace(b'    return x > 0.0 ? cf : 2.0 - cf;',
                                          b'    return Py_NAN;', 1))
        with self.assertRaises(ValueError): patcher.patch_engine(self.root)
        path.write_bytes(original)
        self.assertTrue(patcher.patch_engine(self.root))
        path.write_bytes(path.read_bytes().replace(b'so use the original words', b'so use words'))
        with self.assertRaises(ValueError): patcher.patch_engine(self.root)


class OriginalMathTests(unittest.TestCase):
    def test_original_instructions_produce_ieee_specials(self):
        oracle = oracle_or_skip(self)
        payload = 0x7ff8000000000123
        cases = [
            ('math', 'lgamma', 'f1', (INF,), ('F', (INF,))),
            ('math', 'lgamma', 'f1', (0xfff0000000000000,), ('F', (INF,))),
            ('math', 'lgamma', 'f1', (payload,), ('F', (payload,))),
            ('math', 'lgamma', 'f1', (0x8000000000000000,), ('E', 'ValueError:math domain error')),
            ('math', 'lgamma', 'f1', (0xc008000000000000,), ('E', 'ValueError:math domain error')),
            ('math', 'gamma', 'f1', (0xfff0000000000000,), ('E', 'ValueError:math domain error')),
            ('math', 'log', 'f1', (0,), ('E', 'ValueError:math domain error')),
            ('math', 'log10', 'f1', (0xfff0000000000000,), ('E', 'ValueError:math domain error')),
            ('math', 'log', 'f1', (INF,), ('F', (INF,))),
            ('math', 'atan2', 'f2', (payload, 0x3ff0000000000000), ('F', (NAN,))),
            ('math', 'atan2', 'f2', (0x3ff0000000000000, 0x7ff8000000000000), ('F', (NAN,))),
            ('math', 'atan2', 'f2', (0xfff0000000000000, INF), ('F', (0xbfe921fb54442d18,))),
            ('cmath', 'phase', 'c1', (0x3ff0000000000000, payload), ('F', (NAN,))),
            ('cmath', 'sqrt', 'c1', (0xfff0000000000000, 0xfff0000000000000), ('C', (INF, 0xfff0000000000000))),
            ('cmath', 'acos', 'c1', (NAN, INF), ('C', (NAN, 0xfff0000000000000))),
            ('cmath', 'atanh', 'c1', (0x3ff0000000000000, 0), ('E', 'ValueError:math domain error')),
            ('cmath', 'exp', 'c1', (INF, 0x3ff0000000000000), ('C', (INF, INF))),
            ('cmath', 'rect', 'f2', (INF, 0x4004000000000000), ('C', (0xfff0000000000000, INF))),
        ]
        for module, name, kind, words, expected in cases:
            self.assertEqual(oracle.run(module, name, kind, words)[0], expected, (module, name, words))

    def test_retail_tables_differ_from_source_only_by_merged_zero_signs(self):
        source = (engine_source_or_skip(self)/patcher.ROOT/'cmathmodule.c').read_text()
        oracle = oracle_or_skip(self, source)
        self.assertEqual(len(oracle.merged_zeros), 41)
        self.assertEqual(oracle.run('cmath', 'cosh', 'c1', (0xfff0000000000000, 0))[0],
                         ('C', (INF, 0x8000000000000000)))


class WindowsOutcomeTests(unittest.TestCase):
    """Compare actual Win32 probe outcomes (baseline and patched) with the oracle."""

    def test_patched_runtime_matches_original_or_unchanged_baseline(self):
        results = os.environ.get('WOT_MATH_SPECIALS_RESULTS')
        if not results: self.skipTest('Actual Win32 probe outcomes are required')
        source = (engine_source_or_skip(self)/patcher.ROOT/'cmathmodule.c').read_text()
        oracle = oracle_or_skip(self, source)
        from stock_python_math_specials_oracle import (HostDependent, cases, outcome_text,
                                                       parse_outcomes)
        baseline = parse_outcomes((Path(results)/'baseline-outcomes.txt').read_text())
        patched = parse_outcomes((Path(results)/'patched-outcomes.txt').read_text())
        exact = repaired = unchanged = 0
        complex_abs = []
        for cid, module, name, kind, words in cases():
            try:
                expected = outcome_text(oracle.run(module, name, kind, words)[0])
            except HostDependent:
                self.assertEqual(patched[cid], baseline[cid], (cid, module, name))
                unchanged += 1
                continue
            exact += 1
            repaired += baseline[cid] != expected and patched[cid] == expected
            if patched[cid] != expected and name == 'polar':
                # Objects/complexobject.c _Py_c_abs, outside this patch.
                self.assertEqual((patched[cid][:19], patched[cid][19:], expected[:19]),
                                 ('T:0000000000000000,', expected[19:], 'T:fff8000000000000,'))
                complex_abs.append(cid)
                continue
            self.assertEqual(patched[cid], expected, (cid, module, name, words))
        self.assertEqual((exact, unchanged, len(complex_abs)), (1534, 950, 24))
        self.assertEqual(repaired, 822)


if __name__ == '__main__': unittest.main()
