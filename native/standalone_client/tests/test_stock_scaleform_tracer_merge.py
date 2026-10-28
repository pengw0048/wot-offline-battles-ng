"""AS3 verifier joins: the #1513 4.6.34 type merge on SDK 4.2.22.

Original-instruction, source and compile checks. The real SDK tracer and the
exact lobby.swf ContentTabBar.populateRendererData join are exercised only by
the Win32 probe.
"""
from pathlib import Path
import os
import shutil
import subprocess
import sys
import tempfile
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parent))
from stock_battle_gui_support import client_executable
from test_stock_scaleform_scene import REVISION, SDK_FILES, load_patch
from test_stock_scaleform_class_alias import pinned_sdk, write

TRACER = 'Src/GFx/AS3/AS3_Tracer.cpp'


def body(source, begin, end):
    start = source.index(begin)
    return source[start:source.index(end, start)]


def merge_values(source):
    return body(source.replace('\r\n', '\n'), 'CheckResult Tracer::MergeValues(', '\nvoid Tracer::')


def coerce_a(source):
    # SubstituteOpCode's case, not the unreachable State::exec case.
    return body(source.replace('\r\n', '\n'), '    case Code::op_coerce_a:\n        {\n            Value& v',
                '    case Code::op_coerce_s:')


class ScaleformTracerMergeTest(unittest.TestCase):
    def patched(self):
        files = pinned_sdk(self)
        with tempfile.TemporaryDirectory(prefix='scaleform-tracer-merge-') as directory:
            root = Path(directory)
            write(root, files)
            load_patch().patch_sdk(root)
            return files[TRACER].decode('latin-1'), (root / TRACER).read_text(encoding='latin-1')

    def test_type_joins_fall_back_to_any_and_only_with_mismatch_errors(self):
        pristine, patched = self.patched()
        before, after = merge_values(pristine), merge_values(patched)
        # 4.2.22: with mismatch, null/numeric in both directions and no common base.
        self.assertEqual(before.count('goto error;'), 4)
        # 00e41c60 raises #1068 only for the scope "with" flag (00e41d2a).
        self.assertEqual(after.count('goto error;'), 1)
        self.assertIn('if (to_v.GetWith() != from_v.GetWith())\n            goto error;', after)
        self.assertIn('ThrowMergeTypeError(*to_tr, *from_tr);', after)
        self.assertEqual(after.count('JoinSNodesUpdateType(to_v, from_v, GetClassType());'), 3)
        # Earlier rules keep their 4.2.22 results.
        for rule in ('JoinSNodesUpdateType(to_v, from_v, GetObjectType());',
                     'JoinSNodesUpdateType(to_v, from_v, GetNumberType());',
                     'JoinSNodesAssign(to_v, from_v);'):
            self.assertEqual(before.count(rule), after.count(rule), rule)

    def test_coerce_a_keeps_the_sdk_and_original_type_rule(self):
        pristine, patched = self.patched()
        # 00e40ee8: int/uint become Number, any other type is unchanged.
        self.assertEqual(coerce_a(pristine), coerce_a(patched))
        self.assertIn('if (IsSIntType(v) || IsUIntType(v))', coerce_a(patched))

    def test_patch_fails_closed_on_a_changed_join(self):
        files = pinned_sdk(self)
        with tempfile.TemporaryDirectory(prefix='scaleform-tracer-merge-closed-') as directory:
            root = Path(directory)
            write(root, files)
            source = files[TRACER].replace(b'    // For debugging.\r\n    if (result)',
                                           b'    // For debugging!\r\n    if (result)')
            self.assertNotEqual(source, files[TRACER])
            (root / TRACER).write_bytes(source)
            before = {name: (root / name).read_bytes() for name in SDK_FILES}
            with self.assertRaises(ValueError):
                load_patch().patch_sdk(root)
            self.assertEqual(before, {name: (root / name).read_bytes() for name in SDK_FILES})

    def test_exact_1513_coerce_a_and_join(self):
        exe = client_executable(self)
        # Tracer opcode switch 00e4009d (opcode - 2): coerce_a (0x82) is case 76 at 00e40ee8.
        case = exe.code(0xe417e4 + 0x82 - 2, 1)[0]
        self.assertEqual(case, 76)
        self.assertEqual(exe.word(0xe415dc + 4 * case), 0xe40ee8)
        # int (vm+0x14c) or uint (vm+0x150) -> a Number (vm+0x148) node; else unchanged (00e40484).
        for address, expected in ((0xe40f03, '8b804c010000'), (0xe40f19, '8b8050010000'),
                                  (0xe40f22, '0f855cf5ffff'), (0xe40f33, 'ffb048010000'),
                                  (0xe40f3a, 'e970feffff')):
            self.assertEqual(exe.code(address, len(expected) // 2).hex(), expected, hex(address))
        # #1068 is pushed once in the executable, in the join's scope "with" branch.
        self.assertEqual(exe.data.count(bytes.fromhex('682c040000')), 1)
        self.assertEqual(exe.code(0xe41d2a, 5).hex(), '682c040000')
        self.assertEqual(exe.code(0xe41ccd, 9).hex(), '3a45f00f8408010000')
        # Primitive pairs (00e41f90) and pairs without a common base (00e41f60)
        # become the any type: 00e64880 builds a node of [vm+0x134] (ClassClass).
        self.assertEqual(exe.code(0xe41f60, 5).hex(), 'e81b290200')
        self.assertEqual(exe.code(0xe41f90, 5).hex(), 'e8eb280200')
        self.assertEqual(exe.code(0xe6488a, 6).hex(), '8b8034010000')

    def test_patched_tracer_compiles(self):
        cxx = shutil.which('i686-w64-mingw32-g++')
        if not cxx:
            self.skipTest('i686 MinGW compiler required for the SDK translation-unit check')
        checkout = Path(os.environ.get('WOT_SCALEFORM_SDK_GIT',
                                       '/Users/peng/.cache/wot-independent-client/scaleform-sdk-4.2'))
        with tempfile.TemporaryDirectory(prefix='scaleform-tracer-compile-') as directory:
            root = Path(directory)
            try:
                archive = subprocess.run(['git', '-C', str(checkout), 'archive', REVISION, 'Src', 'Include',
                                          '3rdParty/libpng-1.5.13/png.h', '3rdParty/expat-2.1.0/lib'],
                                         check=True, capture_output=True).stdout
            except (OSError, subprocess.CalledProcessError):
                self.skipTest('Pinned Scaleform SDK checkout unavailable')
            subprocess.run(['tar', '-x', '-C', str(root)], input=archive, check=True)
            load_patch().patch_sdk(root)
            result = subprocess.run([cxx, '-std=c++14', '-w', '-c', '-DSF_BUILD_STATICLIB', '-DSF_BUILD_SHIPPING',
                                     '-DXML_STATIC', '-ISrc', '-IInclude', '-ISrc/GFx', '-I3rdParty/expat-2.1.0/lib', TRACER, '-o', 'tracer.o'],
                                    cwd=root, capture_output=True, text=True)
        self.assertEqual(result.returncode, 0, result.stderr[-4000:])


if __name__ == '__main__':
    unittest.main()
