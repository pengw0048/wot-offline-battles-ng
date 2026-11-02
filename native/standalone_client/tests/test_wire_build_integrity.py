"""Build preparation refuses missing wire messages and stale native tables."""
import copy
import importlib.util
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest
from unittest.mock import patch

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'wire'))
sys.path.insert(0, str(ROOT / 'bigworld'))
import gen_cpp
import prepare_offline_fork as prepare
from schema import SCHEMA

VALID = '''class LANClient(object):
    def hello(self):
        return {'type': 'hello', 'name': 'Player'}
    def select_vehicle(self):
        return {'type': 'select_vehicle', 'outfits': {}}
'''


class WireBuildIntegrity(unittest.TestCase):
    def test_registered_producers_and_unrelated_dicts(self):
        source = VALID + '''    def local_metadata(self):
        return {'kind': 'unregistered_local_kind'}
'''
        self.assertEqual({'hello': {'hello'}, 'select_vehicle': {'select_vehicle'}},
                         gen_cpp.check_client(source))

    def test_unregistered_producer_is_rejected(self):
        for kind in ('select_unregistered_vehicle', 'unknown_command'):
            with self.subTest(kind=kind), self.assertRaisesRegex(ValueError, kind):
                gen_cpp.check_client(VALID.replace("'hello'", repr(kind)))

    def test_missing_or_empty_client_cannot_silently_pass(self):
        for source in ('', 'class LANClient(object):\n    pass\n', VALID + VALID):
            with self.subTest(source=source), self.assertRaises(ValueError):
                gen_cpp.check_client(source)

    def test_removing_registered_message_from_schema_is_rejected(self):
        schema = copy.copy(SCHEMA)
        schema.messages = dict(SCHEMA.messages)
        del schema.messages['select_vehicle']
        with self.assertRaisesRegex(ValueError, 'select_vehicle'):
            gen_cpp.check_client(VALID, schema=schema)

    def test_native_table_drift_is_rejected(self):
        with tempfile.TemporaryDirectory() as directory:
            table = Path(directory) / 'stock_wire_schema.inc'
            gen_cpp.emit(SCHEMA, table)
            gen_cpp.check_client(VALID, table=table)
            table.write_text(table.read_text() + '// stale table\n')
            with self.assertRaisesRegex(ValueError, 'table is stale'):
                gen_cpp.check_client(VALID, table=table)

    def test_generator_output_cli_remains_compatible_and_check_cli_fails(self):
        with tempfile.TemporaryDirectory() as directory:
            table, client = Path(directory) / 'tables.inc', Path(directory) / 'lan_client.py'
            result = subprocess.run([sys.executable, gen_cpp.__file__, str(table)],
                                    capture_output=True, text=True)
            self.assertEqual(0, result.returncode, result.stderr)
            self.assertEqual(gen_cpp.render(SCHEMA), table.read_text())
            client.write_text(VALID)
            result = subprocess.run([sys.executable, gen_cpp.__file__, '--check-client', str(client)],
                                    capture_output=True, text=True)
            self.assertEqual(0, result.returncode, result.stderr)
            client.write_text(VALID.replace("'hello'", "'unknown_command'"))
            result = subprocess.run([sys.executable, gen_cpp.__file__, '--check-client', str(client)],
                                    capture_output=True, text=True)
            self.assertNotEqual(0, result.returncode)
            self.assertIn('unknown_command', result.stderr)

    def test_runtime_gate_checks_the_adapted_client_before_manifest(self):
        with tempfile.TemporaryDirectory() as directory:
            source, staged = Path(directory) / 'source', Path(directory) / 'staged'
            relative = prepare.NATIVE_MATH_PATH.with_name('lan_client.py')
            path = source / relative
            path.parent.mkdir(parents=True)
            path.write_text(VALID)
            with patch.object(prepare, 'RETIRED', ()), patch.object(prepare, 'ADAPTERS', {
                    relative: lambda text: text.replace("'hello'", "'unknown_command'")}):
                with self.assertRaisesRegex(ValueError, 'unknown_command'):
                    prepare.stage_runtime(source, staged)
            self.assertFalse((staged / 'offline-res.json').exists())
            self.assertIn('unknown_command', (staged / prepare.RUNTIME_ROOT /
                          relative.relative_to(prepare.PINNED_RESOURCES)).read_text())

    def test_launcher_gate_prevents_output_publication(self):
        path = ROOT.parents[1] / 'tools/stage_ng_launcher_source.py'
        spec = importlib.util.spec_from_file_location('wire_test_ng_stage', path)
        ng_stage = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(ng_stage)

        def fork(unused_source, unused_revision, tree):
            for name in ('launcher/core.py', 'server/lan_battle_server.py',
                         'src/res/scripts/client/gui/mods/offline_lan_0922/__init__.py',
                         'tools/packed_xml.py'):
                target = tree / name
                target.parent.mkdir(parents=True, exist_ok=True)
                target.write_text('')

        with tempfile.TemporaryDirectory() as directory:
            output = Path(directory) / 'output'
            rejected = subprocess.CompletedProcess([], 1, '', 'Wire schema integrity failed: unknown_command')
            with patch.object(ng_stage, '_copy_fork', fork), patch.object(
                    ng_stage.subprocess, 'run', return_value=rejected) as run:
                with self.assertRaisesRegex(ValueError, 'unknown_command'):
                    ng_stage.stage(output, revision='test')
            self.assertFalse(output.exists())
            self.assertEqual(sys.executable, run.call_args.args[0][0])
            self.assertIn('--check-fork-source', run.call_args.args[0])


if __name__ == '__main__':
    unittest.main()
