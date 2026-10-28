"""Faded-tree manual writes must reach the material selected for the draw."""
import importlib.util
import os
from pathlib import Path
import shutil
import subprocess
import tarfile
import tempfile
import unittest

from native_test_compile_flags import native_test_compile_flags
from test_stock_cursor_camera import function

ROOT = Path(__file__).resolve().parents[1]
SPEC = importlib.util.spec_from_file_location('tree_effect_owners', ROOT / 'bigworld/patch_stock_effect_owners.py')
PATCH = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(PATCH)


class StockTreeMaterialBindingTests(unittest.TestCase):
    def test_production_draws_select_their_private_binding(self):
        archive = Path(os.environ.get('WOT_PUBLIC_ENGINE_ARCHIVE', '/tmp/wot-bigworld-engine-14.4.1.tar.gz'))
        compiler = shutil.which(os.environ.get('CXX', 'c++'))
        if not archive.is_file() or not compiler:
            self.skipTest('Pinned public SDK and host compiler required')
        with tarfile.open(archive) as sdk:
            def read(suffix):
                name = next(name for name in sdk.getnames() if name.endswith(suffix))
                return sdk.extractfile(name).read().decode('utf-8-sig')
            managed = PATCH.patch_managed_lifecycle(read('/lib/moo/managed_effect.cpp'))
            begin = function(read('/lib/moo/effect_material.cpp'), 'bool EffectMaterial::begin() const')
        methods = [function(managed, signature) for signature in (
            'bool ManagedEffect::begin(', 'bool ManagedEffect::beginPass(',
            'bool ManagedEffect::endPass()', 'bool ManagedEffect::end()',
            'bool ManagedEffect::commitChanges()')]
        tree = (ROOT / 'bigworld/stock_tree_scene.cpp').read_text()
        draws = '\n'.join(function(tree, signature) for signature in (
            'ID3DXEffect * selectTreeBinding(', 'bool drawSemitransparentPart(',
            'bool setHidingAlpha(', 'bool blurHidingTarget(',
            'void drawStockTreeSemitransparentStage('))
        generated = (ROOT / 'tests/stock_tree_material_binding_host.hpp').read_text()
        generated = generated.replace('@INSTANCE@', function(tree, 'struct TreeInstanceConstants') + ';')
        generated = generated.replace('@PROPERTIES@', PATCH.PROPERTIES)
        generated = generated.replace('@MANAGED@', PATCH.SELECT_BINDING + '\n'.join(methods))
        generated = generated.replace('@BEGIN@', begin).replace('@TREE@', draws)
        # The old order writes to a definition or the last sibling's binding.
        # This negative uses the same production bodies and generated owners.
        negative = generated.replace('ID3DXEffect * effect = selectTreeBinding( material );',
                                     'ID3DXEffect * effect = material.pEffect()->pEffect();')
        negative = negative.replace('ID3DXEffect * effect = selectTreeBinding( *material );',
                                    'ID3DXEffect * effect = material->pEffect()->pEffect();')
        self.assertNotEqual(negative, generated)
        with tempfile.TemporaryDirectory(prefix='tree-material-binding-') as temp:
            folder = Path(temp)
            for name, source in (('production', generated), ('old-owner', negative)):
                path = folder / (name + '.cpp')
                binary = folder / name
                path.write_text(source)
                subprocess.run([compiler, '-std=c++17', '-Wall', '-Wextra', '-Werror', '-UNDEBUG',
                                *native_test_compile_flags(compiler), str(path), '-o', str(binary)], check=True)
                for case in ('parts', 'alpha', 'stage'):
                    result = subprocess.run([str(binary), case], capture_output=True, text=True)
                    expected = 0 if name == 'production' else {'parts': 41, 'alpha': 42, 'stage': 43}[case]
                    self.assertEqual(result.returncode, expected,
                                     (name, case, result.returncode, result.stdout, result.stderr))


if __name__ == '__main__':
    unittest.main()
