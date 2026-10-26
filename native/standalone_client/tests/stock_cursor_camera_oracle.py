"""Run original #1513 CursorCamera instructions at explicit call boundaries.

D3DX, the SSE2 libm entry points and rand use the shared float32 definitions
below; the host harnesses compile the same definitions. Engine owners outside
CursorCamera (space checks, water queries, BSP traversal, CPython errors) are
scripted and recorded. This proves the camera's control and data flow, not
D3DX, BSP traversal or rendered behaviour.
"""
import ctypes
import math
import struct


def f32(value):
    return ctypes.c_float(value).value


def bits(value):
    return struct.unpack('<I', struct.pack('<f', value))[0]


def unbits(value):
    return struct.unpack('<f', struct.pack('<I', value & 0xffffffff))[0]


class CameraMath:
    """Float32 definitions shared with the host harness, one rounding per op."""

    @staticmethod
    def add(a, b):
        return f32(a + b)

    @staticmethod
    def sub(a, b):
        return f32(a - b)

    @staticmethod
    def mul(a, b):
        return f32(a * b)

    @staticmethod
    def div(a, b):
        if b == 0.0:
            if a == 0.0 or a != a:
                return float('nan')
            return math.copysign(float('inf'), a) * math.copysign(1.0, b)
        return f32(a / b)

    @classmethod
    def dot(cls, a, b):
        return cls.add(cls.add(cls.mul(a[0], b[0]), cls.mul(a[1], b[1])), cls.mul(a[2], b[2]))

    @classmethod
    def cross(cls, a, b):
        return (cls.sub(cls.mul(a[1], b[2]), cls.mul(a[2], b[1])),
                cls.sub(cls.mul(a[2], b[0]), cls.mul(a[0], b[2])),
                cls.sub(cls.mul(a[0], b[1]), cls.mul(a[1], b[0])))

    @classmethod
    def normalize(cls, v):
        length = f32(math.sqrt(cls.dot(v, v)))
        if length == 0.0:
            return (0.0, 0.0, 0.0)
        return tuple(cls.div(c, length) for c in v)

    @classmethod
    def transform(cls, v, m):
        return tuple(cls.add(cls.add(cls.add(cls.mul(v[0], m[0][c]), cls.mul(v[1], m[1][c])),
                                     cls.mul(v[2], m[2][c])), m[3][c]) for c in range(4))

    @classmethod
    def transform_normal(cls, v, m):
        return tuple(cls.add(cls.add(cls.mul(v[0], m[0][c]), cls.mul(v[1], m[1][c])),
                             cls.mul(v[2], m[2][c])) for c in range(3))

    @classmethod
    def inverse(cls, m):
        # The public BigWorld 3x4 cofactor inverse (lib/math/matrix.cpp).
        mul, sub, add = cls.mul, cls.sub, cls.add
        det = add(sub(mul(m[0][0], sub(mul(m[1][1], m[2][2]), mul(m[1][2], m[2][1]))),
                      mul(m[0][1], sub(mul(m[1][0], m[2][2]), mul(m[1][2], m[2][0])))),
                  mul(m[0][2], sub(mul(m[1][0], m[2][1]), mul(m[1][1], m[2][0]))))
        if det == 0.0:
            return None
        rcp = cls.div(1.0, det)
        r = [[0.0] * 4 for unused in range(4)]
        r[0][0] = sub(mul(m[1][1], m[2][2]), mul(m[1][2], m[2][1]))
        r[0][1] = sub(mul(m[0][2], m[2][1]), mul(m[0][1], m[2][2]))
        r[0][2] = sub(mul(m[0][1], m[1][2]), mul(m[0][2], m[1][1]))
        r[1][0] = sub(mul(m[1][2], m[2][0]), mul(m[1][0], m[2][2]))
        r[1][1] = sub(mul(m[0][0], m[2][2]), mul(m[0][2], m[2][0]))
        r[1][2] = sub(mul(m[0][2], m[1][0]), mul(m[0][0], m[1][2]))
        r[2][0] = sub(mul(m[1][0], m[2][1]), mul(m[1][1], m[2][0]))
        r[2][1] = sub(mul(m[0][1], m[2][0]), mul(m[0][0], m[2][1]))
        r[2][2] = sub(mul(m[0][0], m[1][1]), mul(m[0][1], m[1][0]))
        for i in range(3):
            for j in range(3):
                r[i][j] = mul(r[i][j], rcp)
        for j in range(3):
            r[3][j] = -add(add(mul(m[3][0], r[0][j]), mul(m[3][1], r[1][j])), mul(m[3][2], r[2][j]))
        r[3][3] = 1.0
        return r

    @classmethod
    def multiply(cls, a, b):
        return [[cls.add(cls.add(cls.add(cls.mul(a[i][0], b[0][j]), cls.mul(a[i][1], b[1][j])),
                                 cls.mul(a[i][2], b[2][j])), cls.mul(a[i][3], b[3][j]))
                 for j in range(4)] for i in range(4)]

    @classmethod
    def yaw_pitch_roll(cls, yaw, pitch, roll):
        mul, add, sub = cls.mul, cls.add, cls.sub
        sy, cy = cls.sinf(yaw), cls.cosf(yaw)
        sp, cp = cls.sinf(pitch), cls.cosf(pitch)
        sr, cr = cls.sinf(roll), cls.cosf(roll)
        return [[add(mul(cr, cy), mul(mul(sr, sp), sy)), mul(sr, cp),
                 sub(mul(mul(sr, sp), cy), mul(cr, sy)), 0.0],
                [sub(mul(mul(cr, sp), sy), mul(sr, cy)), mul(cr, cp),
                 add(mul(sr, sy), mul(mul(cr, sp), cy)), 0.0],
                [mul(cp, sy), -sp, mul(cp, cy), 0.0],
                [0.0, 0.0, 0.0, 1.0]]

    @classmethod
    def rotation_z(cls, angle):
        s, c = cls.sinf(angle), cls.cosf(angle)
        return [[c, s, 0.0, 0.0], [-s, c, 0.0, 0.0], [0.0, 0.0, 1.0, 0.0], [0.0, 0.0, 0.0, 1.0]]

    @classmethod
    def look_at(cls, eye, at, up):
        z = cls.normalize(tuple(cls.sub(at[i], eye[i]) for i in range(3)))
        x = cls.normalize(cls.cross(up, z))
        y = cls.cross(z, x)
        return [[x[0], y[0], z[0], 0.0], [x[1], y[1], z[1], 0.0], [x[2], y[2], z[2], 0.0],
                [-cls.dot(x, eye), -cls.dot(y, eye), -cls.dot(z, eye), 1.0]]

    @staticmethod
    def _unary(function, value):
        try:
            return f32(function(value))
        except (ValueError, OverflowError):
            return float('nan')

    @classmethod
    def sinf(cls, value):
        return cls._unary(math.sin, value)

    @classmethod
    def cosf(cls, value):
        return cls._unary(math.cos, value)

    @classmethod
    def tanf(cls, value):
        return cls._unary(math.tan, value)

    @classmethod
    def asinf(cls, value):
        return cls._unary(math.asin, value)

    @staticmethod
    def powf(base, exponent):
        try:
            return f32(math.pow(base, exponent))
        except (ValueError, OverflowError):
            return float('nan')


