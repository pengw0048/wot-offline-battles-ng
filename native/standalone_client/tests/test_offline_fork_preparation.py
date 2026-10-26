"""Exercise the real fixed-source preparation and its numerical consumer."""
import os
from pathlib import Path
import shutil
import sys
import tempfile
import types
import unittest
from unittest.mock import patch

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'bigworld'))
from prepare_offline_fork import NATIVE_MATH_PATH, PINNED_COMMIT, adapt_native_math, prepare


class OfflineForkPreparationTests(unittest.TestCase):
    def setUp(self):
        source = os.environ.get('WOT_FORK_0101_SOURCE')
        if not source:
            self.skipTest('The fixed v0.10.1 source input is required')
        self.original = Path(source)
        self.temporary = tempfile.TemporaryDirectory(prefix='wot-fork-prepare-')
        self.addCleanup(self.temporary.cleanup)
        self.root = Path(self.temporary.name)
        self.source = self.root / 'input'
        shutil.copytree(self.original / 'native', self.source / 'native')
        consumer = self.source / NATIVE_MATH_PATH
        consumer.parent.mkdir(parents=True)
        shutil.copyfile(self.original / NATIVE_MATH_PATH, consumer)
        (self.source / 'fork-source.json').write_text('{"revision":"' + PINNED_COMMIT + '"}')
        self.output = self.root / 'output'

    def test_preparation_preserves_input_and_unchanged_output_timestamps(self):
        inputs = {p.relative_to(self.source): p.read_bytes()
                  for p in self.source.rglob('*') if p.is_file()}
        prepare(self.source, self.output)
        outputs = {p: (p.read_bytes(), p.stat().st_mtime_ns)
                   for p in self.output.rglob('*') if p.is_file()}
        self.assertTrue(outputs)
        self.assertFalse(list(self.output.rglob('*.pyd')))
        self.assertIn('sim_control_source_still',
                      (self.output / 'offline_fork_methods.hpp').read_text())
        prepare(self.source, self.output)
        self.assertEqual(outputs, {p: (p.read_bytes(), p.stat().st_mtime_ns) for p in outputs})
        self.assertEqual(inputs, {p.relative_to(self.source): p.read_bytes()
                                 for p in self.source.rglob('*') if p.is_file()})

    def test_wrong_revision_and_changed_consumer_publish_nothing(self):
        identity = self.source / 'fork-source.json'
        identity.write_text('{"revision":"other"}')
        with self.assertRaises(ValueError):
            prepare(self.source, self.output)
        self.assertFalse(self.output.exists())
        identity.write_text('{"revision":"' + PINNED_COMMIT + '"}')
        (self.source / NATIVE_MATH_PATH).write_text('changed loader')
        with self.assertRaises(ValueError):
            prepare(self.source, self.output)
        self.assertFalse(self.output.exists())

    def consumer(self):
        namespace = {'__name__': 'isolated_native_math'}
        exec(compile(adapt_native_math((self.original / NATIVE_MATH_PATH).read_text()),
                     'native_math.py', 'exec'), namespace)
        namespace['_attempted'] = False
        return namespace

    def test_consumer_uses_registered_builtin_and_rejects_old_sidecar(self):
        builtin = types.ModuleType('offline_math_batch_native')
        builtin._wot_offline_fork_revision = PINNED_COMMIT
        with patch.dict(sys.modules, {'offline_math_batch_native': builtin}):
            namespace = self.consumer()
            self.assertIs(namespace['_load'](), builtin)
            self.assertIs(namespace['_load'](), builtin)
        old_sidecar = types.ModuleType('offline_math_batch_native')
        with patch.dict(sys.modules, {'offline_math_batch_native': old_sidecar}):
            namespace = self.consumer()
            self.assertIsNone(namespace['_load']())
            self.assertIsNone(namespace['_backend'])


if __name__ == '__main__':
    unittest.main()
