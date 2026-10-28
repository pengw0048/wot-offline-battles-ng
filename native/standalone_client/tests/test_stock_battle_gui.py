"""#1513 battle crosshair, gun-marker providers and hit/tank indicators.

The production core (stock_battle_gui_data.hpp) is compiled for the host and
compared with the original x86 provider instructions run by Unicorn. A CPython
2.7 host compiles the three production bindings over a recording GFx/render
fixture and drives the original gun_marker_ctrl, gm_components, damage_panel
and indicators PYC. None of this proves Scaleform rendering on Windows.
"""
from pathlib import Path
import ctypes
import math
import os
import random
import shlex
import struct
import shutil
import subprocess
import sys
import tarfile
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(Path(__file__).resolve().parent))
F = ctypes.c_float
D = ctypes.c_double


def f32(value):
    return struct.unpack('<f', struct.pack('<f', value))[0]


def floats(n, values=None):
    return (F * n)(*(values or [0.0] * n))


def build_core(directory):
    if not shutil.which('c++'):
        raise unittest.SkipTest('C++ compiler unavailable: c++')
    library = Path(directory) / 'libstock_battle_gui.so'
    subprocess.run(['c++', '-std=c++17', '-O2', '-Wall', '-Wextra', '-Werror', '-pedantic',
                    '-ffp-contract=off', '-fno-fast-math', '-shared', '-fPIC',
                    '-I', str(ROOT / 'bigworld'), str(ROOT / 'tests/stock_battle_gui_core.cpp'),
                    '-o', str(library)], check=True)
    core = ctypes.CDLL(str(library))
    for name in ('gm_clip_scale', 'crosshair_clip_size', 'tank_angles', 'row_yaw', 'hit_rotation',
                 'spg_element_scale'):
        getattr(core, name).restype = F
    core.hit_label.restype = D
    core.gm_new.restype = core.spg_new.restype = ctypes.c_void_p
    for name in ('gm_free', 'gm_set', 'gm_get', 'gm_acquire', 'gm_release', 'gm_set_start',
                 'gm_update', 'gm_clip_scale', 'spg_free', 'spg_get', 'spg_set', 'spg_points',
                 'spg_relax', 'spg_reset', 'spg_conic', 'spg_update', 'spg_advance'):
        getattr(core, name).argtypes = [ctypes.c_void_p] + {
            'gm_set': [ctypes.POINTER(F)], 'gm_get': [ctypes.POINTER(F)],
            'gm_set_start': [F], 'gm_update': [F, F], 'gm_clip_scale': [F, F],
            'spg_get': [ctypes.POINTER(F)], 'spg_set': [ctypes.POINTER(F)],
            'spg_points': [ctypes.c_int, ctypes.POINTER(F)], 'spg_relax': [F], 'spg_conic': [F],
            'spg_update': [ctypes.POINTER(F)] * 3 + [F, ctypes.c_void_p, ctypes.POINTER(F)],
            'spg_advance': [F, F, F]}.get(name, [])
    core.crosshair_screen.argtypes = [ctypes.POINTER(F), F, F, ctypes.POINTER(D)]
    core.crosshair_clip_size.argtypes = [D, D]
    core.tank_constraint.argtypes = [ctypes.POINTER(ctypes.c_int), F, F, F]
    core.hit_rotation.argtypes = [F, F]
    core.hit_offsets.argtypes = [ctypes.POINTER(F), F, F]
    core.hit_label.argtypes = [F, ctypes.POINTER(F), D]
    core.hit_label_needs.argtypes = [D, D]
    core.trajectory_step.argtypes = [ctypes.POINTER(F)] * 3 + [F, ctypes.c_int, ctypes.POINTER(F),
                                                               ctypes.POINTER(F), ctypes.c_int,
                                                               ctypes.POINTER(F)]
    core.spg_element.argtypes = [ctypes.POINTER(F), ctypes.POINTER(F), F, ctypes.POINTER(F)]
    core.spg_element_scale.argtypes = [F, F]
    core.d3dx_rotation_x.argtypes = core.d3dx_rotation_z.argtypes = [ctypes.POINTER(F), F]
    return core