IMPORT_ARGUMENTS = {
    'D3DXVec3Normalize': 2, 'D3DXVec3Transform': 3, 'D3DXVec3TransformNormal': 3,
    'D3DXMatrixInverse': 3, 'D3DXMatrixRotationYawPitchRoll': 4, 'D3DXMatrixLookAtLH': 4,
    'D3DXMatrixRotationZ': 2, 'D3DXMatrixMultiply': 3,
}


class OriginalCursorCamera:
    """A bounded x86 process image with the #1513 executable mapped."""
    STACK = 0x10000000
    HEAP = 0x20000000
    STUBS = 0x30000000
    STOP = 0x3000fff0

    def __init__(self, executable):
        import pefile
        from unicorn import Uc, UC_ARCH_X86, UC_MODE_32, UC_HOOK_CODE
        from unicorn import x86_const as x86
        self.x86 = x86
        image = pefile.PE(str(executable))
        assert image.FILE_HEADER.Machine == 0x14c, 'The CursorCamera oracle requires the pinned x86 PE'
        base = image.OPTIONAL_HEADER.ImageBase
        mapped = image.get_memory_mapped_image()
        self.uc = Uc(UC_ARCH_X86, UC_MODE_32)
        self.uc.mem_map(base, (len(mapped) + 4095) & ~4095)
        self.uc.mem_write(base, mapped)
        self.uc.mem_map(0, 4096)
        self.uc.mem_map(self.STACK, 0x100000)
        self.uc.mem_map(self.HEAP, 0x1000000)
        self.uc.mem_map(self.STUBS, 0x10000)
        self.heap = self.HEAP
        self.imports = {}
        for library in image.DIRECTORY_ENTRY_IMPORT:
            for entry in library.imports:
                stub = self.STUBS + 0x1000 + len(self.imports) * 8
                name = entry.name.decode() if entry.name else str(entry.ordinal)
                self.imports[stub] = name
                self.uc.mem_write(entry.address, struct.pack('<I', stub))
                self.uc.mem_write(stub, b'\xc3')
        self.handlers = {}
        self.random = []
        self.calls = []
        self.uc.hook_add(UC_HOOK_CODE, self._import, begin=self.STUBS + 0x1000, end=self.STUBS + 0x7fff)
        self.float_return = self.STUBS + 0x100
        self.x87_stubs = {}
        self.entries = 0

    # Memory helpers.
    def alloc(self, size, data=None):
        address = self.heap
        self.heap += (size + 31) & ~31
        assert self.heap < self.HEAP + 0x1000000, 'Oracle heap exhausted'
        self.uc.mem_write(address, data if data is not None else bytes(size))
        return address

    def word(self, address):
        return struct.unpack('<I', self.uc.mem_read(address, 4))[0]

    def put_word(self, address, value):
        self.uc.mem_write(address, struct.pack('<I', value & 0xffffffff))

    def floats(self, address, count):
        return list(struct.unpack('<%df' % count, self.uc.mem_read(address, 4 * count)))

    def put_floats(self, address, values):
        self.uc.mem_write(address, struct.pack('<%df' % len(values), *values))

    def matrix(self, address):
        values = self.floats(address, 16)
        return [values[i * 4:i * 4 + 4] for i in range(4)]

    def put_matrix(self, address, value):
        self.put_floats(address, [c for row in value for c in row])

    def byte(self, address):
        return self.uc.mem_read(address, 1)[0]

    # Call boundaries.
    def _arg(self, index):
        return self.word(self.uc.reg_read(self.x86.UC_X86_REG_ESP) + 4 + 4 * index)

    def _return(self, value=0, cleanup=0):
        esp = self.uc.reg_read(self.x86.UC_X86_REG_ESP)
        self.uc.reg_write(self.x86.UC_X86_REG_EAX, value & 0xffffffff)
        self.uc.reg_write(self.x86.UC_X86_REG_EIP, self.word(esp))
        self.uc.reg_write(self.x86.UC_X86_REG_ESP, esp + 4 + cleanup)

    def _return_x87(self, value, cleanup):
        # fld dword [scratch]; ret cleanup
        stub = self.x87_stubs.get(cleanup)
        if stub is None:
            stub = self.STUBS + 0x200 + 16 * len(self.x87_stubs)
            self.uc.mem_write(stub, b'\xd9\x05' + struct.pack('<I', self.float_return) +
                              b'\xc2' + struct.pack('<H', cleanup))
            self.x87_stubs[cleanup] = stub
        self.put_floats(self.float_return, [value])
        self.uc.reg_write(self.x86.UC_X86_REG_EIP, stub)

    def _xmm(self, register):
        return unbits(self.uc.reg_read(register) & 0xffffffff)

    def _set_xmm0(self, value):
        self.uc.reg_write(self.x86.UC_X86_REG_XMM0, bits(value))

    def _import(self, uc, address, size, data):
        name = self.imports.get(address)
        if name is None:
            return
        x86 = self.x86
        m = CameraMath
        arg = self._arg
        if name in IMPORT_ARGUMENTS:
            count = IMPORT_ARGUMENTS[name]
            result = arg(0)
            if name == 'D3DXVec3Normalize':
                self.put_floats(arg(0), m.normalize(tuple(self.floats(arg(1), 3))))
            elif name == 'D3DXVec3Transform':
                self.put_floats(arg(0), m.transform(tuple(self.floats(arg(1), 3)), self.matrix(arg(2))))
            elif name == 'D3DXVec3TransformNormal':
                self.put_floats(arg(0), m.transform_normal(tuple(self.floats(arg(1), 3)),
                                                           self.matrix(arg(2))))
            elif name == 'D3DXMatrixInverse':
                assert arg(1) == 0, 'The camera never requests the determinant'
                value = m.inverse(self.matrix(arg(2)))
                if value is None:
                    result = 0
                else:
                    self.put_matrix(arg(0), value)
            elif name == 'D3DXMatrixRotationYawPitchRoll':
                self.put_matrix(arg(0), m.yaw_pitch_roll(unbits(arg(1)), unbits(arg(2)), unbits(arg(3))))
            elif name == 'D3DXMatrixLookAtLH':
                self.put_matrix(arg(0), m.look_at(tuple(self.floats(arg(1), 3)),
                                                  tuple(self.floats(arg(2), 3)),
                                                  tuple(self.floats(arg(3), 3))))
            elif name == 'D3DXMatrixRotationZ':
                self.put_matrix(arg(0), m.rotation_z(unbits(arg(1))))
            elif name == 'D3DXMatrixMultiply':
                self.put_matrix(arg(0), m.multiply(self.matrix(arg(1)), self.matrix(arg(2))))
            self._return(result, 4 * count)
            return
        if name.startswith('__libm_sse2_'):
            function = getattr(m, name[len('__libm_sse2_'):])
            first = self._xmm(x86.UC_X86_REG_XMM0)
            if name == '__libm_sse2_powf':
                value = function(first, self._xmm(x86.UC_X86_REG_XMM1))
            else:
                value = function(first)
            self._set_xmm0(value)
            self._return()
            return
        if name == 'rand':
            assert self.random, 'Original rand import has no scripted input'
            self._return(self.random.pop(0))
            return
        if name == 'memset':
            self.uc.mem_write(arg(0), bytes([arg(1) & 255]) * arg(2))
            self._return(arg(0))
            return
        if name in ('malloc', '_malloc_base'):
            self._return(self.alloc(arg(0)))
            return
        if name in ('memmove', 'memcpy'):
            self.uc.mem_write(arg(0), bytes(self.uc.mem_read(arg(1), arg(2))))
            self._return(arg(0))
            return
        if name in ('free', '_free_base'):
            self._return()
            return
        if name == 'InterlockedIncrement':
            value = self.word(arg(0)) + 1
            self.put_word(arg(0), value)
            self._return(value, 4)
            return
        if name == 'InterlockedDecrement':
            value = self.word(arg(0)) - 1
            self.put_word(arg(0), value)
            self._return(value, 4)
            return
        raise RuntimeError('Unscripted original import: ' + name)

    def handle(self, address, function):
        """Replace an internal entry point with a recorded boundary."""
        from unicorn import UC_HOOK_CODE

        def hook(uc, current, size, data):
            function(self)
        self.handlers[address] = self.uc.hook_add(UC_HOOK_CODE, hook, begin=address, end=address)

    def call(self, function, args=(), ecx=0):
        x86 = self.x86
        stack = self.STACK + 0xf0000
        self.uc.mem_write(stack, struct.pack('<' + 'I' * (1 + len(args)), self.STOP, *args))
        self.uc.reg_write(x86.UC_X86_REG_ESP, stack)
        self.uc.reg_write(x86.UC_X86_REG_EBP, stack + 0x400)
        self.uc.reg_write(x86.UC_X86_REG_ECX, ecx)
        self.uc.reg_write(x86.UC_X86_REG_FPCW, 0x37f)
        self.uc.reg_write(x86.UC_X86_REG_MXCSR, 0x1f80)
        self.uc.emu_start(function, self.STOP, count=20000000)
        return self.uc.reg_read(x86.UC_X86_REG_EAX)

    def vtable(self, *slots):
        return self.alloc(4 * len(slots), struct.pack('<%dI' % len(slots), *slots))

    def entry(self):
        """A distinct internal address that can carry a handler."""
        address = self.STUBS + 0x8000 + 16 * self.entries
        self.entries += 1
        self.uc.mem_write(address, b'\xcc')
        return address


