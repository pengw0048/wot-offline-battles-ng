import io
import json
from pathlib import Path
import sys
import tempfile
import unittest
from unittest.mock import patch
import xml.etree.ElementTree as ET
import zipfile


TOOLS = Path(__file__).resolve().parents[1] / 'tools'
sys.path.insert(0, str(TOOLS))
import prepare_native_client as prepare


class NativeClientResourcePaths(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory()
        self.addCleanup(self.temporary.cleanup)
        self.root = Path(self.temporary.name).resolve()
        self.client = self.root / 'owned'
        self.package = self.root / 'independent'
        packages = self.client / 'res/packages'
        packages.mkdir(parents=True)
        for name in ('scripts.pkg', 'hd.pkg', 'sd.pkg'):
            with zipfile.ZipFile(packages / name, 'w') as archive:
                archive.writestr('shared.xml', name.encode('ascii'))
        (self.client / 'paths.xml').write_text('''<root><Paths>
          <Path>res_mods/0.9.22.0.1</Path>
          <Path>res/packages/scripts.pkg</Path>
          <Path type="sd">res/packages/sd.pkg</Path>
          <Path type="hd">res/packages/hd.pkg</Path>
          <Path>res</Path>
        </Paths></root>''')
        (self.client / 'app_type.xml').write_bytes(b'<protocol><app_type>hd</app_type><switch_to_type>sd</switch_to_type></protocol>\r\n')
        for name in ('bwclient_h.exe', 'res/resources.xml', 'engine-res/resources.xml',
                     'offline-res/scripts/client/OfflineGarageEntry.py',
                     'offline-res/scripts/client/OfflineOptionalServices.py'):
            path = self.package / name
            path.parent.mkdir(parents=True, exist_ok=True)
            path.touch()
        (self.package / 'offline-res.json').write_text('{"revision": "pinned"}')

    def test_original_startup_wins_over_bootstrap(self):
        with patch.object(prepare, 'inspect_client', return_value={'version': '0.9.22.0.1', 'build': '1513'}):
            prepare.prepare(self.client, self.package)
        paths = [Path(node.text) for node in ET.parse(self.package / 'paths.xml').findall('Paths/Path')]
        self.assertEqual(paths, [self.package / 'native-res', self.package / 'offline-res',
                                self.client / 'res/packages/scripts.pkg',
                                self.client / 'res/packages/hd.pkg',
                                self.client / 'res', self.package / 'res',
                                self.package / 'engine-res'])
        # Resolve a duplicate exactly as the resource manager does: an owned
        # original entity table cannot be hidden by a stale diagnostic table.
        for root, text in ((self.client / 'res', '<Avatar/><Vehicle/>'),
                           (self.package / 'res', '<StandaloneEntity/>')):
            target = root / 'scripts/entities.xml'
            target.parent.mkdir(parents=True, exist_ok=True)
            target.write_text(text)
        selected = next(root / 'scripts/entities.xml' for root in paths
                        if (root / 'scripts/entities.xml').is_file())
        self.assertEqual(selected.read_text(), '<Avatar/><Vehicle/>')
        state = json.loads((self.package / 'original-client.json').read_text())
        self.assertEqual(state['personality'], 'game')
        self.assertEqual(state['offline_runtime_revision'], 'pinned')
        self.assertFalse(state['original_executable_used_at_runtime'])
        self.assertEqual((self.package / 'app_type.xml').read_bytes(), (self.client / 'app_type.xml').read_bytes())

    def test_missing_authored_package_preserves_previous_paths(self):
        (self.package / 'paths.xml').write_text('previous configuration')
        (self.client / 'res/packages/hd.pkg').unlink()
        with patch.object(prepare, 'inspect_client', return_value={'version': '0.9.22.0.1', 'build': '1513'}):
            with self.assertRaisesRegex(ValueError, 'Missing authored HD package'):
                prepare.prepare(self.client, self.package)
        self.assertEqual((self.package / 'paths.xml').read_text(), 'previous configuration')

    def test_missing_original_content_metadata_preserves_previous_configuration(self):
        (self.package / 'paths.xml').write_text('previous configuration')
        (self.package / 'app_type.xml').write_text('previous metadata')
        (self.client / 'app_type.xml').unlink()
        with patch.object(prepare, 'inspect_client', return_value={'version': '0.9.22.0.1', 'build': '1513'}):
            with self.assertRaises(FileNotFoundError):
                prepare.prepare(self.client, self.package)
        self.assertEqual((self.package / 'paths.xml').read_text(),'previous configuration')
        self.assertEqual((self.package / 'app_type.xml').read_text(),'previous metadata')

    def test_reader_matches_hd_mount_order_and_nested_terrain(self):
        nested = io.BytesIO()
        with zipfile.ZipFile(nested, 'w') as archive:
            archive.writestr('terrain2/heights', b'original terrain bytes')
        with zipfile.ZipFile(self.client / 'res/packages/hd.pkg', 'a') as archive:
            archive.writestr('spaces/test/00000000o.cdata_processed', nested.getvalue())
        (self.client / 'res/shared.xml').write_bytes(b'loose original')
        with prepare.OriginalResources(self.client) as resources:
            self.assertEqual(resources.read('shared.xml'), b'scripts.pkg')
            self.assertEqual(resources.read('spaces/test/00000000o.cdata_processed/terrain2/heights'),
                             b'original terrain bytes')
        tree = ET.parse(self.client / 'paths.xml')
        paths = tree.find('Paths')
        loose = paths.findall('Path')[-1]
        paths.remove(loose)
        paths.insert(0, loose)
        tree.write(self.client / 'paths.xml')
        with prepare.OriginalResources(self.client) as resources:
            self.assertEqual(resources.read('shared.xml'), b'loose original')


if __name__ == '__main__':
    unittest.main()
