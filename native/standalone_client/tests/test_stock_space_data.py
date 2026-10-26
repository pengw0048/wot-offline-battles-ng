"""Pinned engine lifecycle seams and exact-resource typed-space fixtures."""
from pathlib import Path
import importlib.util
import os
import subprocess
import tarfile
import tempfile
import unittest
import zipfile

ROOT = Path(__file__).resolve().parents[1]


def load(name, path):
    spec = importlib.util.spec_from_file_location(name, path)
    result = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(result)
    return result


PATCH = load('patch_stock_space_data', ROOT / 'bigworld/patch_stock_space_data.py')
FIXTURE = load('stock_space_data_fixture', ROOT / 'tests/stock_space_data_fixture.py')


class SpaceDataLifecyclePatchTests(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory(prefix='stock-space-data-patch-')
        self.root = Path(self.temporary.name)
        archive_path = Path(os.environ.get('WOT_PUBLIC_ENGINE_ARCHIVE', '/tmp/wot-bigworld-engine-14.4.1.tar.gz'))
        if not archive_path.exists():
            self.skipTest('Pinned public-engine archive is required')
        self.paths = ('client/script_bigworld.cpp', 'lib/space/client_space.cpp')
        with tarfile.open(archive_path) as archive:
            names = archive.getnames()
            for relative in self.paths:
                matches = [name for name in names if name.endswith('/programming/bigworld/'+relative)]
                self.assertEqual(len(matches), 1)
                target = self.root / relative
                target.parent.mkdir(parents=True, exist_ok=True)
                target.write_bytes(archive.extractfile(matches[0]).read())

    def tearDown(self):
        self.temporary.cleanup()

    def snapshot(self):
        return {str(path.relative_to(self.root)): path.read_bytes() for path in self.root.rglob('*') if path.is_file()}

    def test_real_script_and_space_lifecycle_is_idempotent(self):
        PATCH.patch(self.root)
        first = self.snapshot()
        PATCH.patch(self.root)
        self.assertEqual(self.snapshot(), first)
        script = (self.root / self.paths[0]).read_text()
        self.assertEqual(script.count('initialiseStockSpaceData()'), 1)
        self.assertEqual(script.count('finaliseStockSpaceData()'), 1)
        self.assertLess(script.index('initialiseStockSpaceData()'), script.index('bool ret = EntityType::init();'))
        self.assertLess(script.index('finaliseStockSpaceData()'), script.index('Script::fini();'))
        native = (self.root / self.paths[1]).read_text()
        for function, terminal in (('ClientSpace::~ClientSpace()', 'SpaceManager::instance().delSpace( this );'),
                                   ('void ClientSpace::clear()', 'doClear();')):
            body = native[native.index(function):]
            self.assertLess(body.index('isCleared_ = true;'), body.index('retireStockSpaceData( id_, this );'))
            self.assertLess(body.index('retireStockSpaceData( id_, this );'), body.index(terminal))
        self.assertEqual((self.root / 'lib/stock_space_data.hpp').read_bytes(),
                         (ROOT / 'bigworld/stock_space_data.hpp').read_bytes())
        # A local createSpace has no schema. Publication belongs to the
        # explicit typed native producer, never a generic creation event.
        self.assertNotIn('publishStockSpaceData(', native)
        self.assertNotIn('GeneralSpaceData', script)
        self.assertNotIn('entity_manager.cpp', first)

    def test_later_invalid_seam_leaves_all_files_unchanged(self):
        native = self.root / self.paths[1]
        native.write_text(native.read_text().replace('ClientSpace::~ClientSpace()', 'UnrecognizedNativeSpace::~UnrecognizedNativeSpace()'))
        before = self.snapshot()
        with self.assertRaises(ValueError):
            PATCH.patch(self.root)
        self.assertEqual(self.snapshot(), before)


class SpaceDataResourceTests(unittest.TestCase):
    def test_original_bytes_and_varied_real_parser_inputs(self):
        client = Path(os.environ.get('WOT_0922_CLIENT', '/Users/peng/Downloads/World_of_Tanks_0.09.22.00.01_CH_1513_HD'))
        if not (client / 'res/packages/scripts.pkg').exists():
            self.skipTest('Exact #1513 resource archive is required')
        with tempfile.TemporaryDirectory(prefix='stock-space-data-fixture-') as folder:
            folder = Path(folder)
            FIXTURE.prepare(client, folder)
            with zipfile.ZipFile(client / 'res/packages/scripts.pkg') as archive:
                names = [name for name in archive.namelist() if name == 'scripts/spaces.xml' or
                         name.startswith('scripts/space_defs/') and name.endswith('.def')]
                for name in names:
                    self.assertEqual((folder / 'original' / name).read_bytes(), archive.read(name))
            alternate = (folder / 'alternate/scripts/space_defs/FixtureSpace.def').read_text()
            self.assertIn('<Parent>GeneralSpaceData</Parent>', alternate)
            self.assertIn('<Exposed>false</Exposed>', alternate)
            self.assertIn('<Type>MaskAlias</Type>', alternate)
            probe = os.environ.get('WOT_SPACE_DATA_PROBE')
            if probe:
                for name, arguments in (('original', []), ('alternate', ['--alternate']), ('invalid', ['--invalid'])):
                    subprocess.run([probe, str(folder / name)]+arguments, check=True)


if __name__ == '__main__':
    unittest.main()