ENGINE_FILES = ('lib/pyscript/script.hpp', 'lib/pyscript/script.cpp', 'lib/pyscript/pyobject_plus.hpp')
FIXTURE_HEADERS = ('pch.hpp', 'pyscript/pyobject_plus.hpp', 'pyscript/script.hpp', 'pyscript/script_math.hpp',
                   'math/vector2.hpp', 'math/vector3.hpp', 'moo/render_context.hpp',
                   'scaleform/flash_gui_component.hpp')


def between(text, begin, end, include_end=True):
    start = text.rindex('\n', 0, text.index(begin)) + 1
    stop = text.index(end, start)
    return text[start:stop + (len(end) if include_end else 0)]


def engine_sources(test):
    archive = Path(os.environ.get('WOT_PUBLIC_ENGINE_ARCHIVE', '/tmp/wot-bigworld-engine-14.4.1.tar.gz'))
    if not archive.is_file():
        test.skipTest('Pinned public engine source archive required')
    sources = {}
    with tarfile.open(archive) as tar:
        for member in tar:
            for relative in ENGINE_FILES:
                if member.name.endswith('/programming/bigworld/' + relative):
                    sources[relative] = tar.extractfile(member).read().decode('utf-8')
            if len(sources) == len(ENGINE_FILES):
                break
    test.assertEqual(set(sources), set(ENGINE_FILES))
    return sources


def fixture_source(sources):
    script_hpp, script_cpp = sources['lib/pyscript/script.hpp'], sources['lib/pyscript/script.cpp']
    plus = sources['lib/pyscript/pyobject_plus.hpp']
    bodies = ''.join(between(script_cpp, 'int Script::setData( PyObject * pObject, ' + name, '\n}\n')
                     for name in ('bool & rBool,', 'float & rFloat,', 'double & rDouble,'))
    replacements = {
        '@SCRIPT_BODIES@': bodies + between(script_cpp, 'PyObject * Script::argCountError(', '\n}\n'),
        '@PYAUTO_MACROS@': between(script_hpp, '// Section: Macros: Module Links', '// Section: Macros: Enums',
                                   False) +
        between(script_hpp, '// Section: Macros: Recursion utilities', '// Section: PyModuleLink', False),
        '@PY_AUTO_METHOD_DECLARE@': between(plus, '/// This macro declares an auto-parsed standard python method',
                                            '/// This macro declares a python method that uses keywords.', False),
        '@PY_ATTRIBUTE_DECLARE_MACROS@': between(
            plus, '/// This macro declares a python attribute that is implemented elsewhere',
            '/**\n *\tPyObjectPlus-derived class static data instantiation macros', False),
    }
    text = (ROOT / 'tests/stock_battle_gui_fixture.hpp').read_text()
    for key, value in replacements.items():
        assert text.count(key) == 1, key
        text = text.replace(key, value)
    return text


def build_host(test, directory):
    if not shutil.which('c++'):
        raise unittest.SkipTest('C++ compiler unavailable: c++')
    python = Path(os.environ.get('WOT_PYTHON27', '/Users/peng/.pyenv/versions/2.7.18/bin/python2.7'))
    config = python.with_name('python2.7-config')
    if not config.exists():
        test.skipTest('Pinned CPython 2.7 development files unavailable')
    include = directory / 'include'
    (include / 'fixture').mkdir(parents=True)
    (include / 'fixture/stock_battle_gui_fixture.hpp').write_text(fixture_source(engine_sources(test)))
    for header in FIXTURE_HEADERS:
        (include / header).parent.mkdir(parents=True, exist_ok=True)
        (include / header).write_text('#pragma once\n#include "fixture/stock_battle_gui_fixture.hpp"\n')
    flags = [flag for flag in shlex.split(subprocess.check_output([str(config), '--cflags'], text=True))
             if flag != '-DNDEBUG']
    libs = shlex.split(subprocess.check_output([str(config), '--ldflags'], text=True))
    binary = directory / 'host'
    subprocess.run(['c++', '-std=c++17', '-UNDEBUG', '-DWOT_STOCK_BATTLE_GUI_HOST_MATH', *flags,
                    '-Wall', '-Werror', '-Wno-deprecated-register', '-Wno-register', '-Wno-writable-strings',
                    '-Wno-unused-function', '-Wno-missing-field-initializers', '-Wno-unused-private-field',
                    '-ffp-contract=off', '-I', str(include), '-I', str(ROOT / 'bigworld'),
                    str(ROOT / 'tests/stock_battle_gui_host.cpp'), *libs, '-o', str(binary)], check=True)
    return binary


