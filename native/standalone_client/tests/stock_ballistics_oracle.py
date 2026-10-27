"""Execute exact #1513 PyBallisticsSimulator instructions in Unicorn and compare.

Unchanged original code: chord subdivision 008a9290 with its stack push
0097df80 and vector push_back 00a42080, the plane/segment test 011bd260, the
tracer matrix 008ac170, the per-frame update 008ab9b0 and hideProjectile
008ab800 (terminal plane). operator new/delete, memset and the three D3DX
imports (Vec3Normalize, MatrixMultiply, MatrixScaling) are hooked with the
same single-precision formulas the production core uses; the scene query
f642b0 and water query f645e0 must not be reached by these cases.
"""
import ctypes
import math
import random
import struct

import pefile
import unicorn
from unicorn.x86_const import (UC_X86_REG_EAX, UC_X86_REG_ECX, UC_X86_REG_EIP, UC_X86_REG_ESP,
                               UC_X86_REG_FS, UC_X86_REG_GDTR, UC_X86_REG_SS)

SUBDIVIDE, INTERSECT, MATRIX, UPDATE, HIDE = 0x8a9290, 0x11bd260, 0x8ac170, 0x8ab9b0, 0x8ab800
NEW, DELETE, DELETE_SIZED, MEMSET = 0x13d7fe9, 0x13d8019, 0x13d801e, 0x13dac1d
NORMALISE, MULTIPLY, SCALING = 0x11c3bbe, 0x11c3bc4, 0x11c3bca
SCENE, WATER = 0xf642b0, 0xf645e0
PARAMETERS, SPACE_ID, AUTOSCALE_OFF, CAMERA_GLOBAL = 0x1a17264, 0x1d350ac, 0x1d350b0, 0x1d20788
STACK, DATA, GDT, SENTINEL, HEAP = 0x6000000, 0x6200000, 0x6300000, 0x6400000, 0x7000000
RECORD = 0xc4
CAMERA = DATA + 0x80300


def f32(value):
    return struct.unpack('<f', struct.pack('<f', value))[0]


def bits(value):
    return struct.unpack('<I', struct.pack('<f', value))[0]


