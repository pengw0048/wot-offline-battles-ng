"""Execute #1513 f610d0 with space-feature/CPython calls as explicit boundaries."""
import random
import struct

from stock_battle_scene_queries_oracle import OriginalSceneQueries, f32


class OriginalMaterialQuery(OriginalSceneQueries):
    def __init__(self, executable):
        OriginalSceneQueries.__init__(self, executable)
        self.values = {}
        self.surface = None
        self.query = 0
        self.callback_calls = []
        self.handle(0xf64630, lambda e: e._return(1))  # compiled fashion getter is NULL
        self.handle(0xf63ac0, self._identity)
        self.handle(0x67d120, self._string_ctor)
        self.handle(0x613c20, self._string_assign)
        self.handle(0x11e7c30, lambda e: e._return())
        self.handle(0x677a50, lambda e: e._return())
        self.handle(0xfb3420, lambda e: e._return(e.alloc(12 + e._arg(0) * 4)))
        self.handle(0x68ece0, lambda e: e._return(e._py(e._arg(0))))
        self.handle(0xfe1200, lambda e: e._return(e._py(e._arg(0))))
        self.handle(0x68e9d0, lambda e: e._return(e._py(e._string(e._arg(0)))))
        self.handle(0x68e560, self._callback)
        self.handle(0x690570, self._boolean)
        name_getter = self.entry()
        self.handle(name_getter, self._filename)
        provider = self.alloc(8)
        self.put_word(provider, self.vtable(name_getter))
        self.feature = self.alloc(16)
        self.put_word(self.feature + 8, provider)

    def _py(self, value):
        pointer = self.alloc(16)
        self.put_word(pointer, 1)
        self.values[pointer] = value
        return pointer

    def _string(self, address):
        size = self.word(address + 16)
        source = self.word(address) if self.word(address + 20) >= 16 else address
        return bytes(self.uc.mem_read(source, size)).decode('ascii')

    def _write_string(self, address, text):
        value = text.encode('ascii')
        assert len(value) <= 15
        self.uc.mem_write(address, value + b'\0' * (16 - len(value)))
        self.put_word(address + 16, len(value))
        self.put_word(address + 20, 15)

    def _identity(self, e):
        identity = self.surface[2] if len(self.surface) > 2 else None
        chunk, item = identity or (0, 0)
        output = e._arg(0)
        e.put_word(output, chunk)
        e.put_word(output + 4, (item & 0xffff) | (0x10000 if identity else 0))
        e._return(output)

    def _filename(self, e):
        identity = self.surface[2] if len(self.surface) > 2 else None
        value = b'fixture.model\0' if identity else b'\0'
        e._return(e.alloc(len(value), value), 4)

    def _string_ctor(self, e):
        source = e._arg(0)
        value = bytearray()
        while e.byte(source + len(value)):
            value.append(e.byte(source + len(value)))
        dest = e.uc.reg_read(e.x86.UC_X86_REG_ECX)
        e._write_string(dest, value.decode('ascii'))
        e._return(dest, 4)

    def _string_assign(self, e):
        dest = e.uc.reg_read(e.x86.UC_X86_REG_ECX)
        text = bytes(e.uc.mem_read(e._arg(0), e._arg(1))).decode('ascii')
        e._write_string(dest, text)
        e._return(dest, 8)

    def _callback(self, e):
        args = e._arg(1)
        self.callback_calls.append([self.values[e.word(args + 12 + 4 * i)] for i in range(4)])
        e._return(e._py(self.reject))

    def _boolean(self, e):
        value = self.values[e._arg(0)]
        e.uc.mem_write(e._arg(1), bytes([1 if value else 0]))
        e._return(0)

    def run_query(self, case):
        callback = self.query = self.alloc(0x90)
        self.put_word(callback, 0x174cf00)
        self.put_word(callback + 0x14, self.feature)
        self.put_word(callback + 0x18, self._py('callback') if case['callback'] else 0)
        self.put_floats(callback + 0x1c, [0., 10., 0., 0., -1., 0.] + list(case['point']))
        self.put_floats(callback + 0x40, [f32(3.4028234663852886e38)])
        self.put_word(callback + 0x44, 0x80)
        self._write_string(callback + 0x6c, '')
        self.callback_calls = []
        self.reject = case['reject']
        verdicts = []
        matrix = self.alloc(64)
        self.put_matrix(matrix, [[1., 0., 0., 0.], [0., 1., 0., 0.], [0., 0., 1., 0.], [0., 0., 0., 1.]])
        obstacle = self.alloc(0x20)
        self.put_word(obstacle + 4, matrix)
        self.put_word(obstacle + 8, matrix)
        self.put_word(obstacle + 0xc, 0x1234)
        triangle = self.alloc(0x28)
        self.put_floats(triangle, [-1., 0., -1., 0., 0., 1., 1., 0., -1.])
        for surface in case['surfaces']:
            self.surface = surface
            self.uc.mem_write(triangle + 0x24, struct.pack('<H', surface[0]))
            distance = struct.unpack('<I', struct.pack('<f', surface[1]))[0]
            verdicts.append(self.call(0xf610d0, (obstacle, triangle, distance), ecx=callback))
        result = [bool(self.byte(callback + 0x84)), self.floats(callback + 0x48, 3),
                  self.floats(callback + 0x54, 3), self.word(callback + 0x68),
                  self._string(callback + 0x6c), self.word(callback + 0x64) & 0xffff,
                  self.word(callback + 0x60)]
        return {'result': result, 'calls': self.callback_calls, 'verdicts': verdicts}


def material_cases(seed=0xf610, count=240):
    random_ = random.Random(seed)
    kinds = [0, 3, 70, 71, 72, 86, 87, 100, 101, 255]
    cases = []
    for unused in range(count):
        surfaces = []
        for index in range(random_.randint(0, 8)):
            identity = [random_.choice([0, 12, 0x80000000, 0xffffffff]), random_.choice([0, 4, 65535, 65539])] if random_.randint(0, 1) else None
            surfaces.append([random_.choice(kinds) << 8 | random_.choice([0, 0, 0, 8, 128]),
                             random_.choice([0., 4., 8., 12., 16., 20.]), identity])
        cases.append({'surfaces': surfaces, 'point': [0., float(random_.randint(-12, 12)), 0.],
                      'callback': bool(random_.randint(0, 1)), 'reject': bool(random_.randint(0, 1))})
    return cases