# Original CursorCamera object layout recovered from 0079d430 and its users.
OFFSET = {
    'view': 0x18, 'invView': 0x58, 'sptHiding': 0xa4, 'pivot': 0xa8, 'targetPivot': 0xb4,
    'maxDist': 0xc0, 'targetMaxDist': 0xc4, 'maxDistHalfLife': 0xc8, 'minDist': 0xcc,
    'terrainMinDist': 0xd0, 'movementHalfLife': 0xd4, 'turningHalfLife': 0xd8, 'upright': 0xdc,
    'target': 0xe8, 'source': 0xec, 'firstPerson': 0xf0, 'reverseView': 0xf1,
    'inPosition': 0xf2, 'limitVelocity': 0xf3, 'maxVelocity': 0xf4, 'shakeAmount': 0xf8,
    'shakeTime': 0x104, 'shakeLeft': 0x108, 'inaccuracy': 0x10c, 'models': 0x110,
    'trees': 0x11c, 'yaw': 0x128, 'pitch': 0x12c, 'roll': 0x130, 'lastKnown': 0x134,
    'lastYaw': 0x138, 'lastPitch': 0x13c, 'lastRoll': 0x140, 'cameraPos': 0x144,
    'cameraDir': 0x150, 'stableView': 0x15c, 'stableInvView': 0x19c,
}
SIZE = 0x1dc
UPDATE = 0x79e110
SET_VIEW = 0x79db10
COLLIDE_MODELS = 0x79e2d0
SCENE_SPACE = 0x7a3770
SCENE_BOUNDS = 0x7a3ed0
WATERS = 0xa2cd40
WATER_DISTANCE = 0xa302e0
RENDER_CONTEXT = 0x1d380d4
CAMERA_OVERRIDE = 0x1d2a904
RENDERER = 0x1d20620
PIPELINE_HIDING_SLOT = 0x10c
APPLY_PARAMS = 0x79b8f0
SET_HIDING = 0x79df40
GET_HIDING = 0x79d1d0
CONSTRUCT = 0x79d430
DESTRUCT = 0x79b640
SET_MODELS = 0x79eb00
SET_MODELS_WRAPPER = 0x79b950
PY_NONE = 0x1a5c798
OPERATOR_NEW = 0x13d7fe9
PROVIDER_MATRIX = 0x79d3e0
PY_OBJECT_PLUS = 0x6a8520
PY_LIST_TYPE_FLAG = 0x2000000
PYMODEL_TYPE = 0x1a19088


