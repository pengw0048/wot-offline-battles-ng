"""Exercise launcher data imports without a gameplay source or server payload."""
import os
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile
import unittest

import stage_payload


class FrozenLauncherContractsTest(unittest.TestCase):
    def setUp(self):
        self.source = Path(__file__).resolve().parents[2]
        self.temporary = tempfile.TemporaryDirectory()
        self.addCleanup(self.temporary.cleanup)
        self.root = Path(self.temporary.name)

    def test_only_explicit_launcher_helpers_are_staged(self):
        target = self.root / stage_payload.LAUNCHER_CONTRACTS_DIR
        written = stage_payload.stage_launcher_contracts(target, self.source)
        expected = {stage_payload.LAUNCHER_CONTRACTS_RELATIVE + '/' + relative
                    for relative in stage_payload.LAUNCHER_CONTRACTS}
        self.assertEqual(expected, {Path(path).relative_to(target).as_posix()
                                    for path in written})
        for relative in expected:
            source = self.source / 'src/res/scripts/client' / relative
            self.assertEqual(source.read_bytes(), (target / relative).read_bytes())
        self.assertFalse((self.root / 'servers').exists())

    def test_frozen_imports_and_interior_catalogue_need_no_source_tree(self):
        stage_payload.stage_launcher_contracts(
            self.root / stage_payload.LAUNCHER_CONTRACTS_DIR, self.source)
        shutil.copytree(self.source / 'launcher', self.root / 'launcher',
                        ignore=shutil.ignore_patterns('__pycache__', '*.pyc'))
        script = r'''
from pathlib import Path
import sys
root = Path(sys.argv[1])
sys.path[:0] = [str(root / 'launcher'), sys.argv[2]]
sys.frozen = True
sys._MEIPASS = str(root)
import wot_launcher
import bot_tactics_store
import crew_store
import save_services
import vehicle_internal_viewer
assert bot_tactics_store.contract.empty()['schema']
assert crew_store.crew_skills.seats('ussr:R11_MS-1')
assert save_services.policy.reserve_state({})['counts']
assert vehicle_internal_viewer.catalogue()
from gui.mods.offline_lan_0922 import bot_gunnery, config
assert not (root / 'servers').exists()
assert not (root / 'src').exists()
for name, module in tuple(sys.modules.items()):
    if name.startswith('gui.') and getattr(module, '__file__', None):
        assert Path(module.__file__).is_relative_to(root / 'launcher-contracts'), name
assert 'server_bot_ai' not in sys.modules
assert 'lan_battle_server' not in sys.modules
print('Frozen launcher data boundary passed')
'''
        environment = dict(os.environ, PYTHONDONTWRITEBYTECODE='1')
        completed = subprocess.run(
            [sys.executable, '-I', '-B', '-c', script, str(self.root),
             str(self.source / 'tools')], cwd=self.root, env=environment,
            capture_output=True, text=True, timeout=30)
        self.assertEqual(0, completed.returncode, completed.stdout + completed.stderr)
        self.assertIn('Frozen launcher data boundary passed', completed.stdout)

    def test_an_incomplete_contract_source_is_rejected(self):
        with self.assertRaisesRegex(ValueError, 'Launcher data helper is missing'):
            stage_payload.stage_launcher_contracts(self.root / 'out', self.root / 'missing')


if __name__ == '__main__':
    unittest.main()
