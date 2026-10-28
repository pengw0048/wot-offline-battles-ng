"""Runtime image fills must retain the resource heap through render snapshots."""
from pathlib import Path
import importlib.util
import os
import shutil
import subprocess
import sys
import tempfile
import unittest

HERE = Path(__file__).resolve().parent
ROOT = HERE.parent
sys.path.insert(0, str(HERE))
from test_stock_vector4_basic import block
spec = importlib.util.spec_from_file_location('flash_bitmap_fill_patch', ROOT/'bigworld/patch_stock_flash_bitmap_fill.py')
patcher = importlib.util.module_from_spec(spec)
spec.loader.exec_module(patcher)


def sources(test):
    name = 'WOT_SCALEFORM_SDK_ROOT'
    root = Path(os.environ.get(name, '/Users/peng/.cache/wot-independent-client/scaleform-sdk-4.2'))
    if not root.is_dir():
        if name in os.environ: test.fail('Configured Scaleform SDK not found: ' + str(root))
        test.skipTest('Pinned Scaleform 4.2.22 source SDK required')
    test.assertIn('"4.2.22"', (root/'Include/GFxVersion.h').read_text())
    result = {}
    for name in patcher.FILES:
        path = root/name
        test.assertTrue(path.is_file(), 'Configured SDK member missing: ' + name)
        result[name] = path.read_text(encoding='utf-8-sig')
    return result


def generated(source):
    fill = block(source[patcher.FILL], 'struct ComplexFill :') + ';'
    drawing = source[patcher.DRAW_CPP]
    declaration = source[patcher.DRAW_H]
    start = declaration.index('void    BeginBitmapFill(')
    declaration = declaration[start:declaration.index(';', start)+1]
    graphics = block(source[patcher.GRAPHICS], '    void Graphics::beginBitmapFill(')
    begin = drawing.index('namespace Scaleform { namespace GFx {') + len('namespace Scaleform { namespace GFx {')
    end = drawing.index('DrawingContext::DrawingContext(', begin)
    owner = drawing[begin:end].strip()
    header = source[patcher.BITMAP]
    native_getter = next((line.strip() for line in header.splitlines() if 'GetResourceMovieDef() const' in line), '')
    values = {'@COMPLEX_FILL@': fill, '@RESOURCE_OWNER@': owner, '@DRAW_DECLARATION@': declaration,
              '@GET_OWNER@': native_getter, '@GRAPHICS_HANDOFF@': graphics,
              '@DRAW_HANDOFF@': block(drawing, 'void DrawingContext::BeginBitmapFill(')}
    text = (HERE/'stock_flash_bitmap_fill_host.cpp').read_text()
    for token, value in values.items():
        assert text.count(token) == 1
        text = text.replace(token, value)
    return text


class FlashBitmapFillTest(unittest.TestCase):
    def test_patch_is_atomic_idempotent_and_does_not_change_static_fills(self):
        original = sources(self)
        patched = patcher.patch_sources(original)
        self.assertEqual(patcher.patch_sources(patched), patched)
        expected = dict(original)
        for name, before, after in patcher.SEAMS:
            self.assertEqual(expected[name].count(before), 1, (name, before))
            expected[name] = expected[name].replace(before, after, 1)
        self.assertEqual(patched, expected)
        for name, before, after in patcher.SEAMS:
            changed = dict(original)
            changed[name] = changed[name].replace(before, '/* unknown ownership seam */', 1)
            with tempfile.TemporaryDirectory() as directory:
                root = Path(directory)
                for member, text in changed.items():
                    path = root/member; path.parent.mkdir(parents=True, exist_ok=True); path.write_text(text)
                with self.assertRaises(ValueError): patcher.patch(root)
                self.assertEqual({member:(root/member).read_text() for member in changed}, changed)
        # API getter is native only; no Python/AS3 descriptor or static resource binding is added.
        self.assertEqual(patched[patcher.BITMAP].count('GetResourceMovieDef()'), 1)
        self.assertEqual(patched[patcher.GRAPHICS].count('CreateBitmapFillResourceOwner'), 0)
        self.assertEqual(patched[patcher.AVM_BITMAP].count('CreateBitmapFillResourceOwner(pDefImpl)'), 1)

    def test_runtime_producer_snapshot_copy_assignment_and_negative_controls(self):
        compiler = os.environ.get('CXX', 'c++')
        if not shutil.which(compiler):
            if 'CXX' in os.environ: self.fail('Configured C++ compiler not found')
            self.skipTest('C++ compiler required')
        original = sources(self)
        patched = patcher.patch_sources(original)
        without_assignment = dict(patched)
        without_assignment[patcher.FILL] = without_assignment[patcher.FILL].replace(patcher.ASSIGNMENT, '', 1)
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            for name, source, mode, passes in (
                    ('pristine', original, 'snapshot', False),
                    ('default-assignment', without_assignment, 'assignment', False),
                    ('snapshot', patched, 'snapshot', True),
                    ('assignment', patched, 'assignment', True)):
                cpp, exe = root/(name+'.cpp'), root/name
                cpp.write_text(generated(source))
                flags = ['-DSTOCK_OWNER'] if name != 'pristine' else []
                subprocess.run([compiler, '-std=c++14', '-O0', '-UNDEBUG', '-Wall', '-Wextra', '-Werror',
                                '-Wno-deprecated-copy', *flags, str(cpp), '-o', str(exe)], check=True)
                result = subprocess.run([str(exe), mode], text=True, capture_output=True)
                if passes:
                    self.assertEqual(result.returncode, 0, result.stdout+result.stderr)
                else:
                    self.assertNotEqual(result.returncode, 0, name+' negative control passed')
                    self.assertIn('heap retired before image', result.stdout+result.stderr)


if __name__ == '__main__': unittest.main()
