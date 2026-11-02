"""#1513 HTML inherits mutable default formats, including dynamic chat fonts.

The production HTML setup runs on a host, with source and exact-instruction
oracles for both paths and the CSS guard. SWF and font tables prove the authored
alias/glyph contracts. Actual glyph drawing still requires the Windows client.
"""
from pathlib import Path
import os
import struct
import subprocess
import sys
import tempfile
import unittest
import zipfile
import zlib

sys.path.insert(0, str(Path(__file__).resolve().parent))
from stock_battle_gui_support import client_executable, compiler
from stock_flash_scheduler_abc import ABC, Reader
from test_stock_scaleform_class_alias import pinned_sdk, write
from test_stock_scaleform_scene import ROOT, load_patch
from tools.packed_xml import read_packed_xml

SOURCE = 'Src/GFx/GFx_TextField.cpp'


def setup_blocks(source):
    """Take the production setup immediately before both presentation parsers."""
    source = source.replace('\r\n', '\n')
    start = source.index('bool    TextField::SetTextValue(')
    end = source.index('\n}', start)
    method = source[start:end]
    marker = 'Text::StyledText::HTMLImageTagInfoArray imageInfoArray'
    blocks = []
    offset = 0
    while marker in method[offset:]:
        stop = method.index(marker, offset)
        begin = method.rfind('GetInitialFormats(&txtFmt, &paraFmt);', 0, stop)
        blocks.append(method[begin:stop])
        offset = stop + len(marker)
    assert len(blocks) == 2
    return blocks  # translated, untranslated


def swf_tags(swf):
    body = zlib.decompress(swf[8:]) if swf[:3] in (b'CWS', b'CFX') else swf[8:]
    reader = Reader(body)
    reader.pos = (5 + 4 * (body[0] >> 3) + 7) // 8 + 4
    while reader.pos < len(body):
        tag = int.from_bytes(reader.raw(2), 'little')
        size, kind = tag & 63, tag >> 6
        if size == 63:
            size = int.from_bytes(reader.raw(4), 'little')
        yield kind, reader.raw(size)


def references(abc, code, opcode, name):
    positions = []
    for index, candidate in enumerate(abc.names):
        if candidate != name:
            continue
        encoded = []
        while index >= 128:
            encoded.append((index & 127) | 128)
            index >>= 7
        encoded.append(index)
        instruction = bytes([opcode] + encoded)
        if instruction in code:
            positions.append(code.index(instruction))
    return positions