# Branch targets inside 0079bb30 every update scenario set must reach.
UPDATE_BRANCHES = {
    0x79bd6d: 'smoothing pass', 0x79bdb0: 'pivot decay', 0x79bf2d: 'distance decay',
    0x79bfbe: 'desired angles changed', 0x79c24d: 'shake', 0x79c3a3: 'inaccuracy',
    0x79c52d: 'sphere snap', 0x79c593: 'sphere glide', 0x79c668: 'sphere locked',
    0x79c8be: 'water lift', 0x79c8de: 'first person', 0x79c9ce: 'distance glide',
    0x79ca01: 'range divisor', 0x79ca5f: 'plain half-life', 0x79cb0d: 'zero distance',
    0x79cb1a: 'limited velocity', 0x79cb84: 'velocity step', 0x79cbe6: 'velocity arrival',
    0x79cd01: 'teleport', 0x79cd3d: 'store desired', 0x79cec3: 'matrix destination',
    0x79db40: 'set view',
}


class CameraScene:
    """Explicit owners around one original CursorCamera object."""

    def __init__(self, emulator, near=0.5, fov=1.2, aspect=1.6):
        e = self.emulator = emulator
        self.allocations = []

        def new(x):
            self.allocations.append(x.alloc(x._arg(0)))
            x._return(self.allocations[-1])

        def py_object(x):
            this = x.uc.reg_read(x.x86.UC_X86_REG_ECX)
            x.put_word(this, 1)
            x.put_word(this + 4, x._arg(0))
            x._return(this, 8)
        e.handle(OPERATOR_NEW, new)
        e.handle(PY_OBJECT_PLUS, py_object)
        self.camera = None
        self.coverage = set()
        from unicorn import UC_HOOK_CODE
        for address in UPDATE_BRANCHES:
            e.uc.hook_add(UC_HOOK_CODE, lambda uc, a, size, data: self.coverage.add(a),
                          begin=address, end=address)
        context = e.alloc(0x400)
        e.put_word(RENDER_CONTEXT, context)
        e.put_floats(context + 0x374, [near, 800.0, fov, aspect])
        e.put_word(CAMERA_OVERRIDE, 0)
        self.hiding = []
        pipeline_table = e.alloc(0x200)
        hiding = e.entry()
        e.put_word(pipeline_table + PIPELINE_HIDING_SLOT, hiding)
        pipeline = e.alloc(16)
        e.put_word(pipeline, pipeline_table)
        renderer = e.alloc(0x20)
        e.put_word(renderer + 0x10, pipeline)
        e.put_word(RENDERER, renderer)
        e.handle(hiding, lambda x: (self.hiding.append((x._arg(0), x._arg(1) & 255)), x._return(0, 8)))
        self.providers = {}
        self.matrix_slot = e.entry()
        e.handle(self.matrix_slot, self._provide)
        self.waters = []
        self.water_vector = e.alloc(12)
        e.handle(WATERS, lambda x: x._return(self.water_vector))
        e.handle(WATER_DISTANCE, self._water)
        self.scripts = {'models': [], 'space': [], 'bounds': []}
        self.records = {'models': [], 'space': [], 'bounds': [], 'water': []}
        e.handle(COLLIDE_MODELS, self._models)
        e.handle(SCENE_SPACE, self._space)
        e.handle(SCENE_BOUNDS, self._bounds)

    def provider(self, label, value):
        """A MatrixProvider/Vector4Provider whose slot 2 writes `value`."""
        e = self.emulator
        address = e.alloc(16)
        e.put_word(address, e.vtable(0, 0, self.matrix_slot))
        self.providers[address] = (label, value)
        return address

    def set_provider(self, address, value):
        label = self.providers[address][0]
        self.providers[address] = (label, value)

    def _provide(self, e):
        label, value = self.providers[e.uc.reg_read(e.x86.UC_X86_REG_ECX)]
        if isinstance(value[0], (list, tuple)):
            e.put_matrix(e._arg(0), value)
        else:
            e.put_floats(e._arg(0), value)
        e._return(0, 4)

    def set_waters(self, depths):
        e = self.emulator
        self.waters = [e.alloc(16) for unused in depths]
        self.depths = dict(zip(self.waters, depths))
        array = e.alloc(4 * max(1, len(self.waters)))
        for index, water in enumerate(self.waters):
            e.put_word(array + 4 * index, water)
        e.uc.mem_write(self.water_vector, struct.pack('<3I', array, array + 4 * len(self.waters),
                                                      array + 4 * len(self.waters)))

    def _water(self, e):
        water = e.uc.reg_read(e.x86.UC_X86_REG_ECX)
        start = tuple(e.floats(e._arg(0), 3))
        end = tuple(e.floats(e._arg(1), 3))
        self.records['water'].append((self.waters.index(water), start, end, e._arg(2) & 255))
        e._return_x87(self.depths[water], 0xc)

    def _scripted(self, kind, position):
        result, delta = self.scripts[kind].pop(0) if self.scripts[kind] else (0, (0.0, 0.0, 0.0))
        e = self.emulator
        value = e.floats(position, 3)
        e.put_floats(position, [CameraMath.add(value[i], delta[i]) for i in range(3)])
        return result

    def _models(self, e):
        position, direction, up = e._arg(0), e._arg(1), e._arg(2)
        assert e.uc.reg_read(e.x86.UC_X86_REG_ECX) == self.camera
        self.records['models'].append((tuple(e.floats(position, 3)), tuple(e.floats(direction, 3)),
                                       tuple(e.floats(up, 3))))
        e._return(self._scripted('models', position), 0xc)

    def _space(self, e):
        args = [e._arg(i) for i in range(5)]
        self.records['space'].append(tuple(tuple(e.floats(a, 3)) for a in args[:4]) + (args[4],))
        e._return(self._scripted('space', args[0]))

    def _bounds(self, e):
        args = [e._arg(i) for i in range(4)]
        self.records['bounds'].append(tuple(tuple(e.floats(a, 3)) for a in args))
        e._return(self._scripted('bounds', args[0]))

    # Object state.
    def put(self, name, value):
        e = self.emulator
        address = self.camera + OFFSET[name]
        if isinstance(value, bool):
            e.uc.mem_write(address, bytes([int(value)]))
        elif isinstance(value, int):
            e.put_word(address, value)
        elif isinstance(value, float):
            e.put_floats(address, [value])
        elif isinstance(value[0], (list, tuple)):
            e.put_matrix(address, value)
        else:
            e.put_floats(address, list(value))

    def state(self):
        e = self.emulator
        c = self.camera
        return {
            'cameraPos': e.floats(c + OFFSET['cameraPos'], 3),
            'cameraDir': e.floats(c + OFFSET['cameraDir'], 3),
            'angles': e.floats(c + OFFSET['yaw'], 3),
            'pivot': e.floats(c + OFFSET['pivot'], 3),
            'maxDist': e.floats(c + OFFSET['maxDist'], 1),
            'flags': [e.byte(c + OFFSET[n]) for n in ('inPosition', 'limitVelocity', 'lastKnown')],
            'lastDesired': e.floats(c + OFFSET['lastYaw'], 3),
            'shakeLeft': e.floats(c + OFFSET['shakeLeft'], 1),
            'view': e.floats(c + OFFSET['view'], 16),
            'invView': e.floats(c + OFFSET['invView'], 16),
            'stableView': e.floats(c + OFFSET['stableView'], 16),
            'stableInvView': e.floats(c + OFFSET['stableInvView'], 16),
        }

    def construct(self):
        """Run the original factory constructor (0079d430) for a new camera."""
        self.allocations = []
        result = self.emulator.call(CONSTRUCT, (0,))
        assert result == self.allocations[0] + 4, 'The factory returns the PyObject view'
        self.camera = self.allocations[0]
        for key in self.records:
            self.records[key] = []
        self.hiding = []
        return self.camera

    def update(self, delta):
        return self.emulator.call(UPDATE, (bits(delta),), ecx=self.camera)

    def provided(self, inverse):
        """Read the camera matrix provider slot 0079d3e0 for view or inverse."""
        e = self.emulator
        provider = e.alloc(0x1c)
        e.put_word(provider + 0x10, self.camera)
        e.put_word(provider + 0x14, OFFSET['invView'] if inverse else OFFSET['view'])
        e.put_word(provider + 0x18, OFFSET['stableInvView'] if inverse else OFFSET['stableView'])
        out = e.alloc(64)
        e.call(PROVIDER_MATRIX, (out,), ecx=provider)
        return e.floats(out, 16)


