"""Player UI mods are mounted without exposing them to simulation workers."""
import json
from pathlib import Path
import sys
import tempfile
import unittest
from unittest.mock import patch
import xml.etree.ElementTree as ET
import zipfile

ROOT = Path(__file__).resolve().parents[3]
sys.path.insert(0, str(ROOT / 'tools'))
import prepare_native_client as preparation


class UIModPreparationTests(unittest.TestCase):
    def setUp(self):
        temporary = tempfile.TemporaryDirectory()
        self.addCleanup(temporary.cleanup)
        self.root = Path(temporary.name).resolve()
        self.original = self.root / 'original'
        self.package = self.root / 'player'
        for name in ('res/packages/scripts.pkg', 'app_type.xml'):
            self.write(self.original / name, b'original')
        self.write(self.original / 'paths.xml',
                   b'<root><Paths><Path>res/packages/scripts.pkg</Path></Paths></root>')
        for name in ('bwclient_h.exe', 'res/resources.xml', 'engine-res/resources.xml',
                     'offline-res/scripts/client/OfflineGarageEntry.py',
                     'offline-res/scripts/client/OfflineOptionalServices.py'):
            self.write(self.package / name)
        self.write(self.package / 'offline-res.json', b'{"revision": "fixture"}')
        self.overlay = self.root / 'vehicle-overlay'
        self.overlay.mkdir()
        self.inspection = patch.object(preparation, 'inspect_client',
                                       return_value={'version': '0.9.22.0.1', 'build': '1513'})
        self.inspection.start()
        self.addCleanup(self.inspection.stop)

    def write(self, path, data=b'x'):
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_bytes(data)

    def prepare(self, enabled):
        record = preparation.prepare(self.original, self.package,
                                     overlays=[self.overlay], ui_mods=enabled)
        paths = [Path(node.text) for node in
                 ET.parse(self.package / 'paths.xml').findall('Paths/Path')]
        return record, paths

    def test_player_mounts_loose_and_nested_packages_below_owned_runtime(self):
        archives = self.package / 'mods/0.9.22.0.1'
        names = ('a.wotmod', 'nested/z.WOTMOD')
        for name in names:
            target = archives / name
            target.parent.mkdir(parents=True, exist_ok=True)
            with zipfile.ZipFile(target, 'w') as archive:
                archive.writestr('res/scripts/client/gui/mods/mod_example.pyc', b'fixture')
        self.write(archives / 'ignored.zip')
        self.write(self.original / 'mods/0.9.22.0.1/retail.wotmod')
        record, paths = self.prepare(True)
        expected = [self.package / 'ui-mods/0.9.22.0.1',
                    archives / names[1], archives / names[0]]
        self.assertEqual(paths[:3], [self.overlay, self.package / 'native-res',
                                    self.package / 'offline-res'])
        self.assertEqual(paths[3:6], expected)
        self.assertEqual(record['ui_mod_paths'], list(map(str, expected)))
        self.assertEqual(paths[6], self.original / 'res/packages/scripts.pkg')
        self.assertEqual((self.package / 'app_type.xml').read_bytes(), b'original')
        self.assertNotIn(self.original / 'mods/0.9.22.0.1/retail.wotmod', paths)

    def test_worker_excludes_existing_ui_mods_but_keeps_vehicle_overlay(self):
        self.write(self.package / 'ui-mods/0.9.22.0.1/scripts/client/gui/mods/mod_ui.pyc')
        self.write(self.package / 'mods/0.9.22.0.1/ui.wotmod')
        record, paths = self.prepare(False)
        self.assertEqual(record['ui_mod_paths'], [])
        self.assertEqual(paths[0], self.overlay)
        self.assertFalse(any('ui-mods' in str(path) or path.suffix == '.wotmod'
                             for path in paths))

    def test_removal_is_reflected_on_next_preparation_without_copying_mods(self):
        archive = self.package / 'mods/0.9.22.0.1/ui.wotmod'
        self.write(archive)
        self.assertIn(archive, self.prepare(True)[1])
        archive.unlink()
        self.assertNotIn(archive, self.prepare(True)[1])
        self.assertEqual(json.loads((self.package / 'original-client.json').read_text())
                         ['ui_mod_paths'], [str(self.package / 'ui-mods/0.9.22.0.1')])


if __name__ == '__main__':
    unittest.main()
