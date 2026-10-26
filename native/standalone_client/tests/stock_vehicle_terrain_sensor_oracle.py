"""Execute exact #1513 TerrainMatKindSensor instructions in Unicorn.

TerrainMatKindSensor::update (00796410), its LOD gate (00781f70), the camera
space getter (00ac2f10), collideSegment (00f642b0), ClosestCcbNative (00f62400,
00f63970, 00f64630), LowFrequencyUpdater::advance (013efb40) and the argument
formatter (0068e100) run unchanged. Only documented services are hooked:
SpaceManager lookup and ClientSpace::collide traversal, D3DX vector helpers,
std::function targets, CRT imports and PyErr_Format.
"""
import math
import random
import re
import struct

import pefile
import unicorn
from unicorn.x86_const import (UC_X86_REG_EAX, UC_X86_REG_ECX, UC_X86_REG_EIP, UC_X86_REG_ESP,
                               UC_X86_REG_FPCW, UC_X86_REG_FS, UC_X86_REG_GDTR, UC_X86_REG_MXCSR,
                               UC_X86_REG_SS)

UPDATE, LOD_GATE, ADVANCE, FORMAT_ARGUMENTS = 0x796410, 0x781f70, 0x13efb40, 0x68e100
CLOSEST_HIT, COLLIDE_SPACE = 0xf62400, 0xac2c90
SPACE_MANAGER, SPACE_BY_ID, REGISTRY_GETTER = 0xac3460, 0xac3620, 0x757920
TRANSFORM, NORMALIZE, TRANSFORM_NORMAL = 0x11c3bdc, 0x11c3bbe, 0x11c3c06
BAD_FUNCTION_CALL, CAMERA_SPACE, PYERR_FORMAT = 0x13d7914, 0x1d59748, 0xfc3720
OPERATOR_NEW, OPERATOR_DELETE = 0x13d7fe9, (0x13d8019, 0x13d801e)
STACK, HEAP, STUBS, GDT = 0x6000000, 0x6100000, 0x6200000, 0x6300000
COMPONENT, MATRIX, LOD_STATE, SPACE, MANAGER = HEAP, HEAP + 0x400, HEAP + 0x480, HEAP + 0x500, HEAP + 0x540
OBSTACLE, IDENTITY_MATRIX, NEG_ONE = HEAP + 0x580, HEAP + 0x5c0, HEAP + 0x600
FUNCTIONS = {name: HEAP + 0x700 + 0x40 * index for index, name in enumerate(('lod', 'plane'))}
POINTS, KINDS, TRIANGLES, RESULTS = HEAP + 0x1000, HEAP + 0x2000, HEAP + 0x3000, HEAP + 0x8000
REGISTRY, CLOCK, ENTRIES, UPDATER = HEAP + 0x9000, HEAP + 0x9080, HEAP + 0xa000, HEAP + 0x9100
STRINGS, ALLOCATIONS = HEAP + 0x20000, HEAP + 0x40000
STUB = {name: STUBS + 0x10 * index for index, name in enumerate(('lod', 'plane', 'frame'))}
SENTINEL, DRIVER, IMPORTS = STUBS + 0x200, STUBS + 0x1000, STUBS + 0x8000
NOT_WRITTEN = 0xffffffff
ARGUMENT_ERRORS = {
    'PyTerrainMatKindSensor': 'PyTerrainMatKindSensor() expects 3 arguments of types MatrixProvider, '
                              'BW::vector<Vector3>, float',
    'setLodLink': 'setLodLink() expects 1 argument of type DataLinks::DataLink<LodProcessing::LodState>::Type',
    'setLodSettings': 'setLodSettings() expects 1 argument of type LodProcessing::LodSettings'}


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


