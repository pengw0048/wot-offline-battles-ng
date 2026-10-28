"""Compare original quality arithmetic and exercise the real SDK loader owners."""
import importlib.util
import itertools
import base64
import os
from pathlib import Path
import re
import shutil
import subprocess
import tarfile
import tempfile
import unittest
import zipfile

ROOT = Path(__file__).resolve().parents[1]
SPEC = importlib.util.spec_from_file_location('texture_quality_patch', ROOT/'bigworld/patch_stock_texture_quality.py')
PATCH = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(PATCH)
EXTRA = ('lib/moo/base_texture.cpp', 'lib/moo/streaming_texture.cpp')


def sources(test):
    archive = Path(os.environ.get('WOT_PUBLIC_ENGINE_ARCHIVE', '/tmp/wot-bigworld-engine-14.4.1.tar.gz'))
    if not archive.is_file():
        test.skipTest('Pinned public engine archive is required')
    with tarfile.open(archive) as tar:
        prefix = tar.getmembers()[0].name.split('/')[0] + '/programming/bigworld/'
        return {name: tar.extractfile(prefix+name).read().decode('utf-8') for name in PATCH.FILES + EXTRA}


def write_sources(root, values):
    for name, source in values.items():
        path = root/name
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_text(source)


def generated(values, original=None, negative=None):
    f = PATCH.function
    managed = original[PATCH.MANAGED] if negative == 'managed' else values[PATCH.MANAGED]
    streaming = original[PATCH.STREAMING] if negative == 'streaming' else values[PATCH.STREAMING]
    registration = values[PATCH.MANAGER]
    registration = registration[registration.index('// texture quality settings'):registration.index('// saved settings')]
    publication = (ROOT/'bigworld/stock_graphics_settings.cpp').read_text()
    entries = (
        f(values['lib/moo/base_texture.cpp'], 'BW::StringRef removeTextureExtension('),
        f(values['lib/moo/streaming_texture.cpp'], 'bool StreamingTextureLoader::loadHeaderFromStream('),
        f(values[PATCH.DETAIL], 'void TextureDetailLevel::init( DataSectionPtr'),
        f(values[PATCH.MANAGER], 'int calcMipSkip('),
        f(values[PATCH.MANAGER], 'uint32 TextureManager::stockBaseMipSkip('),
        f(values[PATCH.MANAGER], 'bool TextureManager::isCompressed('),
        f(values[PATCH.MANAGER], 'void TextureManager::setQualityOption('),
        f(values[PATCH.MANAGER], 'void TextureManager::reloadDirtyTextures('),
        f(values[PATCH.MANAGER], 'int TextureManager::configurableMipFilter('),
        f(values[PATCH.MANAGER], 'int TextureManager::configurableMinMagFilter('),
        f(values[PATCH.MANAGER], 'int TextureManager::configurableMaxAnisotropy('),
        f(managed, 'void ManagedTexture::load( const FailurePolicy&'),
        f(values[PATCH.MANAGED], 'ManagedTexture::ManagedTexture('),
        f(values[PATCH.MANAGED], 'class ManagedTexture::ReloadTextureTask') + ';',
        f(streaming, 'void TextureStreamingManager::updateTextureMipSkip('),
        f(values['lib/moo/streaming_texture.cpp'], 'void StreamingTexture::reload()'),
        f(values['lib/moo/streaming_texture.cpp'], 'void StreamingTexture::reload( const BW::StringRef'),
        f(values[PATCH.STREAMING], 'void TextureStreamingManager::reloadTexture('),
        f(streaming, 'bool TextureStreamingManager::StreamingTextureLoadTask::createTargetTexture('),
        'void TextureManager::registerSettings() {\n' + registration + '\n}',
        f(publication, 'Moo::GraphicsSetting::GraphicsSettingVector orderedSettings('),
        f(publication, 'PyObject* stockGraphicsSettingsView('))
    owners = '\n\n'.join(entries)
    if negative == 'locked_reload':
        owners = owners.replace('    reloadDirtyTextures();', '    RecursiveMutexHolder retainedLock(texturesLock_);\n    reloadDirtyTextures();')
    owners = owners.replace('newTexture_ = new ManagedTexture(', 'newTexture_.reset(new ManagedTexture(')
    owners = owners.replace(', stockSplit_ );', ', stockSplit_ ));')
    return (ROOT/'tests/stock_texture_quality_host.cpp').read_text().replace('@OWNERS@', owners)


class TextureQualityTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.scratch = tempfile.TemporaryDirectory(prefix='stock-texture-quality-')
        cls.root = Path(cls.scratch.name)
        cls.original = sources(cls)
        write_sources(cls.root, cls.original)
        PATCH.patch(cls.root)
        cls.patched = {name: (cls.root/name).read_text() for name in cls.original}
        compiler = shutil.which(os.environ.get('CXX', 'clang++'))
        if not compiler:
            raise unittest.SkipTest('Host C++ compiler is required')
        cls.binaries = {}
        for mode in ('after', 'without_counters', 'managed', 'streaming', 'locked_reload'):
            source, binary = cls.root/(mode+'.cpp'), cls.root/mode
            source.write_text(generated(cls.patched, cls.original, mode))
            result = subprocess.run([compiler, '-std=c++14', '-Wall', '-Wextra', '-Werror',
                                     '-Wno-reorder-ctor', '-Wno-unused-parameter', '-Wno-unused-private-field',
                                     '-fsanitize=address,undefined', '-I'+str(ROOT/'include'),
                                     '-DENABLE_RESOURCE_COUNTERS='+str(int(mode!='without_counters')),
                                     str(source), '-o', str(binary)], capture_output=True, text=True)
            if result.returncode:
                raise AssertionError(result.stdout+result.stderr)
            cls.binaries[mode] = binary

    @classmethod
    def tearDownClass(cls):
        cls.scratch.cleanup()

    def test_actual_managed_and_streaming_consumers_and_quality_reload(self):
        for mode in ('after', 'without_counters', 'managed', 'streaming', 'locked_reload'):
            result = subprocess.run([str(self.binaries[mode])], capture_output=True, text=True)
            if mode in ('after', 'without_counters'):
                self.assertEqual(result.returncode, 0, result.stdout+result.stderr)
                self.assertIn('Actual SDK texture quality consumers passed', result.stdout)
            else:
                self.assertNotEqual(result.returncode, 0, mode+' original owner unexpectedly passed')

    def test_exact_pe_quality_records_and_varied_mip_chains(self):
        client = os.environ.get('WOT_0922_CLIENT')
        if not client:
            self.skipTest('Exact #1513 client is required')
        from stock_texture_quality_oracle import TextureQualityOracle
        original = TextureQualityOracle(Path(client)/'WorldOfTanks.exe')
        cases, expected = [], []
        for cap in (0, 1, 2, 3, 4, 6, 8, 16):
            records = original.options(cap)
            for option, lod, split, count, logical in itertools.product(range(4), range(4),
                    (0, 1), (0, 1, 2, 3, 10, 11), (0, 1, 2, 3)):
                mip, filtering, compressed, supported = records[option]
                # MAX intentionally stays unsupported until HD upload exists.
                cases.append((cap, option, lod, split, count, logical))
                expected.append((mip, filtering, int(compressed), int(supported),
                                 original.logical_skip(mip, lod), original.base_skip(logical, split, count)))
        result = subprocess.run([str(self.binaries['after']), 'oracle'],
                                input=''.join(' '.join(map(str, row))+'\n' for row in cases),
                                capture_output=True, text=True, check=True)
        actual = [tuple(map(int, row.split())) for row in result.stdout.splitlines()]
        self.assertEqual(len(actual), len(cases))
        for case, want, got in zip(cases, expected, actual):
            self.assertEqual(want, got, case)

    def test_patch_transaction_and_only_texture_quality_registration_changes(self):
        before = {name: (self.root/name).read_text() for name in PATCH.FILES}
        PATCH.patch(self.root)
        self.assertEqual(before, {name: (self.root/name).read_text() for name in PATCH.FILES})
        original = self.original[PATCH.MANAGER]
        patched = self.patched[PATCH.MANAGER]
        for label in ('compressionSettings_', 'filterSettings_'):
            options = r'^.*this->'+label+r'->addOption.*$'
            self.assertEqual(re.findall(options, original, re.M), re.findall(options, patched, re.M))
        self.assertIn('addOption("MAX", "Max", false)', patched)
        self.assertIn('int qualitySetting = wot::stockTextureQuality(', patched)
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            write_sources(root, self.original)
            (root/PATCH.STREAMING).write_text('unknown owner')
            snapshot = {name: (root/name).read_text() for name in PATCH.FILES}
            with self.assertRaises(ValueError):
                PATCH.patch(root)
            self.assertEqual(snapshot, {name: (root/name).read_text() for name in PATCH.FILES})

    def test_composes_with_existing_streaming_capability_and_retirement_patch(self):
        spec = importlib.util.spec_from_file_location('texture_streaming_patch',
                                                      ROOT/'bigworld/patch_stock_texture_streaming.py')
        streaming = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(streaming)
        combined = []
        for order in ((PATCH.patch, streaming.patch), (streaming.patch, PATCH.patch)):
            with tempfile.TemporaryDirectory() as directory:
                root = Path(directory)
                write_sources(root, self.original)
                for apply in order:
                    apply(root)
                before = {name: (root/name).read_text() for name in self.original}
                for apply in order:
                    apply(root)
                self.assertEqual(before, {name: (root/name).read_text() for name in self.original})
                self.assertIn('supportsTextureFormat( header.format_ )', before[PATCH.STREAMING])
                self.assertIn('if (streamingTextureHandle_.isValid())', before[streaming.TEXTURE])
                combined.append(before)
        self.assertEqual(*combined)

    def test_exact_catalogue_keeps_supported_levels_below_split_top_mip(self):
        client = os.environ.get('WOT_0922_CLIENT')
        if not client:
            self.skipTest('Exact #1513 client is required')
        from tools.packed_xml import read_packed_xml, TYPE_COMPRESSED_STRING
        packages = Path(client)/'res/packages'
        with zipfile.ZipFile(packages/'misc.pkg') as package:
            root = read_packed_xml(package.read('system/data/texture_detail_levels.xml'))
        split_directories = set()
        for name, value in root.children:
            if name != b'directory':
                continue
            children = value.value.children
            path = next((base64.b64encode(v.value).decode() if v.value_type == TYPE_COMPRESSED_STRING
                         else v.value.decode()) for n, v in children if n == b'path')
            for field, rule in children:
                if field == b'rule':
                    pairs = {n: v.value for n, v in rule.value.children}
                    if pairs.get(b'allowSplit'):
                        split_directories.add(path)
                        self.assertNotIn(b'lodMode', pairs)
        self.assertEqual(split_directories, {'content', 'vehicles'})
        # .texformat replaces, rather than inherits, the directory detail.
        # No shipped override combines allowSplit with a lodMode 2/3 bias that
        # could ask HIGH for the unimplemented HD top mip.
        count = 0
        for path in packages.glob('*.pkg'):
            with zipfile.ZipFile(path) as package:
                for name in package.namelist():
                    if name.lower().endswith('.texformat'):
                        detail = read_packed_xml(package.read(name))
                        pairs = {n: v.value for n, v in detail.children}
                        self.assertFalse(pairs.get(b'allowSplit', False), name)
                        count += 1
        self.assertGreater(count, 0)


if __name__ == '__main__':
    unittest.main()
