"""Exercise compound pose publication through the real SDK scene embodiment."""
from pathlib import Path
import os
import shutil
import subprocess
import tarfile
import tempfile
import unittest

from native_test_compile_flags import native_test_compile_flags
from test_vehicle_compound_bounds import block, compound_helpers, sdk_sources

ROOT = Path(__file__).resolve().parents[1]


def scene_sources():
    archive = Path(os.environ.get('WOT_PUBLIC_ENGINE_ARCHIVE') or
                   os.environ.get('WOT_BIGWORLD_SOURCE_ARCHIVE') or
                   '/tmp/wot-bigworld-engine-14.4.1.tar.gz')
    if not archive.is_file():
        raise unittest.SkipTest('Pinned public engine archive required')
    names = ('lib/duplo/py_attachment.cpp',
             'lib/compiled_space/py_attachment_entity_embodiment.cpp',
             'lib/scene/tick_scene_view.cpp')
    result = {}
    with tarfile.open(archive) as tar:
        for member in tar:
            for name in names:
                if member.name.endswith('/programming/bigworld/' + name):
                    result[name] = tar.extractfile(member).read().decode('utf-8')
            if len(result) == len(names):
                break
    if set(result) != set(names):
        raise AssertionError('Pinned SDK attachment/scene owner missing')
    return result


def harness(before=False):
    production = (ROOT / 'bigworld/vehicle_appearance.cpp').read_text()
    compound = production[production.index('class PyCompoundModel :'):]
    sdk = sdk_sources(None)
    scene = scene_sources()
    # Reuse the affine math adapters and the real SDK AABB implementation.
    maths = (ROOT / 'tests/stock_vehicle_compound_bounds_host.hpp').read_text()
    maths = maths[:maths.index('struct ParticleSystem {')]
    ipp = sdk['lib/math/boundbox.ipp']
    signatures = ('INLINE AABB::AABB()', 'INLINE AABB::AABB( const Vector3 & min, const Vector3 & max )',
                  'INLINE BoundingBox::BoundingBox()', 'INLINE BoundingBox::BoundingBox( const Vector3 & min, const Vector3 & max )',
                  'INLINE const Vector3 & AABB::minBounds() const', 'INLINE const Vector3 & AABB::maxBounds() const',
                  'INLINE void AABB::addBounds( const Vector3 & v )', 'INLINE void AABB::addBounds( const AABB & bb )',
                  'INLINE \tbool AABB::insideOut() const')
    definitions = '\n'.join(block(ipp, signature) for signature in signatures)
    definitions += '\n' + block(sdk['lib/math/boundbox.cpp'], 'void AABB::transformBy(')
    maths = maths.replace('@SDK_BOUNDS@', definitions)
    host = (ROOT / 'tests/stock_vehicle_compound_scene_host.hpp').read_text()
    host = host.replace('@MATHS@', maths)
    host = host.replace('using CompoundStatePtr=std::shared_ptr<CompoundState>;\n', 'using CompoundStatePtr=std::shared_ptr<CompoundState>;\n' + compound_helpers(production), 1)
    for token, signature in (
            ('NODE', 'struct CompoundNode'), ('PART', 'struct CompoundPart'),
            ('STATE', 'struct CompoundState'), ('ITEM', 'struct CompoundAttachment :'),
            ('VISIT', 'template<class Callback>')):
        host = host.replace('@' + token + '@', block(production, signature) + (';' if token in ('NODE', 'PART', 'STATE', 'ITEM') else ''))
    for token, signature in (
            ('TICK', 'virtual void tick('), ('UPDATE', 'virtual void updateAnimations('),
            ('LOCAL', 'virtual void localBoundingBox('), ('VISIBILITY', 'virtual void localVisibilityBox('),
            ('WORLD', 'virtual void worldBoundingBox('), ('ENTER', 'virtual void enterWorld()'),
            ('LEAVE', 'virtual void leaveWorld()'), ('RESET', 'void reset()'),
            ('MATRIX', 'void matrix( MatrixProviderPtr value )')):
        body = block(compound, signature)
        if before and token == 'TICK':
            body = body.replace('        if (state_->active)\n            this->worldTransform( state_->nodes[0].world );\n', '')
        host = host.replace('@' + token + '@', body)
    for token, signature in (
            ('ATTACH', 'bool PyAttachment::attach('), ('DETACH', 'void PyAttachment::detach()')):
        host = host.replace('@' + token + '@', block(scene['lib/duplo/py_attachment.cpp'], signature))
    for token, signature in (
            ('SCENE_TICK', 'void PyAttachmentEntityEmbodiment::doTick('),
            ('SCENE_ANIMATE', 'void PyAttachmentEntityEmbodiment::doUpdateAnimations('),
            ('SCENE_SYNC', 'void PyAttachmentEntityEmbodiment::syncTransform()'),
            ('SCENE_TRANSFORM', 'void PyAttachmentEntityEmbodiment::doWorldTransform('),
            ('SCENE_ENTER', 'void PyAttachmentEntityEmbodiment::doEnterSpace('),
            ('SCENE_LEAVE', 'void PyAttachmentEntityEmbodiment::doLeaveSpace('),
            ('SCENE_SET', 'bool PyAttachmentEntityEmbodiment::setMatrix(')):
        host = host.replace('@' + token + '@', block(scene['lib/compiled_space/py_attachment_entity_embodiment.cpp'], signature))
    host = host.replace('@SCENE_PHASES@', block(scene['lib/scene/tick_scene_view.cpp'],
                                              'void TickSceneView::tick('))
    if '@' in host:
        raise AssertionError('Unexpanded scene fixture token')
    return host


class CompoundSceneTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        if not shutil.which('c++'):
            raise unittest.SkipTest('C++ compiler required')
        cls.temporary = tempfile.TemporaryDirectory(prefix='compound-scene-')
        cls.addClassCleanup(cls.temporary.cleanup)
        cls.directory = Path(cls.temporary.name)
        for label, before in (('before', True), ('after', False)):
            source = cls.directory / (label + '.cpp')
            source.write_text(harness(before))
            subprocess.run(['c++', *native_test_compile_flags('c++'), '-std=c++17', '-Wall', '-Wextra',
                            '-Werror', '-Wno-unused-parameter', '-UNDEBUG', str(source),
                            '-o', str(cls.directory / label)], check=True)

    def run_owner(self, label, scenario, expected=0):
        result = subprocess.run([str(self.directory / label), scenario], capture_output=True, text=True)
        self.assertEqual(result.returncode, expected, result.stdout + result.stderr)
        return result.stdout

    def test_compound_pose_and_rotated_bounds_reach_scene_before_visibility(self):
        self.run_owner('before', 'motion', 41)
        self.assertIn('moving/rotating compounds update scene bounds and LOD', self.run_owner('after', 'motion'))

    def test_late_root_binding_and_invisible_compounds_still_publish(self):
        self.run_owner('before', 'latebind', 41)
        self.assertIn('late root binding and offscreen publication', self.run_owner('after', 'latebind'))

    def test_unload_rebind_and_second_round_retire_old_scene(self):
        self.run_owner('before', 'rounds', 41)
        self.assertIn('detached owner and second round use distinct scene membership', self.run_owner('after', 'rounds'))

    def test_reset_during_producer_or_child_tick_does_not_publish_retired_pose(self):
        self.assertIn('reset callbacks cannot republish a retired compound', self.run_owner('after', 'reentry'))


if __name__ == '__main__':
    unittest.main()
