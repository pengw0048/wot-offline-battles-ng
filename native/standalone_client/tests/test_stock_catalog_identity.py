"""Raw WGDE row identity shared by the native scene owner and the staged catalogs.

These prove source/data agreement and the pinned loaders' acceptance, not a
Windows battle.
"""
from pathlib import Path
import io
import json
import os
import subprocess
import sys
import tarfile
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
REPOSITORY = ROOT.parents[1]
sys.path.insert(0, str(ROOT / 'bigworld'))
sys.path.insert(0, str(ROOT / 'tests'))
sys.path.insert(0, str(REPOSITORY / 'tools'))
CXX = os.environ.get('CXX', 'clang++')
PINNED = '144619f6eb2925f8da4b13637bf65b8550fc898f'
PYTHON27 = Path(os.environ.get('WOT_PYTHON27', '/Users/peng/.pyenv/versions/2.7.18/bin/python2.7'))

import stock_catalog_identity as identity  # noqa: E402

PINNED_LOADERS = r'''
import imp, sys, types
pinned, staged = sys.argv[1], sys.argv[2]
package = 'gui.mods.offline_lan_0922'
for name in ('gui', 'gui.mods', package):
    module = types.ModuleType(name)
    module.__path__ = [pinned + '/' + name.replace('.', '/')]
    sys.modules[name] = module
config = types.ModuleType(package + '.config')
config.CONFIG_PATH = staged + '/config.json'
sys.modules[package + '.config'] = config
def load(name):
    module = imp.load_source(package + '.' + name, pinned + '/gui/mods/offline_lan_0922/' + name + '.py')
    sys.modules[package + '.' + name] = module
    return module
load('navigation_graph_schema'); load('prebaked_navigation')
destructibles, foliage = load('prebaked_destructibles'), load('prebaked_foliage')
for name in destructibles.SUPPORTED_MAPS:
    assert destructibles.load_catalog(name) is not None, name
    assert foliage.load_foliage(name) is not None, name
van = [row[14:16] for row in destructibles.load_catalog('07_lakeville')['instances']
       if row[12].endswith('env420_OldGLightVan_02.model')]
assert [33149, 189] in van, van
print(len(destructibles.SUPPORTED_MAPS))
'''


def pinned_tree(directory):
    """The pinned release's catalogs and gameplay loaders, from this repository's history."""
    archive = subprocess.run(
        ['git', '-C', str(REPOSITORY), 'archive', PINNED, 'destructibles', 'foliage', 'navgraphs',
         'src/res/scripts/client/gui/mods/offline_lan_0922'], capture_output=True)
    if archive.returncode:
        return None
    with tarfile.open(fileobj=io.BytesIO(archive.stdout)) as tar:
        tar.extractall(directory)
    return Path(directory)


