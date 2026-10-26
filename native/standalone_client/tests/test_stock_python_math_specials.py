"""Math/cmath special-value boundaries against original #1513 instructions."""
import importlib.util
import os
from pathlib import Path
import stat
import tempfile
import unittest

from stock_python_math_specials_oracle import TABLES, source_table_words

ROOT = Path(__file__).resolve().parents[1]
spec = importlib.util.spec_from_file_location('math_patch', ROOT/'bigworld/patch_stock_python_math_specials.py')
patcher = importlib.util.module_from_spec(spec)
spec.loader.exec_module(patcher)
PATHS = [Path(relative) for relative, unused, unused_sites in patcher.FILES.values()]
SITES = [(6, 7), (1, 2), (0, 1)]
INF, NAN = 0x7ff0000000000000, 0xfff8000000000000


def oracle_or_skip(test):
    client = os.environ.get('WOT_0922_CLIENT')
    if not client: test.skipTest('The pinned original PE is required')
    try:
        from stock_python_math_specials_oracle import OriginalPythonMathOracle
        return OriginalPythonMathOracle(Path(client)/'WorldOfTanks.exe')
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
        for relative, sites in zip(PATHS, SITES):
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

    def test_tables_change_only_listed_zero_signs(self):
        before = (self.root/PATHS[1]).read_text()
        patcher.patch_engine(self.root)
        public = source_table_words(before)
        patched = source_table_words((self.root/PATHS[1]).read_text())
        changed = [i for i, (a, b) in enumerate(zip(public, patched)) if a != b]
        self.assertEqual(len(changed), 41)
        # Every change is a zero whose sign flips; no other word moves.
        self.assertTrue(all(public[i] ^ patched[i] == 1 << 63 and public[i] & ~(1 << 63) == 0
                            for i in changed))
        self.assertEqual(patcher.unwrap((self.root/PATHS[1]).read_text()), before)

    def test_crlf_and_late_failure_are_transactional(self):
        for relative in PATHS:
            path = self.root/relative
            path.write_bytes(path.read_bytes().replace(b'\n', b'\r\n'))
        self.assertTrue(patcher.patch_engine(self.root))
        for relative in PATHS:
            self.assertNotIn(b'\n', (self.root/relative).read_bytes().replace(b'\r\n', b''))
        first = (self.root/PATHS[0]).read_bytes()
        last = self.root/PATHS[2]
        last.write_bytes(last.read_bytes().replace(b'return _Py_1513_NAN;', b'return 0.;'))
        with self.assertRaises(ValueError): patcher.patch_engine(self.root)
        self.assertEqual((self.root/PATHS[0]).read_bytes(), first)

    def test_fresh_late_failure_does_not_publish_first_file(self):
        first = (self.root/PATHS[0]).read_bytes()
        cmath = self.root/PATHS[1]
        for old, new in ((b'        return Py_NAN;\n    if (Py_IS_INFINITY(z.imag))',
                          b'        return 0.;\n    if (Py_IS_INFINITY(z.imag))'),
                         (b'C(P12,0.) C(P12,-0.)', b'C(P12,0.) C(P12,1.)')):
            original = cmath.read_bytes()
            cmath.write_bytes(original.replace(old, new, 1))
            with self.assertRaises(ValueError): patcher.patch_engine(self.root)
            self.assertEqual((self.root/PATHS[0]).read_bytes(), first)
            cmath.write_bytes(original)

    def test_late_write_failure_restores_earlier_files(self):
        before = {p: (self.root/p).read_bytes() for p in PATHS}
        last = self.root/PATHS[2]
        last.chmod(stat.S_IRUSR)
        self.addCleanup(last.chmod, stat.S_IRUSR | stat.S_IWUSR)
        with self.assertRaises(OSError): patcher.patch_engine(self.root)
        self.assertEqual({p: (self.root/p).read_bytes() for p in PATHS}, before)

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
        domain, too_large = ('E', 'ValueError:math domain error'), ('E', 'OverflowError:absolute value too large')
        cases = [
            ('math', 'lgamma', 'f1', (INF,), ('F', (INF,))),
            ('math', 'lgamma', 'f1', (0xfff0000000000000,), ('F', (INF,))),
            ('math', 'lgamma', 'f1', (payload,), ('F', (payload,))),
            ('math', 'lgamma', 'f1', (0x8000000000000000,), domain),
            ('math', 'lgamma', 'f1', (0xc008000000000000,), domain),
            ('math', 'gamma', 'f1', (0xfff0000000000000,), domain),
            ('math', 'log', 'f1', (0,), domain),
            ('math', 'log10', 'f1', (0xfff0000000000000,), domain),
            ('math', 'log', 'f1', (INF,), ('F', (INF,))),
            ('math', 'atan2', 'f2', (payload, 0x3ff0000000000000), ('F', (NAN,))),
            ('math', 'atan2', 'f2', (0x3ff0000000000000, 0x7ff8000000000000), ('F', (NAN,))),
            ('math', 'atan2', 'f2', (0xfff0000000000000, INF), ('F', (0xbfe921fb54442d18,))),
            ('cmath', 'phase', 'c1', (0x3ff0000000000000, payload), ('F', (NAN,))),
            ('cmath', 'polar', 'c1', (0x3ff0000000000000, payload), ('T', (NAN, NAN))),
            ('cmath', 'polar', 'c1', (payload, INF), ('T', (INF, NAN))),
            ('cmath', 'polar', 'c1r', (payload, 0x3ff0000000000000), ('E', 'OverflowError:math range error')),
            ('__builtin__', 'abs', 'c1', (payload, 0x8000000000000000), ('F', (NAN,))),
            ('__builtin__', 'abs', 'c1', (NAN, 0xfff0000000000000), ('F', (INF,))),
            ('__builtin__', 'abs', 'c1r', (0, NAN), too_large),
            ('__builtin__', 'abs', 'c1r', (INF, NAN), ('F', (INF,))),
            ('cmath', 'sqrt', 'c1', (0xfff0000000000000, 0xfff0000000000000), ('C', (INF, 0xfff0000000000000))),
            ('cmath', 'acos', 'c1', (NAN, INF), ('C', (NAN, 0xfff0000000000000))),
            ('cmath', 'cosh', 'c1', (0xfff0000000000000, 0), ('C', (INF, 0))),
            ('cmath', 'exp', 'c1', (0xfff0000000000000, INF), ('C', (0, 0x8000000000000000))),
            ('cmath', 'atanh', 'c1', (0x3ff0000000000000, 0), domain),
            ('cmath', 'exp', 'c1', (INF, 0x3ff0000000000000), ('C', (INF, INF))),
            ('cmath', 'rect', 'f2', (INF, 0x4004000000000000), ('C', (0xfff0000000000000, INF))),
        ]
        for module, name, kind, words, expected in cases:
            self.assertEqual(oracle.run(module, name, kind, words)[0], expected, (module, name, words))

    def test_patched_source_tables_equal_retail_initcmath_words(self):
        source = engine_source_or_skip(self)
        oracle = oracle_or_skip(self)
        retail = [word for name, words in sorted(oracle.table_words().items(),
                                                 key=lambda item: TABLES.index(item[0]))
                  for word in words]
        text = (source/PATHS[1]).read_text()
        public = source_table_words(text)
        patched = source_table_words(patcher.patched_source(text.encode(), 'cmathmodule.c').decode())
        self.assertEqual(sum(a != b for a, b in zip(public, retail)), 41)
        self.assertEqual(patched, retail)