# Camera state exchanged with the host harness, in protocol order.
FIELDS = (
    ('pivot', 'f', 3), ('targetPivot', 'f', 3), ('maxDist', 'f', 1), ('targetMaxDist', 'f', 1),
    ('maxDistHalfLife', 'f', 1), ('minDist', 'f', 1), ('movementHalfLife', 'f', 1),
    ('turningHalfLife', 'f', 1), ('upright', 'f', 3), ('firstPerson', 'b', 1),
    ('reverseView', 'b', 1), ('inPosition', 'b', 1), ('limitVelocity', 'b', 1),
    ('maxVelocity', 'f', 1), ('shakeAmount', 'f', 3), ('shakeTime', 'f', 1), ('shakeLeft', 'f', 1),
    ('yaw', 'f', 1), ('pitch', 'f', 1), ('roll', 'f', 1), ('lastKnown', 'b', 1),
    ('lastYaw', 'f', 1), ('lastPitch', 'f', 1), ('lastRoll', 'f', 1), ('cameraPos', 'f', 3),
    ('cameraDir', 'f', 3), ('sptHiding', 'b', 1))
IDENTITY = ((1.0, 0.0, 0.0, 0.0), (0.0, 1.0, 0.0, 0.0), (0.0, 0.0, 1.0, 0.0), (0.0, 0.0, 0.0, 1.0))


