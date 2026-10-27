"""GUI.WGMinimapFlashAS3: the #1513 battle minimap entry component.

The host compiles the production minimap core with an in-memory AS3 display
tree and checks entry IDs, containers, transform flags and per-frame position,
rotation and scale against an independent float32 reference. The executable
audit pins the method table, defaults and constants to #1513. None of this
proves GFx rendering in the Windows client.
"""
import math
import re
import struct
import sys
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
from stock_battle_gui_support import ROOT, client_executable, f32, run_host  # noqa: E402

# #1513 014b2548: 0x42652ee0, one ulp below float(180 / pi).
DEGREES = 57.2957763671875


def arena(bottom_left, top_right):
    return [f32(1.0 / f32(top_right[0] - bottom_left[0])), f32(1.0 / f32(top_right[1] - bottom_left[1])),
            f32(f32(bottom_left[0] + top_right[0]) * 0.5), f32(f32(top_right[1] + bottom_left[1]) * 0.5)]


def length(x, y, z):
    return f32(math.sqrt(f32(f32(f32(x * x) + f32(y * y)) + f32(z * z))))


def entry(world, transform, map_size, rotate, scale):
    m = [world[i * 4:i * 4 + 4] for i in range(4)]
    inverse_width, inverse_height, center_x, center_y = transform
    result = {'x': f32(f32(f32(m[3][0] - center_x) * inverse_width) * map_size[0]),
              'y': f32(f32(f32(center_y - m[3][2]) * inverse_height) * map_size[1])}
    if rotate:
        x, y, z = m[2][:3]
        size = length(x, y, z)
        x, y, z = (0.0, 0.0, 0.0) if size == 0 else (f32(x / size), f32(y / size), f32(z / size))
        result['rotation'] = f32(f32(math.atan2(x, z)) * DEGREES)
    if scale:
        result['xScale'] = f32(length(*m[0][:3]) * 100.0)
        result['yScale'] = f32(length(*m[2][:3]) * 100.0)
    return result


class StockBattleMinimapHostTest(unittest.TestCase):
    def test_minimap_core_matches_independent_reference(self):
        result = run_host(self, 'stock_battle_minimap_host.cpp')
        self.assertEqual(result['arena'], arena((-500.0, -400.0), (500.0, 600.0)))
        self.assertEqual(result['mapSize'], [210.0, 210.0])
        rotations = {}
        for item in result['entries']:
            expected = entry(item['world'], result['arena'], result['mapSize'], item['rotate'], item['scale'])
            self.assertTrue(item['visible'])
            for key, value in expected.items():
                self.assertEqual(item[key], value, (item['id'], key))
            if not item['scale']:
                self.assertEqual((item['xScale'], item['yScale']), (100.0, 100.0))
            if not item['rotate']:
                self.assertEqual(item['rotation'], 0.0)
            rotations[item['id']] = item.get('rotation')
        self.assertAlmostEqual(rotations[1], 30.0, places=4)
        self.assertAlmostEqual(rotations[2], -135.0, places=4)
        first = result['entries'][0]
        self.assertAlmostEqual(first['xScale'], 200.0, places=4)
        # Entry 1 at (120, -45) on a 1000x1000 arena centred on (0, 100).
        self.assertAlmostEqual(first['x'], 120 * 0.21, places=4)
        self.assertAlmostEqual(first['y'], (100 + 45) * 0.21, places=4)

    def test_transform_flags_follow_python_names(self):
        full, no_position, no_rotation, no_scale = 0xffffffff, 1, 2, 4
        default = full ^ no_scale
        rotates = lambda flags: bool((flags >> 1) & 1)
        scales = lambda flags: bool((flags >> 2) & 1)
        self.assertEqual((rotates(default), scales(default)), (True, False))
        self.assertEqual((rotates(full ^ no_rotation), scales(full ^ no_rotation)), (False, True))
        self.assertEqual((rotates(full ^ no_position), scales(full ^ no_position)), (True, True))


class StockBattleMinimapSourceTest(unittest.TestCase):
    def test_binding_declares_the_original_surface(self):
        source = (ROOT / 'bigworld/stock_battle_minimap.cpp').read_text()
        self.assertEqual(re.findall(r'^\s+PY_METHOD\( (\w+) \)', source, re.M),
                         ['addEntry', 'delEntry', 'moveEntry', 'entryInvoke', 'entrySetMatrix',
                          'entrySetActive', 'setBackground', 'setArenaBB'])
        self.assertEqual(re.findall(r'^\s+PY_ATTRIBUTE\( (\w+) \)', source, re.M), ['mapSize'])
        self.assertIn('Py_Header( WGMinimapGUIComponentAS3, FlashGUIComponent )', source)
        self.assertIn('PY_FACTORY_NAMED( WGMinimapGUIComponentAS3, "WGMinimapFlashAS3", GUI )', source)
        self.assertIn('OPTARG( MatrixProviderPtr, NULL, OPTARG( bool, false, OPTARG( uint, 0xffffffff,',
                      source)
        self.assertIn('PyArg_ParseTuple( args, "Os", &pMovie, &path )', source)


