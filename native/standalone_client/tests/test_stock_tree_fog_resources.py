"""Audit all public fog callers and compile complete effects with Windows D3DX.

Portable tests validate the rewrite, not HLSL validity. The optional Windows
probe compiles both complete billboard effects in both pipeline modes.
"""
import importlib.util
import os
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
SPEC = importlib.util.spec_from_file_location('tree_fog_patch', ROOT / 'bigworld/patch_stock_tree_fog_resources.py')
PATCH = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(PATCH)


class TreeFogResourceTests(unittest.TestCase):
    def test_call_class_keeps_nested_arguments_comments_and_scalar_owner(self):
        source = '''// vertexFog(world, depth)
/* vertexFog(world, depth) */
#include "vertexFog(world, depth)"
float vertexFog(float depth, float start, float end) { return depth; }
a = vertexFog(float4(world, 1), projected.w);
b = vertexFog(selectWorld(a, b), selectDepth(x, y));
c = bw_vertexFog(world, depth);
d = vertexFog(depth, start, end);
'''
        spans = PATCH.legacy_calls(source)
        self.assertEqual(len(spans), 2)
        for begin, end in reversed(spans):
            source = source[:begin] + 'bw_vertexFog' + source[end:]
        self.assertEqual(PATCH.legacy_calls(source), [])
        self.assertIn('bw_vertexFog(float4(world, 1), projected.w)', source)
        self.assertIn('bw_vertexFog(selectWorld(a, b), selectDepth(x, y))', source)
        self.assertIn('vertexFog(depth, start, end)', source)
        with self.assertRaisesRegex(ValueError, 'Unclosed'):
            PATCH.legacy_calls('vertexFog(world, depth')

    def resources(self):
        source = Path(os.environ.get('WOT_PUBLIC_RESOURCES', '/tmp/wot-bigworld-public-res-r10'))
        if not (source / 'shaders/std_effects/fog_helpers.fxh').is_file():
            if os.environ.get('WOT_PUBLIC_RESOURCES'):
                self.fail('Configured public fog resources missing: ' + str(source))
            self.skipTest('Pinned public shader resources are unavailable')
        return source

    def test_complete_downloaded_tree_and_idempotence(self):
        source = self.resources()
        with tempfile.TemporaryDirectory(prefix='tree-fog-resources-') as directory:
            target = Path(directory)
            shutil.copytree(source / 'shaders', target / 'shaders')
            before = {p.relative_to(target): p.read_bytes() for p in target.rglob('*') if p.is_file()}
            affected = {p.as_posix() for p, data in before.items()
                        if p.suffix in ('.fx', '.fxh') and PATCH.legacy_calls(data.decode('latin1'))}
            changed = set(PATCH.patch_resources(target))
            self.assertEqual(changed, affected)
            for path in (target / 'shaders').rglob('*'):
                if path.suffix in ('.fx', '.fxh'):
                    self.assertEqual(PATCH.legacy_calls(path.read_bytes().decode('latin1')), [], str(path))
            after = {p.relative_to(target): p.read_bytes() for p in target.rglob('*') if p.is_file()}
            self.assertEqual({p.as_posix() for p in before if before[p] != after[p]}, affected)
            self.assertEqual(before[Path('shaders/std_effects/fog_helpers.fxh')],
                             after[Path('shaders/std_effects/fog_helpers.fxh')])
            self.assertEqual(PATCH.patch_resources(target), [])
            self.assertEqual(after, {p.relative_to(target): p.read_bytes() for p in target.rglob('*') if p.is_file()})

    def test_complete_effects_with_windows_d3dx_compiler(self):
        probe = os.environ.get('WOT_TREE_FOG_COMPILER_PROBE')
        if not probe:
            self.skipTest('Windows D3DX effect compiler probe is required for real HLSL compilation')
        self.assertTrue(Path(probe).is_file(), 'Configured Windows compiler probe missing')
        source = self.resources()
        with tempfile.TemporaryDirectory(prefix='tree-fog-compile-') as directory:
            target = Path(directory)
            shutil.copytree(source / 'shaders', target / 'shaders')
            # A pristine r10 tree proves the original full effect fails before
            # the real fog owner is selected. Already-patched caches need no
            # invented rollback; the complete after-state is still compiled.
            affected = any(PATCH.legacy_calls(p.read_bytes().decode('latin1'))
                           for p in (target / 'shaders/speedtree').glob('*.fx'))
            before = subprocess.run([probe, str(target)], capture_output=True, text=True)
            if affected:
                self.assertNotEqual(before.returncode, 0, before.stdout + before.stderr)
                self.assertIn('vertexFog', before.stdout + before.stderr)
            PATCH.patch_resources(target)
            after = subprocess.run([probe, str(target)], capture_output=True, text=True)
            self.assertEqual(after.returncode, 0, after.stdout + after.stderr)
            self.assertIn('4 complete billboard/pipeline effects compiled', after.stdout)
            for pipeline, value in (('deferred', '1'), ('forward', '0')):
                self.assertEqual(after.stdout.count('pipeline=' + pipeline +
                    ' WG_DEFERRED_SHADING=' + value + ' BW_DEFERRED_SHADING=' + value), 2,
                    after.stdout)


if __name__ == '__main__':
    unittest.main()
