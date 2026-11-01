"""Public HDRSupport drives the original HDR luminance and ToneMapping effects."""
import importlib.util
import io
import os
from pathlib import Path
import re
import shutil
import subprocess
import sys
import tarfile
import tempfile
import unittest
import zipfile


ROOT = Path(__file__).resolve().parents[1]
spec = importlib.util.spec_from_file_location('stock_hdr_patch', ROOT / 'bigworld/patch_stock_hdr.py')
patch = importlib.util.module_from_spec(spec)
spec.loader.exec_module(patch)
FILES = ('lib/romp/hdr_support.cpp', 'lib/romp/hdr_support.hpp',
         'lib/romp/hdr_settings.cpp', 'lib/romp/hdr_settings.hpp')


def public_sources(test, root):
    archive = Path(os.environ.get('WOT_PUBLIC_ENGINE_ARCHIVE', '/tmp/wot-bigworld-engine-14.4.1.tar.gz'))
    if not archive.is_file():
        test.skipTest('Pinned public source archive is required')
    with tarfile.open(archive) as original:
        names = original.getnames()
        for relative in FILES:
            member = next(name for name in names if name.endswith('/programming/bigworld/' + relative))
            path = root / relative
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_bytes(original.extractfile(member).read())


def compile_host(test, source, directory):
    compiler = shutil.which(os.environ.get('CXX', 'c++'))
    if not compiler:
        test.skipTest('Host C++ compiler is required')
    path = directory / 'host.cpp'
    path.write_text(source)
    binary = directory / 'host'
    subprocess.run([compiler, '-std=c++17', '-I', str(ROOT / 'include'), str(path),
                    str(ROOT / 'src/stock_shader_binary.cpp'), '-o', str(binary)], check=True)
    return binary


