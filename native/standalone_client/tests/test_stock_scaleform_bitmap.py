"""BitmapData must keep a loaded image's MovieData heap alive through release."""
from pathlib import Path
import os
import subprocess
import sys
import tempfile
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parent))
from test_stock_scaleform_scene import SDK_FILES, load_patch, pinned_sdk

SOURCE = 'Src/GFx/AS3/Obj/Display/AS3_Obj_Display_BitmapData.cpp'


class ScaleformBitmapLifetimeTest(unittest.TestCase):
    def test_patch_is_atomic_and_repeatable_for_both_lifetime_seams(self):
        files = pinned_sdk(self)
        patch = load_patch()
        seams = ['    BitmapData::~BitmapData()\n    {\n\n    }',
                 '        result->pImage = *SF_NEW DrawableImage(Transparent, '
                 '(Render::Image*)pImage.GetPtr(), dicontext);']
        with tempfile.TemporaryDirectory(prefix='scaleform-bitmap-patch-') as directory:
            root = Path(directory)

            def reset():
                for name, data in files.items():
                    path = root / name
                    path.parent.mkdir(parents=True, exist_ok=True)
                    path.write_bytes(data)

            def snapshot():
                return {name: (root / name).read_bytes() for name in SDK_FILES}

            reset()
            patch.patch_sdk(root)
            applied = snapshot()
            patch.patch_sdk(root)
            self.assertEqual(applied, snapshot())
            for seam in seams:
                reset()
                path = root / SOURCE
                source = path.read_text()
                self.assertEqual(source.count(seam), 1)
                path.write_text(source.replace(seam, '// Unknown SDK ownership seam'))
                before = snapshot()
                with self.assertRaises(ValueError):
                    patch.patch_sdk(root)
                self.assertEqual(before, snapshot())

    def test_real_heappt_bitmap_lifetimes(self):
        fixed = os.environ.get('WOT_SCALEFORM_BITMAP_PROBE')
        if not fixed:
            self.skipTest('Set WOT_SCALEFORM_BITMAP_PROBE to run the real Windows SDK probe')
        for variable, case in [('WOT_SCALEFORM_BITMAP_BEFORE_PROBE', 'direct'),
                               ('WOT_SCALEFORM_BITMAP_DESTRUCTOR_ONLY_PROBE', 'clone')]:
            negative = os.environ.get(variable)
            if negative:
                result = subprocess.run([negative, case], capture_output=True, timeout=60)
                self.assertEqual(result.returncode & 0xffffffff, 0xc0000005,
                                 result.stdout.decode(errors='replace') +
                                 result.stderr.decode(errors='replace'))
        result = subprocess.run([fixed], capture_output=True, timeout=60)
        self.assertEqual(result.returncode, 0, result.stdout.decode(errors='replace') +
                         result.stderr.decode(errors='replace'))
        output = result.stdout.replace(b'\r\n', b'\n')
        for case in ('direct', 'dispose', 'rebind', 'clone'):
            self.assertEqual(output.count(('PASS BitmapData ' + case + '\n').encode()), 3)
        self.assertIn(b'PASS real SDK BitmapData ownership and full SDK reinitialization', output)


if __name__ == '__main__':
    unittest.main()