COLLIDE = ctypes.CFUNCTYPE(F, ctypes.POINTER(F), ctypes.POINTER(F))


def ground(height):
    """Ccb_ distance to the plane y = height along start->end."""
    def distance(start, end):
        if (start[1] - height) * (end[1] - height) > 0.0 or start[1] == end[1]:
            return None
        t = (start[1] - height) / (start[1] - end[1])
        length = math.sqrt(sum((end[i] - start[i]) ** 2 for i in range(3)))
        return f32(t * length)

    def native(start, end):
        d = distance(start[:3], end[:3])
        return -1.0 if d is None else d
    return distance, COLLIDE(native)


def look_projection(eye, target, fov=1.0, aspect=16.0 / 9.0, near=0.5, far=1500.0):
    """A row-vector D3DX view*projection for a camera at eye looking at target."""
    def sub(a, b): return [a[i] - b[i] for i in range(3)]
    def norm(v):
        n = math.sqrt(sum(x * x for x in v))
        return [x / n for x in v]
    def cross(a, b): return [a[1]*b[2]-a[2]*b[1], a[2]*b[0]-a[0]*b[2], a[0]*b[1]-a[1]*b[0]]
    z = norm(sub(target, eye))
    x = norm(cross([0.0, 1.0, 0.0], z))
    y = cross(z, x)
    view = [[x[0], y[0], z[0], 0.0], [x[1], y[1], z[1], 0.0], [x[2], y[2], z[2], 0.0],
            [-sum(x[i]*eye[i] for i in range(3)), -sum(y[i]*eye[i] for i in range(3)),
             -sum(z[i]*eye[i] for i in range(3)), 1.0]]
    h = 1.0 / math.tan(fov / 2)
    proj = [[h / aspect, 0, 0, 0], [0, h, 0, 0], [0, 0, far / (far - near), 1],
            [0, 0, -near * far / (far - near), 0]]
    vp = [[sum(view[r][k] * proj[k][c] for k in range(4)) for c in range(4)] for r in range(4)]
    return [f32(v) for row in vp for v in row], [f32(v) for row in view for v in row]


class CoreTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.directory = tempfile.TemporaryDirectory(prefix='battle-gui-core-')
        cls.core = build_core(cls.directory.name)

    @classmethod
    def tearDownClass(cls):
        cls.directory.cleanup()

    def oracle(self):
        client = os.environ.get('WOT_0922_CLIENT')
        if not client:
            self.skipTest('WOT_0922_CLIENT is required for the original instruction oracle')
        try:
            import stock_battle_gui_oracle as oracle
        except ImportError:
            self.skipTest('pefile and Unicorn are required for the original instruction oracle')
        if not hasattr(CoreTest, '_original'):
            CoreTest._original = oracle.OriginalProviders(Path(client) / 'WorldOfTanks.exe',
                                                          self.core)
        return CoreTest._original

    def gm(self, fields):
        handle = self.core.gm_new()
        self.core.gm_set(handle, floats(7, fields))
        return handle

    def gm_fields(self, handle):
        out = floats(7)
        self.core.gm_get(handle, out)
        return list(out)

    def test_default_size_filter_matches_original(self):
        original = self.oracle()
        rng = random.Random(1513)
        cases = 0
        special = [0.0, -0.0, 0.0003, -0.0003, 0.0004, -0.0004, 0.0005, 1e-30, float('inf'),
                   float('nan'), -2.0, 0.1, 1.0]
        for _ in range(300):
            fields = [f32(rng.choice([5.0, rng.uniform(0, 50)])),
                      f32(rng.choice([300.0, rng.uniform(0, 2000)])),
                      f32(rng.uniform(-10, 400)), f32(rng.uniform(-10, 400)),
                      f32(rng.uniform(-10, 400)), f32(rng.uniform(0, 3)),
                      f32(rng.choice([0.0, rng.uniform(0, 30), float('nan')]))]
            size = f32(rng.uniform(-50, 600))
            relax = f32(rng.choice(special + [rng.uniform(-1, 1)]))
            dt = f32(rng.choice([0.0, 0.016, rng.uniform(0, 1), float('nan')]))
            clip = f32(rng.choice([512.0, 1.0, rng.uniform(1, 600), 0.0]))
            for operation in ('start', 'update', 'scale'):
                handle = self.gm(fields)
                if operation == 'start':
                    self.core.gm_set_start(handle, size)
                    expected = original.set_start(fields, size)
                    got = self.gm_fields(handle)
                elif operation == 'update':
                    self.core.gm_update(handle, size, relax)
                    expected = original.update_size(fields, size, relax)
                    got = self.gm_fields(handle)
                else:
                    value = self.core.gm_clip_scale(handle, dt, clip)
                    expected_value, expected = original.clip_scale(fields, dt, clip)
                    got = self.gm_fields(handle)
                    self.assertEqual(struct.pack('<f', value), struct.pack('<f', expected_value),
                                     (fields, dt, clip))
                self.assertEqual(struct.pack('<7f', *got), struct.pack('<7f', *expected),
                                 (operation, fields, size, relax))
                self.core.gm_free(handle)
                cases += 1
        print('Original 13eca80/13ecfb0/13ec360 matched the core: %d cases' % cases)

    def spg_state(self, count, fields, current, previous, output):
        handle = self.core.spg_new(count)
        self.core.spg_set(handle, floats(14, fields))
        return handle

    def test_spg_blend_matches_original(self):
        original = self.oracle()
        rng = random.Random(22)
        cases = 0
        for _ in range(150):
            count = rng.choice([1, 2, 5, 15])
            fields = [f32(rng.uniform(0, 0.5)), f32(rng.choice([10.0, rng.uniform(0, 40)])), 5.0,
                      0.1, f32(rng.uniform(1, 20)), f32(rng.choice([-1.0, rng.uniform(0, 30)])),
                      f32(rng.uniform(0, 30)), -1.0, 0.0, 0.0, 1.0, 0.0, 0.0, 0.0]
            pts = lambda: [tuple(f32(rng.uniform(-500, 500)) for _ in range(3)) for _ in range(count)]
            current, previous = pts(), pts()
            dt = f32(rng.choice([0.0, 0.016, rng.uniform(0, 0.3)]))
            limits = (f32(rng.uniform(0, 40)), f32(rng.uniform(40, 400)))
            handle = self.core.spg_new(count)
            self.core.spg_set(handle, floats(14, fields))
            # Load the point vectors through update-free state: copy via a snap.
            ready, size, out = original.spg_advance(count, fields, current, previous,
                                                    [(0.0, 0.0, 0.0)] * count, dt, limits)
            expected = self._core_advance(count, fields, current, previous, dt, limits)
            self.assertEqual(ready, expected[0])
            if ready:
                self.assertEqual(struct.pack('<f', size), struct.pack('<f', expected[1]))
                self.assertEqual(struct.pack('<%df' % (3 * count), *sum(out, ())),
                                 struct.pack('<%df' % (3 * count), *sum(expected[2], ())))
            self.core.spg_free(handle)
            cases += 1
        print('Original 13ecda0 matched the core blend: %d cases' % cases)

    def _core_advance(self, count, fields, current, previous, dt, limits):
        # Reproduce 13ecda0 through the production state's public update path:
        # a snap with a zero-length trajectory leaves current == previous, so
        # the test fixes the point vectors directly through spg_set_points.
        handle = self.core.spg_new(count)
        self.core.spg_set(handle, floats(14, fields))
        set_points = self.core.spg_set_points
        set_points.argtypes = [ctypes.c_void_p, ctypes.c_int, ctypes.POINTER(F)]
        set_points(handle, 0, floats(3 * count, list(sum(current, ()))))
        set_points(handle, 1, floats(3 * count, list(sum(previous, ()))))
        ready = self.core.spg_advance(handle, dt, limits[0], limits[1])
        state = floats(14)
        self.core.spg_get(handle, state)
        out = floats(3 * count)
        self.core.spg_points(handle, 2, out)
        self.core.spg_free(handle)
        points = [tuple(out[3 * i:3 * i + 3]) for i in range(count)]
        return ready, state[7], points

    def test_trajectory_subdivision_matches_original(self):
        original = self.oracle()
        rng = random.Random(9)
        cases = 0
        for _ in range(120):
            eye = [rng.uniform(-50, 50), rng.uniform(5, 200), rng.uniform(-50, 50)]
            target = [rng.uniform(-300, 300), 0.0, rng.uniform(-300, 300)]
            vp, _ = look_projection(eye, target)
            position = [f32(rng.uniform(-100, 100)), f32(rng.uniform(0, 30)), f32(rng.uniform(-100, 100))]
            speed = rng.uniform(100, 900)
            pitch = rng.uniform(-0.2, 1.3)
            yaw = rng.uniform(-math.pi, math.pi)
            velocity = [f32(speed * math.cos(pitch) * math.sin(yaw)), f32(speed * math.sin(pitch)),
                        f32(speed * math.cos(pitch) * math.cos(yaw))]
            gravity = [0.0, f32(-rng.choice([9.81, 50.0, 200.0, 1000.0])), 0.0]
            dt = f32(rng.choice([0.1, 0.05, 0.3]))
            refined = rng.random() < 0.6
            points, state = original.trajectory_step(position, velocity, gravity, dt, refined, vp)
            out = floats(3 * 4096)
            state_out = floats(6)
            n = self.core.trajectory_step(floats(3, position), floats(3, velocity), floats(3, gravity),
                                          dt, int(refined), floats(16, vp), out, 4096, state_out)
            got = [tuple(out[3 * i:3 * i + 3]) for i in range(n)]
            self.assertEqual(struct.pack('<%df' % (3 * len(points)), *sum(points, ())),
                             struct.pack('<%df' % (3 * len(got)), *sum(got, ())), (refined, dt))
            self.assertEqual(struct.pack('<6f', *state), struct.pack('<6f', *state_out))
            cases += 1
        print('Original 13ea7c0 matched the core subdivision: %d cases' % cases)

    def test_trajectory_and_cone_match_original(self):
        original = self.oracle()
        rng = random.Random(77)
        cases = hits = 0
        for _ in range(16):
            count = rng.choice([3, 5, 9, 15])
            height = f32(rng.uniform(-5, 5))
            distance, native = ground(height)
            fields = [0.0, 10.0, f32(rng.choice([5.0, 2.0])), f32(rng.choice([0.1, 0.25])), 10.0,
                      -1.0, -1.0, -1.0, 0.0, 0.0, 1.0, 0.0, 0.0, 0.0]
            eye = [rng.uniform(-50, 50), rng.uniform(50, 300), rng.uniform(-50, 50)]
            vp, _ = look_projection(eye, [0.0, 0.0, 300.0])
            position = [0.0, f32(height + rng.uniform(1, 4)), 0.0]
            speed = rng.uniform(150, 400)
            pitch = rng.uniform(-0.2, 0.35)
            velocity = [0.0, f32(speed * math.sin(pitch)), f32(speed * math.cos(pitch))]
            gravity = [0.0, f32(-rng.uniform(40.0, 200.0)), 0.0]
            angle = f32(rng.uniform(0.0, 0.05))

            def collider(start, end):
                d = distance(start, end)
                if d is None:
                    return None
                direction = [end[i] - start[i] for i in range(3)]
                n = floats(3)
                self.core.d3dx_normalize(n, floats(3, direction))
                return [f32(start[i] + f32(n[i] * d)) for i in range(3)]
            expected = original.conic(count, fields, position, velocity, gravity, angle, vp, collider)
            handle = self.core.spg_new(count)
            self.core.spg_set(handle, floats(14, fields))
            self.core.spg_conic(handle, angle)
            self.core.spg_update(handle, floats(3, position), floats(3, velocity), floats(3, gravity),
                                 1.0, ctypes.cast(native, ctypes.c_void_p), floats(16, vp))
            out = floats(3 * count)
            self.core.spg_points(handle, 0, out)
            got = [tuple(out[3 * i:3 * i + 3]) for i in range(count)]
            self.core.spg_free(handle)
            self.assertEqual(struct.pack('<%df' % (3 * count), *sum(expected, ())),
                             struct.pack('<%df' % (3 * count), *sum(got, ())))
            hits += sum(p[0] != -100000.0 for p in got)
            cases += 1
        self.assertGreater(hits, cases)
        print('Original 13ec050/13ebdc0 matched the core cone: %d cones, %d hits, %d scene queries'
              % (cases, hits, original.collisions))


