"""The shared battle Flash bridge and the production canvas/minimap bindings.

The production stock_battle_flash/marker_canvas/minimap sources are compiled
against host stand-ins for the engine, GFx display tree and pyscript macros and
run in real CPython 2.7 under the exact #1513 MarkersManager and
MinimapComponent bytecode. The executable audit pins invokePy2As and its
converters. Neither proves the MSVC build, SDK 4.2.22 AS3 behaviour or GFx
rendering in the Windows client.
"""
import os
import re
import struct
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
from stock_battle_gui_support import (ROOT, TESTS, build_probe, client_executable,  # noqa: E402
                                      client_package, python27)


class StockBattleFlashConsumerTest(unittest.TestCase):
    def test_production_bindings_under_exact_pyc_consumers(self):
        python = python27(self)
        package = client_package(self)
        with tempfile.TemporaryDirectory(prefix='stock-battle-flash-') as directory:
            build_probe(self, python, directory)
            result = subprocess.run([python, str(TESTS / 'stock_battle_gui_consumer.py'), directory,
                                     str(package)], env=dict(os.environ, PYTHONDONTWRITEBYTECODE='1'),
                                    stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True)
        self.assertEqual(result.returncode, 0, result.stdout)
        self.assertIn('passed the exact PYC consumers', result.stdout)


class StockBattleFlashSourceTest(unittest.TestCase):
    def test_bridge_shares_the_parent_movie(self):
        bridge = (ROOT / 'bigworld/stock_battle_flash.cpp').read_text()
        update = bridge[bridge.index('void StockSharedMovieComponent::update'):]
        update = update[:update.index('\n}\n')]
        self.assertEqual(re.sub(r'\s+', ' ', update[update.index('{'):]), '{ this->tick( dTime );')
        draw = bridge[bridge.index('void StockSharedMovieComponent::drawSelf'):]
        self.assertEqual(re.sub(r'\s+', '', draw[draw.index('{'):draw.index('}') + 1]), '{}')
        header = (ROOT / 'bigworld/stock_battle_flash.hpp').read_text()
        self.assertNotRegex(header, r'(?m)^\s*Py_Header\(')
        for name in ('getVariable', 'getMember', 'getParent', 'attachMovie', 'invoke', 'removeFromParent',
                     'swapChildren', 'addChild', 'instanceName', 'invokePy2As', 'release'):
            self.assertRegex(header, r'\b%s\(' % name)
        # The bridge needs no engine or SDK patch.
        for name in ('flash', 'marker_canvas', 'minimap'):
            self.assertFalse(list((ROOT / 'bigworld').glob('patch_stock_battle_%s*' % name)))


class StockBattleFlashExecutableTest(unittest.TestCase):
    def test_invoke_py2as_and_converters(self):
        exe = client_executable(self)
        # 013ed040 error texts and the plain Exception they raise.
        for address, text in ((0x1945b7c, 'invokePy2As expected at least 1 argument.'),
                              (0x1945ba8, 'invokePy2As - The first argument must be a String.'),
                              (0x1945bdc, 'invokePy2As - Failed to invoke method.'),
                              (0x1945c04, 'invokePy2As - Failed to invoke method. Bad argument count.')):
            self.assertEqual(exe.cstr(address), text)
        self.assertEqual(exe.cstr(exe.word(exe.word(0x1a5ca9c) + 12)), 'exceptions.Exception')
        code = exe.code(0x13ed040, 0x2f3)
        # Exact str for the name, exact list for the expanded arguments.
        self.assertIn(bytes.fromhex('817804') + struct.pack('<I', 0x1a61400), code)
        self.assertIn(bytes.fromhex('817804') + struct.pack('<I', 0x1a60a88), code)
        self.assertEqual(exe.cstr(exe.word(0x1a61400 + 12)), 'str')
        self.assertEqual(exe.cstr(exe.word(0x1a60a88 + 12)), 'list')
        # 00aaef10: exact unicode, str, int, long, float and bool; anything else is null.
        converter = exe.code(0xaaef10, 0x170)
        types = [struct.unpack('<I', converter[i + 1:i + 5])[0] for i in range(len(converter) - 5)
                 if converter[i] == 0x3d]
        self.assertEqual([exe.cstr(exe.word(t + 12)) for t in types],
                         ['unicode', 'str', 'int', 'long', 'float', 'bool'])
        self.assertEqual(exe.code(0xaaf070, 9), bytes.fromhex('8b4d0c5f5e5de9') + exe.code(0xaaf077, 2))
        self.assertEqual(exe.code(0xaaec20, 7), bytes.fromhex('c7460c01000000'))
        # 00aaf450: jump table for undefined..stringW; Int/UInt and objects write nothing.
        table = [exe.word(0xaaf504 + 4 * i) for i in range(8)]
        self.assertEqual(table[0], table[1])
        self.assertEqual(table[3], table[4])
        self.assertEqual(table[3], 0xaaf4ff)
        self.assertEqual(exe.code(0xaaf461, 6), bytes.fromhex('83f9070f8795'))
        # The three callers of invokePy2As.
        for call in (0x13e0b7c, 0x13e3ec9, 0x13e9037):
            self.assertEqual(call + 5 + struct.unpack('<i', exe.code(call + 1, 4))[0], 0x13ed040)
        # The shared no-op drawSelf of the shared-movie WG components.
        for vtable in (0x1943ef8, 0x1944f6c, 0x1944cbc, 0x1944884):
            self.assertEqual(exe.word(vtable + 46 * 4), 0x1347f90)
        self.assertEqual(exe.word(0x19441dc + 46 * 4), exe.word(0x150b29c + 46 * 4))


if __name__ == '__main__':
    unittest.main()
