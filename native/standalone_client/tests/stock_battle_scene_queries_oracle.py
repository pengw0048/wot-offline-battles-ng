"""Run original #1513 scene-query instructions at explicit call boundaries.

D3DX uses the shared float32 definitions from the CursorCamera oracle, which
the host harness compiles too. Space traversal, CPython and the identity
feature are scripted. This proves the query's data flow, not D3DX or BSP.
"""
import math
import random
import struct

import pefile  # noqa: F401  Missing oracle tooling skips the instruction tests.
import unicorn  # noqa: F401

from stock_cursor_camera_oracle import OriginalCursorCamera, f32

WATER_DISTANCE = 0xa302e0      # Water::a302e0(start, end, edgeAlpha)
HIT_RECORD = 0xf63970          # hit record update(base, obstacle, triangle, dist)
GUN_MARKER_CALLBACK = 0xf5c1d0  # Ccb_ (vtable 0174c304) operator
TRIANGLE_INTERSECTS = 0x1172190  # WorldTriangle::intersects(start, dir, length)
SEGMENT_CALLBACK = 0xf61a90    # wg_collideSegment callback constructor
SPACE_MANAGER = 0xac3460
SPACE_LOOKUP = 0xac3620
PY_CALLABLE_CHECK = 0xfb38d0
INT_SUBCLASS = 1 << 23
GUARD = 0x40000000             # alpha table region with read-watched guards


