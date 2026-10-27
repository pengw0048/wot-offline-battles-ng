"""Execute the #1513 gun-marker provider arithmetic directly from WorldOfTanks.exe.

Size filter, SPG blend, trajectory subdivision, per-element trajectory and the
conic cone run as original x86 instructions. Harness boundaries are explicit:
operator new/delete, CRT memory imports, D3DX imports (served by the
production core's portable D3DX operations), the f5d160 scene query (a test
collider) and the render-context view-projection global.
"""
import ctypes
import struct

STOP = 0x30000000
STUB = 0x3000f000
FLOAT_OUT = 0x3000f300
STACK_TOP = 0x10080000
HEAP = 0x20100000
RENDER_CONTEXT = 0x20000000
RC_GLOBAL = 0x1d380d4

SET_START, UPDATE_SIZE, CLIP_SCALE = 0x13eca80, 0x13ecfb0, 0x13ec360
SPG_ADVANCE, TRAJECTORY_STEP, SIMULATE, CONIC = 0x13ecda0, 0x13ea7c0, 0x13ebdc0, 0x13ec050
OPERATOR_NEW, OPERATOR_DELETE, SEGMENT_COLLISION = 0x13d7fe9, 0x13d8019, 0xf5d160
# Return addresses of 13ea7c0's two D3DXVec4Transform calls with pOut/pV swapped.
SWAPPED_TRANSFORMS = (0x13eac18, 0x13eacc7)


def f32(value):
    return struct.unpack('<f', struct.pack('<f', value))[0]


def bits(value):
    return struct.unpack('<I', struct.pack('<f', value))[0]


