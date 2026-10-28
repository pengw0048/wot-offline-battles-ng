"""Run SDK shared-default property methods with the real native material owners."""
import os
from pathlib import Path
import shutil
import subprocess
import tarfile
import tempfile
import unittest

from native_test_compile_flags import native_test_compile_flags


ROOT = Path(__file__).resolve().parents[1]


def function(source, signature):
    start = source.index(signature)
    cursor = source.index('{', start)
    end, depth = cursor, 1
    while depth:
        end += 1
        depth += (source[end] == '{') - (source[end] == '}')
    return source[start:end + 1]


class EffectPropertyOwnershipTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        configured = os.environ.get('WOT_PUBLIC_ENGINE_ARCHIVE') or os.environ.get('WOT_BIGWORLD_SOURCE_ARCHIVE')
        archive = Path(configured or '/tmp/wot-bigworld-engine-14.4.1.tar.gz')
        if not archive.is_file():
            if configured:
                raise AssertionError('Configured SDK archive missing: ' + str(archive))
            raise unittest.SkipTest('Pinned SDK archive required')
        configured_compiler = os.environ.get('CXX')
        cls.compiler = shutil.which(configured_compiler or 'c++')
        if not cls.compiler:
            if configured_compiler:
                raise AssertionError('Configured compiler missing: ' + configured_compiler)
            raise unittest.SkipTest('C++ compiler required')
        sdk = {}
        with tarfile.open(archive) as source:
            for name in ('effect_material.cpp', 'effect_property.cpp'):
                matches = [m for m in source.getmembers() if m.name.endswith('/programming/bigworld/lib/moo/' + name)]
                if len(matches) != 1:
                    raise AssertionError('Pinned SDK member not unique: ' + name)
                sdk[name] = source.extractfile(matches[0]).read().decode('utf-8-sig')
        cls.temporary = tempfile.TemporaryDirectory(prefix='effect-property-ownership-')
        cls.addClassCleanup(cls.temporary.cleanup)
        cls.root = Path(cls.temporary.name)
        material = sdk['effect_material.cpp']
        methods = [function(material, signature) for signature in (
            'bool EffectMaterial::load(',
            'bool EffectMaterial::initFromEffect(',
            'void EffectMaterial::loadProperties(',
            'void EffectMaterial::loadPropertyHandles(',
            'EffectPropertyPtr EffectMaterial::getProperty(',
            'bool EffectMaterial::isDefault(',
            'void EffectMaterial::replaceDefaults()',
            'template<class T>\nbool EffectMaterial::setPropertyType(',
        )]
        properties = [function(sdk['effect_property.cpp'], 'class ' + name + ' : public EffectProperty') + ';'
                      for name in ('Vector4Property', 'MatrixProperty', 'TextureProperty',
                                   'FloatProperty', 'BoolProperty', 'IntProperty')]
        cls.host = (ROOT / 'tests/stock_effect_property_ownership_host.hpp').read_text().replace(
            '@PROPERTY_CLASSES@', '\n'.join(properties)).replace('@SDK_METHODS@', '\n'.join(methods))
        cls.static = function((ROOT / 'bigworld/stock_static_model_scene.cpp').read_text(),
                              'bool StockStaticModelSceneLoader::State::loadMaterials(')
        fashions = (ROOT / 'bigworld/stock_vehicle_fashions.cpp').read_text()
        cls.clone = function(fashions, 'Moo::ComplexEffectMaterialPtr cloneMaterial(')
        cls.variant = function(fashions, 'Moo::ComplexEffectMaterialPtr materialVariant(')

    def run_owner(self, label, static, variant, expected):
        source = self.root / (label + '.cpp')
        source.write_text(self.host.replace('@STATIC_OWNER@', static).replace(
            '@CLONE@', self.clone).replace('@VARIANT@', variant))
        binary = self.root / (label + ('.exe' if os.name == 'nt' else ''))
        result = subprocess.run([self.compiler, *native_test_compile_flags(self.compiler), '-std=c++17',
                                 '-Wall', '-Wextra', '-Werror', '-UNDEBUG', '-I', str(ROOT / 'include'),
                                 str(source), '-o', str(binary)], capture_output=True, text=True)
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        result = subprocess.run([str(binary)], capture_output=True, text=True)
        self.assertEqual(result.returncode, expected, result.stdout + result.stderr)

    def test_real_sdk_defaults_isolated_at_both_native_owners(self):
        self.run_owner('after', self.static, self.variant, 0)

    def test_real_sdk_static_owner_before_negative_control(self):
        statement = 'base->replaceDefaults();'
        self.assertEqual(self.static.count(statement), 1)
        self.run_owner('before-static', self.static.replace(statement, ''), self.variant, 2)

    def test_real_sdk_variant_owner_before_negative_control(self):
        statement = 'result->baseMaterial()->replaceDefaults();'
        self.assertEqual(self.variant.count(statement), 1)
        self.run_owner('before-variant', self.static, self.variant.replace(statement, ''), 3)


if __name__ == '__main__':
    unittest.main()
