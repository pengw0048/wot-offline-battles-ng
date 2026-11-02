"""Mask filter source targets must not leak color into their parent target."""
import os
import itertools
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest
import zipfile
import zlib

from stock_flash_scheduler_abc import ABC, Reader

from native_test_compile_flags import native_test_compile_flags
from test_stock_cursor_camera import function
from test_stock_scaleform_scene import load_patch, pinned_sdk

ROOT = Path(__file__).resolve().parents[1]
HAL = 'Src/Render/D3D9/D3D9_HAL.cpp'
FINAL_COMPOSITE = '    // Pop the temporary target, begin rendering to the previous surface.'


class ScaleformMaskFilterTests(unittest.TestCase):
    def test_original_minimap_caches_its_mask(self):
        client = Path(os.environ.get('WOT_0922_CLIENT',
            '/Users/peng/Downloads/World_of_Tanks_0.09.22.00.01_CH_1513_HD'))
        package = client / 'res/packages/gui.pkg'
        if not package.is_file():
            self.skipTest('Original #1513 battle.swf required')
        with zipfile.ZipFile(package) as archive:
            swf = archive.read('gui/flash/battle.swf')
        data = zlib.decompress(swf[8:]) if swf[:3] in (b'CWS', b'CFX') else swf[8:]
        reader = Reader(data)
        reader.pos = (5 + 4 * (data[0] >> 3) + 7) // 8 + 4

        def uint(value):
            encoded = bytearray()
            while value >= 128:
                encoded.append((value & 127) | 128)
                value >>= 7
            return bytes(encoded + bytes([value]))

        matches = 0
        while reader.pos < len(data):
            tag = int.from_bytes(reader.raw(2), 'little')
            size, kind = tag & 63, tag >> 6
            if size == 63:
                size = int.from_bytes(reader.raw(4), 'little')
            payload = reader.raw(size)
            if kind != 82:
                continue
            abc = ABC(payload[payload.index(b'\0', 4) + 1:])
            required = ('entriesContainerMask', 'cacheAsBitmap', 'entriesContainer', 'mask')
            if any(name not in abc.names for name in required):
                continue
            choices = [[uint(index) for index, name in enumerate(abc.names) if name == wanted]
                       for wanted in required]
            # Namespace-distinct multinames can share their short spelling.
            for indices in itertools.product(*choices):
                # The original constructor enables CaB and immediately assigns
                # that same clip as the entries container's mask.
                sequence = (b'\xd0\x66' + indices[0] + b'\x26\x61' + indices[1] +
                    b'\xd0\x66' + indices[2] + b'\xd0\x66' + indices[0] + b'\x61' + indices[3])
                matches += sum(body.count(sequence) for body in abc.bodies.values())

        self.assertEqual(matches, 1, 'Original minimap mask consumer contract changed')

    def test_actual_filter_push_and_parent_composite_state(self):
        compiler = shutil.which(os.environ.get('CXX', 'c++'))
        if not compiler:
            self.skipTest('A host C++ compiler is required')
        source = pinned_sdk(self)[HAL].decode('utf-8-sig').replace('\r\n', '\n')
        patch = load_patch()
        path = Path('sdk') / HAL
        changed = {path: source}

        def edit(target, before, after, label):
            changed[target] = patch.replace_once(changed[target], before, after, label)

        patch.d3d9_mask_filter_state_edits(Path('sdk'), edit)
        patched = changed[path]
        patch.d3d9_mask_filter_state_edits(Path('sdk'), edit)
        self.assertEqual(changed[path], patched)
        # The edit is anchored to the final-composite boundary, not target setup.
        changed[path] = source.replace(
            '// Now actually draw the filtered sub-scene to the target below.',
            '// Unreviewed parent composite.')
        with self.assertRaises(ValueError):
            patch.d3d9_mask_filter_state_edits(Path('sdk'), edit)
        host = (ROOT / 'tests/stock_scaleform_mask_filter_host.cpp').read_text()
        with tempfile.TemporaryDirectory(prefix='scaleform-mask-filter-') as temp:
            for label, text in (('baseline', source), ('production', patched)):
                push = function(text, 'void HAL::PushFilters(')
                uncached = function(text, 'void HAL::drawUncachedFilter(')
                # Exercise the exact caller ordering after PopRenderTarget. Image
                # generation is stubbed; the actual cached-target body performs
                # the final quad draw against a device state observer.
                tail = uncached[uncached.index(FINAL_COMPOSITE):]
                cached = function(text, 'void HAL::drawCachedFilter(')
                cached = cached[cached.index('        case FilterPrimitive::Cache_Target:'):cached.index('        // Should have been one of the other two caching types.')]
                target, binary = Path(temp) / (label + '.cpp'), Path(temp) / label
                target.write_text(host.replace('@PUSH_FILTERS@', push).replace(
                    '@UNCACHED_FINAL_COMPOSITE@', tail).replace('@CACHED_TARGET@', cached))
                subprocess.run([compiler, '-std=c++14', '-Wall', '-Wextra', '-Werror',
                    '-UNDEBUG', *native_test_compile_flags(compiler), str(target), '-o', str(binary)], check=True)
                result = subprocess.run([str(binary)], capture_output=True, text=True)
                self.assertEqual(result.returncode, 41 if label == 'baseline' else 0,
                    (label, result.stdout, result.stderr))
                if label == 'baseline':
                    self.assertEqual(result.stdout.count('mask color leaked'), 8)
                else:
                    self.assertIn('48 cases', result.stdout)


if __name__ == '__main__':
    unittest.main()
