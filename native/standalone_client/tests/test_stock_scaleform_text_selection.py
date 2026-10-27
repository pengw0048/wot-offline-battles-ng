"""scaleform.gfx.TextFieldEx selection colours: the #1513 4.6.34 setters on SDK 4.2.22.

The host runs the patched TextFieldEx bodies over the SDK's own Text::EditorKit
colour setters, invalidation and focus code. The original SWF callers and the
#1513 table are audited statically. Neither proves the Windows renderer.
"""
from pathlib import Path
import os
import re
import struct
import subprocess
import sys
import tempfile
import unittest
import zipfile
import zlib

sys.path.insert(0, str(Path(__file__).resolve().parent))
from test_stock_scaleform_scene import ROOT, REVISION, SDK_FILES, load_patch
from stock_battle_gui_support import client_executable, compiler

HEADER = 'Src/GFx/AS3/Obj/Gfx/AS3_Obj_Gfx_TextFieldEx.h'
SOURCE = 'Src/GFx/AS3/Obj/Gfx/AS3_Obj_Gfx_TextFieldEx.cpp'
KIT_H = 'Src/GFx/Text/Text_EditorKit.h'
KIT_CPP = 'Src/GFx/Text/Text_EditorKit.cpp'
CORE_H = 'Src/Render/Text/Text_Core.h'
SETTERS = ('setSelectionTextColor', 'setSelectionBkgColor', 'setInactiveSelectionTextColor',
           'setInactiveSelectionBkgColor')

# 0x400 Flags_Focused selects the active pair (EditorKit::InvalidateSelectionColors,
# #1513 00caeeb0); OnSetFocus/OnKillFocus reapply the stored pairs.
EXPECTED = '''defaults || kit ffffffff ff000000 ffffffff ff808080 focused=0 view 00000000 00000000
text-manager | docview text ffffffff | docview background ff808080 | docview text ffffffff \
| docview background ff808080 | docview text ff1e1e1b | docview background ff808080 \
| docview text ff1e1e1b | docview background ff969687 || kit ff1e1e1b ff969687 ff1e1e1b ff969687 \
focused=0 view ff1e1e1b ff969687
active-unfocused | docview text ff1e1e1b | docview background ff969687 | docview text ff1e1e1b \
| docview background ff969687 || kit ff112233 ff445566 ff1e1e1b ff969687 focused=0 view ff1e1e1b ff969687
focus | docview text ff112233 | docview background ff445566 || kit ff112233 ff445566 ff1e1e1b ff969687 \
focused=1 view ff112233 ff445566
inactive-while-focused | docview text ff112233 | docview background ff445566 \
|| kit ff112233 ff445566 ff1e1e1b 80776655 focused=1 view ff112233 ff445566
active-while-focused | docview text 00ffffff | docview background ff445566 \
|| kit 00ffffff ff445566 ff1e1e1b 80776655 focused=1 view 00ffffff ff445566
blur | docview text ff1e1e1b | docview background 80776655 || kit 00ffffff ff445566 ff1e1e1b 80776655 \
focused=0 view ff1e1e1b 80776655
missing-editor || no editor (view kit 0x0)
extensions-off || kit 00ffffff ff445566 ff1e1e1b 80776655 focused=0 view ff1e1e1b 80776655
extensions-off-null | ArgumentError #1507 textField || kit 00ffffff ff445566 ff1e1e1b 80776655 \
focused=0 view ff1e1e1b 80776655
rejected | ArgumentError #1507 textField | TypeError #1034 flash.text.TextField \
| TypeError #1034 flash.text.TextField | TypeError #1034 flash.text.TextField \
| TypeError #1034 flash.text.TextField \
|| kit 00ffffff ff445566 ff1e1e1b 80776655 focused=0 view ff1e1e1b 80776655
pending-exception || kit 00ffffff ff445566 ff1e1e1b 80776655 focused=0 view ff1e1e1b 80776655
'''


def pinned(test, names):
    checkout = Path(os.environ.get('WOT_SCALEFORM_SDK_GIT',
                                   '/Users/peng/.cache/wot-independent-client/scaleform-sdk-4.2'))
    try:
        return {name: subprocess.run(['git', '-C', str(checkout), 'show', REVISION + ':' + name],
                                     check=True, capture_output=True).stdout for name in names}
    except (OSError, subprocess.CalledProcessError):
        test.skipTest('Pinned Scaleform SDK checkout unavailable')


def write(root, files):
    for name, data in files.items():
        (root / name).parent.mkdir(parents=True, exist_ok=True)
        (root / name).write_bytes(data)


def snapshot(root):
    return {name: (root / name).read_bytes() for name in SDK_FILES}