class OriginalSceneQueries(OriginalCursorCamera):
    def __init__(self, executable):
        OriginalCursorCamera.__init__(self, executable)
        from unicorn import UC_HOOK_MEM_READ
        self.uc.mem_map(GUARD, 0x200000)
        self.table = None
        self.outside = False
        self.uc.hook_add(UC_HOOK_MEM_READ, self._table_read, begin=GUARD, end=GUARD + 0x200000 - 1)
        self.callables = set()
        self.handle(SPACE_MANAGER, lambda e: e._return(0x1234))
        self.handle(SPACE_LOOKUP, lambda e: e._return(0, 4))
        self.handle(PY_CALLABLE_CHECK, lambda e: e._return(1 if e._arg(0) in e.callables else 0))

    def _import(self, uc, address, size, data):
        # a302e0 calls msvcr ceil(double) for its upper alpha taps.
        if self.imports.get(address) != 'ceil':
            return OriginalCursorCamera._import(self, uc, address, size, data)
        esp = self.uc.reg_read(self.x86.UC_X86_REG_ESP)
        value = struct.unpack('<d', bytes(self.uc.mem_read(esp + 4, 8)))[0]
        result = float(math.ceil(value)) if math.isfinite(value) else value
        assert f32(result) == result, 'ceil of a float32 is a float32'
        self._return_x87(result, 0)

    def _table_read(self, uc, access, address, size, value, data):
        begin, end = self.table
        if address < begin or address + size > end:
            self.outside = True

    def _water_stub(self, water, flag, points, result):
        # The code is written once; Unicorn may keep a translated block of
        # rewritten instructions, so each case only changes the data slots.
        # fninit; mov ecx,[w]; push [flag]; push [end]; push [start];
        # call a302e0; mov eax,[result]; fstp dword [eax]; jmp STOP
        if not hasattr(self, '_stub'):
            data = self._stub_data = self.alloc(32)
            address = self._stub = self.alloc(64)
            call = address + 2 + 6 + 6 + 6 + 6
            jump = call + 5 + 5 + 2
            code = (b'\xdb\xe3' + b'\x8b\x0d' + struct.pack('<I', data) +
                    b'\xff\x35' + struct.pack('<I', data + 4) +
                    b'\xff\x35' + struct.pack('<I', data + 8) +
                    b'\xff\x35' + struct.pack('<I', data + 12) +
                    b'\xe8' + struct.pack('<i', WATER_DISTANCE - (call + 5)) +
                    b'\xa1' + struct.pack('<I', data + 16) + b'\xd9\x18' +
                    b'\xe9' + struct.pack('<i', self.STOP - (jump + 5)))
            self.uc.mem_write(address, code)
        self.uc.mem_write(self._stub_data, struct.pack('<5I', water, flag, points + 12, points, result))
        return self._stub

    def water_alpha(self, case):
        """True/False for the alpha decision, None when the original reads outside wAlpha_."""
        water = self.alloc(0x300)
        height = 2.0
        identity = [[1., 0., 0., 0.], [0., 1., 0., 0.], [0., 0., 1., 0.]]
        self.put_matrix(water + 0x17c, identity + [[0., height, 0., 1.]])
        self.put_matrix(water + 0x1bc, identity + [[0., -height, 0., 1.]])
        x0, z0, x1, z1 = case['bb']
        self.put_floats(water + 0x2e0, [x0, -1., z0, x1, 1., z1])
        self.put_floats(water + 0x28, [case['size'][0], case['size'][1]])
        self.put_floats(water + 0x34, [case['tess']])
        self.uc.mem_write(water + 0xa4, b'\x01')
        self.uc.mem_write(water + 0x2a1, b'\x01')
        self.uc.mem_write(water + 0x2a5, b'\x01')
        self.put_word(water + 0x208, case['grid'][0])
        self.put_word(water + 0x20c, case['grid'][1])
        alpha = case['alpha']
        begin = GUARD + 0x100000
        self.uc.mem_write(begin - 0x1000, b'\xff' * 0x1000)
        self.uc.mem_write(begin, struct.pack('<%dI' % len(alpha), *alpha))
        end = begin + 4 * len(alpha)
        self.uc.mem_write(end, b'\xff' * 0x1000)
        self.put_word(water + 0x228, begin)
        self.put_word(water + 0x22c, end)
        self.put_word(water + 0x230, end)
        self.table = (begin, end)
        points = self.alloc(32)
        self.put_floats(points, [case['x'], height + 10., case['z'], case['x'], height - 10., case['z']])
        result = self.alloc(4)
        outputs = []
        for flag in (0, 1):
            self.outside = False
            self.call(self._water_stub(water, flag, points, result))
            outputs.append((self.floats(result, 1)[0], self.outside))
        (plain, unused), (edge, outside) = outputs
        self.last_edge = edge
        assert plain == 10.0, ('The fixture must pass the false-alpha geometry', case, plain)
        if outside:
            return None
        assert edge in (plain, -1.0), edge
        return edge == plain

    def hit_record(self, case):
        record = self.alloc(0x70)
        base = self.alloc(0x20)          # spaceID, space, three features; identity feature null
        matrix = self.alloc(64)
        self.put_matrix(matrix, case['matrix'])
        obstacle = self.alloc(0x20)
        self.put_word(obstacle + 4, matrix)
        self.put_word(obstacle + 8, matrix)
        triangle = self.alloc(0x28)
        self.put_floats(triangle, [c for v in case['vertices'] for c in v])
        self.uc.mem_write(triangle + 0x24, struct.pack('<H', case['flags']))
        self.call(HIT_RECORD, (base, obstacle, triangle, struct.unpack('<I', struct.pack('<f', case['distance']))[0]),
                  ecx=record)
        return {'distance': self.floats(record, 1)[0], 'normal': self.floats(record + 4, 3),
                'materialKind': self.word(record + 0x10), 'chunkID': self.word(record + 0x18),
                'itemIndex': self.word(record + 0x1c) & 0xffff,
                'translation': self.floats(record + 0x50, 3)}

    def triangle_hit(self, vertices, start, direction, length=10.0):
        """(hit, length) from the original WorldTriangle::intersects."""
        triangle = self.alloc(0x28)
        self.put_floats(triangle, [c for v in vertices for c in v])
        a, b, c = self.alloc(12), self.alloc(12), self.alloc(4)
        self.put_floats(a, start)
        self.put_floats(b, direction)
        self.put_floats(c, [length])
        return bool(self.call(TRIANGLE_INTERSECTS, (a, b, c), ecx=triangle) & 0xff), self.floats(c, 1)[0]

    def identity_record(self, chunk, item):
        """f63970 identity words when the destruction feature resolves the obstacle."""
        record = self.alloc(0x70)
        feature = self.alloc(8)
        code = self.entry()

        def lookup(e):
            e.put_word(e._arg(2), chunk)
            e.put_word(e._arg(3), item)
            e._return(1, 16)
        self.handle(code, lookup)
        self.put_word(feature, self.vtable(0, 0, 0, 0, 0, 0, code))
        base = self.alloc(0x20)
        self.put_word(base + 0xc, feature)
        matrix = self.alloc(64)
        self.put_matrix(matrix, [[1., 0., 0., 0.], [0., 1., 0., 0.], [0., 0., 1., 0.], [0., 0., 0., 1.]])
        obstacle = self.alloc(0x20)
        self.put_word(obstacle + 4, matrix)
        self.put_word(obstacle + 0xc, 0x1234)
        triangle = self.alloc(0x28)
        self.put_floats(triangle, [0., 0., 0., 0., 0., 1., 1., 0., 0.])
        self.call(HIT_RECORD, (base, obstacle, triangle, 0x3f800000), ecx=record)
        return self.word(record + 0x18) | (self.word(record + 0x1c) << 32)

    def gun_marker(self, skip, required, surfaces):
        """(verdicts, distance) from the original Ccb_ over one surface sequence."""
        callback = self.alloc(0x10)
        self.put_word(callback, 0x174c304)
        self.put_floats(callback + 4, [-1.])
        self.uc.mem_write(callback + 8, struct.pack('<HH', skip, required))
        obstacle = self.alloc(0x20)
        self.put_word(obstacle + 0xc, 0xffffffff)   # f64630 accepts a handle of -1
        triangle = self.alloc(0x28)
        verdicts = []
        for flags, distance in surfaces:
            self.uc.mem_write(triangle + 0x24, struct.pack('<H', flags))
            bits = struct.unpack('<I', struct.pack('<f', distance))[0]
            verdicts.append(self.call(GUN_MARKER_CALLBACK, (obstacle, triangle, bits), ecx=callback))
        return verdicts, self.floats(callback + 4, 1)[0]

    def py_object(self, flags, value=0, callable_=False):
        kind = self.alloc(0x60)
        self.put_word(kind + 0x54, flags)
        self.put_word(kind, 1 << 20)
        address = self.alloc(16)
        self.put_word(address, 1 << 20)
        self.put_word(address + 4, kind)
        self.put_word(address + 8, value)
        if callable_:
            self.callables.add(address)
        return address

    def classify_filter(self, argument):
        """(select mask, callable stored) after the original constructor."""
        if argument is None:
            pointer = 0
        else:
            flags, value, callable_ = argument
            pointer = self.py_object(flags, value, callable_)
        callback = self.alloc(0xb0)
        self.call(SEGMENT_CALLBACK, (7, 0x80, pointer, 0), ecx=callback)
        mask = struct.unpack('<i', struct.pack('<I', self.word(callback + 0x28)))[0]
        stored = self.word(callback + 0x1c)
        assert stored in (0, pointer)
        return mask, stored != 0


