"""Use real CMake regeneration; no SDK target or gameplay source is compiled."""

import os
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]


class OfflineForkConfigureDependencyTests(unittest.TestCase):
    def test_incremental_build_regenerates_local_adapter_and_runtime_inputs(self):
        cmake = shutil.which('cmake')
        if not cmake:
            self.skipTest('A real CMake host is required')
        with tempfile.TemporaryDirectory(prefix='offline-fork-configure-') as directory:
            root = Path(directory)
            source, build = root / 'source', root / 'build'
            adapter = source / 'bigworld'
            entry, wire = adapter / 'offline_entry', source / 'wire'
            entry.mkdir(parents=True)
            wire.mkdir()
            shutil.copyfile(ROOT / 'bigworld/offline_fork.cmake', adapter / 'offline_fork.cmake')
            inputs = [adapter / 'stock_native_battle_shell.py', entry / 'OfflineGarageEntry.py',
                      entry / 'compat_account_attempt.txt', adapter / 'wgde_empty_rows.json',
                      wire / 'gen_cpp.py', wire / 'codec.py', wire / 'schema.py']
            for path in inputs:
                path.write_text('initial\n')
            unused = adapter / 'patch_unused_sdk.py'
            unused.write_text('# SDK-only patch\n')
            (adapter / 'stock_offline_platform.cpp').write_text('int platform_fixture;\n')
            for name in ('stock_motion_rotation_batch.cpp', 'stock_rotation_stage_timing.cpp'):
                (adapter / name).write_text('int fixture;\n')
            # Only the generator's small fixture body is substituted. The
            # production CMake include, reconfigure rules and build are real.
            (adapter / 'prepare_offline_fork.py').write_text('''
from pathlib import Path
import sys
here = Path(__file__).parent
output = Path(sys.argv[2])
output.mkdir(parents=True, exist_ok=True)
counter = output / 'prepare-count'
counter.write_text(str(int(counter.read_text()) + 1) if counter.exists() else '1')
inputs = sorted(path for path in here.glob('*.py') if path.name != 'patch_unused_sdk.py')
inputs += sorted((here / 'offline_entry').glob('*'))
inputs += [here / 'wgde_empty_rows.json'] + sorted((here.parent / 'wire').glob('*.py'))
payload = ''.join(path.read_text() for path in inputs)
for name, data in (('payload.txt', payload),
                   ('offline_fork_sources.cmake', 'set(WOT_OFFLINE_MATH_SOURCES)\\n')):
    path = output / name
    if not path.exists() or path.read_text() != data:
        path.write_text(data)
''')
            (source / 'dummy.cpp').write_text('int client_fixture;\n')
            (source / 'CMakeLists.txt').write_text('''
cmake_minimum_required(VERSION 3.20)
project(OfflineForkConfigureTest LANGUAGES CXX)
add_library(bwclient STATIC dummy.cpp)
set(BW_SOURCE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/unused-sdk")
set(WOT_OFFLINE_FORK_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/unused-pinned-input")
include(bigworld/offline_fork.cmake)
add_custom_target(check_generated
    COMMAND "${CMAKE_COMMAND}" -E compare_files
        "${CMAKE_BINARY_DIR}/generated/offline-fork/payload.txt"
        "${CMAKE_BINARY_DIR}/expected.txt")
''')

            def run(*arguments):
                result = subprocess.run([cmake, *map(str, arguments)], capture_output=True, text=True,
                                        env=dict(os.environ, PYTHONDONTWRITEBYTECODE='1'))
                self.assertEqual(result.returncode, 0, result.stdout + result.stderr)

            configure = ['-S', source, '-B', build]
            if shutil.which('ninja'):
                configure += ['-G', 'Ninja']
            run(*configure)
            generated = build / 'generated/offline-fork'
            counter, payload = generated / 'prepare-count', generated / 'payload.txt'

            def expected():
                paths = sorted(path for path in adapter.glob('*.py') if path.name != unused.name)
                paths += sorted(entry.glob('*'))
                paths += [adapter / 'wgde_empty_rows.json'] + sorted(wire.glob('*.py'))
                (build / 'expected.txt').write_text(''.join(path.read_text() for path in paths))

            def build_check():
                expected()
                run('--build', build, '--target', 'check_generated')

            self.assertEqual(counter.read_text(), '1')
            initial_mtime = payload.stat().st_mtime_ns
            build_check()
            self.assertEqual(counter.read_text(), '1')
            self.assertEqual(payload.stat().st_mtime_ns, initial_mtime)
            for number, path in enumerate(inputs, 2):
                path.write_text('changed %d\n' % number)
                build_check()
                self.assertEqual(counter.read_text(), str(number), path.name)
            # The glob also tracks newly added runtime helpers. A subsequent
            # no-op build must not run preparation or rewrite identical output.
            added = entry / 'new_runtime_helper.py'
            added.write_text('new helper\n')
            build_check()
            count = counter.read_text()
            self.assertEqual(count, str(len(inputs) + 2))
            unchanged = payload.stat().st_mtime_ns
            # Broad local *.py registration may reconfigure for an SDK-only
            # patch, but unchanged generated payload keeps its timestamp.
            unused.write_text('# Changed SDK-only patch\n')
            build_check()
            self.assertEqual(counter.read_text(), str(int(count) + 1))
            self.assertEqual(payload.stat().st_mtime_ns, unchanged)
            count = counter.read_text()
            build_check()
            self.assertEqual(counter.read_text(), count)
            self.assertEqual(payload.stat().st_mtime_ns, unchanged)


if __name__ == '__main__':
    unittest.main()