class ScaleformDefaultFormatTest(unittest.TestCase):
    def test_production_html_setup_preserves_mutable_defaults_and_css(self):
        files = pinned_sdk(self)
        patch = load_patch()
        with tempfile.TemporaryDirectory(prefix='scaleform-html-format-') as directory:
            root = Path(directory)
            write(root / 'sdk', files)
            patch.patch_sdk(root / 'sdk')
            patched = (root / 'sdk' / SOURCE).read_text()
            final = (root / 'sdk' / SOURCE).read_bytes()
            timestamps = {name: (root / 'sdk' / name).stat().st_mtime_ns for name in files}
            patch.patch_sdk(root / 'sdk')
            self.assertEqual(final, (root / 'sdk' / SOURCE).read_bytes())
            self.assertEqual(timestamps, {name: (root / 'sdk' / name).stat().st_mtime_ns
                                          for name in files})
            outputs = {}
            for mode, source in [('pristine', files[SOURCE].decode('utf-8-sig')), ('patched', patched)]:
                translated, untranslated = setup_blocks(source)
                host = (ROOT / 'tests/stock_scaleform_default_format_host.cpp').read_text()
                host = host.replace('@TRANSLATED@', translated).replace('@UNTRANSLATED@', untranslated)
                path = root / (mode + '.cpp')
                path.write_text(host)
                binary = root / mode
                subprocess.run([compiler(self), '-std=c++14', '-Wall', '-Wextra', '-Werror',
                                str(path), '-o', str(binary)], check=True)
                outputs[mode] = subprocess.run([str(binary)], check=True, capture_output=True,
                                               text=True).stdout.splitlines()
            for before, after in zip(outputs['pristine'], outputs['patched']):
                flags = after.split()[0]
                if flags[1] == '1':  # CSS ignores mutable defaults, exactly as #1513.
                    self.assertEqual(after, before)
                    self.assertTrue(after.endswith('12 false  left 0 0'), after)
                else:
                    self.assertEqual(after, flags + ' $FieldFont 14 false green left 3 2')
                    self.assertNotEqual(before, after)
                    if flags[2] == '1':
                        self.assertIn('Times New Roman', before)
            # A changed setup must fail without writing any partial SDK edits.
            write(root / 'sdk', files)
            changed = files[SOURCE].replace(b'GetInitialFormats(&txtFmt, &paraFmt);',
                                            b'GetInitialFormats( &txtFmt, &paraFmt );')
            (root / 'sdk' / SOURCE).write_bytes(changed)
            with self.assertRaises(ValueError):
                patch.patch_sdk(root / 'sdk')
            self.assertEqual(changed, (root / 'sdk' / SOURCE).read_bytes())

    def test_exact_original_html_merges_and_css_guards(self):
        exe = client_executable(self)
        for guard, merge_text, merge_paragraph, parse in (
                (0xca0911, 0xca092d, 0xca0959, 0xca09d4),
                (0xca0adb, 0xca0af7, 0xca0b23, 0xca0b9f)):
            code = exe.code(guard, 10)
            self.assertEqual(code[:4], bytes.fromhex('f6435001'))  # HasStyleSheet.
            self.assertEqual(code[4], 0x75)
            target = guard + 6 + struct.unpack('<b', code[5:6])[0]
            self.assertGreater(target, merge_paragraph)
            self.assertLess(target, parse)
            for call, destination in ((merge_text, 0xbb9120), (merge_paragraph, 0xbb9cb0),
                                      (merge_text + 9, 0xbb8a60), (merge_paragraph + 9, 0xbba170)):
                instruction = exe.code(call, 5)
                self.assertEqual(instruction[0], 0xe8)
                self.assertEqual(call + 5 + struct.unpack('<i', instruction[1:])[0], destination)
            # StyledText current text/paragraph defaults are the merge RHS.
            prefix = exe.code(guard + 6, merge_paragraph - guard - 6)
            self.assertIn(bytes.fromhex('ff701c'), prefix)
            self.assertIn(bytes.fromhex('ff7018'), prefix)

    def test_stock_battle_message_sets_the_field_alias_before_html(self):
        client = os.environ.get('WOT_0922_CLIENT')
        if not client:
            self.skipTest('WOT_0922_CLIENT is required for exact #1513 SWF/font data')
        with zipfile.ZipFile(Path(client) / 'res/packages/gui.pkg') as package:
            for kind, data in swf_tags(package.read('gui/flash/battle.swf')):
                if kind == 82 and b'BattleMessengerPool' in data:
                    abc = ABC(data[data.index(b'\0', 4) + 1:])
                    break
            else:
                self.fail('Original BattleMessenger ABC not found')
        # The caller stores Fonts.FIELD_FONT on its own TextFormat; renderers
        # assign that object to messageField.defaultTextFormat before htmlText.
        # Exact battle.swf method 13911 is BattleMessage's instance initializer.
        constructor = abc.bodies[13911]
        field_font = references(abc, constructor, 0x66, 'FIELD_FONT')
        font_set = references(abc, constructor, 0x61, 'font')
        self.assertEqual(len(field_font), 1)
        self.assertEqual(len(font_set), 1)
        self.assertLess(field_font[0], font_set[0])
        self.assertIn('$FieldFont', abc.strings)
        update = abc.bodies[abc.lookup('BattleMessage', 'updateTextFieldProperties')]
        self.assertLess(references(abc, update, 0x66, '_textFormat')[-1],
                        references(abc, update, 0x61, 'defaultTextFormat')[0])
        update = abc.bodies[abc.lookup('BattleMessage', 'updateData')]
        self.assertEqual(len(references(abc, update, 0x61, 'htmlText')), 1)
        config = read_packed_xml((Path(client) / 'res/gui/flash/fontconfig.xml').read_bytes())
        config = next(value.value for name, value in config.children if name == b'config')
        self.assertIn((b'fontlib', b'fonts_zh_cn_sg.swf'),
                      [(name, value.value) for name, value in config.children])
        mapping = next(value.value for name, value in config.children if name == b'map')
        aliases = [dict((name, value.value) for name, value in alias.value.children)
                   for unused, alias in mapping.children]
        self.assertIn({b'embedded': b'$FieldFont', b'runtime': b'Tahoma'}, aliases)
        # Exact fontlib really contains the Chinese glyphs in these messages.
        fonts = Path(client) / 'res/gui/flash/fonts_zh_cn_sg.swf'
        seen = {}
        for kind, data in swf_tags(fonts.read_bytes()):
            if kind != 75:  # DefineFont3.
                continue
            flags, length = data[2], data[4]
            name = data[5:5 + length].rstrip(b'\0').decode('latin-1')
            count = int.from_bytes(data[5 + length:7 + length], 'little')
            width = 4 if flags & 8 else 2
            table = 7 + length
            code_start = table + int.from_bytes(data[table + count * width:table + (count + 1) * width],
                                                'little')
            codes = {int.from_bytes(data[code_start + i * 2:code_start + i * 2 + 2], 'little')
                     for i in range(count)}
            if name == 'Tahoma':
                seen[name] = codes
        self.assertIn('Tahoma', seen)
        self.assertTrue({ord(c) for c in '注意遵命赤色彗星'} <= seen['Tahoma'])


if __name__ == '__main__':
    unittest.main()
