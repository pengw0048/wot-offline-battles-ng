"""Pinned RawImage decoding respects independently mapped mip allocations."""
import importlib.util
import os
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest
import zipfile

from native_test_compile_flags import native_test_compile_flags
from test_stock_cursor_camera import function
from test_stock_scaleform_scene import REVISION

ROOT = Path(__file__).resolve().parents[1]
SDK_FILE = 'Src/Render/Render_Image.cpp'


def sdk_source(test):
    sdk = Path(os.environ.get('WOT_SCALEFORM_SDK_GIT',
        '/Users/peng/.cache/wot-independent-client/scaleform-sdk-4.2'))
    try:
        return subprocess.check_output(['git', '-C', str(sdk), 'show',
            REVISION + ':' + SDK_FILE]).decode('utf-8-sig').replace('\r\n', '\n')
    except (OSError, subprocess.CalledProcessError):
        test.skipTest('Pinned Scaleform SDK required')


def patch_module():
    spec = importlib.util.spec_from_file_location('scaleform_mip_patch',
        ROOT / 'bigworld/patch_scaleform_sdk.py')
    patch = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(patch)
    return patch


class ScaleformMipUploadTests(unittest.TestCase):
    def test_actual_decoder_and_plane_geometry_with_independent_guarded_mips(self):
        source = sdk_source(self)
        compiler = shutil.which(os.environ.get('CXX', 'c++'))
        if not compiler:
            self.skipTest('Host C++ compiler required')
        patch = patch_module()
        geometry = '\n'.join(function(source, signature) for signature in (
            'void    ImagePlane::GetMipLevel(', 'unsigned ImageData::GetPlaneCount(',
            'void    ImageData::GetPlane(', 'unsigned ImageData::GetFormatPlaneCount(',
            'unsigned  ImageData::GetFormatBitsPerPixel(', 'UPInt  ImageData::GetFormatPitch(',
            'UPInt  ImageData::GetFormatScanlineCount(', 'UPInt ImageData::GetMipLevelSize('))
        host = (ROOT / 'tests/stock_scaleform_mip_upload_host.cpp').read_text()
        outputs = {}
        with tempfile.TemporaryDirectory(prefix='scaleform-guarded-mips-') as folder:
            root = Path(folder)
            resources = []
            client = Path(os.environ.get('WOT_0922_CLIENT',
                '/Users/peng/Downloads/World_of_Tanks_0.09.22.00.01_CH_1513_HD'))
            package = client / 'res/packages/gui.pkg'
            if package.is_file():
                with zipfile.ZipFile(package) as files:
                    for name in ('gui/maps/vehicles/decals/china_star.dds',
                                 'gui/maps/vehicles/decals/china_kuomintang.dds',
                                 'gui/maps/vehicles/decals/inscriptions/china/inscription_01.dds'):
                        target = root / name.rsplit('/', 1)[-1]
                        target.write_bytes(files.read(name))
                        resources.append(str(target))
            for label, body in (('pristine', function(source, 'bool RawImage::Decode(')),
                                ('patched', patch.RAW_IMAGE_DECODE_AFTER)):
                path, binary = root / (label + '.cpp'), root / label
                path.write_text(host.replace('@GEOMETRY@', geometry).replace('@DECODE@', body))
                subprocess.run([compiler, '-std=c++14', '-Wall', '-Wextra', '-Werror',
                    '-UNDEBUG', *native_test_compile_flags(compiler), str(path), '-o', str(binary)], check=True)
                outputs[label] = subprocess.run([str(binary), *resources], capture_output=True, text=True)
        self.assertEqual(outputs['pristine'].returncode, 41,
            (outputs['pristine'].stdout, outputs['pristine'].stderr))
        self.assertIn('copy outside destination mip allocation', outputs['pristine'].stdout)
        self.assertEqual(outputs['patched'].returncode, 0,
            (outputs['patched'].stdout, outputs['patched'].stderr))
        self.assertIn('guarded mip layouts passed', outputs['patched'].stdout)
        if resources:
            self.assertEqual(outputs['patched'].stdout.count('exact DDS upload passed'), 3)

    def test_pinned_and_cached_sdk_patch_is_idempotent(self):
        source, patch = sdk_source(self), patch_module()
        path = ROOT / 'unused-sdk' / SDK_FILE
        changes = {path: source}

        def edit(target, before, after, label):
            changes[target] = patch.replace_once(changes[target], before, after, label)

        patch.image_mip_decode_edits(ROOT / 'unused-sdk', edit)
        self.assertEqual(function(changes[path], 'bool RawImage::Decode('), patch.RAW_IMAGE_DECODE_AFTER)
        first = changes[path]
        patch.image_mip_decode_edits(ROOT / 'unused-sdk', edit)
        self.assertEqual(changes[path], first)
        changes[path] = source.replace('pdestin = pdestin ? pdestin : dplane.pData;', 'unknown_owner();')
        with self.assertRaises(ValueError):
            patch.image_mip_decode_edits(ROOT / 'unused-sdk', edit)


if __name__ == '__main__':
    unittest.main()
