"""Execute exact #1513 WaterSensor::update (00794cb0) instructions in Unicorn.

The original body, its LOD gate (00781f70), camera-space getter (00ac2f10) and
cookie check run unchanged. Call boundaries are hooked: the Waters scan
(00f645e0), D3DXVec3Transform, collideSegment (00f642b0), std::function
targets and Interlocked imports. D3DX itself remains a Windows boundary.
"""
import math
import random
import struct

import pefile
import unicorn
from unicorn.x86_const import (UC_X86_REG_EAX, UC_X86_REG_ECX, UC_X86_REG_EIP, UC_X86_REG_ESP,
                               UC_X86_REG_FPCW, UC_X86_REG_FS, UC_X86_REG_GDTR, UC_X86_REG_MXCSR,
                               UC_X86_REG_SS)

UPDATE, LOD_GATE, WATERS, TRANSFORM, COLLIDE = 0x794cb0, 0x781f70, 0xf645e0, 0x11c3bdc, 0xf642b0
BAD_FUNCTION_CALL, CAMERA_SPACE = 0x13d7914, 0x1d59748
STACK, HEAP, STUBS, GDT = 0x6000000, 0x6100000, 0x6200000, 0x6300000
COMPONENT, MATRIX, LOD_STATE, VALUE, SPACE = HEAP, HEAP + 0x400, HEAP + 0x480, HEAP + 0x4c0, HEAP + 0x500
FUNCTIONS = {name: HEAP + 0x600 + 0x40 * index
             for index, name in enumerate(('lod', 'plane', 'speed', 'splash', 'switch'))}
STUB = {name: STUBS + 0x10 * index for index, name in enumerate(FUNCTIONS)}
LOAD_VALUE, SENTINEL = STUBS + 0x100, STUBS + 0x200


def f32(value):
    return struct.unpack('<f', struct.pack('<f', value))[0]


def bits(value):
    return struct.unpack('<I', struct.pack('<f', value))[0]


def transform(point, matrix):
    """Host reference identical to the C++ fixture: left-to-right single precision."""
    out = []
    for column in range(4):
        value = f32(point[0] * matrix[column])
        value = f32(value + f32(point[1] * matrix[4 + column]))
        value = f32(value + f32(point[2] * matrix[8 + column]))
        out.append(f32(value + matrix[12 + column]))
    return out