class WindowsOutcomeTests(unittest.TestCase):
    """Compare actual Win32 probe outputs (baseline and patched) with the oracle."""

    def test_patched_runtime_matches_original_or_unchanged_baseline(self):
        results = os.environ.get('WOT_MATH_SPECIALS_RESULTS')
        if not results: self.skipTest('Actual Win32 probe outcomes are required')
        oracle = oracle_or_skip(self)
        from stock_python_math_specials_oracle import (HostDependent, cases, outcome_text,
                                                       parse_outcomes, parse_tables)
        baseline = parse_outcomes((Path(results)/'baseline-outcomes.txt').read_text())
        patched = parse_outcomes((Path(results)/'patched-outcomes.txt').read_text())
        exact = repaired = unchanged = 0
        for cid, module, name, kind, words in cases():
            try:
                expected = outcome_text(oracle.run(module, name, kind, words)[0])
            except HostDependent:
                self.assertEqual(patched[cid], baseline[cid], (cid, module, name))
                unchanged += 1
                continue
            exact += 1
            repaired += baseline[cid] != expected
            self.assertEqual(patched[cid], expected, (cid, module, name, words))
        self.assertEqual((exact, unchanged, repaired), (1714, 1091, 960))
        retail = oracle.table_words()
        self.assertEqual(parse_tables((Path(results)/'patched-tables.txt').read_text()), retail)
        baseline_tables = parse_tables((Path(results)/'baseline-tables.txt').read_text())
        self.assertEqual(sum(a != b for n in TABLES for a, b in zip(baseline_tables[n], retail[n])), 513)


if __name__ == '__main__': unittest.main()