def water_cases(seed=0x1513, count=4000):
    generator = random.Random(seed)
    cases = []
    for index in range(count):
        tess = generator.choice([0.5, 1.0, 1.25, 2.0, 3.0])
        gx, gz = generator.randint(2, 9), generator.randint(2, 9)
        sx = f32((gx - 1) * tess - generator.choice([0.0, 0.25 * tess]))
        sz = f32((gz - 1) * tess - generator.choice([0.0, 0.25 * tess]))
        bb = (f32(-sx / 2), f32(-sz / 2), f32(sx / 2), f32(sz / 2))
        size = gx * gz - generator.choice([0, 0, 0, 1, gx])
        alpha = [generator.choice([0, 9, 10, 11, 40, 255]) << 24 | generator.randint(0, 0xffffff)
                 for unused in range(max(size, 1))]
        if index % 5 == 0:
            # Points on cell corners and edges exercise floor/ceil equality.
            x = min(f32(bb[0] + generator.randint(0, gx - 1) * tess), bb[2])
            z = min(f32(bb[1] + generator.randint(0, gz - 1) * tess * generator.choice([1.0, 0.5])), bb[3])
        elif index % 5 == 1:
            # Within the original 1 mm bbActual tolerance outside the edges.
            x = f32(generator.choice([bb[0] - 0.0009, bb[2] + 0.0009, bb[0] + 0.0004]))
            z = f32(generator.uniform(bb[1], bb[3]))
        else:
            x = f32(generator.uniform(bb[0], bb[2]))
            z = f32(generator.uniform(bb[1], bb[3]))
        cases.append({'grid': (gx, gz), 'size': (sx, sz), 'tess': tess, 'bb': bb,
                      'x': x, 'z': z, 'alpha': alpha})
    return cases