class OriginalTerrainSensor(object):
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
        for name, impl in FUNCTIONS.items():
            vtable = impl + 0x20
            u.mem_write(vtable, struct.pack('<4I', 0, 0, STUB[name], 0))
            u.mem_write(impl, struct.pack('<I', vtable))
        identity = [1., 0., 0., 0., 0., 1., 0., 0., 0., 0., 1., 0., 0., 0., 0., 1.]
        u.mem_write(IDENTITY_MATRIX, struct.pack('<16f', *identity))
        u.mem_write(OBSTACLE, struct.pack('<4I', 0, IDENTITY_MATRIX, 0, NOT_WRITTEN))
        u.mem_write(NEG_ONE, struct.pack('<f', -1.))
        self.handlers = {TRANSFORM: self._transform, NORMALIZE: self._normalize,
                         TRANSFORM_NORMAL: self._transform_normal, SPACE_MANAGER: lambda: self._return(MANAGER),
                         SPACE_BY_ID: self._space_by_id, COLLIDE_SPACE: self._collide,
                         BAD_FUNCTION_CALL: self._bad, SENTINEL: self._done, PYERR_FORMAT: self._pyerr_format,
                         OPERATOR_NEW: self._new, REGISTRY_GETTER: lambda: self._return(REGISTRY),
                         STUB['lod']: self._lod, STUB['plane']: self._plane,
                         STUB['frame']: lambda: self._return(self.frame)}
        for address in OPERATOR_DELETE:
            self.handlers[address] = lambda: self._return(0)
        services = {'InterlockedIncrement': lambda: self._interlocked(1),
                    'InterlockedDecrement': lambda: self._interlocked(-1),
                    'memset': self._memset, 'memcpy': self._memcpy, 'memmove': self._memcpy,
                    'strncmp': self._strncmp}
        stub = IMPORTS
        for entry in pe.DIRECTORY_ENTRY_IMPORT:
            for imported in entry.imports:
                name = (imported.name or b'').decode()
                if name in services:
                    u.mem_write(imported.address, struct.pack('<I', stub))
                    self.handlers[stub] = services[name]
                    stub += 0x10
        for address in self.handlers:
            u.hook_add(unicorn.UC_HOOK_CODE, self._hook, begin=address, end=address)
        u.mem_write(REGISTRY + 0x50, struct.pack('<I', CLOCK))
        u.mem_write(CLOCK, struct.pack('<I', CLOCK + 0x10))
        u.mem_write(CLOCK + 0x10 + 0x10, struct.pack('<I', STUB['frame']))
        self.frame = 0
        self.heap = ALLOCATIONS

    def _hook(self, u, address, size, data):
        handler = self.handlers.get(address)
        if handler is not None:
            handler()

    def _arg(self, index):
        return struct.unpack('<I', self.u.mem_read(self.u.reg_read(UC_X86_REG_ESP) + 4 * index, 4))[0]

    def _floats(self, address, count):
        return list(struct.unpack('<%df' % count, self.u.mem_read(address, 4 * count)))

    def _string(self, address):
        return bytes(self.u.mem_read(address, 256)).split(b'\0')[0].decode('latin1')

    def _return(self, value, pop=0):
        u, esp = self.u, self.u.reg_read(UC_X86_REG_ESP)
        if value is not None:
            u.reg_write(UC_X86_REG_EAX, value & 0xffffffff)
        u.reg_write(UC_X86_REG_EIP, self._arg(0))
        u.reg_write(UC_X86_REG_ESP, esp + 4 + pop)

    def _interlocked(self, delta):
        address = self._arg(1)
        value = (struct.unpack('<i', self.u.mem_read(address, 4))[0] + delta) & 0xffffffff
        self.u.mem_write(address, struct.pack('<I', value))
        self._return(value, 4)

    def _memset(self):
        target, value, count = self._arg(1), self._arg(2) & 0xff, self._arg(3)
        self.u.mem_write(target, bytes([value]) * count)
        self._return(target)

    def _memcpy(self):
        target, source, count = self._arg(1), self._arg(2), self._arg(3)
        self.u.mem_write(target, bytes(self.u.mem_read(source, count)))
        self._return(target)

    def _strncmp(self):
        a, b = self._string(self._arg(1))[:self._arg(3)], self._string(self._arg(2))[:self._arg(3)]
        self._return(0 if a == b else (0xffffffff if a < b else 1))

    def _new(self):
        size = max(self._arg(1), 4)
        address, self.heap = self.heap, (self.heap + size + 15) & ~15
        self._return(address)

    def _pyerr_format(self):
        pattern = self._string(self._arg(2))
        values, index = [], 3
        for spec in re.findall(r'%[sd]', pattern):
            value = self._arg(index)
            index += 1
            values.append(self._string(value) if spec == '%s' else struct.unpack('<i', struct.pack('<I', value))[0])
        self.errors.append(pattern % tuple(values))
        self._return(0)

    def _transform(self):
        out, point, matrix = self._arg(1), self._floats(self._arg(2), 3), self._floats(self._arg(3), 16)
        self.trace['transforms'].append([bits(v) for v in point])
        self.u.mem_write(out, struct.pack('<4f', *transform(point, matrix)))
        self._return(out, 12)

    def _normalize(self):
        out, vector = self._arg(1), self._floats(self._arg(2), 3)
        length = math.sqrt(sum(v * v for v in vector))
        self.u.mem_write(out, struct.pack('<3f', *[v / length if length else 0. for v in vector]))
        self._return(out, 8)

    def _transform_normal(self):
        out, vector, matrix = self._arg(1), self._floats(self._arg(2), 3), self._floats(self._arg(3), 16)
        result = [sum(vector[k] * matrix[k * 4 + c] for k in range(3)) for c in range(3)]
        self.u.mem_write(out, struct.pack('<3f', *result))
        self._return(out, 12)

    def _space_by_id(self):
        camera = self.case['camera']
        found = camera is not None and camera[1] and self._arg(1) == camera[0]
        self._return(SPACE if found else 0, 4)

    def _collide(self):
        # ClientSpace::collide's traversal offers the scripted triangles to the
        # original ClosestCcbNative::operator() through a generated call chain.
        start, end, callback = self._floats(self._arg(1), 3), self._floats(self._arg(2), 3), self._arg(3)
        script = self.case['camera'][2]
        triangles = script[self.cursor % len(script)] if script else []
        results = RESULTS + 0x100 * len(self.trace['segments'])
        self.cursor += 1
        code = b''
        for index, (flags, distance) in enumerate(triangles):
            triangle = TRIANGLES + 0x30 * index
            self.u.mem_write(triangle, struct.pack('<9fH', 0., 0., 0., 1., 0., 0., 0., 0., 1., flags))
            code += struct.pack('<BIBIBIBI', 0x68, bits(distance), 0x68, triangle, 0x68, OBSTACLE, 0xb9, callback)
            call = DRIVER + len(code)
            code += b'\xe8' + struct.pack('<i', CLOSEST_HIT - (call + 5))
            code += b'\xa3' + struct.pack('<I', results + 4 * index)
        code += b'\xd9\x05' + struct.pack('<I', NEG_ONE) + b'\xc2\x0c\x00'
        self.u.mem_write(DRIVER, code)
        # The rewritten driver must not run as a stale translated block.
        self.u.ctl_remove_cache(DRIVER, DRIVER + 0x1000)
        self.trace['segments'].append((struct.unpack('<I', self.u.mem_read(SPACE + 8, 4))[0],
                                       [bits(v) for v in start], [bits(v) for v in end], results, len(triangles)))
        self.u.reg_write(UC_X86_REG_EIP, DRIVER)

    def _lod(self):
        self.trace['lodReads'] += 1
        self._return(LOD_STATE)

    def _plane(self):
        self.trace['matrixReads'] += 1
        self._return(MATRIX)

    def _bad(self):
        self.trace['bad_function_call'] = True
        self.u.emu_stop()

    def _done(self):
        self.u.emu_stop()

    def _call(self, address, ecx, arguments, count=2000000):
        u = self.u
        u.reg_write(UC_X86_REG_ESP, STACK + 0x80000)
        u.mem_write(STACK + 0x80000, struct.pack('<I', SENTINEL) + arguments)
        if ecx is not None:
            u.reg_write(UC_X86_REG_ECX, ecx)
        u.reg_write(UC_X86_REG_FPCW, 0x27f)
        u.reg_write(UC_X86_REG_MXCSR, 0x1f80)
        u.emu_start(address, SENTINEL + 1, count=count)
        assert u.reg_read(UC_X86_REG_EIP) in (SENTINEL, BAD_FUNCTION_CALL), hex(u.reg_read(UC_X86_REG_EIP))
        return u.reg_read(UC_X86_REG_EAX)

    def run(self, case):
        u, self.case, self.cursor = self.u, case, 0
        self.trace = {'transforms': [], 'segments': [], 'matrixReads': 0, 'lodReads': 0}
        points, lod, camera = case['points'], case['lod'], case['camera']
        u.mem_write(COMPONENT, b'\0' * 0x90)
        settings = lod[2:] if lod else (3.4028234663852886e38, 0xffffffff)
        u.mem_write(COMPONENT + 0x18, struct.pack('<fI', *settings))
        u.mem_write(LOD_STATE, struct.pack('<If', *(lod[:2] if lod else (0, 0.))))
        u.mem_write(COMPONENT + 0x44, struct.pack('<I', FUNCTIONS['lod'] if lod else 0))
        u.mem_write(COMPONENT + 0x6c, struct.pack('<I', FUNCTIONS['plane']))
        u.mem_write(MATRIX, struct.pack('<16f', *case['matrix']))
        packed = b''.join(struct.pack('<3f', *point) for point in points)
        u.mem_write(POINTS, packed + b'\0')
        u.mem_write(COMPONENT + 0x70, struct.pack('<3I', POINTS, POINTS + len(packed), POINTS + len(packed)))
        u.mem_write(KINDS, struct.pack('<%dI' % len(points), *([NOT_WRITTEN] * len(points))) + b'\0')
        u.mem_write(COMPONENT + 0x7c, struct.pack('<3I', KINDS, KINDS + 4 * len(points), KINDS + 4 * len(points)))
        u.mem_write(COMPONENT + 0x88, struct.pack('<f', case['scanLength']))
        u.mem_write(SPACE, struct.pack('<III', 0, 5, camera[0] if camera else 0))
        u.mem_write(CAMERA_SPACE, struct.pack('<I', SPACE if camera else 0))
        self.errors = []
        self._call(UPDATE, COMPONENT, b'')
        assert struct.unpack('<I', u.mem_read(SPACE + 4, 4))[0] == 5
        segments = []
        for space, start, end, results, count in self.trace['segments']:
            values = list(struct.unpack('<%dI' % count, u.mem_read(results, 4 * count))) if count else []
            segments.append([space, start, end, values])
        return {'kinds': list(struct.unpack('<%dI' % len(points), u.mem_read(KINDS, 4 * len(points)))),
                'segments': segments, 'transforms': self.trace['transforms'],
                'matrixReads': self.trace['matrixReads'], 'lodReads': self.trace['lodReads']}

    def lod_gate(self, maximum_distance_bits, maximum_priority, distance_bits, priority):
        u = self.u
        u.mem_write(COMPONENT + 0x18, struct.pack('<II', maximum_distance_bits, maximum_priority))
        u.mem_write(COMPONENT + 0x44, struct.pack('<I', FUNCTIONS['lod']))
        u.mem_write(LOD_STATE, struct.pack('<II', priority, distance_bits))
        self.trace = {'lodReads': 0}
        return self._call(LOD_GATE, COMPONENT + 0x18, b'') & 0xff

    def format_arguments(self, name, low, high, types):
        pointers, cursor = [], STRINGS
        for text in [name] + types:
            self.u.mem_write(cursor, text.encode() + b'\0')
            pointers.append(cursor)
            cursor += len(text) + 1
        self.errors = []
        self._call(FORMAT_ARGUMENTS, None, struct.pack('<3I', pointers[0], low, high) +
                   struct.pack('<%dI' % len(types), *pointers[1:]))
        return self.errors[-1]


