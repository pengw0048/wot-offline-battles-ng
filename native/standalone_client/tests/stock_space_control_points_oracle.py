"""Execute original #1513 control-point colour and UV instructions."""
import struct
import math


def colour_cases(executable, cases):
    from stock_tree_lod_oracle import OriginalTreeInstructionOracle
    oracle = OriginalTreeInstructionOracle(executable)
    owner, rgba = oracle.allocate(0x16c), oracle.allocate(16)
    output = []
    for value in cases:
        oracle.emulator.mem_write(owner, bytes(0x16c))
        oracle.emulator.mem_write(rgba, struct.pack('<4f', *value))
        oracle.call(0x737fd0, (rgba,), owner)
        output.append(struct.unpack('<I', oracle.emulator.mem_read(owner + 0x18, 4))[0])
    return output


def uv_cases(executable, cases):
    from stock_tree_lod_oracle import OriginalTreeInstructionOracle
    from unicorn.x86_const import UC_X86_REG_EDI, UC_X86_REG_EBP, UC_X86_REG_ESP
    oracle = OriginalTreeInstructionOracle(executable)
    owner, rect = oracle.allocate(0x16c), oracle.allocate(16)
    output = []
    for value in cases:
        oracle.emulator.mem_write(rect, struct.pack('<4f', *value))
        oracle.emulator.reg_write(UC_X86_REG_EDI, owner)
        oracle.emulator.reg_write(UC_X86_REG_EBP, oracle.stack)
        oracle.emulator.reg_write(UC_X86_REG_ESP, oracle.stack - 4096)
        # This bounded range starts after real texture lookup, whose ownership
        # is exercised separately by the production scene host.
        oracle.emulator.mem_write(oracle.stack + 0xc, struct.pack('<I', rect))
        oracle.emulator.emu_start(0x73832c, 0x738392, count=1000)
        output.append(bytes(oracle.emulator.mem_read(owner + 0x138, 16)))
    return output


def sound_positions(executable, cases):
    import stock_runtime_services_oracle as base
    import unicorn
    from unicorn.x86_const import UC_X86_REG_ECX, UC_X86_REG_EIP, UC_X86_REG_ESP
    machine = base.OriginalRuntimeServices(executable)
    u = machine.u
    owner, scene, audio = [base.MARKS + n for n in (0x20000, 0x21000, 0x22000)]
    u.mem_write(0x215aa05, b'\0')
    u.mem_write(scene, struct.pack('<I', audio))
    output = []
    def consume():
        assert u.reg_read(UC_X86_REG_ECX) == audio
        assert machine._arg(1) == owner + 0xe4
        output.append(struct.unpack('<3f', u.mem_read(machine._arg(2), 12)))
        machine._return(0)
        u.reg_write(UC_X86_REG_ESP, u.reg_read(UC_X86_REG_ESP) + 8)
    u.hook_add(unicorn.UC_HOOK_CODE, lambda uc, a, size, data: consume(), begin=0x124a700, end=0x124a700)
    u.hook_add(unicorn.UC_HOOK_CODE, lambda uc, a, size, data: machine._return(0), begin=0x13d809e, end=0x13d809e)
    for translation, bone in cases:
        u.mem_write(owner + 0x68, struct.pack('<3f', *translation))
        u.mem_write(owner + 0x154, struct.pack('<3f', *bone))
        stack = base.STACK + 0x80000
        u.mem_write(stack, struct.pack('<2I', base.SENTINEL, scene))
        u.reg_write(UC_X86_REG_ECX, owner); u.reg_write(UC_X86_REG_ESP, stack)
        u.emu_start(0x736f90, base.SENTINEL + 1, count=1000)
        assert u.reg_read(UC_X86_REG_EIP) == base.SENTINEL
    return output