def record_cases(seed=0x1970, count=200):
    generator = random.Random(seed)
    cases = []
    for index in range(count):
        yaw = generator.uniform(-math.pi, math.pi)
        c, s = f32(math.cos(yaw)), f32(math.sin(yaw))
        scale = generator.choice([1.0, 1.0, 0.5, 2.0])
        matrix = [[f32(c * scale), 0., f32(-s * scale), 0.], [0., f32(scale), 0., 0.],
                  [f32(s * scale), 0., f32(c * scale), 0.],
                  [f32(generator.uniform(-500, 500)), f32(generator.uniform(-20, 80)),
                   f32(generator.uniform(-500, 500)), 1.]]
        vertices = [[f32(generator.uniform(-5, 5)) for unused in range(3)] for unused in range(3)]
        cases.append({'matrix': matrix, 'vertices': vertices,
                      # Collision flags 0xff are TRIANGLE_NOT_IN_BSP and never reach a callback.
                      'flags': generator.randint(0, 0xffff) & ~0x01, 'distance': f32(generator.uniform(0, 50))})
    return cases


def zero_direction_cases(seed=0x0172, count=500):
    generator = random.Random(seed)
    cases = []
    for unused in range(count):
        vertices = [[f32(generator.uniform(-5, 5)), f32(generator.uniform(-1, 1)),
                     f32(generator.uniform(-5, 5))] for unused in range(3)]
        a, b = generator.random(), generator.random()
        if a + b > 1:
            a, b = 1 - a, 1 - b
        point = [f32(vertices[0][k] + a * (vertices[1][k] - vertices[0][k]) +
                     b * (vertices[2][k] - vertices[0][k])) for k in range(3)]
        cases.append((vertices, point))
    return cases


def gun_marker_cases(seed=0x5d1c, count=300):
    generator = random.Random(seed)
    flags = [0, 0x08, 0x10, 0x40, 0x80, 0x88, 0xc8, 0x0300, 0x8080, 0xffff]
    cases = []
    for index in range(count):
        skip = 0x80 if index % 3 else generator.choice(flags)
        required = 0 if index % 4 else generator.choice(flags)
        surfaces = [(generator.choice(flags) | (generator.randint(0, 255) << 8 if index % 2 else 0),
                     f32(generator.choice([0., 1., 2.5, 2.5, 7., -0.5, generator.uniform(0, 50)])))
                    for unused in range(generator.randint(0, 6))]
        cases.append({'skip': skip, 'required': required, 'surfaces': surfaces})
    return cases


def filter_cases():
    return [
        (None, (-1, False)),
        ((INT_SUBCLASS, 8, False), (8, False)),
        ((INT_SUBCLASS, 0, False), (0, False)),
        ((INT_SUBCLASS, 0xfffffff8, False), (-8, False)),
        ((INT_SUBCLASS | 1, 1, True), (1, False)),   # an int wins even when callable
        ((0, 0, True), (-1, True)),
        ((0, 0, False), (-1, False)),
        ((1 << 24, 5, False), (-1, False)),           # a long is not an int subclass
    ]


def compiled_item_identities(executable, payload, bsmi_count):
    """Reverse BSMI keys from the original 6ba4f0 load and 6baa10 per reference.

    The (chunk, row, reference) walk is 006bab30's loop over the same arrays;
    each object comes from the unchanged forward lookup, last key wins.
    """
    from stock_space_sounds_oracle import OriginalSpaceSoundReader
    reader = compiled_item_identities.reader = getattr(
        compiled_item_identities, 'reader', None) or OriginalSpaceSoundReader(executable)
    reader.identities(payload, [])
    count = struct.unpack_from('<II', payload, 0)[1]
    chunks = [struct.unpack_from('<III', payload, 8 + i * 12) for i in range(count)]
    offset = 8 + count * 12
    items_count = struct.unpack_from('<II', payload, offset)[1]
    items = [struct.unpack_from('<II', payload, offset + 8 + i * 8) for i in range(items_count)]
    output = reader.feature + 0x40
    result = {}
    for chunk, first, rows in chunks:
        for row in range(rows):
            begin, end = items[first + row]
            for reference in range((end - begin + 1) & 0xffffffff):
                reader.call(0x6baa10, (output, chunk, row, reference), this=reader.owner + 0x10)
                handle = reader.word(output)
                kind = reader.machine.mem_read(output + 4, 1)[0]
                if handle != 0xffffffff and kind == 0x33 and handle < bsmi_count:
                    result[handle] = (chunk, row)
    return result