# #1513 type objects (01ce37e8 ...): name -> (type address, base write address).
TYPES = {
    'WGAbstractGunMarkerDataProvider': (0x1ce4240, 0x60227c),
    'WGGunMarkerDataProvider': (0x1ce4308, 0x602412),
    'WGSPGGunMarkerDataProvider': (0x1ce43d0, 0x60250d),
    'WGCrosshairGUIComponent': (0x1ce37e8, 0x600c62),
    'WGSPGCrosshairGUIComponent': (0x1ce3e70, 0x601b32),
    'WGHitIndicatorGUIComponent': (0x1ce3910, 0x600ea2),
    'WGTankIndicatorGUIComponent': (0x1ce3d70, 0x6018f7),
}
# Members outside the normal RANDOM/TRAINING route: artillery-strike equipment
# and the census-only hit-indicator methods without a retained PYC caller.
EXCLUDED = {'setupFlatRadialDispersion', 'getPointsInside', 'setGlobalYaw', 'invoke'}


def original_type_tables(executable):
    import capstone
    import pefile
    pe = pefile.PE(str(executable), fast_load=True)
    base = pe.OPTIONAL_HEADER.ImageBase
    image = pe.get_memory_mapped_image()
    word = lambda a: struct.unpack('<I', image[a - base:a - base + 4])[0]

    def cstr(a):
        o = a - base
        if not 0 <= o < len(image):
            return None
        e = image.find(b'\0', o, o + 64)
        text = image[o:e]
        return text.decode() if e > o and all(48 <= c < 123 or c == 95 for c in text) else None

    md = capstone.Cs(capstone.CS_ARCH_X86, capstone.CS_MODE_32)
    tables = {}
    for name, (address, base_write) in TYPES.items():
        methods, m = [], word(address + 0x74)
        while word(m):
            methods.append(cstr(word(m)))
            m += 16
        def names(start, end):
            found, calls = [], []
            for ins in md.disasm(image[start - base:end - base], start):
                operand = ins.op_str.rsplit(',', 1)[-1].strip()
                if ins.mnemonic == 'mov' and ins.op_str.startswith('dword ptr [ebp -') and operand.startswith('0x'):
                    text = cstr(int(operand, 16))
                    if text and text not in ('__members__', '__methods__'):
                        found.append(text)
                elif ins.mnemonic == 'call' and operand.startswith('0x'):
                    calls.append(int(operand, 16))
                if ins.mnemonic == 'ret':
                    break
            return found, calls

        start = image.rfind(b'\xcc\xcc', 0, base_write - base) + 2 + base
        getsets, calls = names(start, base_write)
        if not getsets and calls:
            # The SPG type builds its getset table in a helper (13ec600).
            getsets, _ = names(calls[-1], calls[-1] + 0x400)
        tables[name] = (methods, getsets, word(base_write + 6))
    return tables