def hexes(values):
    return ' '.join('%08x' % bits(v) for v in values)


def flat(matrix):
    return [c for row in matrix for c in row]


def run_update(scene, scenario):
    """Run a scenario through the original update; return the camera's trace and host input."""
    e = scene.emulator
    camera = scene.construct()
    for name, value in scenario.get('camera', {}).items():
        scene.put(name, value)
    tokens = []
    for name, kind, count in FIELDS:
        address = camera + OFFSET[name]
        if kind == 'b':
            tokens.append(e.byte(address))
        else:
            tokens.extend(bits(v) for v in e.floats(address, count))
    frames = scenario['frames']
    first = frames[0]
    providers = {}
    for name in ('target', 'source'):
        if scenario.get(name, True):
            providers[name] = scene.provider(name, first.get(name, IDENTITY))
            scene.put(name, providers[name])
    if 'inaccuracy' in first:
        providers['inaccuracy'] = scene.provider('inaccuracy', first['inaccuracy'])
        scene.put('inaccuracy', providers['inaccuracy'])
    tokens += [int('target' in providers), int('source' in providers), int('inaccuracy' in providers),
               len(frames)]
    labels = {0: 0, providers.get('target', -1): 1}
    trace = []
    for frame in frames:
        tokens.append(bits(frame['dt']))
        if 'set' in frame:
            tokens += [1] + [bits(v) for v in flat(frame['set'])]
            view = e.alloc(64)
            e.put_matrix(view, frame['set'])
            e.call(SET_VIEW, (view,), ecx=camera)
        else:
            tokens.append(0)
        for name in ('target', 'source'):
            value = frame.get(name, IDENTITY)
            tokens.extend(bits(v) for v in flat(value))
            if name in providers:
                scene.set_provider(providers[name], value)
        inaccuracy = frame.get('inaccuracy', (0.0, 0.0, 0.0, 0.0))
        tokens.extend(bits(v) for v in inaccuracy)
        if 'inaccuracy' in providers:
            scene.set_provider(providers['inaccuracy'], inaccuracy)
        randoms = list(frame.get('random', ()))
        tokens += [len(randoms)] + randoms
        e.random = list(randoms)
        for kind in ('models', 'space', 'bounds'):
            result, delta = frame.get(kind, (0, (0.0, 0.0, 0.0)))
            tokens += [result] + [bits(v) for v in delta]
            scene.scripts[kind] = [(result, delta)]
        depths = frame.get('waters', ())
        tokens += [len(depths)] + [bits(v) for v in depths]
        scene.set_waters(depths)
        for key in scene.records:
            scene.records[key] = []
        scene.hiding = []
        scene.update(frame['dt'])
        assert not e.random, 'The original update consumed fewer random values than scripted'
        for record in scene.records['models']:
            trace.append('M ' + hexes(record[0] + record[1] + record[2]))
        for record in scene.records['space']:
            assert record[4] == 0xffffffff
            trace.append('C ' + hexes(record[0] + record[1] + record[2] + record[3]))
        for index, start, end, flag in scene.records['water']:
            assert flag == 0, 'The camera queries the non-alpha water distance'
            trace.append('W %d ' % index + hexes(start + end))
        for target, enabled in scene.hiding:
            trace.append('H %d %d' % (labels[target], enabled))
        state = scene.state()
        trace.append('S ' + hexes(state['cameraPos'] + state['cameraDir'] + state['angles'] +
                                  state['pivot'] + state['maxDist']) +
                     ' %d %d %d ' % tuple(state['flags']) +
                     hexes(state['lastDesired'] + state['shakeLeft']))
        for name in ('view', 'invView', 'stableView', 'stableInvView'):
            trace.append(name + ' ' + hexes(state[name]))
        trace.append('P ' + hexes(scene.provided(False) + scene.provided(True)))
    return trace, ' '.join(str(t) for t in tokens)


WORLD_TRANSFORM = 0x8f6080
BSP_SWEEP = 0x1178090