def argument_errors(original):
    return {'PyTerrainMatKindSensor': original.format_arguments(
                'PyTerrainMatKindSensor', 3, 3, ['MatrixProviderPtr', 'BW::vector<Vector3>', 'float']),
            'setLodLink': original.format_arguments(
                'setLodLink', 1, 1, ['DataLinks::DataLink<LodProcessing::LodState>::Type']),
            'setLodSettings': original.format_arguments('setLodSettings', 1, 1, ['LodProcessing::LodSettings'])}


def scenarios(seed=1513, count=600):
    generator = random.Random(seed)

    def rotation():
        yaw, pitch, roll = (generator.uniform(-math.pi, math.pi) if generator.random() < .6 else 0. for _ in range(3))
        cy, sy, cp, sp, cr, sr = math.cos(yaw), math.sin(yaw), math.cos(pitch), math.sin(pitch), math.cos(roll), math.sin(roll)
        return [f32(v) for v in (cy * cr + sy * sp * sr, sr * cp, -sy * cr + cy * sp * sr, 0.,
                                 -cy * sr + sy * sp * cr, cr * cp, sr * sy + cy * sp * cr, 0.,
                                 sy * cp, -sp, cy * cp, 0.)]

    def triangle():
        flags = generator.choice((0, 0, 1, 2, 4, 8, 0x10, 0x12, 0x20, 0x40, 0x80, 0x82, 0x48, 0x11))
        kind = generator.choice((0, 1, 7, 0x2a, 0x47, 0x64, 0xff, generator.randrange(256)))
        distance = generator.choice((f32(generator.uniform(0., 4.)), 1., 2., 2., 0., -0., -1.,
                                     float('inf'), float('nan'), 3.4028234663852886e38))
        return [flags | kind << 8, distance]

    cases = []
    for unused in range(count):
        size = generator.choice((0, 1, 2, 3, 3, 3, 4))
        points = [[f32(generator.uniform(-3., 3.)), f32(generator.uniform(-1., 3.)), f32(generator.uniform(-4., 4.))]
                  for _ in range(size)]
        position = [f32(generator.uniform(-500., 500.)), f32(generator.uniform(-5., 60.)), f32(generator.uniform(-500., 500.))]
        lod = None
        if generator.random() < .6:
            lod = [generator.choice((0, 3, 15, 16, 40, 0xffffffff)),
                   f32(generator.choice((0., 50., 100., 100.5, 400., float('nan'), float('-inf')))),
                   f32(generator.choice((100., 150., float('nan'), 3.4028234663852886e38))),
                   generator.choice((15, 3, 0, 0xffffffff))]
        camera = None
        if generator.random() < .85:
            script = [[triangle() for _ in range(generator.randrange(5))] for _ in range(generator.randrange(4))]
            camera = [generator.randrange(1, 9), generator.random() < .9, script]
        scan = generator.choice((4., 4., 0., -2., f32(generator.uniform(.1, 9.)), float('nan')))
        cases.append({'points': points, 'scanLength': f32(scan), 'matrix': rotation() + position + [1.],
                      'lod': lod, 'camera': camera})
    return cases