class BindingTest(unittest.TestCase):
    def setUp(self):
        client = os.environ.get('WOT_0922_CLIENT')
        if not client:
            self.skipTest('WOT_0922_CLIENT is required for the original PYC and type tables')
        self.client = Path(client)

    def run_host(self, script, *args):
        with tempfile.TemporaryDirectory(prefix='battle-gui-host-') as directory:
            binary = build_host(self, Path(directory))
            result = subprocess.run([str(binary), str(script), *args], capture_output=True, text=True)
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        return result.stdout

    def test_original_pyc_consumers_over_the_bindings(self):
        output = self.run_host(ROOT / 'tests/stock_battle_crosshair_consumer.py',
                               str(self.client / 'res/packages/scripts.pkg'))
        print(output.strip())

    def test_type_tables_match_the_original(self):
        try:
            original = original_type_tables(self.client / 'WorldOfTanks.exe')
        except ImportError:
            self.skipTest('pefile and capstone are required for the original type tables')
        bases = {0x1a1f858: 'FlashGUIComponent', 0x1a0c600: 'PyObjectPlus',
                 0x1ce4240: 'WGAbstractGunMarkerDataProvider'}
        script = Path(tempfile.mkdtemp(prefix='battle-gui-types-')) / 'types.py'
        script.write_text('import _battle_gui_host as h, json\nh.initScripts()\nprint(json.dumps('
                          '{n: h.typeInfo(n) for n in %r}))\n' % sorted(TYPES))
        import json
        ported = json.loads(self.run_host(script))
        for name, (methods, getsets, base) in original.items():
            self.assertEqual(ported[name]['methods'], [m for m in methods if m not in EXCLUDED], name)
            self.assertEqual(ported[name]['getsets'], getsets, name)
            self.assertEqual(ported[name]['base'], bases[base], name)
        print('Ported type tables match #1513 for %d types (excluded: %s)' %
              (len(original), ', '.join(sorted(EXCLUDED))))


if __name__ == '__main__':
    unittest.main()