class OriginalWaterSensor(object):
    def __init__(self, executable):
        pe = pefile.PE(str(executable))
        assert pe.FILE_HEADER.Machine == 0x14c
        image, base = pe.get_memory_mapped_image(), pe.OPTIONAL_HEADER.ImageBase
        self.u = u = unicorn.Uc(unicorn.UC_ARCH_X86, unicorn.UC_MODE_32)
        u.mem_map(base, (len(image) + 0xfff) & ~0xfff)
        u.mem_write(base, image)
        for region in (STACK, HEAP, STUBS, GDT):
            u.mem_map(region, 0x100000)
        # Flat 32-bit stack segment and a TEB-like FS for the SEH prologues.
        teb = (0x1000 & 0xffff | (GDT + 0x1000 & 0xffffff) << 16 | 0x92 << 40 | 0x4 << 52 |
               ((GDT + 0x1000) >> 24 & 0xff) << 56)
        flat = 0xffff | 0x92 << 40 | 0xf << 48 | 0xc << 52
        u.mem_write(GDT, struct.pack('<QQQ', 0, teb, flat))
        u.reg_write(UC_X86_REG_GDTR, (0, GDT, 23, 0))
        u.reg_write(UC_X86_REG_FS, 1 << 3)
        u.reg_write(UC_X86_REG_SS, 2 << 3)
        u.mem_write(LOAD_VALUE, b'\xd9\x05' + struct.pack('<I', VALUE) + b'\xc3')
        for name, impl in FUNCTIONS.items():
            vtable = impl + 0x20
            u.mem_write(vtable, struct.pack('<4I', 0, 0, STUB[name], 0))
            u.mem_write(impl, struct.pack('<I', vtable))
        increment, decrement = (struct.unpack('<I', u.mem_read(a, 4))[0] for a in (0x149139c, 0x14913a0))
        self.handlers = {WATERS: self._waters, TRANSFORM: self._transform, COLLIDE: self._collide,
                         BAD_FUNCTION_CALL: self._bad, SENTINEL: self._done,
                         increment: lambda: self._interlocked(1), decrement: lambda: self._interlocked(-1),
                         STUB['lod']: lambda: self._return(LOD_STATE),
                         STUB['plane']: self._plane, STUB['speed']: self._speed,
                         STUB['splash']: self._splash, STUB['switch']: self._switch}
        for address in self.handlers:
            u.hook_add(unicorn.UC_HOOK_CODE, self._hook, begin=address, end=address)

    def _hook(self, u, address, size, data):
        self.handlers[address]()

    def _arg(self, index):
        return struct.unpack('<I', self.u.mem_read(self.u.reg_read(UC_X86_REG_ESP) + 4 * index, 4))[0]

    def _floats(self, address, count):
        return list(struct.unpack('<%df' % count, self.u.mem_read(address, 4 * count)))

    def _return(self, value, pop=0):
        u, esp = self.u, self.u.reg_read(UC_X86_REG_ESP)
        if value is not None:
            u.reg_write(UC_X86_REG_EAX, value)
        u.reg_write(UC_X86_REG_EIP, self._arg(0))
        u.reg_write(UC_X86_REG_ESP, esp + 4 + pop)

    def _load(self, value):
        self.u.mem_write(VALUE, struct.pack('<f', value))
        self.u.reg_write(UC_X86_REG_EIP, LOAD_VALUE)

    def _waters(self):
        start, end, flag = self._floats(self._arg(1), 3), self._floats(self._arg(2), 3), self._arg(3) & 0xff
        self.trace['waterCalls'].append([start, end, flag])
        if flag:
            value = self.step['corners'][len([c for c in self.trace['waterCalls'] if c[2]]) - 1]
        else:
            value = self.step['height']
        self._load(value)

    def _transform(self):
        out, point, matrix = self._arg(1), self._floats(self._arg(2), 3), self._floats(self._arg(3), 16)
        self.trace['transforms'].append(point)
        self.u.mem_write(out, struct.pack('<4f', *transform(point, matrix)))
        self._return(out, 12)

    def _collide(self):
        out, space = self._arg(1), self._arg(2)
        start, end, flags = self._floats(self._arg(3), 3), self._floats(self._arg(4), 3), self._arg(5) & 0xffff
        camera = self.step['camera']
        self.trace['segments'].append([space, start, end, flags])
        found = bool(camera[1]) and any(not (triangle & flags) for triangle in camera[2])
        self.u.mem_write(out, struct.pack('<I', int(found)) + b'\0' * 0x34)
        self._return(out)

    def _interlocked(self, delta):
        address = self._arg(1)
        value = struct.unpack('<i', self.u.mem_read(address, 4))[0] + delta
        self.u.mem_write(address, struct.pack('<i', value))
        self._return(value & 0xffffffff, 4)

    def _plane(self):
        self.trace['planeReads'] += 1
        self._return(MATRIX)

    def _speed(self):
        self.trace['speedReads'] += 1
        self._load(self.step['speed'])

    def _splash(self):
        point = self._floats(self._arg(1), 3)
        heavy = bool(self.u.mem_read(self._arg(1) + 12, 1)[0])
        self.trace['events'].append(['splash', point, heavy])
        self._return(None, 4)

    def _switch(self):
        self.trace['events'].append(['switch', bool(self.u.mem_read(self._arg(1), 1)[0] & 1)])
        self._return(None, 4)

    def _bad(self):
        self.trace['bad_function_call'] = True
        self.u.emu_stop()

    def _done(self):
        self.u.emu_stop()

    def load(self, config, initial):
        state = initial or [0, 0, 0, 0]
        u = self.u
        u.mem_write(COMPONENT, b'\0' * 0x110)
        u.mem_write(COMPONENT + 0x48, struct.pack('<8f', *config))
        u.mem_write(COMPONENT + 0xb8, struct.pack('<I', state[3]))
        u.mem_write(COMPONENT + 0xbc, bytes(state[:3]))

    def run(self, step):
        u, self.step = self.u, step
        self.trace = {'waterCalls': [], 'transforms': [], 'segments': [], 'events': [],
                      'planeReads': 0, 'speedReads': 0, 'bad_function_call': False}
        lod = step['lod']
        u.mem_write(COMPONENT + 0x18, struct.pack('<fI', *(lod[2:] if lod else (3.4028234663852886e38, 0xffffffff))))
        u.mem_write(LOD_STATE, struct.pack('<If', *(lod[:2] if lod else (0, 0.))))
        for name, offset, present in (('lod', 0x44, lod is not None), ('plane', 0x8c, step['plane']),
                                      ('speed', 0xb4, step['speed'] is not None),
                                      ('splash', 0xe4, step['splash']), ('switch', 0x10c, step['switch'])):
            u.mem_write(COMPONENT + offset, struct.pack('<I', FUNCTIONS[name] if present else 0))
        u.mem_write(MATRIX, struct.pack('<16f', *step['matrix']))
        camera = step['camera']
        u.mem_write(SPACE, struct.pack('<III', 0, 5, camera[0] if camera else 0))
        u.mem_write(CAMERA_SPACE, struct.pack('<I', SPACE if camera else 0))
        u.reg_write(UC_X86_REG_ESP, STACK + 0x80000)
        u.mem_write(STACK + 0x80000, struct.pack('<I', SENTINEL))
        u.reg_write(UC_X86_REG_ECX, COMPONENT)
        u.reg_write(UC_X86_REG_FPCW, 0x27f)
        u.reg_write(UC_X86_REG_MXCSR, 0x1f80)
        u.emu_start(UPDATE, SENTINEL + 1, count=200000)
        assert u.reg_read(UC_X86_REG_EIP) in (SENTINEL, BAD_FUNCTION_CALL), hex(u.reg_read(UC_X86_REG_EIP))
        raw = u.mem_read(COMPONENT + 0xb8, 7)
        self.trace['history'] = [raw[4], raw[5], raw[6], struct.unpack('<I', raw[:4])[0]]
        return self.trace