class OriginalLowFrequencyUpdater(object):
    """LowFrequencyUpdater::advance 013efb40 with its MSVC heap helpers."""

    def __init__(self, sensor):
        self.sensor = sensor

    def run(self, entries, elapsed, frame, limit):
        sensor, u = self.sensor, self.sensor.u
        sensor.frame = frame
        packed = b''.join(struct.pack('<ffII', *entry) for entry in entries)
        u.mem_write(ENTRIES, packed + b'\0')
        end = ENTRIES + len(packed)
        u.mem_write(UPDATER, struct.pack('<IIIIf', ENTRIES, end, end, limit, .25))
        sensor._call(ADVANCE, UPDATER, struct.pack('<f', elapsed))
        return [list(struct.unpack('<ffII', u.mem_read(ENTRIES + 16 * n, 16))) for n in range(len(entries))]


def updater_cases(seed=1513, count=2000):
    generator = random.Random(seed)
    cases = []
    for unused in range(count):
        entries = []
        for index in range(generator.randrange(0, 13)):
            remaining = generator.choice((f32(generator.uniform(-.3, .3)), .25, 0., -0., float('nan'), .125))
            entries.append([remaining, .25, generator.choice((0, 1, 2, 3, 7)), index + 1])
        elapsed = generator.choice((0., .125, .25, f32(generator.uniform(0., .3)), float('nan')))
        # The terrain manager (005af760) always passes three; other limits probe the shared body.
        cases.append((entries, elapsed, generator.randrange(1, 20), generator.choice((3, 3, 3, 1, 5))))
    return cases


