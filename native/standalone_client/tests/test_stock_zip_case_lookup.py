"""Run actual SDK ZIP producers/consumers and owning maps on the host.

This establishes canonical-key behavior and removes the linear fallback. The
host CRT shim covers C-locale bytes, not Windows locale or load-time acceptance.
"""
import importlib.util
import os
from pathlib import Path
import shutil
import subprocess
import sys
import tarfile
import tempfile
import unittest
from unittest.mock import patch

from native_test_compile_flags import native_test_compile_flags
from test_stock_cursor_camera import function

ROOT = Path(__file__).resolve().parents[1]
SPEC = importlib.util.spec_from_file_location('zip_case_patch', ROOT / 'bigworld/patch_stock_zip_case_lookup.py')
patcher = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(patcher)
SOURCE = patcher.SOURCE


def pinned_sources(test):
    archive = Path(os.environ.get('WOT_PUBLIC_ENGINE_ARCHIVE', '/tmp/wot-bigworld-engine-14.4.1.tar.gz'))
    if not archive.is_file():
        raise unittest.SkipTest('Pinned public BigWorld archive required')
    result = {}
    with tarfile.open(archive) as sdk:
        for member in sdk:
            for relative in (SOURCE, 'lib/cstdmf/stringmap.hpp'):
                if member.name.endswith('/programming/bigworld/' + relative):
                    result[relative] = sdk.extractfile(member).read().decode('utf-8-sig')
            if len(result) == 2:
                return result
    raise AssertionError('Pinned ZIP and owning StringMap sources missing')


def generated_host(sources, zip_source):
    stringmap = sources['lib/cstdmf/stringmap.hpp']
    # The production policy really allocates length-aware keys, and the real
    # StringMap performs find, insertion, erasure, copy and teardown.
    maps = '\n'.join(function(stringmap, signature) + ';' for signature in (
        'class StrdupPolicy', 'struct StringLess', 'struct OwningStringRefPolicy'))
    start = stringmap.index('template< class MAPPED_TYPE, class POLICY = StrdupPolicy,')
    maps += '\n' + stringmap[start:stringmap.index('\n\ntemplate< class POLICY', start)]
    start = stringmap.index('template< class mapped_type, class policy = OwningStringRefPolicy >')
    maps += '\n' + stringmap[start:stringmap.index('\n\ntemplate', start + 1)]
    lookups = '\n'.join(function(zip_source, signature) for signature in (
        'void cleanSlashes(',
        'template< class MapType > inline typename MapType::iterator fileMapLookup(',
        'template< class MapType > inline typename MapType::const_iterator fileMapLookupConst('))
    methods = '\n'.join(function(zip_source, signature) for signature in (
        'bool ZipFileSystem::checkDuplicate(', 'BW::StringRef ZipFileSystem::decodeDuplicate(',
        'BW::string ZipFileSystem::encodeDuplicate(', 'void ZipFileSystem::resolvePath(',
        'bool ZipFileSystem::resolveDuplicate(', 'void ZipFileSystem::updateFile(',
        'bool ZipFileSystem::writeToStructure(', 'bool ZipFileSystem::eraseFileOrDirectory(',
        'bool ZipFileSystem::moveFileOrDirectory(', 'bool ZipFileSystem::copyFileOrDirectory(',
        'bool ZipFileSystem::dirTest(', 'void ZipFileSystem::clear(', 'void ZipFileSystem::closeZip('))
    start = zip_source.index('\t\tcleanSlashes( localFile.filename_ );')
    end = zip_source.index('\n\t}\n\n\n\treturn true;', start)
    ingest = zip_source[start:end]
    # The skipped bytes are only physical ZIP record offsets, not map behavior.
    ingest = ingest.replace('\t\tdirOffset += entry.extraFieldLength + entry.fileCommentLength;', '')
    host = (ROOT / 'tests/stock_zip_case_lookup_host.hpp').read_text()
    for token, value in (('@MAP@', maps), ('@LOOKUPS@', lookups), ('@METHODS@', methods), ('@INGEST@', ingest)):
        assert host.count(token) == 1
        host = host.replace(token, value)
    return host