def scenarios(seed=1513, sequences=48, steps=24):
    generator = random.Random(seed)
    result = []

    def rotation():
        yaw, pitch, roll = (generator.uniform(-math.pi, math.pi) if generator.random() < .6 else 0. for _ in range(3))
        cy, sy, cp, sp, cr, sr = math.cos(yaw), math.sin(yaw), math.cos(pitch), math.sin(pitch), math.cos(roll), math.sin(roll)
        return [f32(v) for v in (cy * cr + sy * sp * sr, sr * cp, -sy * cr + cy * sp * sr, 0.,
                                 -cy * sr + sy * sp * cr, cr * cp, sr * sy + cy * sp * cr, 0.,
                                 sy * cp, -sp, cy * cp, 0.)]

    for index in range(sequences):
        config = [f32(v) for v in (generator.uniform(-.5, .5), generator.uniform(.5, 3.), generator.uniform(-1, 1),
                                   generator.uniform(.5, 4.), generator.uniform(1., 6.),
                                   generator.uniform(0., 4.), generator.uniform(2., 9.), generator.choice((.5, .3, 1.)))]
        initial = None
        if index % 3 == 1:
            initial = [generator.randrange(256) for _ in range(3)] + [bits(f32(generator.uniform(-2., 3.)))]
        sequence = {'config': config, 'initial': initial, 'steps': []}
        for unused in range(steps):
            position = [f32(generator.uniform(-500., 500.)), f32(generator.uniform(-5., 60.)),
                        f32(generator.uniform(-500., 500.))]
            depth = generator.choice((-1., -1., f32(generator.uniform(.001, .6)), f32(generator.uniform(.6, 4.)),
                                      f32(generator.uniform(4., 30.))))
            lod = None
            if generator.random() < .5:
                lod = [generator.choice((0, 3, 15, 16, 40)), f32(generator.choice((0., 50., 150., 150.5, 400.))),
                       f32(generator.choice((150., 100., 500.))), generator.choice((15, 3, 0xffffffff))]
            camera = None
            if generator.random() < .85:
                camera = [generator.randrange(1, 9), generator.random() < .9,
                          [generator.choice((0, 1, 2, 0x80, 0x82, 0x10, 0x40)) for _ in range(generator.randrange(3))]]
            sequence['steps'].append({
                'matrix': rotation() + position + [1.], 'lod': lod,
                'plane': generator.random() < .96, 'speed': f32(generator.uniform(0., 12.)) if generator.random() < .95 else None,
                'height': depth, 'corners': [generator.choice((-1., f32(generator.uniform(.01, 3.)))) for _ in range(4)],
                'camera': camera, 'splash': generator.random() < .9, 'switch': generator.random() < .9})
        result.append(sequence)
    return result