def between(text, begin, end, include_end=False):
    start = text.index(begin)
    stop = text.index(end, start)
    return text[start:stop + (len(end) if include_end else 0)]


def registrations(source):
    block = between(source, 'const ThunkInfo TextFieldEx::ti[', '    };')
    return re.findall(r'"(\w+)", NULL, Abc::NS_Public, CT_Method, (\d), (\d)', block)


class ScaleformTextSelectionPatchTest(unittest.TestCase):
    def patched(self, root):
        files = pinned(self, SDK_FILES)
        write(root, files)
        load_patch().patch_sdk(root)
        return files

    def test_patch_is_idempotent_and_fails_closed(self):
        patch = load_patch()
        with tempfile.TemporaryDirectory(prefix='scaleform-selection-patch-') as directory:
            root = Path(directory)
            files = self.patched(root)
            final = snapshot(root)
            patch.patch_sdk(root)
            self.assertEqual(final, snapshot(root))
            for name in SETTERS:
                self.assertNotIn(name.encode(), files[SOURCE])
            # Partial states (header only, count only) complete to the same final pair.
            (root / SOURCE).write_bytes(files[SOURCE])
            patch.patch_sdk(root)
            self.assertEqual(final, snapshot(root))
            (root / HEADER).write_bytes(files[HEADER].replace(b'ThunkInfoNum = 10', b'ThunkInfoNum = 14'))
            patch.patch_sdk(root)
            self.assertEqual(final, snapshot(root))
            text = final[SOURCE].decode()
            entry = ('        {TFunc_Classes_TextFieldEx_setSelectionTextColor::Func, NULL, '
                     '"setSelectionTextColor", NULL, Abc::NS_Public, CT_Method, 2, 2},\n')
            changed = (
                (SOURCE, files[SOURCE].replace(b'"getNoTranslate", NULL, Abc::NS_Public, CT_Method, 1, 1}',
                                               b'"getNoTranslate", NULL, Abc::NS_Public, CT_Method, 1, 2}')),
                (SOURCE, text.replace(entry, entry + entry).encode()),
                (SOURCE, text.replace(entry, '').encode()),
                (HEADER, final[HEADER].replace(b'ThunkInfoNum = 14', b'ThunkInfoNum = 12')),
            )
            for name, data in changed:
                write(root, final)
                (root / name).write_bytes(data)
                before = snapshot(root)
                with self.assertRaises(ValueError, msg=name):
                    patch.patch_sdk(root)
                self.assertEqual(before, snapshot(root))

    def test_table_appends_the_four_setters(self):
        with tempfile.TemporaryDirectory(prefix='scaleform-selection-table-') as directory:
            root = Path(directory)
            files = self.patched(root)
            source = (root / SOURCE).read_text()
            header = (root / HEADER).read_text()
        original = registrations(files[SOURCE].decode().replace('\r\n', '\n'))
        self.assertEqual(len(original), 10)
        self.assertEqual(registrations(source), original + [(name, '2', '2') for name in SETTERS])
        self.assertIn('enum { ThunkInfoNum = 14 };', header)
        for name in SETTERS:
            self.assertIn('void %s(const Value& result, const Value& textField, UInt32 color);' % name, header)
            self.assertIn('const Value, const Value&, UInt32> TFunc_Classes_TextFieldEx_%s;' % name, source)
        self.assertNotIn('getSelectionTextColor', source)

    def test_setters_drive_the_sdk_editor_kit(self):
        cxx = compiler(self)
        kit = pinned(self, (KIT_H, KIT_CPP, CORE_H))
        with tempfile.TemporaryDirectory(prefix='scaleform-selection-host-') as directory:
            root = Path(directory)
            self.patched(root / 'sdk')
            source = (root / 'sdk' / SOURCE).read_text()
            kit_h = kit[KIT_H].decode('latin-1').replace('\r\n', '\n')
            kit_cpp = kit[KIT_CPP].decode('latin-1').replace('\r\n', '\n')
            core = kit[CORE_H].decode('latin-1').replace('\r\n', '\n')
            host = (ROOT / 'tests/stock_scaleform_text_selection_host.cpp').read_text()
            for key, value in (
                    ('@SEL_DEFAULTS@', between(core, '#define GFX_ACTIVE_SEL_BKCOLOR',
                                               '#define GFX_INACTIVE_SEL_TEXTCOLOR  0xFFFFFFFFu\n', True)),
                    ('@FOCUS_FLAGS@', between(kit_h, '    void SetFocused()', '    bool IsFocused()')),
                    ('@SELECTION_API@', between(kit_h, '    void SetActiveSelectionTextColor(UInt32 color)',
                                                '    // clipboard operations')),
                    ('@EDITORKIT_BODIES@', between(kit_cpp, 'void EditorKit::OnSetFocus()',
                                                   'void EditorKit::SetSelection(') +
                     between(kit_cpp, 'void EditorKit::InvalidateSelectionColors()', '\n}\n', True)),
                    ('@SETTER_BODIES@', between(source, '    // #1513 00e25ae0/00e25c00',
                                                '//##protect##"class_$methods"'))):
                self.assertEqual(host.count(key), 1, key)
                host = host.replace(key, value)
            (root / 'host.cpp').write_text(host)
            subprocess.run([cxx, '-std=c++14', '-Wall', '-Wextra', '-Werror', '-Wno-unused-parameter',
                            str(root / 'host.cpp'), '-o', str(root / 'host')], check=True)
            output = subprocess.run([str(root / 'host')], check=True, capture_output=True, text=True).stdout
        self.assertEqual(output, EXPECTED)