class ModelCollisionScene:
    """Original 0079e2d0 with PyModels, their bounds and BSP sweeps scripted."""

    def __init__(self, emulator, near=0.5, fov=1.2, aspect=1.6):
        e = self.emulator = emulator
        context = e.alloc(0x400)
        e.put_word(RENDER_CONTEXT, context)
        e.put_floats(context + 0x374, [near, 800.0, fov, aspect])
        self.bounds_slot = e.entry()
        e.handle(self.bounds_slot, self._bounds)
        e.handle(WORLD_TRANSFORM, self._world)
        e.handle(BSP_SWEEP, self._sweep)
        self.trace = []

    def _model_index(self, cxx):
        return self.cxx.index(cxx)

    def _world(self, e):
        index = self._model_index(e.uc.reg_read(e.x86.UC_X86_REG_ECX))
        self.trace.append('X %d' % index)
        e._return(self.worlds[index])

    def _bounds(self, e):
        index = self._model_index(e.uc.reg_read(e.x86.UC_X86_REG_ECX))
        self.trace.append('B %d %d' % (index, e._arg(1) & 255))
        bounds = self.scenario['models'][index].get('bounds')
        if bounds is not None:
            e.put_floats(e._arg(0), list(bounds[0]) + list(bounds[1]))
        e._return(0, 8)

    def _sweep(self, e):
        tree = self.trees.index(e.uc.reg_read(e.x86.UC_X86_REG_ECX))
        triangle = e.floats(e._arg(0), 9)
        flags = struct.unpack('<H', e.uc.mem_read(e._arg(0) + 36, 2))[0]
        translation = e.floats(e._arg(1), 3)
        call = self.sweeps[tree]
        self.sweeps[tree] += 1
        self.trace.append('T %d %d %d ' % (tree, call, flags) + hexes(triangle + translation))
        hits = self.scenario['models'][tree].get('hits', {}).get(call, ())
        code = bytearray()
        for hit in hits:
            address = e.alloc(40)
            e.put_floats(address, [c for vertex in hit for c in vertex])
            # mov ecx,[esp+0xc]; push 0; push hit; mov eax,[ecx]; call [eax+4]
            code += b'\x8b\x4c\x24\x0c\x6a\x00\x68' + struct.pack('<I', address) + b'\x8b\x01\xff\x50\x04'
        code += b'\xc2\x0c\x00'
        # Fresh code per sweep: translated blocks are not reliably invalidated.
        stub = e.alloc(len(code), bytes(code))
        e.uc.reg_write(e.x86.UC_X86_REG_EIP, stub)

    def run(self, scenario):
        e = self.emulator
        self.scenario = scenario
        self.trace = []
        camera = e.alloc(SIZE)
        models = scenario['models']
        self.cxx = []
        self.worlds = []
        self.trees = []
        self.sweeps = []
        for model in models:
            cxx = e.alloc(0x40)
            e.put_word(cxx, e.vtable(*([0] * 6 + [self.bounds_slot])))
            self.cxx.append(cxx)
            world = e.alloc(64)
            e.put_matrix(world, model['world'])
            self.worlds.append(world)
            self.trees.append(e.alloc(16))
            self.sweeps.append(0)
        owners = e.alloc(4 * max(1, len(models)), struct.pack('<%dI' % len(models), *[c + 4 for c in self.cxx]))
        trees = e.alloc(4 * max(1, len(models)), struct.pack('<%dI' % len(models), *self.trees))
        e.uc.mem_write(camera + OFFSET['models'], struct.pack('<3I', owners, owners + 4 * len(models),
                                                             owners + 4 * len(models)))
        e.uc.mem_write(camera + OFFSET['trees'], struct.pack('<3I', trees, trees + 4 * len(models),
                                                            trees + 4 * len(models)))
        ideal, direction, up = (e.alloc(12), e.alloc(12), e.alloc(12))
        e.put_floats(ideal, scenario['ideal'])
        e.put_floats(direction, scenario['direction'])
        e.put_floats(up, scenario['up'])
        result = e.call(COLLIDE_MODELS, (ideal, direction, up), ecx=camera) & 255
        self.trace.append('R %d ' % result + hexes(e.floats(ideal, 3)))
        return self.trace


def collision_tokens(scenario):
    tokens = [bits(v) for v in list(scenario['ideal']) + list(scenario['direction']) + list(scenario['up'])]
    tokens.append(len(scenario['models']))
    for model in scenario['models']:
        tokens += [bits(v) for v in flat(model['world'])]
        bounds = model.get('bounds')
        tokens.append(int(bounds is not None))
        if bounds is not None:
            tokens += [bits(v) for v in list(bounds[0]) + list(bounds[1])]
        hits = model.get('hits', {})
        tokens.append(len(hits))
        for call, triangles in sorted(hits.items()):
            tokens += [call, len(triangles)]
            for triangle in triangles:
                tokens += [bits(c) for vertex in triangle for c in vertex]
    return ' '.join(str(t) for t in tokens)


PY_ERR_SET_STRING = 0xfc3720
PY_RUNTIME_ERROR = 0x1a5da00
MODEL_DECOMPOSE_SLOT = 0x28