REGISTRY, CLOCK, ENTRIES, UPDATER = HEAP + 0x800, HEAP + 0x880, HEAP + 0x1000, HEAP + 0x900
ADVANCE, REGISTRY_GETTER = 0x13efb40, 0x757920


class OriginalLowFrequencyUpdater(object):
    """LowFrequencyUpdater::advance 013efb40 with its MSVC heap helpers."""

    def __init__(self, sensor):
        self.u, u = sensor.u, sensor.u
        u.mem_write(REGISTRY + 0x50, struct.pack('<I', CLOCK))
        u.mem_write(CLOCK, struct.pack('<I', CLOCK + 0x10))
        u.mem_write(CLOCK + 0x10 + 0x10, struct.pack('<I', STUBS + 0x300))
        sensor.handlers[REGISTRY_GETTER] = lambda: sensor._return(REGISTRY)
        sensor.handlers[STUBS + 0x300] = lambda: sensor._return(self.frame)
        for address in (REGISTRY_GETTER, STUBS + 0x300):
            u.hook_add(unicorn.UC_HOOK_CODE, sensor._hook, begin=address, end=address)
        self.sensor = sensor

    def run(self, entries, elapsed, frame, limit):
        u, self.frame = self.u, frame
        packed = b''.join(struct.pack('<ffII', *entry) for entry in entries)
        u.mem_write(ENTRIES, packed)
        end = ENTRIES + len(packed)
        u.mem_write(UPDATER, struct.pack('<IIIIf', ENTRIES, end, end, limit, .25))
        u.reg_write(UC_X86_REG_ESP, STACK + 0x80000)
        u.mem_write(STACK + 0x80000, struct.pack('<If', SENTINEL, elapsed))
        u.reg_write(UC_X86_REG_ECX, UPDATER)
        u.reg_write(UC_X86_REG_MXCSR, 0x1f80)
        u.emu_start(ADVANCE, SENTINEL + 1, count=200000)
        assert u.reg_read(UC_X86_REG_EIP) == SENTINEL
        return [list(struct.unpack('<ffII', u.mem_read(ENTRIES + 16 * n, 16))) for n in range(len(entries))]