class ScaleformTextSelectionOriginalTest(unittest.TestCase):
    def test_original_table_and_bodies(self):
        exe = client_executable(self)
        # 015e4b58..015e4c08: setters with (void, TextField, uint) between their getters.
        table = {}
        for entry in range(0x15e4b58, 0x15e4c18, 0x18):
            table[exe.cstr(exe.word(entry + 8))] = (exe.word(entry), exe.word(entry + 4))
        self.assertEqual([name for name in table if name.startswith('set')], list(SETTERS))
        for name, (thunk, types) in table.items():
            if name.startswith('set'):
                self.assertEqual([exe.word(types + 4 * i) for i in range(3)], [0, 0x15dd7f0, 0x15bf7dc])
        self.assertEqual(exe.cstr(exe.word(0x15dd7f0 + 16)), 'TextField')
        self.assertEqual(exe.cstr(exe.word(0x15bf7dc + 16)), 'uint')
        # 005dbaf0/005dba50/005dba70/005dba90 bind the bodies; each writes one EditorKit field.
        for slot, body, field in ((0x1d67ce8, 0xe25ae0, 0x74), (0x1d67cf8, 0xe25c00, 0x70),
                                  (0x1d67d08, 0xe25d20, 0x7c), (0x1d67d18, 0xe25e40, 0x78)):
            init = exe.data.find(b'\xc7\x05' + struct.pack('<I', slot) + struct.pack('<I', body))
            self.assertGreater(init, 0, hex(body))
            code = exe.code(body, 0x8a)
            self.assertIn(b'\x68\xe3\x05\x00\x00', code)                       # push 1507
            self.assertIn(b'\xc7\x00' + struct.pack('<I', 0x15e5e70), code)     # "textField"
            self.assertIn(b'\x80\xb8\x34\x02\x00\x00\x00', code)                # ExtensionsEnabled
            self.assertIn(b'\x8b\x88\xa4\x00\x00\x00\x85\xc9', code)            # EditorKit or return
            self.assertIn(b'\x89\x41' + bytes([field]), code)
            call = code.index(b'\x89\x41' + bytes([field])) + 3
            self.assertEqual(code[call], 0xe8)
            self.assertEqual(body + call + 5 + struct.unpack('<i', code[call + 1:call + 5])[0], 0xcaeeb0)
            self.assertLess(code.index(b'\x68\xe3\x05\x00\x00'), code.index(b'\x80\xb8\x34\x02'))
        self.assertEqual(exe.cstr(0x15e5e70), 'textField')
        # 00caeeb0: focused (0x400) pushes +0x74/+0x70, otherwise +0x7c/+0x78.
        self.assertEqual(exe.code(0xcaeeb3, 0x0f), bytes.fromhex('b8000400008b4e0866858680000000'))

    def test_original_callers_use_the_setters(self):
        client = os.environ.get('WOT_0922_CLIENT')
        if not client:
            self.skipTest('WOT_0922_CLIENT is required for the original SWF')
        package = zipfile.ZipFile(Path(client) / 'res/packages/gui.pkg')
        for swf, expected in (('gui/flash/lobby.swf', ('TextAreaSimple', 'TextInput', 'TextManager',
                                                        'RssNewsFeedRenderer')),
                              ('gui/flash/battle.swf', ('TextInput', 'TextManager'))):
            data = package.read(swf)
            body = zlib.decompress(data[8:]) if data[:3] == b'CWS' else data[8:]
            for name in SETTERS + expected + ('TextFieldEx',):
                self.assertIn(name.encode(), body, (swf, name))


if __name__ == '__main__':
    unittest.main()
