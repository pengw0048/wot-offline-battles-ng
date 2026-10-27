"""GUI.WGVehicleMarkersCanvasFlashAS3: the #1513 battle vehicle-marker canvas.

The host compiles the production canvas core with an in-memory AS3 display
tree and checks IDs, depths, provider rules, per-frame Scaleform 3D projection
and the swapChildren depth sort against an independent float32 reference. The
executable audit pins the method table, defaults and constants to #1513. None of
this proves GFx rendering in the Windows client.
"""
import math
import re
import struct
import sys
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
from stock_battle_gui_support import ROOT, client_executable, f32, run_host  # noqa: E402


def transform(matrix, x, y, z):
    m = [matrix[i * 4:i * 4 + 4] for i in range(4)]
    out = []
    for c in range(4):
        value = f32(x * m[0][c])
        value = f32(value + f32(y * m[1][c]))
        value = f32(value + f32(z * m[2][c]))
        out.append(f32(value + m[3][c]))
    return out


def distance_value(properties, view_z, far, zoom):
    minimum, maximum, initial, speed = properties
    distance = f32(view_z * f32(1.0 / far))
    value = f32(initial - f32(f32(f32(speed * distance) * zoom) * 100.0))
    if minimum > value:
        return minimum
    return maximum if maximum < value else value


def multiply(a, b):
    result = []
    for r in range(4):
        for c in range(4):
            value = f32(a[r * 4] * b[c])
            for k in range(1, 4):
                value = f32(value + f32(a[r * 4 + k] * b[k * 4 + c]))
            result.append(value)
    return result


def projection(camera, x, y, view_z):
    width = f32(camera['halfWidth'] + camera['halfWidth'])
    height = f32(camera['halfHeight'] + camera['halfHeight'])
    depth = f32(camera['far'] - camera['near'])
    p = [0.0] * 16
    scale_x = f32(f32(0.05) / width)
    scale_y = f32(f32(-0.05) / height)
    p[0] = f32(scale_x + scale_x)
    p[5] = f32(scale_y + scale_y)
    p[10] = f32(1.0 / depth)
    p[11] = f32(f32(-1.0 / depth) * camera['near'])
    p[15] = 1.0
    t = [1.0, 0, 0, 0, 0, 1.0, 0, 0, 0, 0, 1.0, 0, 0, 0, 0, 1.0]
    t[3] = f32(camera['halfWidth'] * x * 20.0)
    t[7] = f32(camera['halfHeight'] * y * -20.0)
    t[11] = view_z
    return multiply(p, t)


