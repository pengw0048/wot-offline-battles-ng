"""The Hybrid client compiles out the profiler, memory debug and BW_GUARD stack tracker like #1513's consumer build."""
import importlib.util
import os
from pathlib import Path
import re
import shutil
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
spec = importlib.util.spec_from_file_location(
    'consumer_instrumentation_patch', ROOT / 'bigworld/patch_stock_consumer_instrumentation.py')
patcher = importlib.util.module_from_spec(spec)
spec.loader.exec_module(patcher)
FEATURES = ('ENABLE_PROFILER', 'ENABLE_MEMORY_DEBUG', 'ENABLE_STACK_TRACKER', 'ENABLE_WATCHERS',
            'ENABLE_MINI_DUMP', 'ENABLE_MSG_LOGGING', 'ENABLE_ALLOCATOR_STATISTICS')


def resolved(config):
    """Each feature's preprocessor value for an MSVC Windows Hybrid client translation unit."""
    compiler = shutil.which('c++')
    with tempfile.TemporaryDirectory() as directory:
        header = Path(directory) / 'cstdmf/config.hpp'
        header.parent.mkdir()
        header.write_text(config)
        probe = Path(directory) / 'probe.cpp'
        probe.write_text('#include "cstdmf/config.hpp"\n' + ''.join(
            '#if %s\nint value_%s = 1;\n#else\nint value_%s = 0;\n#endif\n' % (name, name, name) for name in FEATURES))
        output = subprocess.run([compiler, '-E', '-U__APPLE__', '-U__clang__', '-D_WIN32', '-DBW_CLIENT', '-w',
                                 '-I', directory, str(probe)], capture_output=True, text=True, check=True).stdout
    return dict((name, int(value)) for name, value in re.findall(r'int value_(\w+) = (\d);', output))


class ConsumerInstrumentationTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        engine = os.environ.get('WOT_BW_ENGINE_SOURCE')
        if not engine:
            raise unittest.SkipTest('WOT_BW_ENGINE_SOURCE must point to the pinned engine source')
        cls.original = (Path(engine) / patcher.PATH).read_text()
        cls.patched = patcher.patch_text(cls.original)

    def test_exact_seams_and_fail_closed(self):
        restored = self.patched
        for before, after in patcher.SEAMS:
            restored = restored.replace(after, before, 1)
        self.assertEqual(restored, self.original)
        self.assertEqual(patcher.patch_text(self.patched), self.patched)
        mutations = [self.original.replace(patcher.SEAMS[0][0], patcher.SEAMS[0][1], 1),
                     self.original.replace(patcher.SEAMS[2][0], 'changed', 1),
                     self.original + '\n' + patcher.SEAMS[1][0]]
        for text in mutations:
            with self.assertRaises(ValueError):
                patcher.patch_text(text)

    @unittest.skipUnless(shutil.which('c++'), 'A C preprocessor is required')
    def test_hybrid_matches_consumer_and_keeps_force_switches(self):
        before, after = resolved(self.original), resolved(self.patched)
        self.assertEqual([before[name] for name in FEATURES[:3]], [1, 1, 1])
        self.assertEqual([after[name] for name in FEATURES[:3]], [0, 0, 0])
        # Watchers, minidumps, logging and allocator statistics keep their Hybrid values.
        self.assertEqual(dict((name, after[name]) for name in FEATURES[3:]),
                         dict((name, before[name]) for name in FEATURES[3:]))
        forced = resolved(re.sub(r'(#define FORCE_ENABLE_(PROFILER|STACK_TRACKER)\s+)0', r'\g<1>1', self.patched))
        self.assertEqual((forced['ENABLE_PROFILER'], forced['ENABLE_STACK_TRACKER'], forced['ENABLE_MEMORY_DEBUG']), (1, 1, 0))


if __name__ == '__main__':
    unittest.main()
