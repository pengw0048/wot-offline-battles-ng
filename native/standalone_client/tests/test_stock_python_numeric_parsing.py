"""Local FP-model boundaries and original #1513 special-value evidence."""
import importlib.util
import os
from pathlib import Path
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
spec = importlib.util.spec_from_file_location('numeric_patch', ROOT/'bigworld/patch_stock_python_numeric_parsing.py')
patcher = importlib.util.module_from_spec(spec)
spec.loader.exec_module(patcher)


class NumericPatchTests(unittest.TestCase):
    def setUp(self):
        source = os.environ.get('WOT_BW_ENGINE_SOURCE')
        if not source: self.skipTest('The pinned public engine source is required')
        self.temp = tempfile.TemporaryDirectory(prefix='wot-python-numeric-')
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)
        self.paths = [patcher.PATH, Path('third_party/python/Objects/floatobject.c')]
        for relative in self.paths:
            target = self.root/relative
            target.parent.mkdir(parents=True, exist_ok=True)
            target.write_bytes((Path(source)/relative).read_bytes())

    def test_only_precision_boundaries_change_and_repeat_preserves_mtime(self):
        before = {p:(self.root/p).read_bytes() for p in self.paths}
        self.assertTrue(patcher.patch_engine(self.root))
        times = {}
        for relative in self.paths:
            path = self.root/relative
            data = path.read_bytes()
            self.assertEqual(data.count(patcher.PREFIX.encode()), 1)
            self.assertEqual(data.count(patcher.SUFFIX.encode()), 1)
            statement = patcher.DECIMAL_NAN if relative == patcher.PATH else patcher.HEX_NAN
            self.assertEqual(patcher.unwrap(data.decode(), statement).encode(), before[relative])
            times[relative] = path.stat().st_mtime_ns
        self.assertFalse(patcher.patch_engine(self.root))
        self.assertEqual(times, {p:(self.root/p).stat().st_mtime_ns for p in self.paths})

    def test_crlf_and_late_failure_are_transactional(self):
        for relative in self.paths:
            path = self.root/relative
            path.write_bytes(path.read_bytes().replace(b'\n', b'\r\n'))
        self.assertTrue(patcher.patch_engine(self.root))
        for relative in self.paths:
            self.assertNotIn(b'\n', (self.root/relative).read_bytes().replace(b'\r\n', b''))
        first = (self.root/self.paths[0]).read_bytes()
        second = self.root/self.paths[1]
        second.write_bytes(second.read_bytes().replace(b'x = Py_NAN;', b'x = 0.;'))
        with self.assertRaises(ValueError): patcher.patch_engine(self.root)
        self.assertEqual((self.root/self.paths[0]).read_bytes(), first)

    def test_fresh_late_failure_does_not_publish_first_file(self):
        first = (self.root/self.paths[0]).read_bytes()
        second = self.root/self.paths[1]
        second.write_bytes(second.read_bytes().replace(b'x = Py_HUGE_VAL;', b'x = 1.;'))
        with self.assertRaises(ValueError): patcher.patch_engine(self.root)
        self.assertEqual((self.root/self.paths[0]).read_bytes(), first)


class OriginalNumericTests(unittest.TestCase):
    def test_original_instructions_produce_exact_signed_specials(self):
        client = os.environ.get('WOT_0922_CLIENT')
        if not client: self.skipTest('The pinned original PE is required')
        try:
            from stock_python_numeric_parsing_oracle import OriginalPythonNumericOracle
            oracle = OriginalPythonNumericOracle(Path(client)/'WorldOfTanks.exe')
        except ImportError:
            self.skipTest('pefile and Unicorn are required for original instructions')
        cases = [('inf',0x7ff0000000000000),('-inf',0xfff0000000000000),
                 ('+INF',0x7ff0000000000000),('infinity',0x7ff0000000000000),
                 ('-INFINITY',0xfff0000000000000),('nan',0xfff8000000000000),
                 ('-nan',0x7ff8000000000000),('+NaN',0xfff8000000000000)]
        for text,bits in cases:
            self.assertEqual(oracle.decimal(text),(bits,len(text)))
            hex_bits = 0xfff8000000000000 if 'nan' in text.lower() else bits
            self.assertEqual(oracle.fromhex(text),hex_bits)
        self.assertEqual(oracle.decimal('nAnSuffix'),(0xfff8000000000000,3))
        self.assertEqual(oracle.fromhex('nAnSuffix'),None)
        self.assertEqual(oracle.decimal('not-a-number'),(0xbff0000000000000,0))
        self.assertEqual(oracle.decimal('1.5'),(0x3ff8000000000000,3))
        self.assertEqual(oracle.decimal('-0'),(0x8000000000000000,2))
        self.assertEqual(set(oracle.counts),{'_errno','__control87_2','__fpe_flt_rounds'})


if __name__ == '__main__': unittest.main()