class ZipCasePatchTests(unittest.TestCase):
    def setUp(self):
        self.source = pinned_sources(self)[SOURCE]
        self.temporary = tempfile.TemporaryDirectory(prefix='stock-zip-patch-')
        self.addCleanup(self.temporary.cleanup)
        self.root = Path(self.temporary.name)
        self.path = self.root / SOURCE
        self.path.parent.mkdir(parents=True)
        self.path.write_text(self.source)

    def test_complete_patch_is_idempotent_and_preserves_other_algorithms(self):
        self.assertTrue(patcher.patch(self.root))
        final = self.path.read_text()
        self.assertEqual(patcher.patch_source(final), final)
        mtime = self.path.stat().st_mtime_ns
        self.assertFalse(patcher.patch(self.root))
        self.assertEqual(self.path.stat().st_mtime_ns, mtime)
        restored = final
        for before, after in patcher.SEAMS:
            restored = restored.replace(after, before, 1)
        self.assertEqual(restored, self.source)
        for signature in ('void ZipFileSystem::updateFile(', 'void ZipFileSystem::clear(',
                          'bool ZipFileSystem::eraseFileOrDirectory(', 'void ZipFileSystem::closeZip('):
            self.assertEqual(function(final, signature), function(self.source, signature))

    def test_partial_duplicated_or_changed_seams_fail_without_write(self):
        final = patcher.patch_source(self.source)
        cases = []
        for before, after in patcher.SEAMS:
            cases += [self.source.replace(before, after, 1), self.source + before,
                      final + after, final.replace(after, after.replace('{', '{ /* drift */', 1), 1),
                      self.source.replace(before, before.replace('{', '{ /* drift */', 1), 1)]
        for source in cases:
            with self.subTest(source=source[-120:]):
                self.path.write_text(source)
                before = self.path.read_bytes()
                with self.assertRaisesRegex(ValueError, 'partial, duplicated or changed'):
                    patcher.patch(self.root)
                self.assertEqual(self.path.read_bytes(), before)
                self.assertEqual(list(self.path.parent.iterdir()), [self.path])

    def test_atomic_replace_failure_preserves_source_and_removes_temporary(self):
        before = self.path.read_bytes()
        with patch.object(patcher.os, 'replace', side_effect=OSError('injected rename failure')):
            with self.assertRaisesRegex(OSError, 'injected rename'):
                patcher.patch(self.root)
        self.assertEqual(self.path.read_bytes(), before)
        self.assertEqual(list(self.path.parent.iterdir()), [self.path])


class ZipCaseHostTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        compiler = shutil.which(os.environ.get('CXX', 'c++'))
        if not compiler:
            raise unittest.SkipTest('Host C++ compiler required')
        sources = pinned_sources(cls)
        cls.temporary = tempfile.TemporaryDirectory(prefix='stock-zip-host-')
        cls.addClassCleanup(cls.temporary.cleanup)
        cls.directory = Path(cls.temporary.name)
        cls.binaries = {}
        for label, source in (('original_sdk', sources[SOURCE]), ('patched', patcher.patch_source(sources[SOURCE]))):
            cpp, binary = cls.directory / (label + '.cpp'), cls.directory / label
            cpp.write_text(generated_host(sources, source))
            subprocess.run([compiler, '-std=c++17', '-O1', '-UNDEBUG', '-Wall', '-Wextra', '-Werror',
                            '-Wno-unused-parameter', '-Wno-unused-function', *native_test_compile_flags(compiler),
                            str(cpp), '-o', str(binary)], check=True)
            cls.binaries[label] = binary

    def test_normalization_native_maps_central_collision_and_mutation_lifecycle(self):
        subprocess.run([str(self.binaries['patched'])], check=True)

    def test_regression_restores_last_central_entry_and_eliminates_linear_miss_scan(self):
        reports = {}
        for label, binary in self.binaries.items():
            report = subprocess.check_output([str(binary), 'report'], text=True).splitlines()
            reports[label] = [list(map(int, line.split())) for line in report]
        self.assertEqual(reports['original_sdk'][0], [0, 1])
        self.assertEqual(reports['patched'][0], [1, 1])
        self.assertEqual(reports['original_sdk'][1][0], 16384 * 32)
        self.assertEqual(reports['patched'][1][0], 0)
        # The production map remains a tree. Misses are logarithmic and cannot
        # visit every key through case checking as the old SDK did.
        self.assertLess(reports['patched'][1][1], 32 * 64)


if __name__ == '__main__':
    unittest.main()