class Original(object):
    def __init__(self, executable):
        pe = pefile.PE(str(executable))
        assert pe.FILE_HEADER.Machine == 0x14c
        image, base = pe.get_memory_mapped_image(), pe.OPTIONAL_HEADER.ImageBase
        assert len(image) > AUTOSCALE_OFF - base
        self.u = u = unicorn.Uc(unicorn.UC_ARCH_X86, unicorn.UC_MODE_32)
        u.mem_map(base, (len(image) + 0xfff) & ~0xfff)
        u.mem_write(base, image)
        for region in (STACK, DATA, GDT, SENTINEL):
            u.mem_map(region, 0x100000)
        u.mem_map(HEAP, 0x1000000)
        self.heap = HEAP
        teb = (0x1000 | (GDT + 0x1000 & 0xffffff) << 16 | 0x92 << 40 | 0x4 << 52 |
               ((GDT + 0x1000) >> 24 & 0xff) << 56)
        flat = 0xffff | 0x92 << 40 | 0xf << 48 | 0xc << 52
        u.mem_write(GDT, struct.pack('<QQQ', 0, teb, flat))
        u.reg_write(UC_X86_REG_GDTR, (0, GDT, 23, 0))
        u.reg_write(UC_X86_REG_FS, 8)
        u.reg_write(UC_X86_REG_SS, 16)
        self.hooks = {SENTINEL: u.emu_stop, NEW: self._new, DELETE: lambda: self.ret(0, 0),
                      DELETE_SIZED: lambda: self.ret(0, 0), MEMSET: self._memset,
                      NORMALISE: self._normalise, MULTIPLY: self._multiply, SCALING: self._scaling,
                      SCENE: self._unexpected, WATER: self._unexpected}
        for address in self.hooks:
            u.hook_add(unicorn.UC_HOOK_CODE, lambda uc, a, s, d: self.hooks[a](), begin=address, end=address)
        # Camera object chain read by 008ac31f: [[01d20788]+0xb0] -> object, +0x84 position.
        self.put(CAMERA_GLOBAL, '<I', DATA + 0x80000)
        self.put(DATA + 0x800b0, '<I', DATA + 0x80200)
        self.put(DATA + 0x80200, '<I', CAMERA)
        self.put(CAMERA, '<I', 1000)

    def arg(self, index):
        return struct.unpack('<I', self.u.mem_read(self.u.reg_read(UC_X86_REG_ESP) + 4 * index, 4))[0]

    def farg(self, index):
        return struct.unpack('<f', self.u.mem_read(self.u.reg_read(UC_X86_REG_ESP) + 4 * index, 4))[0]

    def ret(self, value, pop):
        esp = self.u.reg_read(UC_X86_REG_ESP)
        if value is not None:
            self.u.reg_write(UC_X86_REG_EAX, value)
        self.u.reg_write(UC_X86_REG_EIP, self.arg(0))
        self.u.reg_write(UC_X86_REG_ESP, esp + 4 + pop)

    def floats(self, address, count):
        return list(struct.unpack('<%df' % count, self.u.mem_read(address, 4 * count)))

    def put(self, address, fmt, *values):
        self.u.mem_write(address, struct.pack(fmt, *values))

    def call(self, address, ecx, args):
        esp = STACK + 0xf0000
        self.put(esp, '<%dI' % (len(args) + 1), SENTINEL, *args)
        self.u.reg_write(UC_X86_REG_ESP, esp)
        self.u.reg_write(UC_X86_REG_ECX, ecx)
        self.u.emu_start(address, 0, count=5000000)
        return self.u.reg_read(UC_X86_REG_EAX)

    def _unexpected(self):
        raise AssertionError('scene query reached at 0x%x' % self.u.reg_read(UC_X86_REG_EIP))

    def _new(self):
        size = self.arg(1)
        address = self.heap
        self.heap += (size + 0x3f) & ~0x1f
        self.ret(address, 0)

    def _memset(self):
        self.u.mem_write(self.arg(1), bytes([self.arg(2) & 0xff]) * self.arg(3))
        self.ret(self.arg(1), 0)

    def _normalise(self):
        x, y, z = self.floats(self.arg(2), 3)
        length = f32(math.sqrt(f32(f32(f32(x * x) + f32(y * y)) + f32(z * z))))
        out = [0.0, 0.0, 0.0] if length == 0.0 else [f32(x / length), f32(y / length), f32(z / length)]
        self.put(self.arg(1), '<3f', *out)
        self.ret(self.arg(1), 8)

    def _multiply(self):
        a, b = self.floats(self.arg(2), 16), self.floats(self.arg(3), 16)
        out = []
        for row in range(4):
            for column in range(4):
                total = f32(f32(a[4 * row] * b[column]) + f32(a[4 * row + 1] * b[4 + column]))
                total = f32(total + f32(a[4 * row + 2] * b[8 + column]))
                out.append(f32(total + f32(a[4 * row + 3] * b[12 + column])))
        self.put(self.arg(1), '<16f', *out)
        self.ret(self.arg(1), 12)

    def _scaling(self):
        sx, sy, sz = self.farg(2), self.farg(3), self.farg(4)
        self.put(self.arg(1), '<16f', sx, 0, 0, 0, 0, sy, 0, 0, 0, 0, sz, 0, 0, 0, 0, 1)
        self.ret(self.arg(1), 16)

    def parameters(self, values, autoscale_off, camera):
        self.put(PARAMETERS, '<4f', *values)
        self.put(AUTOSCALE_OFF, '<B', 1 if autoscale_off else 0)
        self.put(CAMERA + 0x84, '<3f', *camera)

    def subdivide(self, position, velocity, gravity, dt, tolerance):
        out, vector = DATA + 0x100, DATA + 0x200
        self.put(DATA + 0x300, '<9f', *(position + velocity + gravity))
        self.put(vector, '<3I', 0, 0, 0)
        self.call(SUBDIVIDE, 0, [out, vector, DATA + 0x300, DATA + 0x30c, DATA + 0x318,
                                 bits(dt), bits(tolerance)])
        begin, end = struct.unpack('<2I', self.u.mem_read(vector, 8))
        return self.floats(out, 6), self.floats(begin, (end - begin) // 4) if end > begin else []

    def intersect(self, plane, a, b):
        self.put(DATA + 0x400, '<4f', *plane)
        self.put(DATA + 0x410, '<6f', *(a + b))
        self.put(DATA + 0x430, '<3f', 0, 0, 0)
        hit = self.call(INTERSECT, DATA + 0x400, [DATA + 0x430, DATA + 0x410, DATA + 0x41c]) & 0xff
        return hit, self.floats(DATA + 0x430, 3)

    def matrix(self, record, position):
        self.u.mem_write(DATA + 0x1000, record)
        self.put(DATA + 0x1100, '<3f', *position)
        self.call(MATRIX, DATA + 0x1000, [DATA + 0x1100])
        return bytes(self.u.mem_read(DATA + 0x1000, RECORD))

    def update(self, record, points, now):
        array = DATA + 0x2000
        for index, point in enumerate(points):
            self.u.mem_write(array + 0x20 * index, point)
        end = array + 0x20 * len(points)
        record = record[:8] + struct.pack('<3I', array, end, end) + record[0x14:]
        self.u.mem_write(DATA + 0x1000, record)
        terminal = self.call(UPDATE, DATA + 0x1000, [bits(now)]) & 0xff
        return terminal, bytes(self.u.mem_read(DATA + 0x1000, RECORD))

    def hide(self, record, end):
        # Simulator: vector<record> at +0, handle index map at +0xc8.
        simulator, records = DATA + 0x3000, DATA + 0x3400
        self.u.mem_write(records, record[:8] + struct.pack('<3I', 0, 0, 0) + record[0x14:])
        self.put(simulator, '<3I', records, records + RECORD, records + RECORD)
        self.put(DATA + 0x3800, '<3f', *end)
        shot = struct.unpack('<i', record[:4])[0]
        self.call(HIDE, simulator, [shot & 0xffffffff, DATA + 0x3800])
        after = bytes(self.u.mem_read(records, RECORD))
        return struct.unpack('<i', after[:4])[0], list(struct.unpack('<4f', after[0x14:0x24]))


class Production(object):
    def __init__(self, library):
        self.lib = lib = ctypes.CDLL(str(library))
        F = ctypes.POINTER(ctypes.c_float)
        lib.sb_subdivide.argtypes = [F, F, F, ctypes.c_float, ctypes.c_float, F, F, ctypes.c_int]
        lib.sb_intersect.argtypes = [F, F, F, F]
        lib.sb_set_matrix.argtypes = [ctypes.c_void_p, F, F, ctypes.c_int, F]
        lib.sb_update.argtypes = [ctypes.c_void_p, ctypes.c_void_p, ctypes.c_int, ctypes.c_float, F,
                                  ctypes.c_int, F]
        lib.sb_terminal_plane.argtypes = [ctypes.c_void_p, F, F]

    @staticmethod
    def floats(values):
        return (ctypes.c_float * len(values))(*values)

    def subdivide(self, position, velocity, gravity, dt, tolerance):
        end, chords = (ctypes.c_float * 6)(), (ctypes.c_float * 30000)()
        count = self.lib.sb_subdivide(self.floats(position), self.floats(velocity), self.floats(gravity),
                                      dt, tolerance, end, chords, 10000)
        return list(end), list(chords[:3 * count])

    def intersect(self, plane, a, b):
        out = (ctypes.c_float * 3)()
        hit = self.lib.sb_intersect(self.floats(plane), self.floats(a), self.floats(b), out)
        return hit, list(out) if hit else [0.0, 0.0, 0.0]

    def matrix(self, record, position, values, autoscale_off, camera):
        buffer = ctypes.create_string_buffer(record, RECORD)
        self.lib.sb_set_matrix(buffer, self.floats(position), self.floats(values), int(autoscale_off),
                               self.floats(camera))
        return buffer.raw

    def update(self, record, points, now, values, autoscale_off, camera):
        buffer = ctypes.create_string_buffer(record, RECORD)
        array = ctypes.create_string_buffer(b''.join(points), max(1, 0x20 * len(points)))
        terminal = self.lib.sb_update(buffer, array, len(points), now, self.floats(values),
                                      int(autoscale_off), self.floats(camera))
        return terminal, buffer.raw

    def plane(self, record, end):
        out = (ctypes.c_float * 4)()
        self.lib.sb_terminal_plane(ctypes.create_string_buffer(record, RECORD), self.floats(end), out)
        return list(out)


def record(shot, ref_start, ref_velocity, velocity0, start, velocity, gravity, start_time,
           hold=-1.0, death=-1.0, visible=0, autoscale=1, plane=(0.0, 0.0, 0.0, 0.0), position=(0, 0, 0)):
    matrix = [1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, position[0], position[1], position[2], 1]
    return struct.pack('<iI3I4f16f18f5f2B2x', shot, 0x01000005, 0, 0, 0, *(list(plane) + matrix +
                       list(ref_start) + list(ref_velocity) + list(velocity0) + list(start) +
                       [0.0, 0.0, 0.0] + list(velocity) + [gravity, start_time, 500.0, hold, death] +
                       [visible, autoscale]))


def point(position, time, final, item=0, material=0):
    return struct.pack('<4fQIB3x', *(list(position) + [time, item, material, final]))


def mask(raw):
    # Ignore the vector pointers; the comparison covers every other byte.
    return raw[:8] + b'\0' * 12 + raw[0x14:]


def run(executable, library, seed=1513):
    original, production = Original(executable), Production(library)
    rng = random.Random(seed)
    counts = {'subdivide': 0, 'intersect': 0, 'matrix': 0, 'update': 0, 'plane': 0}
    mismatches = []

    def vec(scale):
        return [f32(rng.uniform(-scale, scale)) for _ in range(3)]

    for case in range(60):
        position, velocity = vec(500.0), vec(900.0)
        velocity[1] = f32(abs(velocity[1]) * (1 if case % 3 else -1))
        gravity = [0.0, f32(-rng.choice([9.81, 25.0, 0.5])), 0.0]
        dt = f32(rng.choice([0.1, 0.03, 0.5, 2.0]))
        a = original.subdivide(position, velocity, gravity, dt, f32(0.03))
        b = production.subdivide(position, velocity, gravity, dt, f32(0.03))
        counts['subdivide'] += 1
        if [bits(x) for x in a[0] + a[1]] != [bits(x) for x in b[0] + b[1]]:
            mismatches.append(('subdivide', case, a, b))

    for case in range(200):
        normal = vec(1.0)
        length = math.sqrt(sum(x * x for x in normal)) or 1.0
        plane = [f32(x / length) for x in normal] + [f32(rng.uniform(-50, 50))]
        a, b = vec(100.0), vec(100.0)
        if case % 7 == 0:
            b = list(a)
        counts['intersect'] += 1
        if original.intersect(plane, a, b) != production.intersect(plane, a, b):
            mismatches.append(('intersect', case, plane, a, b))

    values = [f32(0.05), f32(2.0), f32(180.0), f32(0.1)]
    for case in range(120):
        velocity = vec(800.0)
        if case % 10 == 0:
            velocity = [f32(0.01), f32(-500.0), f32(0.02)]
        if case % 17 == 0:
            velocity = [0.0, 0.0, 0.0]
        autoscale, off = case % 2, case % 5 == 0
        camera = vec(600.0)
        raw = record(3, vec(10), vec(10), vec(10), vec(10), velocity, 9.81, 1.0, autoscale=autoscale)
        position = vec(700.0)
        original.parameters(values, off, camera)
        counts['matrix'] += 1
        if original.matrix(raw, position) != production.matrix(raw, position, values, off, camera):
            mismatches.append(('matrix', case))

    for case in range(300):
        ref_start, ref_velocity = vec(200.0), vec(900.0)
        start = [f32(ref_start[i] + rng.uniform(-3, 3)) for i in range(3)]
        velocity0 = [f32(ref_velocity[i] + rng.uniform(-5, 5)) for i in range(3)]
        start_time = f32(rng.uniform(0, 100))
        now = f32(start_time + rng.choice([-0.1, 0.0, 0.02, 0.07, 0.4, 1.3, 3.0]))
        hold = f32(0.05) if case % 4 == 0 else -1.0
        plane = (0.0, 0.0, 0.0, 0.0)
        position = vec(300.0)
        if case % 3 == 0:
            n = [f32(x) for x in ref_velocity]
            length = math.sqrt(sum(x * x for x in n)) or 1.0
            n = [f32(x / length) for x in n]
            end = [f32(start[i] + velocity0[i] * rng.uniform(0.01, 2.0)) for i in range(3)]
            plane = tuple(n + [f32(f32(f32(end[1] * n[1]) + f32(end[0] * n[0])) + f32(end[2] * n[2]))])
            position = [f32(start[i] + velocity0[i] * 0.005) for i in range(3)]
        points = []
        if case % 5 == 1:
            hit = [f32(start[i] + velocity0[i] * 0.3) for i in range(3)]
            points = [point(hit, f32(0.3), 1)]
        if case % 5 == 2:
            points = [point(vec(100.0), f32(50.0), 1)]
        raw = record(9, ref_start, ref_velocity, velocity0, start, velocity0, f32(rng.choice([9.81, 0.0])),
                     start_time, hold=hold, visible=case % 2, autoscale=case % 3 == 1, plane=plane,
                     position=position)
        camera = vec(600.0)
        off = case % 7 == 0
        original.parameters(values, off, camera)
        a = original.update(raw, points, now)
        b = production.update(raw, points, now, values, off, camera)
        counts['update'] += 1
        if a[0] != b[0] or mask(a[1]) != mask(b[1]):
            mismatches.append(('update', case))

    for case in range(100):
        ref_velocity = vec(900.0)
        if case % 9 == 0:
            ref_velocity[0] = 0.0
        raw = record(rng.randint(1, 10000), vec(200.0), ref_velocity, vec(10), vec(10), vec(10),
                     f32(9.81), 0.0)
        end = vec(800.0)
        shot, plane = original.hide(raw, end)
        expected = production.plane(raw, end)
        counts['plane'] += 1
        if shot != -struct.unpack('<i', raw[:4])[0] or [bits(x) for x in plane] != [bits(x) for x in expected]:
            mismatches.append(('plane', case, plane, expected))
    return counts, mismatches
