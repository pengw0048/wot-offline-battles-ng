"""Run compound aggregation with real SDK empty bounds and particle consumers."""
from pathlib import Path
import os
import shutil
import subprocess
import tarfile
import tempfile
import unittest

from native_test_compile_flags import native_test_compile_flags

ROOT = Path(__file__).resolve().parents[1]


def block(source, signature):
    start = source.index(signature)
    cursor = source.index('{', start)
    depth = 1
    while depth:
        cursor += 1
        depth += (source[cursor] == '{') - (source[cursor] == '}')
    return source[start:cursor + 1]

def compound_helpers(source):
    """Production per-frame part refs and evaluation scope, spliced after CompoundStatePtr."""
    return '\n'.join((block(source, 'struct CompoundPartRef') + ';',
                      block(source, 'inline std::vector<CompoundPartRef> compoundPartRefs('),
                      block(source, 'struct CompoundEvaluationScope') + ';', ''))


def sdk_sources(test):
    configured = os.environ.get('WOT_PUBLIC_ENGINE_ARCHIVE') or os.environ.get('WOT_BIGWORLD_SOURCE_ARCHIVE')
    archive = Path(configured or '/tmp/wot-bigworld-engine-14.4.1.tar.gz')
    if not archive.is_file():
        if configured:
            raise AssertionError('Configured engine archive is missing: ' + str(archive))
        raise unittest.SkipTest('Pinned public engine archive required')
    names = ('lib/math/boundbox.ipp', 'lib/math/boundbox.cpp',
             'lib/particle/particle_system.cpp', 'lib/particle/meta_particle_system.cpp')
    result = {}
    with tarfile.open(archive) as tar:
        for member in tar:
            for name in names:
                if member.name.endswith('/programming/bigworld/' + name):
                    result[name] = tar.extractfile(member).read().decode('utf-8')
            if len(result) == len(names):
                break
    if set(result) != set(names):
        raise AssertionError('Configured archive is missing required SDK owners')
    return result


def harness(sdk, before=False):
    source = (ROOT / 'bigworld/vehicle_appearance.cpp').read_text()
    if before:
        source = source.replace('            if (local.insideOut()) continue;\n', '')
        source = source.replace('                if (!state->active || !item.active || !item.ownsObject() || local.insideOut()) return;\n', '')
    ipp = sdk['lib/math/boundbox.ipp']
    signatures = ('INLINE AABB::AABB()', 'INLINE AABB::AABB( const Vector3 & min, const Vector3 & max )',
                  'INLINE BoundingBox::BoundingBox()', 'INLINE BoundingBox::BoundingBox( const Vector3 & min, const Vector3 & max )',
                  'INLINE const Vector3 & AABB::minBounds() const', 'INLINE const Vector3 & AABB::maxBounds() const',
                  'INLINE void AABB::addBounds( const Vector3 & v )', 'INLINE void AABB::addBounds( const AABB & bb )',
                  'INLINE \tbool AABB::insideOut() const')
    definitions = '\n'.join(block(ipp, name) for name in signatures)
    definitions += '\n' + block(sdk['lib/math/boundbox.cpp'], 'void AABB::transformBy(')
    host = (ROOT / 'tests/stock_vehicle_compound_bounds_host.hpp').read_text()
    owners = source[source.index('class PyCompoundModel :'):]
    host = host.replace('using CompoundStatePtr=std::shared_ptr<CompoundState>;\n', 'using CompoundStatePtr=std::shared_ptr<CompoundState>;\n' + compound_helpers(source), 1)
    replacements = {
        'SDK_BOUNDS': definitions,
        'SDK_PARTICLE': block(sdk['lib/particle/particle_system.cpp'], 'void ParticleSystem::localBoundingBox('),
        'SDK_META': block(sdk['lib/particle/meta_particle_system.cpp'], 'void MetaParticleSystem::localBoundingBox('),
        'VISIT': block(source, 'template<class Callback>'),
        'LOCAL': block(owners, 'virtual void localBoundingBox('),
        'VISIBILITY': block(owners, 'virtual void localVisibilityBox('),
        'WORLD': block(owners, 'virtual void worldBoundingBox('),
        'WORLD_VISIBILITY': block(owners, 'virtual void worldVisibilityBox('),
    }
    for key, value in replacements.items():
        host = host.replace('@' + key + '@', value)
    return host


class CompoundBoundsTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        if not shutil.which('c++'):
            raise unittest.SkipTest('C++ compiler required')
        sdk = sdk_sources(cls)
        cls.temporary = tempfile.TemporaryDirectory(prefix='compound-bounds-')
        cls.addClassCleanup(cls.temporary.cleanup)
        cls.directory = Path(cls.temporary.name)
        for label, before in (('before', True), ('after', False)):
            source = cls.directory / (label + '.cpp')
            source.write_text(harness(sdk, before))
            subprocess.run(['c++', *native_test_compile_flags('c++'), '-std=c++17', '-Wall', '-Wextra',
                            '-Werror', '-UNDEBUG', str(source), '-o', str(cls.directory / label)], check=True)

    def run_owner(self, label, scenario, expected=0):
        result = subprocess.run([str(self.directory / label), scenario], capture_output=True, text=True)
        self.assertEqual(result.returncode, expected, result.stdout + result.stderr)
        return result.stdout

    def test_sdk_empty_particle_sentinel_reproduces_the_old_transform_assert(self):
        self.run_owner('before', 'particles', 73)
        self.assertIn('empty particles become populated', self.run_owner('after', 'particles'))

    def test_empty_parts_mixed_bounds_and_nonempty_affine_transforms(self):
        self.run_owner('before', 'mixed', 73)
        self.assertIn('mixed local/world/visibility', self.run_owner('after', 'mixed'))

    def test_retirement_reentry_excludes_retired_attachment_contributions(self):
        self.assertIn('retired contributions excluded', self.run_owner('after', 'retire'))


if __name__ == '__main__':
    unittest.main()