class OriginalProviders:
    def __init__(self, executable, core):
        import pefile
        from unicorn import Uc, UC_ARCH_X86, UC_MODE_32, UC_HOOK_CODE
        from unicorn import x86_const as x86
        self.x86 = x86
        self.core = core
        pe = pefile.PE(str(executable))
        assert pe.FILE_HEADER.Machine == 0x14c, 'The oracle requires the pinned x86 PE'
        base = pe.OPTIONAL_HEADER.ImageBase
        image = pe.get_memory_mapped_image()
        u = self.u = Uc(UC_ARCH_X86, UC_MODE_32)
        u.mem_map(base, (len(image) + 4095) & ~4095)
        u.mem_write(base, image)
        u.mem_map(0, 4096)
        u.mem_map(0x10000000, 0x80000)
        u.mem_map(0x20000000, 0x4000000)
        u.mem_map(0x30000000, 0x10000)
        # Float returns: fstp dword [FLOAT_OUT]; push STOP; ret.
        u.mem_write(STUB, b'\xd9\x1d' + struct.pack('<I', FLOAT_OUT) + b'\x68' +
                    struct.pack('<I', STOP) + b'\xc3')
        self.imports = {}
        for directory in pe.DIRECTORY_ENTRY_IMPORT:
            for entry in directory.imports:
                stub = 0x30001000 + len(self.imports) * 8
                self.imports[stub] = entry.name.decode() if entry.name else str(entry.ordinal)
                u.mem_write(entry.address, struct.pack('<I', stub))
                u.mem_write(stub, b'\xc3')
        self.heap = HEAP
        self.collider = None
        self.collisions = 0
        self.d3dx = {}
        u.hook_add(UC_HOOK_CODE, self._import, begin=0x30001000, end=0x3000efff)
        for address in (OPERATOR_NEW, OPERATOR_DELETE, SEGMENT_COLLISION):
            u.hook_add(UC_HOOK_CODE, self._internal, begin=address, end=address)
        self.write(RC_GLOBAL, struct.pack('<I', RENDER_CONTEXT))

    # --- memory -----------------------------------------------------------------
    def allocate(self, size):
        address = self.heap
        self.heap += (size + 31) & ~31
        assert self.heap < 0x23f00000, 'Original provider exceeded the bounded heap'
        self.u.mem_write(address, bytes(size))
        return address

    def write(self, address, data):
        self.u.mem_write(address, data)

    def read(self, address, size):
        return bytes(self.u.mem_read(address, size))

    def word(self, address):
        return struct.unpack('<I', self.read(address, 4))[0]

    def floats(self, address, count):
        return list(struct.unpack('<%df' % count, self.read(address, 4 * count)))

    def put_floats(self, address, values):
        self.write(address, struct.pack('<%df' % len(values), *values))

    def vector(self, values):
        """A BW::vector<Vector3> {begin, end, capacity} for the given points."""
        header = self.allocate(12)
        data = self.allocate(max(1, len(values)) * 12)
        for i, v in enumerate(values):
            self.put_floats(data + 12 * i, v)
        end = data + 12 * len(values)
        self.write(header, struct.pack('<III', data, end, end))
        return header

    def vector_points(self, header):
        begin, end, _ = struct.unpack('<III', self.read(header, 12))
        count = (end - begin) // 12
        return [tuple(self.floats(begin + 12 * i, 3)) for i in range(count)]

    def set_view_projection(self, matrix):
        self.put_floats(RENDER_CONTEXT + 0x374 + 0x118, matrix)

    # --- hooks ------------------------------------------------------------------
    def _return(self, value=0, cleanup=0):
        x86 = self.x86
        esp = self.u.reg_read(x86.UC_X86_REG_ESP)
        self.u.reg_write(x86.UC_X86_REG_EAX, value & 0xffffffff)
        self.u.reg_write(x86.UC_X86_REG_ESP, esp + 4 + cleanup)
        self.u.reg_write(x86.UC_X86_REG_EIP, self.word(esp))

    def _arg(self, n):
        return self.word(self.u.reg_read(self.x86.UC_X86_REG_ESP) + 4 + 4 * n)

    def _float_arg(self, n):
        return struct.unpack('<f', struct.pack('<I', self._arg(n)))[0]

    def _import(self, uc, address, size, data):
        name = self.imports.get(address)
        if name is None:
            return
        a = self._arg
        c = ctypes.c_float
        def buf(n, address=None):
            values = self.floats(address, n) if address else [0.0] * n
            return (c * n)(*values)
        if name in ('malloc', '_malloc_base'):
            return self._return(self.allocate(a(0)))
        if name in ('free', '_free_base'):
            return self._return()
        if name in ('memmove', 'memcpy'):
            self.write(a(0), self.read(a(1), a(2)))
            return self._return(a(0))
        if name == 'memset':
            self.write(a(0), bytes([a(1) & 255]) * a(2))
            return self._return(a(0))
        self.d3dx[name] = self.d3dx.get(name, 0) + 1
        if name == 'D3DXVec4Transform':
            out = buf(4)
            if self.word(self.u.reg_read(self.x86.UC_X86_REG_ESP)) in SWAPPED_TRANSFORMS:
                # 13ea7c0 passes (endpoint, result, M): read the endpoint it built
                # and return the result where it reads it, as the port does.
                self.core.d3dx_vec4_transform(out, buf(4, a(0)), buf(16, a(2)))
                self.put_floats(a(1), list(out))
            else:
                self.core.d3dx_vec4_transform(out, buf(4, a(1)), buf(16, a(2)))
                self.put_floats(a(0), list(out))
        elif name == 'D3DXVec3Normalize':
            out = buf(3)
            self.core.d3dx_normalize(out, buf(3, a(1)))
            self.put_floats(a(0), list(out))
        elif name == 'D3DXMatrixLookAtLH':
            out = buf(16)
            self.core.d3dx_lookat(out, buf(3, a(1)), buf(3, a(2)), buf(3, a(3)))
            self.put_floats(a(0), list(out))
        elif name == 'D3DXMatrixInverse':
            out = buf(16)
            self.core.d3dx_inverse(out, buf(16, a(2)))
            self.put_floats(a(0), list(out))
        elif name in ('D3DXMatrixRotationX', 'D3DXMatrixRotationZ'):
            out = buf(16)
            fn = self.core.d3dx_rotation_x if name.endswith('X') else self.core.d3dx_rotation_z
            fn(out, c(self._float_arg(1)))
            self.put_floats(a(0), list(out))
        elif name == 'D3DXVec3TransformNormal':
            out = buf(3)
            self.core.d3dx_transform_normal(out, buf(3, a(1)), buf(16, a(2)))
            self.put_floats(a(0), list(out))
        else:
            raise RuntimeError('unsupported import %s' % name)
        # d3dx9 functions are stdcall and return their output pointer.
        cleanup = {'D3DXVec4Transform': 12, 'D3DXVec3Normalize': 8, 'D3DXMatrixLookAtLH': 16,
                   'D3DXMatrixInverse': 12, 'D3DXMatrixRotationX': 8, 'D3DXMatrixRotationZ': 8,
                   'D3DXVec3TransformNormal': 12}[name]
        self._return(a(0), cleanup)

    def _internal(self, uc, address, size, data):
        if address == OPERATOR_NEW:
            return self._return(self.allocate(self._arg(0)))
        if address == OPERATOR_DELETE:
            return self._return()
        # f5d160(hit*, start*, end*, flags, flags2) -> al, cdecl.
        self.collisions += 1
        hit, start, end, flags, flags2 = (self._arg(i) for i in range(5))
        assert (flags, flags2) == (0x80, 0), (flags, flags2)
        point = self.collider(tuple(self.floats(start, 3)), tuple(self.floats(end, 3)))
        if point is not None:
            self.put_floats(hit, point)
        self._return(1 if point is not None else 0)

    # --- calls ------------------------------------------------------------------
    def call(self, function, args=(), ecx=0, float_result=False):
        x86 = self.x86
        stack = STACK_TOP - 0x1000
        ret = STUB if float_result else STOP
        self.write(stack, struct.pack('<%dI' % (1 + len(args)), ret, *args))
        self.u.reg_write(x86.UC_X86_REG_ESP, stack)
        self.u.reg_write(x86.UC_X86_REG_EBP, stack + 0x800)
        self.u.reg_write(x86.UC_X86_REG_ECX, ecx)
        self.u.reg_write(x86.UC_X86_REG_FPCW, 0x27f)
        self.u.reg_write(x86.UC_X86_REG_MXCSR, 0x1f80)
        self.u.emu_start(function, STOP, count=50000000)
        if float_result:
            return self.floats(FLOAT_OUT, 1)[0]
        return self.u.reg_read(x86.UC_X86_REG_EAX)

    # Default provider object: +0xc min, +0x10 max, +0x1c current .. +0x2c rate.
    def size_object(self, fields):
        obj = self.allocate(0x30)
        minimum, maximum, current, start, target, elapsed, rate = fields
        self.put_floats(obj + 0xc, [minimum, maximum])
        self.put_floats(obj + 0x1c, [current, start, target, elapsed, rate])
        return obj

    def size_fields(self, obj):
        return self.floats(obj + 0xc, 2) + self.floats(obj + 0x1c, 5)

    def set_start(self, fields, size):
        obj = self.size_object(fields)
        self.call(SET_START, (bits(size),), obj)
        return self.size_fields(obj)

    def update_size(self, fields, size, relax):
        obj = self.size_object(fields)
        self.call(UPDATE_SIZE, (bits(size), bits(relax)), obj)
        return self.size_fields(obj)

    def clip_scale(self, fields, dt, clip):
        obj = self.size_object(fields)
        value = self.call(CLIP_SCALE, (bits(dt), bits(clip)), obj, float_result=True)
        return value, self.size_fields(obj)

    # SPG provider object (0xd0 bytes) for advance/simulate/conic.
    def spg_object(self, count, fields, current, previous, output):
        obj = self.allocate(0xd0)
        (elapsed, relax_rate, max_time, tick, scale_rate, size, previous_size,
         output_size, angle, smooth, alpha, beta, filtered_angle, filtered_rate) = fields
        self.put_floats(obj + 0xc, [5.0, 300.0])
        self.write(obj + 0x1c, struct.pack('<I', count))
        self.put_floats(obj + 0x20, [elapsed, relax_rate, max_time, tick, scale_rate,
                                     size, previous_size, output_size])
        for offset, points in ((0x40, current), (0x4c, previous), (0x58, output)):
            header = self.vector(points)
            self.write(obj + offset, self.read(header, 12))
        self.write(obj + 0xb4, struct.pack('<I', 0))
        self.put_floats(obj + 0xb8, [angle])
        self.write(obj + 0xbc, bytes([1 if smooth else 0]))
        self.put_floats(obj + 0xc0, [alpha, beta, filtered_angle, filtered_rate])
        return obj

    def spg_points(self, obj, offset, count):
        begin = self.word(obj + offset)
        return [tuple(self.floats(begin + 12 * i, 3)) for i in range(count)]

    def spg_advance(self, count, fields, current, previous, output, dt, limits):
        obj = self.spg_object(count, fields, current, previous, output)
        self.put_floats(obj + 0xc, list(limits))
        ready = self.call(SPG_ADVANCE, (bits(dt),), obj) & 255
        return ready, self.floats(obj + 0x3c, 1)[0], self.spg_points(obj, 0x58, count)

    def trajectory_step(self, position, velocity, gravity, dt, refined, view_projection):
        self.set_view_projection(view_projection)
        out = self.allocate(24)
        points = self.vector([])
        self.write(points, struct.pack('<III', 0, 0, 0))
        args = [self.allocate(12) for _ in range(3)]
        for address, value in zip(args, (position, velocity, gravity)):
            self.put_floats(address, value)
        self.call(TRAJECTORY_STEP, (out, points, args[0], args[1], args[2], bits(dt),
                                    1 if refined else 0))
        return self.vector_points(points), self.floats(out, 6)

    def simulate(self, count, fields, index, position, velocity, gravity, refined,
                 view_projection, collider):
        self.set_view_projection(view_projection)
        self.collider = collider
        zero = [(0.0, 0.0, 0.0)] * count
        obj = self.spg_object(count, fields, zero, zero, zero)
        self.write(obj + 0x64, struct.pack('<III', 0, 0, 0))
        args = [self.allocate(12) for _ in range(3)]
        for address, value in zip(args, (position, velocity, gravity)):
            self.put_floats(address, value)
        self.call(SIMULATE, (index, args[0], args[1], args[2], 1 if refined else 0), obj)
        return self.spg_points(obj, 0x40, count)[index]

    def conic(self, count, fields, position, velocity, gravity, angle, view_projection,
              collider):
        self.set_view_projection(view_projection)
        self.collider = collider
        zero = [(0.0, 0.0, 0.0)] * count
        obj = self.spg_object(count, fields, zero, zero, zero)
        self.write(obj + 0x64, struct.pack('<III', 0, 0, 0))
        args = [self.allocate(12) for _ in range(3)]
        for address, value in zip(args, (position, velocity, gravity)):
            self.put_floats(address, value)
        self.call(CONIC, (args[0], args[1], args[2], bits(angle)), obj)
        return self.spg_points(obj, 0x40, count)
