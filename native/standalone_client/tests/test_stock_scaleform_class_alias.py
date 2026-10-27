"""flash.net registerClassAlias/getClassByAlias: the #1513 4.6.34 contract on SDK 4.2.22.

The host runs the patched SDK bodies over an AS3 fixture. The real SDK and the
exact gui/flash/lobby.swf are exercised only by the Win32 probe.
"""
from pathlib import Path
import os
import re
import subprocess
import sys
import tempfile
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parent))
from test_stock_scaleform_scene import ROOT, REVISION, load_patch

GLOBAL_H = 'Src/GFx/AS3/Obj/AS3_Obj_Global.h'
GLOBAL_CPP = 'Src/GFx/AS3/Obj/AS3_Obj_Global.cpp'
VM_H = 'Src/GFx/AS3/AS3_VM.h'
VM_CPP = 'Src/GFx/AS3/AS3_VM.cpp'
SDK_FILES = ('Src/GFx/AMP/Amp_Server.h', '3rdParty/libpng-1.5.13/png.h', 'Src/Kernel/SF_SIMD_SSE.h',
             'Src/GFx/AS3/Obj/Utils/AS3_Obj_Utils_ByteArray.cpp', 'Src/GFx/AS3/Obj/AS3_Obj_Date.cpp',
             'Src/GFx/AS3/Obj/Display/AS3_Obj_Display_MovieClip.cpp', GLOBAL_H, GLOBAL_CPP, VM_H, VM_CPP)
ALIAS = 'net.wg.gui.components.controls.VO.ActionPriceVO'
# 00d99f10/00e59350 resolve; 00e59390 moves an alias between files; 00e4fa20
# drops a destroyed file's aliases. Builtin classes have no file.
EXPECTED = '''unknown {0} -> ReferenceError #1014 {0}
registered {0} -> ActionPriceVO
  lobby.swf: {0}
second-alias second -> ActionPriceVO
moved tankman -> SettingsControlProp
  lobby.swf: {0} second
  settings: tankman
null-class ignored -> ReferenceError #1014 ignored
null-alias null -> TankmanVO
text-null null -> ReferenceError #1014 null
  lobby.swf: {0} null second
settings-gone tankman -> ReferenceError #1014 tankman
settings-gone second -> ActionPriceVO
lobby-gone {0} -> ReferenceError #1014 {0}
lobby-gone second -> ReferenceError #1014 second
lobby-gone null -> ReferenceError #1014 null
lobby-gone builtin -> Object
remaining 1
'''.format(ALIAS)


def pinned_sdk(test):
    checkout = Path(os.environ.get('WOT_SCALEFORM_SDK_GIT',
                                   '/Users/peng/.cache/wot-independent-client/scaleform-sdk-4.2'))
    try:
        return {name: subprocess.run(['git', '-C', str(checkout), 'show', REVISION + ':' + name],
                                     check=True, capture_output=True).stdout for name in SDK_FILES}
    except (OSError, subprocess.CalledProcessError):
        test.skipTest('Pinned Scaleform SDK checkout unavailable')


def write(root, files):
    for name, data in files.items():
        (root / name).parent.mkdir(parents=True, exist_ok=True)
        (root / name).write_bytes(data)


def between(text, begin, end):
    start = text.index(begin)
    return text[start:text.index(end, start)]


def flash_net(source):
    block = between(source.replace('\r\n', '\n'), '"flash.net", NULL};', '            };')
    return re.findall(r'"(\w+)", NULL, Abc::NS_Public, CT_Method, (\d), (\d)', block)


class ScaleformClassAliasTest(unittest.TestCase):
    def test_patch_is_idempotent_preserves_bytes_and_fails_closed(self):
        files = pinned_sdk(self)
        patch = load_patch()
        with tempfile.TemporaryDirectory(prefix='scaleform-alias-patch-') as directory:
            root = Path(directory)
            write(root, files)
            patch.patch_sdk(root)
            first = {name: (root / name).read_bytes() for name in SDK_FILES}
            # AS3_VM.cpp keeps its single Windows-1252 comment byte.
            self.assertEqual(files[VM_CPP].count(b'\x92'), 1)
            self.assertEqual(first[VM_CPP].count(b'\x92'), 1)
            patch.patch_sdk(root)
            self.assertEqual(first, {name: (root / name).read_bytes() for name in SDK_FILES})
            write(root, files)
            (root / VM_CPP).write_bytes(files[VM_CPP].replace(b'VMFile::~VMFile()\r\n{\r\n}',
                                                              b'VMFile::~VMFile()\r\n{\r\n\r\n}'))
            before = {name: (root / name).read_bytes() for name in SDK_FILES}
            with self.assertRaises(ValueError):
                patch.patch_sdk(root)
            self.assertEqual(before, {name: (root / name).read_bytes() for name in SDK_FILES})

    def test_flash_net_table_matches_original_order_and_arity(self):
        files = pinned_sdk(self)
        patch = load_patch()
        self.assertEqual(flash_net(files[GLOBAL_CPP].decode()), [('navigateToURL', '1', '2')])
        with tempfile.TemporaryDirectory(prefix='scaleform-alias-table-') as directory:
            root = Path(directory)
            write(root, files)
            patch.patch_sdk(root)
            source = (root / GLOBAL_CPP).read_text()
        # 015c30b0: navigateToURL (1, 2), getClassByAlias (1, 1), registerClassAlias (2, 2).
        self.assertEqual(flash_net(source), [('navigateToURL', '1', '2'), ('getClassByAlias', '1', '1'),
                                             ('registerClassAlias', '2', '2')])
        self.assertIn('getClassByAlias::Func, &AS3::ClassClassTI,', source)

    def test_patched_bodies_match_original_runtime(self):
        files = pinned_sdk(self)
        patch = load_patch()
        with tempfile.TemporaryDirectory(prefix='scaleform-alias-host-') as directory:
            root = Path(directory)
            write(root / 'sdk', files)
            patch.patch_sdk(root / 'sdk')
            text = {name: (root / 'sdk' / name).read_text(encoding='latin-1')
                    for name in (GLOBAL_CPP, VM_H, VM_CPP)}
            host = (ROOT / 'tests/stock_scaleform_class_alias_host.cpp').read_text()
            for key, value in (
                    ('@FILE_API@', between(text[VM_H], '    // The flash.net aliases of this file',
                                           '\n\npublic:')),
                    ('@FILE_TEARDOWN@', between(text[VM_CPP], '    // 00e4fa20', '\n}\n')),
                    ('@VM_BODIES@', between(text[VM_CPP], 'void VM::RegisterClassAlias(',
                                            'void VM::AddVMAbcFileWeak(')),
                    ('@GLOBAL_BODIES@', between(text[GLOBAL_CPP], '    // 00d99f10',
                                                '    void GlobalObjectCPP::fscommand(')
                     .replace('GlobalObjectCPP::', '::GlobalObjectCPP::'))):
                host = host.replace(key, value)
            (root / 'host.cpp').write_text(host)
            subprocess.run(['c++', '-std=c++14', '-UNDEBUG', '-Wall', '-Werror', str(root / 'host.cpp'),
                            '-o', str(root / 'host')], check=True)
            output = subprocess.run([str(root / 'host')], check=True, capture_output=True, text=True).stdout
        self.assertEqual(output, EXPECTED)


if __name__ == '__main__':
    unittest.main()