class StockBattleMarkerCanvasHostTest(unittest.TestCase):
    def test_canvas_core_matches_independent_reference(self):
        result = run_host(self, 'stock_battle_marker_canvas_host.cpp')
        camera = result['camera']
        scale, alpha = result['scale'], result['alpha']
        self.assertEqual(scale, [40.0, 100.0, 100.0, f32(0.45)])
        self.assertEqual(alpha, [50.0, 80.0, 100.0, 2.0])
        # Bare (z 160, depth 4) and marker 1 (z 100, depth 2) swap; marker 3 is behind.
        self.assertEqual(result['children'], ['VehicleMarker0', 'VehicleMarker2', 'BareMarker4',
                                              'VehicleMarker3', 'VehicleMarker1'])
        for marker in result['markers']:
            world = marker['world']
            x, y, z = world[12:15]
            view_z = transform(camera['view'], x, y, z)[2]
            self.assertEqual(marker['viewZ'], view_z, marker['id'])
            clip = transform(camera['viewProjection'], x, y, z)
            visible = clip[3] > 0
            self.assertEqual(marker['visible'], visible, marker['id'])
            if visible:
                inverse = f32(1.0 / clip[3])
                clip[0], clip[1] = f32(clip[0] * inverse), f32(clip[1] * inverse)
            self.assertEqual(marker['projection'], projection(camera, clip[0], clip[1], view_z),
                             marker['id'])
            self.assertEqual(marker['scale'], distance_value(scale, view_z, camera['far'], camera['zoom']))
            self.assertEqual(marker['alpha'], distance_value(alpha, view_z, camera['far'], camera['zoom']))
            value = f32(100.0 * f32(0.01))
            self.assertEqual(marker['matrix3D'], [value, 0, 0, 0, 0, value, 0, 0, 0, 0, 1.0, 0])
        near = next(marker for marker in result['markers'] if marker['id'] == 1)
        far = next(marker for marker in result['markers'] if marker['id'] == 0)
        # A positive speed shrinks and fades distant markers within (min, max).
        self.assertGreater(near['scale'], far['scale'])
        self.assertGreater(near['alpha'], far['alpha'])
        self.assertTrue(40.0 < far['scale'] < near['scale'] < 100.0)
        self.assertEqual(near['alpha'], 80.0)
        self.assertTrue(50.0 < far['alpha'] < 80.0)
        # Markers on screen project their origin to the vehicle's NDC position.
        for marker in result['markers']:
            if marker['visible']:
                m = marker['projection']
                self.assertLess(abs(m[3]), 1.0)
                self.assertLess(abs(m[7]), 1.0)

    def test_distance_value_edges(self):
        defaults = [50.0, 100.0, 100.0, f32(-0.3)]
        self.assertEqual(distance_value(defaults, 0.0, 1500.0, 1.0), 100.0)
        self.assertEqual(distance_value(defaults, -100.0, 1500.0, 1.0), 98.0)
        self.assertEqual(distance_value(defaults, 1e9, 1500.0, 1.0), 100.0)
        self.assertEqual(distance_value([50.0, 100.0, 100.0, 0.3], 1500.0, 1500.0, 1.0), 70.0)
        self.assertEqual(distance_value([50.0, 100.0, 100.0, 0.3], 1e6, 1500.0, 1.0), 50.0)


class StockBattleMarkerCanvasSourceTest(unittest.TestCase):
    def test_binding_declares_the_original_surface(self):
        source = (ROOT / 'bigworld/stock_battle_marker_canvas.cpp').read_text()
        methods = re.findall(r'^\s+PY_METHOD\( (\w+) \)', source, re.M)
        self.assertEqual(methods, ['addMarker', 'delMarker', 'markerInvoke', 'markerSetMatrix',
                                   'markerSetActive'])
        self.assertEqual(re.findall(r'^\s+PY_ATTRIBUTE\( (\w+) \)', source, re.M),
                         ['scaleProperties', 'alphaProperties'])
        self.assertIn('Py_Header( WGVehicleMarkersCanvasGUIComponentAS3, FlashGUIComponent )', source)
        self.assertIn('PY_FACTORY_NAMED( WGVehicleMarkersCanvasGUIComponentAS3, '
                      '"WGVehicleMarkersCanvasFlashAS3", GUI )', source)
        self.assertIn('ARG( MatrixProviderPtr, ARG( BW::string, OPTARG( bool, false, END ) ) )', source)
        self.assertIn('"_root.vehicleMarkersCanvas"', source)
        bridge = (ROOT / 'bigworld/stock_battle_flash.cpp').read_text()
        update = bridge[bridge.index('StockSharedMovieComponent::update'):]
        update = update[:update.index('\n}\n')]
        self.assertNotIn('Advance', update)
        self.assertNotIn('SimpleGUIComponent::update', update)