class StockBattleMinimapExecutableTest(unittest.TestCase):
    def test_exact_type_methods_defaults_and_constants(self):
        exe = client_executable(self)
        self.assertEqual(exe.cstr(0x1945434), 'WGMinimapFlashAS3')
        minimap = exe.type_object(0x1ce40b0)
        self.assertEqual(minimap['name'], 'WGMinimapGUIComponentAS3')
        self.assertEqual(minimap['basicsize'], 0x1f8)
        self.assertEqual([(name, flags) for name, _, flags in exe.method_table(minimap['methods'])],
                         [('addEntry', 3), ('delEntry', 3), ('moveEntry', 3), ('entryInvoke', 3),
                          ('entrySetMatrix', 3), ('entrySetActive', 3), ('setBackground', 3),
                          ('setArenaBB', 3)])
        self.assertIn(b'\xc7\x05\x30\x41\xce\x01\x58\xf8\xa1\x01', exe.code(0x601fe1, 0x10))
        self.assertEqual(exe.cstr(0x19455c0), 'mapSize')
        self.assertEqual(exe.cstr(0x1764b6c), 'Os')
        # 013ea283..013ea2ca: inverse sizes, centre and map size defaults.
        defaults = exe.code(0x13ea283, 0x51)
        self.assertEqual(defaults.count(struct.pack('<f', 300.0)), 4)
        # 013e8dee: transformProps defaults to 0xffffffff; 013e98a0: bits 1 and 2.
        self.assertEqual(exe.code(0x13e8dee, 3), bytes.fromhex('83c8ff'))
        self.assertEqual(exe.code(0x13e989e, 12), bytes.fromhex('8bc18b7de8d1e82401c1e902'))
        # 013e970d..013e9727: a recycled ID, else map size + 1.
        self.assertEqual(exe.code(0x13e9719, 3), bytes.fromhex('8b4b04'))
        self.assertEqual(exe.code(0x13e9721, 1), b'\x41')
        self.assertEqual(exe.float(0x14ae8e4), 0.5)
        self.assertEqual(exe.code(0x14b2548, 4), struct.pack('<f', DEGREES))
        self.assertEqual(exe.float(0x14b0950), 100.0)
        for address, text in ((0x1945620, 'GUI.WGMinimapFlash.delEntry - invalid entry id %i'),
                              (0x1945654, 'GUI.WGMinimapFlash.moveEntry - invalid entry id %i'),
                              (0x1945688, 'GUI.WGMinimapFlash.entryInvoke - invalid entry id %i'),
                              (0x19456c0, 'GUI.WGMinimapFlash.entrySetMatrix - invalid entry id %i'),
                              (0x19456f8, 'GUI.WGMinimapFlash.entrySetActive - invalid entry id %i'),
                              (0x1945730, 'GUI.WGMinimapFlash.setBackground - method not implemented'),
                              (0x19455d0, "GUI.WGMinimapFlash: Runtime error: Container '%s' is not "
                                          "found into the movie."),
                              (0x1945770, 'GUI.WGMinimapFlash: Argument parsing error: Expected a '
                                          'PyMovieView object and String'),
                              (0x19457c8, 'GUI.WGMinimapFlash: Argument parsing error: Expected a '
                                          'PyMovieView object'),
                              (0x1945818, "GUI.WGMinimapFlash: Argument parsing error: PyMovieView "
                                          "object hasn't '%s' movie clip.")):
            self.assertEqual(exe.cstr(address), text)
        # Invalid IDs raise KeyError and print it (PyErr_PrintEx(0) at 00fc7680).
        self.assertEqual(exe.cstr(exe.word(exe.word(0x1a5d61c) + 12)), 'exceptions.KeyError')
        self.assertEqual(exe.cstr(exe.word(exe.word(0x1a5da04) + 12)), 'exceptions.NotImplementedError')
        for call in (0x13e9b92, 0x13e9ff2, 0x13e906f, 0x13e91b8):
            self.assertEqual(exe.code(call, 1), b'\xe8')
            self.assertEqual(call + 5 + struct.unpack('<i', exe.code(call + 1, 4))[0], 0xfc7680)
        # 013ea4c0 update never advances the movie; 013ea3b0 is reached only
        # with a background texture, which no code sets (013ea2d4 writes 0).
        update = exe.code(0x13ea4c0, 0x300)
        self.assertNotIn(bytes.fromhex('8b4060ffd0'), update)
        self.assertEqual(exe.code(0x13ea2d4, 10), bytes.fromhex('c787f001000000000000'))


if __name__ == '__main__':
    unittest.main()