class StockHdrTests(unittest.TestCase):
    def test_tonemapping_parameters_follow_the_original_formula(self):
        # a72a33..a72a7f: pow(2, -1/blackPoint) and (whitePoint*0.1+1)/(1-that).
        source = r'''
#include "wot/stock_hdr_tonemapping.hpp"
#include <cassert>
#include <cmath>
int main() {
  wot::StockToneMappingProfile p;
  assert(p.eyeDarkLimit == 0.05f && p.eyeLightLimit == 1.5f && p.middleGray == 0.5f &&
         p.whitePoint == 5.f && p.blackPoint == 0.15f);
  p.middleGray = 0.27f;
  const auto params = wot::stock_tonemapping_params(p);
  assert(params[0] == 0.27f && params[1] == 5.f && params[2] == 0.15f && params[3] == 0.f);
  const auto r = wot::stock_reinhard_params(p);
  const float black = std::pow(2.f, -1.f / 0.15f);
  assert(r[0] == black && r[1] == (5.f * 0.1f + 1.f) / (1.f - black) && r[2] == 0.f && r[3] == 0.f);
  assert(wot::stock_active_tonemapping(0) == 0 && wot::stock_active_tonemapping(2) == 2);
  assert(wot::stock_active_tonemapping(-1) == 1 && wot::stock_active_tonemapping(3) == 1);
}
'''
        with tempfile.TemporaryDirectory(prefix='wot-hdr-params-') as directory:
            subprocess.run([str(compile_host(self, source, Path(directory)))], check=True)

    def test_patch_drives_original_effects_and_is_stable(self):
        with tempfile.TemporaryDirectory(prefix='wot-hdr-patch-') as directory:
            root = Path(directory)
            public_sources(self, root)
            patch.patch(root)
            patched = {relative: (root / relative).read_text() for relative in FILES}
            patch.patch(root)
            self.assertEqual(patched, {relative: (root / relative).read_text() for relative in FILES})
            support = patched['lib/romp/hdr_support.cpp']
            self.assertIn('"shaders/hdr/tonemapping.fx"', support)
            self.assertNotIn('hdr_tonemapping.fx', support)
            self.assertNotIn('m_averageLuminanceMat->pEffect()->pEffect();\n', support.replace(' : NULL;', ''))
            self.assertEqual(support.count('m_averageLuminanceMat->properties().select('), 4)
            self.assertEqual(support.count('setStockHdrTarget(effect, "g_previousMipMap", '), 3)
            self.assertNotIn('SetTexture("g_previousMipMap"', support)
            self.assertIn('drawStockToneMapping(m_toneMappingMat, g_cfg,', support)
            self.assertIn('Vector4 params(1.0f, 1.0f, 1.0f, 1.0f);', support)
            self.assertIn('        m_successed = false;\n        m_hdrRT->pop();\n', support)
            settings = patched['lib/romp/hdr_settings.cpp']
            self.assertIn('loadStockToneMappings(iData, *this);', settings)
            self.assertIn('"environment/sunlightLumMultiplier", e.m_sunLumMultiplier', settings)
            self.assertNotIn('fogLumMultiplier"', settings.split('bool HDRSettings::save')[0])
            self.assertIn('m_whitePoint(5.0f)', settings)
            # A changed anchor is rejected before any file is written.
            for relative in FILES:
                (root / relative).unlink()
            public_sources(self, root)
            settings_path = root / 'lib/romp/hdr_settings.cpp'
            settings_path.write_text(settings_path.read_text().replace('"tonemapping/whitePoint"', '"x"'))
            before = {relative: (root / relative).read_bytes() for relative in FILES}
            with self.assertRaises(RuntimeError):
                patch.patch(root)
            self.assertEqual(before, {relative: (root / relative).read_bytes() for relative in FILES})

    def test_selected_names_exist_in_the_original_effects(self):
        # A missing technique or parameter leaves luminance at zero and the scene white.
        client = os.environ.get('WOT_0922_CLIENT')
        if not client:
            self.skipTest('WOT_0922_CLIENT is required')
        sys.path.insert(0, str(ROOT.parents[1] / 'tools'))
        from prepare_native_client import OriginalResources
        support = (ROOT / 'bigworld/patch_stock_hdr.py').read_text()
        techniques = {name for pair in re.findall(
            r'hTechnique\((?:g_cfg\.m_useLogAvgLuminance \? )?"([A-Z][A-Za-z]+)"(?: : "([A-Z][A-Za-z]+)")?', support)
            for name in pair if name}
        self.assertEqual(techniques, {'DownsampleColor', 'AverageLogLum', 'AverageLum', 'DownsampleLum',
                                      'FinalAverageLogLum', 'FinalAverageLum'})
        tonemap = (ROOT / 'bigworld/stock_hdr.cpp').read_text()
        required = set(re.findall(r'Set(?:Texture|Bool|Vector|Int)\("([A-Za-z_]+)"', tonemap))
        required |= {name + 'DRRParams' for name in re.findall(r'setStockHdrTarget\(effect, "([A-Za-z_]+)"', tonemap)}
        source = r'''
#include "wot/stock_shader_binary.hpp"
#include <cstdio>
#include <fstream>
#include <iterator>
int main(int, char** argv) {
  std::ifstream input(argv[1], std::ios::binary);
  std::vector<unsigned char> bytes((std::istreambuf_iterator<char>(input)), {});
  wot::StockEffectBinary effect; std::string error;
  if (!wot::decode_stock_dx9_effect(bytes.data(), bytes.size(), effect, error)) return 1;
  for (const auto& technique : effect.techniques) std::printf("technique %s\n", technique.name.c_str());
  for (const auto& buffer : effect.uniform_buffers)
    for (const auto& uniform : buffer.uniforms) std::printf("parameter %s\n", uniform.name.c_str());
  for (const auto& resource : effect.resource_views) std::printf("parameter %s\n", resource.name.c_str());
}
'''
        resources = OriginalResources(client)
        with tempfile.TemporaryDirectory(prefix='wot-hdr-effects-') as directory:
            binary = compile_host(self, source, Path(directory))
            for name, expected in (('average_luminance', {'technique ' + t for t in techniques} |
                                    {'parameter g_previousMipMap', 'parameter g_previousMipMapDRRParams'}),
                                   ('tonemapping', {'technique ToneMapping'} |
                                    {'parameter ' + p for p in required})):
                for model in (10, 11):
                    with zipfile.ZipFile(io.BytesIO(resources.read('shaders/hdr/%s.%d.dx9.fxo' % (name, model)))) as fxo:
                        (Path(directory) / 'effect').write_bytes(fxo.read('effect'))
                    found = set(subprocess.run([str(binary), str(Path(directory) / 'effect')], check=True,
                                               capture_output=True, text=True).stdout.splitlines())
                    self.assertTrue(expected <= found, (name, model, sorted(expected - found)))


if __name__ == '__main__':
    unittest.main()
