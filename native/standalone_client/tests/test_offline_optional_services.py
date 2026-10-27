"""Run the offline optional-service policies against exact #1513 consumers."""
import ast
import base64
import json
import os
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest
import zipfile

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT.parents[1] / 'tools'))
import packed_xml

ENTRY_ROOT = ROOT / 'bigworld/offline_entry'
HARNESS = Path(__file__).with_name('optional_services_harness.py')
PY27 = Path(os.environ.get('WOT_TEST_PY27', '/Users/peng/.pyenv/versions/2.7.18/bin/python2.7'))
PINNED_GAMEPLAY = Path('src/res/scripts/client/gui/mods/offline_lan_0922')


def _text(value):
    # Type 5 is BigWorld's base64 blob; readString returns its encoding.
    if value.value_type == packed_xml.TYPE_COMPRESSED_STRING:
        return base64.b64encode(value.value).decode('ascii')
    if value.value_type == packed_xml.TYPE_STRING:
        return value.value.decode('utf-8')
    return value.value


def vibration_settings(client):
    """The exact vibroeffects/effects_settings.xml read by EffectsSettings."""
    with zipfile.ZipFile(client / 'res/packages/misc.pkg') as archive:
        root = packed_xml.read_packed_xml(archive.read('vibroeffects/effects_settings.xml'))
    data = {'values': {}, 'buttons': {}, 'effects': []}
    for name, value in root.children:
        name = name.decode('ascii')
        if name == 'buttons':
            data['buttons'] = {key.decode('ascii'): _text(child)
                               for key, child in value.value.children}
        elif name == 'effects':
            data['effects'] = [{key.decode('ascii'): _text(field)
                                for key, field in effect.value.children}
                               for unused, effect in value.value.children]
        else:
            data['values'][name] = _text(value)
    return data


def pinned_server_settings(source):
    tree = ast.parse((source / PINNED_GAMEPLAY / 'compat.py').read_text(encoding='utf-8'))
    for node in tree.body:
        if isinstance(node, ast.Assign) and getattr(node.targets[0], 'id', '') == '_SERVER_SETTINGS':
            return ast.literal_eval(node.value)
    raise AssertionError('The pinned offline account settings are missing')


class OptionalServiceConsumerTests(unittest.TestCase):
    def setUp(self):
        client = os.environ.get('WOT_0922_CLIENT')
        if not client or not PY27.is_file():
            self.skipTest('WOT_0922_CLIENT and CPython 2.7 are required')
        self.client = Path(client)
        self.package = str(self.client / 'res/packages/scripts.pkg')

    def harness(self, case, data=None):
        command = [str(PY27), '-E', '-s', '-B', str(HARNESS), self.package, str(ENTRY_ROOT), case]
        with tempfile.TemporaryDirectory(prefix='offline-optional-') as temporary:
            if data is not None:
                path = Path(temporary) / 'data.json'
                path.write_text(json.dumps(data))
                command.append(str(path))
            result = subprocess.run(command, capture_output=True, text=True,
                                    env=dict(os.environ, PYTHONDONTWRITEBYTECODE='1'))
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        self.assertIn('PASS', result.stdout)

    def test_login_preferences_keep_defaults_without_credential_io(self):
        self.harness('preferences')

    def test_messenger_keeps_battle_chat_without_lobby_chat_or_cache(self):
        self.harness('messenger')

    def test_vibration_owner_runs_the_stock_disconnected_lifecycle(self):
        data = vibration_settings(self.client)
        self.assertIn('move_low_veff', {effect['name'] for effect in data['effects']})
        self.harness('vibration', data)

    def pinned_settings(self):
        source = os.environ.get('WOT_FORK_0101_SOURCE')
        if not source:
            self.skipTest('The fixed v0.10.1 source input is required')
        return Path(source), pinned_server_settings(Path(source))

    def test_pinned_offline_payload_never_initialises_voice(self):
        unused, settings = self.pinned_settings()
        self.assertFalse([key for key in settings if key.startswith('voip')])
        self.harness('voice', {'pinned_server_settings': settings})

    def test_pinned_offline_payload_keeps_xmpp_disabled(self):
        source, settings = self.pinned_settings()
        self.assertFalse([key for key in settings if key.startswith('xmpp')])
        # Account.__synchronizeServerSettings is the only later updater; the
        # pinned account service never sends a serverSettings diff.
        producers = [path for path in (source / 'src').rglob('*.py')
                     if '/tests/' not in path.as_posix() and
                     "'serverSettings'" in path.read_text(encoding='utf-8')]
        self.assertEqual(producers, [])
        self.harness('xmpp', {'pinned_server_settings': settings})


class OptionalServicesSourceTests(unittest.TestCase):
    def test_module_compiles_for_python_27(self):
        if not PY27.is_file():
            self.skipTest('CPython 2.7 is required')
        for name in ('OfflineOptionalServices.py', 'OfflineGarageEntry.py'):
            source = ENTRY_ROOT / name
            subprocess.run([str(PY27), '-E', '-s', '-B', '-c',
                            'import sys; compile(open(sys.argv[1], "rb").read(), sys.argv[1], "exec")',
                            str(source)], check=True)


if __name__ == '__main__':
    unittest.main()