class StockCatalogIdentityTest(unittest.TestCase):
    def test_raw_row_skips_every_empty_row_at_or_before_it(self):
        cases = [
            ([], 0, 0), ([], 5, 5),
            ([0], 0, 1), ([0, 1, 2], 0, 3),
            ([188], 187, 187), ([188], 188, 189), ([188], 220, 221),
            ([2, 3], 1, 1), ([2, 3], 2, 4), ([2, 3], 3, 5),
            ([1, 4, 5, 9], 3, 6), ([1, 4, 5, 9], 6, 10),
        ]
        for empty, compacted, raw in cases:
            self.assertEqual(raw, identity.raw_row(compacted, empty), (empty, compacted))
        # Inverse: raw rows are strictly increasing and never land on an empty row.
        empty = [0, 3, 4, 8]
        rows = [identity.raw_row(index, empty) for index in range(10)]
        self.assertEqual(rows, sorted(set(rows)))
        self.assertFalse(set(rows) & set(empty))

    def test_rewire_changes_only_identity_fields(self):
        chunks = {'7': [1], '9': [0, 2]}
        signature = list(range(12))
        destructibles = {
            'instances': [signature + ['a.model', 0, 7, 1, 1.0],
                          signature + ['b.model', None, 9, 1, 2.0],
                          signature + ['c.model', 1, 8, 4, 1.0]],
            'tree_instances': [signature + ['t.spt', 7, 0], signature + ['u.spt', 9, 0]],
            'excluded_instances': [[7, 2, 2], [8, 0, 1], [9, 1, 0x7fff4000]],
            'ambiguous_instances': [signature + [[['d.model', 0]]]],
            'source': {'space_bin_sha256': 'x'},
        }
        result = identity.rewire_destructibles(json.loads(json.dumps(destructibles)), chunks)
        self.assertEqual([row[14:16] for row in result['instances']], [[7, 2], [9, 3], [8, 4]])
        self.assertEqual([row[13:15] for row in result['tree_instances']], [[7, 0], [9, 1]])
        self.assertEqual([row[:2] for row in result['excluded_instances']], [[7, 3], [8, 0], [9, 3]])
        for key in ('instances', 'tree_instances', 'excluded_instances'):
            for before, after in zip(destructibles[key], result[key]):
                first = {'instances': 14, 'tree_instances': 13, 'excluded_instances': 0}[key]
                self.assertEqual(before[:first] + before[first + 2:], after[:first] + after[first + 2:])
        self.assertEqual(destructibles['ambiguous_instances'], result['ambiguous_instances'])
        foliage = identity.rewire_foliage({'fallen_trees': [[9, 0, 1.5, None], [7, 0, 2.0, 3]]}, chunks)
        self.assertEqual(foliage['fallen_trees'], [[9, 1, 1.5, None], [7, 0, 2.0, 3]])

    def test_committed_table_binds_every_catalog_space(self):
        table = identity.load_table()
        manifest = json.loads((REPOSITORY / 'destructibles/manifest.json').read_text(encoding='utf-8'))
        spaces = dict((record['map'], record['space_bin_sha256']) for record in manifest['maps'])
        self.assertEqual(set(spaces), set(table['maps']))
        for name, entry in table['maps'].items():
            self.assertEqual(spaces[name], entry['space_bin_sha256'], name)
            for rows in entry['empty_rows'].values():
                self.assertEqual(rows, sorted(set(rows)))
        affected = sorted(name for name, entry in table['maps'].items() if entry['empty_rows'])
        self.assertEqual(['07_lakeville', '11_murovanka', '18_cliff', '23_westfeld',
                          '34_redshire', '36_fishing_bay'], affected)
        self.assertEqual([188], table['maps']['07_lakeville']['empty_rows']['33149'])

    def test_real_client_rows_catalogs_and_native_owner_agree(self):
        client = os.environ.get('WOT_0922_CLIENT')
        if not client:
            self.skipTest('WOT_0922_CLIENT is required for the original space data')
        import bake_destructibles_0922 as baker
        import zipfile
        from space_bin_0922 import CompiledSpace as RawSpace
        from test_stock_battle_scene_queries import IDENTITY_PROBE
        from wot_space_bin_utils import CompiledSpace
        self.assertEqual(identity.load_table(), baker.bake_empty_rows(client))
        with tempfile.TemporaryDirectory(prefix='catalog-identity-') as directory:
            path = Path(directory)
            (path / 'probe.cpp').write_text(IDENTITY_PROBE)
            subprocess.run([CXX, '-std=c++14', '-Wall', '-Wextra', '-Werror', '-I', str(ROOT / 'bigworld'),
                            str(path / 'probe.cpp'), '-o', str(path / 'probe')], check=True)
            shifted = 0
            for name in baker.SUPPORTED_MAPS:
                unused, package = baker._client_inputs(client, name)
                with zipfile.ZipFile(package) as archive:
                    space = archive.read('spaces/%s/space.bin' % name)
                compiled = CompiledSpace(io.BytesIO(space), baker.DECODER_VERSION, baker.DECODER_REGION,
                                         ['BSMI', 'WGDE', 'SpTr'])
                bsmi = len(compiled.sections['BSMI']._data['transforms'])
                rows, unused_wires, unused_trees = baker.native_wires(compiled, bsmi)
                (path / 'wgde').write_bytes(RawSpace(space).section_data('WGDE'))
                output = subprocess.check_output([str(path / 'probe'), str(path / 'wgde'), str(bsmi)], text=True)
                native = {}
                for line in output.split('\n'):
                    if line:
                        index, chunk, row = map(int, line.split())
                        native[index] = (chunk, row)
                # The catalog baker and the compiled scene owner give one identity per BSMI row.
                self.assertEqual(rows, native, name)
                table = identity.load_table()['maps'][name]['empty_rows']
                shifted += sum(1 for chunk, row in rows.values()
                               if any(empty < row for empty in table.get(str(chunk), ())))
            self.assertGreater(shifted, 0)
            print('Catalog baker and native WGDE owner: %d maps agree; %d BSMI rows follow an empty row'
                  % (len(baker.SUPPORTED_MAPS), shifted))

    def test_staged_pinned_catalogs_equal_a_raw_bake_and_load_in_the_pinned_runtime(self):
        client = os.environ.get('WOT_0922_CLIENT')
        if not client:
            self.skipTest('WOT_0922_CLIENT is required to bake the comparison catalogs')
        import bake_destructibles_0922 as baker
        import zipfile
        from wot_space_bin_utils import CompiledSpace
        with tempfile.TemporaryDirectory(prefix='catalog-stage-') as directory:
            path = Path(directory)
            pinned = pinned_tree(path / 'pinned')
            if pinned is None:
                self.skipTest('The pinned v0.10.1 commit is not in this repository')
            identity.stage(pinned, path / 'package')
            staged = path / 'package' / identity.CONFIG_ROOT
            baker.bake_all(client, str(path / 'baked'))
            for name in baker.SUPPORTED_MAPS:
                # Only identity differs between the pinned bake and a raw-row bake.
                self.assertEqual((path / 'baked' / (name + '.json')).read_bytes(),
                                 (staged / 'destructibles' / (name + '.json')).read_bytes(), name)
            unchanged = [name for name in baker.SUPPORTED_MAPS
                         if (pinned / 'foliage' / (name + '.json')).read_bytes() ==
                         (staged / 'foliage' / (name + '.json')).read_bytes()]
            self.assertEqual(35, len(unchanged))
            # Every staged fallen-tree wire is a raw SpTr wire of its own map.
            for name in baker.SUPPORTED_MAPS:
                unused, package = baker._client_inputs(client, name)
                with zipfile.ZipFile(package) as archive:
                    space = archive.read('spaces/%s/space.bin' % name)
                compiled = CompiledSpace(io.BytesIO(space), baker.DECODER_VERSION, baker.DECODER_REGION,
                                         ['BSMI', 'WGDE', 'SpTr'])
                unused_rows, unused_wires, trees = baker.native_wires(
                    compiled, len(compiled.sections['BSMI']._data['transforms']))
                data = json.loads((staged / 'foliage' / (name + '.json')).read_text(encoding='utf-8'))
                wires = set(tuple(row[:2]) for row in data['fallen_trees'])
                self.assertLessEqual(wires, set(trees.values()), name)
            for name in ('destructibles', 'foliage'):
                manifest = json.loads((staged / name / 'manifest.json').read_text(encoding='utf-8'))
                self.assertEqual(identity.IDENTITY_RULE, manifest['identity']['rule'])
            self.assertEqual(sorted(os.listdir(pinned / 'navgraphs')), sorted(os.listdir(staged / 'navgraphs')))
            if not PYTHON27.exists():
                self.skipTest('The pinned runtime loaders need CPython 2.7')
            (path / 'loaders.py').write_text(PINNED_LOADERS)
            output = subprocess.check_output(
                [str(PYTHON27), str(path / 'loaders.py'),
                 str(pinned / 'src/res/scripts/client'), str(staged)], text=True,
                env=dict(os.environ, PYTHONDONTWRITEBYTECODE='1'))
            self.assertEqual('41', output.strip())


if __name__ == '__main__':
    unittest.main()