class StockBattleMarkerCanvasExecutableTest(unittest.TestCase):
    def test_exact_type_methods_defaults_and_constants(self):
        exe = client_executable(self)
        self.assertEqual(exe.cstr(0x1944848), 'WGVehicleMarkersCanvasFlashAS3')
        canvas = exe.type_object(0x1ce3b30)
        self.assertEqual(canvas['name'], 'WGVehicleMarkersCanvasGUIComponentAS3')
        self.assertEqual(canvas['basicsize'], 0x1e0)
        self.assertEqual([(name, flags) for name, _, flags in exe.method_table(canvas['methods'])],
                         [('addMarker', 3), ('delMarker', 3), ('markerInvoke', 3),
                          ('markerSetMatrix', 3), ('markerSetActive', 3)])
        # 006013fc: tp_base = FlashGUIComponent.
        self.assertIn(b'\xc7\x05\xb0\x3b\xce\x01\x58\xf8\xa1\x01', exe.code(0x6013f7, 0x10))
        self.assertEqual(exe.cstr(exe.word(0x1a1f858 + 12)), 'FlashGUIComponent')
        # 00601380/006013ab: getset names in order.
        init = exe.code(0x601380, 0x40)
        self.assertLess(init.index(struct.pack('<I', 0x1944994)), init.index(struct.pack('<I', 0x19449a4)))
        self.assertEqual(exe.cstr(0x1944994), 'scaleProperties')
        self.assertEqual(exe.cstr(0x19449a4), 'alphaProperties')
        # 013e4c98..013e4ce4: (50, 100, 100, -0.3) for both properties.
        defaults = exe.code(0x13e4c98, 0x56)
        for value in (50.0, 100.0, 100.0, f32(-0.3)):
            self.assertIn(struct.pack('<f', value), defaults)
        self.assertEqual(exe.cstr(0x19449b4), '_root.vehicleMarkersCanvas')
        self.assertEqual((exe.cstr(0x1944aa0), exe.cstr(0x1944aa8)), ('marker', 'bgShadow'))
        self.assertEqual(exe.cstr(0x19449f4), '{0}{1}')
        self.assertEqual(exe.cstr(0x1944a00),
                         'GUI.WGVehicleMarkersCanvasFlash.markerSetActive - invalid marker id %i')
        self.assertEqual(exe.cstr(0x1944a48), 'GUI.WGVehicleMarkersCanvasFlash: Argument parsing '
                         'error: Expected a PyMovieView object')
        self.assertEqual(exe.cstr(0x14add94), 'O')
        # Projection constants read by 013e5528..013e55e6 and 013e56fd.
        for address, value in ((0x14ae8d4, 0.05), (0x14b4340, -0.05), (0x14ae8f0, -1.0),
                               (0x14b094c, 20.0), (0x1944ac8, -20.0), (0x14b0950, 100.0),
                               (0x14b2500, 0.01), (0x14ae260, 1.0)):
            self.assertEqual(exe.float(address), f32(value), hex(address))
        # 013e4230: sort by view depth; 013e4210: signed depth comparison.
        self.assertEqual(exe.code(0x13e423d, 0xc), bytes.fromhex('f30f1040700f2f41700f97c0'))
        self.assertEqual(exe.code(0x13e421f, 5), bytes.fromhex('3b010f9cc0'))
        # 013e484a..013e4869: free ID or map size (no +1); 013e48ca: depth counter.
        self.assertEqual(exe.code(0x13e4856, 3), bytes.fromhex('8b4b04'))
        self.assertEqual(exe.code(0x13e486c, 3), bytes.fromhex('894df0'))
        self.assertEqual(exe.code(0x13e48ca, 0x10), bytes.fromhex('8b87ac010000894510408987ac010000'))
        # 013e4151: an active marker ignores a cleared provider.
        self.assertEqual(exe.code(0x13e4151, 0xb), bytes.fromhex('807979008b4508740485c0'))
        # 013e5694/013e56b3/013e56de: view matrix, projection matrix and visibility flags.
        update = exe.code(0x13e5694, 0x60)
        self.assertIn(bytes.fromhex('b800200000'), update)
        self.assertIn(bytes.fromhex('b800100000'), update)
        self.assertIn(bytes.fromhex('66838d24ffffff40'), update)
        # The canvas draws nothing (01347f90) and does not advance the movie.
        self.assertEqual(exe.word(0x1944884 + 46 * 4), 0x1347f90)
        self.assertEqual(exe.code(0x1347f90, 3), bytes.fromhex('c20c00'))


if __name__ == '__main__':
    unittest.main()