def encode_updater_case(case):
    entries, elapsed, frame, limit = case
    head = '%d %d %d %d ' % (len(entries), bits(elapsed), frame, limit)
    return head + ' '.join('%d %d %d %d' % (bits(r), bits(s), stamp, token) for r, s, stamp, token in entries) + '\n'


def encode_entries(entries):
    return ' '.join('%d %d %d %d' % (bits(r), bits(s), stamp, token) for r, s, stamp, token in entries) + \
        (' ' if entries else '')


def lod_cases(seed=1513, count=3000):
    generator = random.Random(seed)
    floats = (0., -0., 1., 99.99, 100., 100.0001, 150., float('inf'), float('-inf'), float('nan'),
              3.4028234663852886e38)
    priorities = (0, 1, 14, 15, 16, 0x7fffffff, 0x80000000, 0xfffffffe, 0xffffffff)
    return [(bits(generator.choice(floats)), generator.choice(priorities),
             bits(generator.choice(floats)), generator.choice(priorities)) for _ in range(count)]


SCHEDULER_PROBE = r'''
#include "stock_vehicle_water_sensor_data.hpp"
#include <cstring>
#include <iostream>
float f(unsigned bits) { float v; std::memcpy(&v, &bits, 4); return v; }
unsigned b(float v) { unsigned bits; std::memcpy(&bits, &v, 4); return bits; }
int main() {
    unsigned count, elapsed, frame, limit;
    while (std::cin >> count >> elapsed >> frame >> limit) {
        std::vector<wot::StockLowFrequencyEntry> entries(count);
        for (auto& e : entries) { unsigned r, s, stamp, token; std::cin >> r >> s >> stamp >> token;
            e = {f(r), f(s), stamp, token}; }
        wot::advanceStockLowFrequency(entries, f(elapsed), frame, limit);
        for (auto& e : entries) std::cout << b(e.remaining) << ' ' << b(e.reset) << ' ' << e.stamp << ' ' << e.token << ' ';
        std::cout << '\n';
    }
}
'''

LOD_PROBE = r'''
#include "stock_vehicle_terrain_sensor_data.hpp"
#include <cstring>
#include <iostream>
int main() {
    unsigned maxDistance, maxPriority, distance, priority;
    while (std::cin >> maxDistance >> maxPriority >> distance >> priority) {
        float a, b;
        std::memcpy(&a, &maxDistance, 4); std::memcpy(&b, &distance, 4);
        std::cout << int(wot::stockTerrainSensorLodAllows(a, maxPriority, b, priority)) << '\n';
    }
}
'''