class OriginalRadiusProjection:
    """Run the whole 7395f0 vertex/triangle consumer with native service seams.

    Resource bytes, all original iteration/branch/filter instructions remain
    unchanged. Terrain/water queries and imported D3DX primitives are bounded
    fixture inputs; this is not real Windows terrain or rasterisation proof.
    """
    def __init__(self, executable):
        import stock_runtime_services_oracle as base
        import unicorn
        from unicorn.x86_const import UC_X86_REG_ECX, UC_X86_REG_EIP, UC_X86_REG_ESP
        self.base = base
        self.machine = base.OriginalRuntimeServices(executable)
        self.u = u = self.machine.u
        self.owner, self.capture, self.target, self.original = [base.MARKS + n for n in (0x10000, 0x11000, 0x20000, 0x30000)]
        self.matrix, self.inverse, self.indirect = [base.MARKS + n for n in (0x12000, 0x12100, 0x12200)]
        self.primitive, self.index_reference, self.index_data = [base.MARKS + n for n in (0x13000, 0x13100, 0x40000)]
        self.space, self.scalar, self.thunk, self.atomic = [base.MARKS + n for n in (0x14000, 0x15000, 0x15100, 0x15200)]
        self.success, self.range = base.MARKS + 0x15300, base.MARKS + 0x15400
        self.indices = []
        f = base.float32
        def ret(value=0, pop=0):
            self.machine._return(value)
            if pop: u.reg_write(UC_X86_REG_ESP, u.reg_read(UC_X86_REG_ESP) + pop)
        def transform():
            output, point, matrix = [self.machine._arg(i) for i in (1, 2, 3)]
            v = struct.unpack('<3f', u.mem_read(point, 12)) + (1.,)
            m = struct.unpack('<16f', u.mem_read(matrix, 64))
            result = [f(f(f(f(v[0]*m[c])+f(v[1]*m[4+c]))+f(v[2]*m[8+c]))+m[12+c]) for c in range(4)]
            u.mem_write(output, struct.pack('<4f', *result)); ret(output, 12)
        def normalise():
            output, point = [self.machine._arg(i) for i in (1, 2)]
            v = struct.unpack('<3f', u.mem_read(point, 12))
            length = f(math.sqrt(f(f(f(v[0]*v[0])+f(v[1]*v[1]))+f(v[2]*v[2]))))
            result = [f(x/length) for x in v] if length else [0., 0., 0.]
            u.mem_write(output, struct.pack('<3f', *result)); ret(output, 8)
        def current_space():
            output = self.machine._arg(1)
            u.mem_write(output, struct.pack('<I', self.space)); ret(output)
        def terrain():
            point = struct.unpack('<3f', u.mem_read(self.machine._arg(1), 12))
            self.float_result(f(10. + f(point[0] * self.slope)), 4)
        def copy():
            target, source, size = [self.machine._arg(i) for i in (1, 2, 3)]
            u.mem_write(target, bytes(u.mem_read(source, size))); ret(target)
        def append():
            self.indices.append(struct.unpack('<I', u.mem_read(self.machine._arg(1), 4))[0]); ret(0, 4)
        hooks = {0x13dac17: copy, 0x11c3bdc: transform, 0x11c3bbe: normalise,
                 0xac2f10: current_space, 0xac2e20: terrain,
                 0xf645e0: lambda: self.float_result(-1., 0),
                 0x9a9650: lambda: ret(self.index_reference), 0xa041b0: append,
                 0x13d809e: lambda: ret(), self.atomic: lambda: ret(1, 4)}
        u.mem_write(0x14913a0, struct.pack('<I', self.atomic))
        for address, callback in hooks.items():
            u.hook_add(unicorn.UC_HOOK_CODE, lambda uc, a, size, data, callback=callback: callback(), begin=address, end=address)
        self.ret = ret
        self.ecx, self.eip, self.esp = UC_X86_REG_ECX, UC_X86_REG_EIP, UC_X86_REG_ESP

    def float_result(self, value, cleanup):
        self.u.mem_write(self.scalar, struct.pack('<f', value))
        # Distinct immutable return thunks avoid Unicorn reusing a translated
        # RET 4 block for the adjacent cdecl water query's RET instruction.
        thunk = self.thunk + cleanup * 16
        self.u.mem_write(thunk, b'\xd9\x05' + struct.pack('<I', self.scalar) +
                         (b'\xc2' + struct.pack('<H', cleanup) if cleanup else b'\xc3'))
        self.u.reg_write(self.eip, thunk)

    def project(self, vertices, stride, indices, world, inverse, height, slope):
        u, f, base = self.u, self.base.float32, self.base
        self.indices = []; self.slope = f(slope)
        u.mem_write(self.original, vertices)
        u.mem_write(self.owner, bytes(0x158))
        u.mem_write(self.owner + 0xc, struct.pack('<II', stride, len(vertices)//stride))
        u.mem_write(self.owner + 0x38, struct.pack('<I', 0xffffffff))
        u.mem_write(self.owner + 0x84, struct.pack('<16f', *world))
        u.mem_write(self.owner + 0x128, struct.pack('<f', height))
        u.mem_write(self.owner + 0x150, struct.pack('<I', self.primitive))
        u.mem_write(self.primitive + 0x20, struct.pack('<I', len(indices)))
        u.mem_write(self.index_data, struct.pack('<%dH' % len(indices), *indices))
        u.mem_write(self.index_reference, struct.pack('<4I', 0, self.index_data, len(indices), 0))
        u.mem_write(self.inverse, struct.pack('<16f', *inverse))
        u.mem_write(self.success, b'\1\1')
        u.mem_write(self.range, struct.pack('<2f', 3.402823466e38, -3.402823466e38))
        u.mem_write(self.capture, struct.pack('<7I', self.owner, self.success, self.range, self.original,
                                            self.inverse, self.success + 1, self.indirect))
        # The original format lookup identifies POSITION at byte zero in the
        # exact CheckPoint stream. A missing POSITION would skip this filter.
        u.mem_write(self.owner + 0x38, struct.pack('<I', 0))
        u.mem_write(self.owner + 0x12c, struct.pack('<f', -1.))
        stack = base.STACK + 0x80000
        u.mem_write(stack, struct.pack('<3I', base.SENTINEL, self.target, self.target + len(vertices)))
        u.reg_write(self.esp, stack); u.reg_write(self.ecx, self.capture)
        u.emu_start(0x7395f0, base.SENTINEL + 1, count=3000000)
        assert u.reg_read(self.eip) == base.SENTINEL
        assert bytes(u.mem_read(self.success, 1)) == b'\1'
        return bytes(u.mem_read(self.target, len(vertices))), list(self.indices)