class ModelListScene:
    """Original 0079eb00 over scripted CPython lists and PyModel internals."""

    def __init__(self, emulator):
        e = self.emulator = emulator
        e.handle(OPERATOR_NEW, lambda x: x._return(x.alloc(x._arg(0))))
        e.handle(PY_ERR_SET_STRING, self._error)
        self.list_type = e.alloc(0x100)
        e.put_word(self.list_type + 0x54, PY_LIST_TYPE_FLAG)
        self.other_type = e.alloc(0x100)
        self.decompose = e.entry()
        e.handle(self.decompose, self._decompose)
        self.model_table = e.vtable(*([0] * (MODEL_DECOMPOSE_SLOT // 4) + [self.decompose]))
        self.camera = e.alloc(SIZE)
        self.errors = []
        self.trees = {}

    def _error(self, e):
        message = bytearray()
        address = e._arg(1)
        while e.uc.mem_read(address, 1)[0]:
            message += e.uc.mem_read(address, 1)
            address += 1
        self.errors.append((e._arg(0) == e.word(PY_RUNTIME_ERROR), message.decode()))
        e._return()

    def _decompose(self, e):
        e._return(self.trees.get(e.uc.reg_read(e.x86.UC_X86_REG_ECX), 0))

    def model(self, tree):
        """A PyModel whose top model decomposes to BSP `tree` (0 for none)."""
        e = self.emulator
        cxx = e.alloc(0x40)
        e.put_word(cxx + 4, 100)
        e.put_word(cxx + 8, PYMODEL_TYPE)
        top = e.alloc(16)
        e.put_word(top, self.model_table)
        e.put_word(top + 4, 1)
        self.trees[top] = tree
        super_model = e.alloc(16)
        e.put_word(super_model + 8, top)
        e.put_word(cxx + 0x24, super_model)
        return cxx + 4

    def other(self):
        e = self.emulator
        address = e.alloc(16)
        e.put_word(address, 100)
        e.put_word(address + 4, self.other_type)
        return address

    def sequence(self, items, is_list=True):
        e = self.emulator
        array = e.alloc(4 * max(1, len(items)), struct.pack('<%dI' % len(items), *items))
        address = e.alloc(20)
        e.uc.mem_write(address, struct.pack('<5I', 100, self.list_type if is_list else self.other_type,
                                            len(items), array, len(items)))
        return address

    def set_models(self, argument):
        """Call 0079eb00 as the wrapper does, with one reference handed over."""
        e = self.emulator
        self.errors = []
        before = e.word(argument) if argument else None
        e.call(SET_MODELS, (argument,), ecx=self.camera)
        if argument:
            assert e.word(argument) == before - 1, 'The by-value argument releases its reference once'
        return self.errors

    def lists(self):
        e = self.emulator
        result = []
        for name in ('models', 'trees'):
            begin, end = struct.unpack('<2I', e.uc.mem_read(self.camera + OFFSET[name], 8))
            result.append([e.word(begin + 4 * i) for i in range((end - begin) // 4)])
        return result


ARG_COUNT_ERROR = 0x68e100
SET_BOOL = 0x690570
GET_BOOL = 0x68ee00
BASE_DESTRUCT = 0x7968e0
TREES_DESTRUCT = 0xf56eb0
MODELS_DESTRUCT = 0x62c400
DEFERRED_HIDING = 0x9c5dd0
FORWARD_HIDING = 0x9c6a40
TREE_HIDING_TARGET = 0x1d59f74
HIDING_OWNER = 0x1d211b4
FOCUS_TARGETS = (0x1d38e44, 0x1d38830)
PIPELINE_HIDING_OWNER_SLOT = 0xac
HIDING_OWNER_FIELD = 0x1d0c


class ExtensionScene:
    """The original Python wrappers, destructor and renderer hiding slots."""

    def __init__(self, emulator):
        e = self.emulator = emulator
        self.events = []
        self.tuple_type = e.alloc(0x100)
        e.put_word(self.tuple_type + 0x54, 0x4000000)
        self.camera = e.alloc(SIZE)
        e.put_word(self.camera, 0x14d9560)
        e.handle(UPDATE, lambda x: (self.events.append(('update', x.uc.reg_read(x.x86.UC_X86_REG_ECX),
                                                         unbits(x._arg(0)))), x._return(0, 4)))
        e.handle(SET_MODELS, lambda x: (self.events.append(('set_models', x._arg(0))), x._return(0, 4)))
        e.handle(ARG_COUNT_ERROR, self._count_error)
        e.handle(SET_BOOL, self._set_bool)
        e.handle(GET_BOOL, lambda x: (self.events.append(('get_bool', x._arg(0) & 255)), x._return(0x4321)))
        e.handle(BASE_DESTRUCT, lambda x: (self.events.append(('base',)), x._return()))
        e.handle(TREES_DESTRUCT, lambda x: (self.events.append(('trees',)), x._return()))
        e.handle(MODELS_DESTRUCT, lambda x: (self.events.append(('models',)), x._return()))
        # rp() and its hiding slot, the shared renderer owner of 0079cf65.
        table = e.alloc(0x200)
        slot = e.entry()
        e.put_word(table + PIPELINE_HIDING_SLOT, slot)
        owner_slot = e.entry()
        e.put_word(table + PIPELINE_HIDING_OWNER_SLOT, owner_slot)
        self.pipeline = e.alloc(16)
        e.put_word(self.pipeline, table)
        renderer = e.alloc(0x20)
        e.put_word(renderer + 0x10, self.pipeline)
        e.put_word(RENDERER, renderer)
        e.handle(slot, lambda x: (self.events.append(('hiding', x._arg(0), x._arg(1) & 255)), x._return(0, 8)))
        self.hiding_owner = e.alloc(HIDING_OWNER_FIELD + 4)
        e.handle(owner_slot, lambda x: x._return(self.hiding_owner))
        self.set_bool = (0, 0)

    def _count_error(self, e):
        name = bytearray()
        address = e._arg(0)
        while e.uc.mem_read(address, 1)[0]:
            name += e.uc.mem_read(address, 1)
            address += 1
        self.events.append(('arg_count', name.decode(), e._arg(1), e._arg(2)))
        e._return(0)

    def _set_bool(self, e):
        result, value = self.set_bool
        e.uc.mem_write(e._arg(1), bytes([value]))
        self.events.append(('set_bool', e._arg(0)))
        e._return(result & 0xffffffff)

    def arguments(self, *items):
        e = self.emulator
        address = e.alloc(12 + 4 * len(items))
        e.uc.mem_write(address, struct.pack('<%dI' % (3 + len(items)), 100, self.tuple_type, len(items), *items))
        return address

    def call_wrapper(self, function, *items):
        self.events = []
        e = self.emulator
        none_references = e.word(PY_NONE)
        result = e.call(function, (self.camera + 4, self.arguments(*items), 0))
        return result, e.word(PY_NONE) - none_references, self.events

    def set_hiding(self, result, value):
        self.events = []
        self.set_bool = (result, value)
        returned = self.emulator.call(SET_HIDING, (self.camera + 4, 0x7777))
        return returned, self.emulator.byte(self.camera + OFFSET['sptHiding']), self.events

    def get_hiding(self, value):
        self.events = []
        self.emulator.uc.mem_write(self.camera + OFFSET['sptHiding'], bytes([value]))
        return self.emulator.call(GET_HIDING, (self.camera + 4,)), self.events

    def destruct(self):
        self.events = []
        self.emulator.call(DESTRUCT, (0,), ecx=self.camera)
        return self.events

    def renderer_slot(self, deferred, target, enabled, owner_present=True):
        """Run the real pipeline slot and return the renderer owners it wrote."""
        e = self.emulator
        e.put_word(HIDING_OWNER, self.hiding_owner if owner_present else 0)
        e.call(DEFERRED_HIDING if deferred else FORWARD_HIDING, (target, int(enabled)), ecx=self.pipeline)
        return (e.word(TREE_HIDING_TARGET), e.word(self.hiding_owner + HIDING_OWNER_FIELD),
                tuple(e.word(address) for address in FOCUS_TARGETS))

    def provider(self):
        e = self.emulator
        address = e.alloc(16)
        e.put_word(address + 4, 100)
        return address
