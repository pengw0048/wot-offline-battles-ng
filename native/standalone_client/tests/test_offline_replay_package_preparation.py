"""The pinned recorder consumes the complete original release as a physical ZIP."""
import importlib.util
import io
import os
from pathlib import Path
import shutil
import sys
import tempfile
import unittest
from unittest.mock import patch
import zipfile

HERE = Path(__file__).resolve().parent
ROOT = HERE.parent
sys.path.insert(0, str(ROOT / 'bigworld'))
from prepare_offline_fork import (
    REPLAY_PACKAGE_INPUT, REPLAY_PACKAGE_OUTPUT, REPLAY_PROCESS_PATH,
    stage_replay_package)


class OfflineReplayPackagePreparationTests(unittest.TestCase):
    def setUp(self):
        value = os.environ.get('WOT_FORK_0101_SOURCE')
        if not value:
            self.skipTest('WOT_FORK_0101_SOURCE must contain pinned source and its dist/v0.10.1 wotmod')
        original = Path(value)
        self.assertTrue((original / REPLAY_PACKAGE_INPUT).is_file(),
                        'Configured v0.10.1 source lacks original dist wotmod')
        self.temporary = tempfile.TemporaryDirectory(prefix='offline-replay-package-')
        self.addCleanup(self.temporary.cleanup)
        self.root = Path(self.temporary.name)
        self.source = self.root / 'source'
        shutil.copytree(original / 'src', self.source / 'src')
        shutil.copyfile(original / 'LICENSE', self.source / 'LICENSE')
        self.package = self.source / REPLAY_PACKAGE_INPUT
        self.package.parent.mkdir(parents=True)
        shutil.copyfile(original / REPLAY_PACKAGE_INPUT, self.package)
        self.original_package = self.package.read_bytes()
        self.output = self.root / 'game'

    def rewrite(self, replace=None, remove=None, duplicate=None):
        replace = replace or {}
        buffer = io.BytesIO()
        with zipfile.ZipFile(io.BytesIO(self.original_package)) as original, \
                zipfile.ZipFile(buffer, 'w') as changed:
            for info in original.infolist():
                if info.filename == remove:
                    continue
                changed.writestr(info, replace.get(info.filename, original.read(info.filename)))
            if duplicate:
                # A valid ZIP can still have ambiguous duplicated members.
                with self.assertWarns(UserWarning):
                    changed.writestr(duplicate, b'duplicate')
        self.package.write_bytes(buffer.getvalue())

    def test_complete_original_archive_is_staged_unchanged_without_mounting(self):
        before = {path.relative_to(self.source): path.read_bytes()
                  for path in self.source.rglob('*') if path.is_file()}
        stage_replay_package(self.source, self.output)
        output = self.output / REPLAY_PACKAGE_OUTPUT
        self.assertEqual(output.read_bytes(), self.original_package)
        self.assertEqual(before, {path.relative_to(self.source): path.read_bytes()
                                  for path in self.source.rglob('*') if path.is_file()})
        self.assertEqual([REPLAY_PACKAGE_OUTPUT], [path.relative_to(self.output)
            for path in self.output.rglob('*') if path.is_file()])
        self.assertFalse((self.output / 'paths.xml').exists())

    def test_actual_pinned_recorder_loader_fails_before_and_reads_after(self):
        # Execute the actual original consumer, including its runtime digest,
        # extraction limits, cache identity and repeated-cache verification.
        path = self.source / 'src' / REPLAY_PROCESS_PATH
        spec = importlib.util.spec_from_file_location('pinned_recorder_package_consumer', path)
        consumer = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(consumer)
        self.output.mkdir()
        with patch.dict(os.environ, {'LOCALAPPDATA': str(self.root / 'cache')}):
            with self.assertRaises(FileNotFoundError):
                consumer.prepare_runtime(str(self.output))
            stage_replay_package(self.source, self.output)
            command = consumer.prepare_runtime(str(self.output))
            self.assertEqual(command, consumer.prepare_runtime(str(self.output)))
            self.assertEqual(command[1:3], ['-I', '-u'])
            self.assertEqual(Path(command[0]).name, 'python.exe')
            self.assertEqual(Path(command[-1]).name, 'replay_writer_process.py')
            self.assertTrue(Path(command[0]).read_bytes().startswith(b'MZ'))
            self.assertTrue(Path(command[-1]).read_bytes())
        # Preparing the real writer does not execute a Windows binary here.
        self.assertIsNone(getattr(consumer, '_process', None))

    def test_missing_runtime_only_or_incomplete_mod_is_rejected_before_output(self):
        self.package.unlink()
        with self.assertRaisesRegex(ValueError, 'wotmod input is missing'):
            stage_replay_package(self.source, self.output)
        self.assertFalse(self.output.exists())
        runtime = 'res/offline_replay/recorder-runtime.zip'
        with zipfile.ZipFile(io.BytesIO(self.original_package)) as original, \
                zipfile.ZipFile(self.package, 'w') as incomplete:
            incomplete.writestr(runtime, original.read(runtime))
        with self.assertRaises(ValueError):
            stage_replay_package(self.source, self.output)
        self.assertFalse(self.output.exists())
        for member in (runtime, 'res/scripts/client/gui/mods/offline_lan_0922/replay_process.pyc',
                       'res/offline_replay/replay_reader_process.py', 'licenses/Boost-1.0.txt'):
            self.rewrite(remove=member)
            with self.assertRaises(ValueError, msg=member):
                stage_replay_package(self.source, self.output)
            self.assertFalse(self.output.exists())

    def test_wrong_version_runtime_bytecode_duplicate_and_corruption_are_rejected(self):
        module = 'res/scripts/client/gui/mods/mod_offline_lan_0922.pyc'
        mutations = (
            {'replace': {'meta.xml': b'<root><id>org.colorfulmeans.offline_lan_0922</id><version>1.0</version></root>'}},
            {'replace': {'res/offline_replay/recorder-runtime.zip': b'wrong recorder'}},
            {'replace': {module: b'wrong interpreter bytecode'}},
            {'duplicate': module},
        )
        for mutation in mutations:
            self.rewrite(**mutation)
            with self.assertRaises(ValueError, msg=mutation):
                stage_replay_package(self.source, self.output)
            self.assertFalse(self.output.exists())
        self.package.write_bytes(b'not a ZIP')
        with self.assertRaises(ValueError):
            stage_replay_package(self.source, self.output)
        self.assertFalse(self.output.exists())

    def test_changed_original_consumer_is_rejected(self):
        path = self.source / 'src' / REPLAY_PROCESS_PATH
        original = path.read_text()
        for constant in ('PACKAGE_NAME', 'RUNTIME_RESOURCE', 'RUNTIME_DIGEST'):
            lines = original.splitlines(True)
            for index, line in enumerate(lines):
                if line.startswith(constant + ' = '):
                    lines[index] = constant + " = 'changed'\n"
                    break
            else:
                self.fail('Pinned consumer constant missing: ' + constant)
            path.write_text(''.join(lines))
            with self.assertRaises(ValueError):
                stage_replay_package(self.source, self.output)
            self.assertFalse(self.output.exists())
        path.write_text(original)


if __name__ == '__main__':
    unittest.main()