def updater_cases(seed=1513, count=4000):
    generator = random.Random(seed)
    cases = []
    for unused in range(count):
        size = generator.randrange(0, 13)
        entries = []
        for index in range(size):
            remaining = generator.choice((f32(generator.uniform(-.3, .3)), .25, 0., -0., float('nan'), .125))
            entries.append([remaining, .25, generator.choice((0, 1, 2, 3, 7)), index + 1])
        elapsed = generator.choice((0., .125, .25, f32(generator.uniform(0., .3)), float('nan')))
        cases.append((entries, elapsed, generator.randrange(1, 20), generator.choice((3, 3, 1, 2, 5))))
    return cases

class OriginalFloatLink:
    """Original float-functor cloning and valid-object conversion instructions.

    Allocation and the Python producer/error-writer boundaries are supplied;
    759140->7583f0, 759320 and its actual 68fba0 converter execute unchanged.
    A supplied NULL models the already-reviewed producer failure boundary and
    proves its unsafe dereference, not a complete original-client crash.
    """
    def __init__(self, executable):
        self.original = OriginalWaterSensor(executable)
        self.u = self.original.u
        self.source = HEAP + 0x10000
        self.owner = HEAP + 0x11000
        self.number = HEAP + 0x12000
        self.allocation = HEAP + 0x13000
        self.result = HEAP + 0x14000
        self.return_stub = STUBS + 0x800
        self.errors = 0
        self.return_object = 0
        self.fault = None
        self.u.mem_write(self.return_stub, b'\xd9\x1d' + struct.pack('<I', self.result)
                         + b'\xe9' + struct.pack('<i', SENTINEL-(self.return_stub+11)))
        self.u.mem_write(self.owner, struct.pack('<II', 100, 0))

        def allocate():
            assert self.original._arg(1) == 0x24
            self.original._return(self.allocation)
        def producer(): self.original._return(self.return_object)
        def error():
            self.errors += 1
            self.original._return(0)
        handlers = {0x13d7fe9: allocate, 0x75ae40: producer, 0xfc3720: error}
        self.original.handlers.update(handlers)
        for address in handlers:
            self.u.hook_add(unicorn.UC_HOOK_CODE, self.original._hook, begin=address, end=address)
        def invalid(unused_u, unused_access, address, unused_size, unused_value, unused_data):
            self.fault = address
            return False
        self.u.hook_add(unicorn.UC_HOOK_MEM_INVALID, invalid)

    def setup(self, entry, this, args=(), result=False):
        u = self.u
        sp = STACK + 0x80000
        u.mem_write(sp, struct.pack('<'+'I'*(len(args)+1),
                                   self.return_stub if result else SENTINEL, *args))
        u.reg_write(UC_X86_REG_ESP, sp)
        u.reg_write(UC_X86_REG_ECX, this)
        u.reg_write(UC_X86_REG_FPCW, 0x27f)
        u.reg_write(UC_X86_REG_MXCSR, 0x1f80)
        u.emu_start(entry, SENTINEL+1, count=100000)

    def clone(self, cached):
        self.u.mem_write(self.source, struct.pack('<III', 0x14d670c, cached, self.owner)
                         + bytes(16) + struct.pack('<II', 0, 15))
        count = struct.unpack('<I', self.u.mem_read(self.owner, 4))[0]
        self.setup(0x759140, self.source, (0,))
        copied = self.u.reg_read(UC_X86_REG_EAX)
        assert copied == self.allocation
        result = struct.unpack('<II', self.u.mem_read(copied+4, 8))
        assert struct.unpack('<I', self.u.mem_read(self.owner, 4))[0] == count+1
        return result

    def sample(self, address, value):
        if value is None:
            self.return_object = 0x1a5c798  # Actual original Py_None object.
        elif value == 'NULL':
            self.return_object = 0
        else:
            self.u.mem_write(self.number, struct.pack('<IId', 100, 0x1a64370, value))
            self.return_object = self.number
        self.errors = 0
        self.fault = None
        try:
            self.setup(0x759320, address, result=True)
        except unicorn.UcError:
            if self.fault != 4: raise
            return None
        return struct.unpack('<I', self.u.mem_read(self.result, 4))[0]
