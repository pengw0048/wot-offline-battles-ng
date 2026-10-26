"""Execute original #1513 projected-LOD functions with explicit D3DX inputs.

The threshold function executes unchanged. The metric's sole external boundary
is D3DXVec3Transform: this audit supplies finite affine matrix multiplication,
with binary32 operations, for the actual vectors passed by the original body.
This proves scalar native math, not a Windows D3D rendering path.
"""
import math
import struct


def original_lod_outputs(executable, thresholds, metrics):
    from unicorn import UC_HOOK_CODE
    from unicorn.x86_const import (UC_X86_REG_FP7, UC_X86_REG_FPSW, UC_X86_REG_FPTAG,
                                   UC_X86_REG_ESP, UC_X86_REG_EIP, UC_X86_REG_EAX)
    from stock_tree_lod_oracle import OriginalTreeInstructionOracle
    oracle = OriginalTreeInstructionOracle(executable)
    emulator = oracle.emulator
    bounds, world, view = oracle.allocate(24), oracle.allocate(64), oracle.allocate(64)
    stub = 0x3000ff00
    emulator.mem_write(0x149216c, struct.pack('<I', stub))
    emulator.mem_write(stub, b'\xc3')
    transformed = 0

    def f32(value):
        return struct.unpack('<f', struct.pack('<f', value))[0]

    def transform_boundary(uc, address, size, context):
        nonlocal transformed
        assert address == stub
        transformed += 1
        esp = uc.reg_read(UC_X86_REG_ESP)
        result, point, matrix = [oracle.word(esp + offset) for offset in (4, 8, 12)]
        p = struct.unpack('<3f', uc.mem_read(point, 12))
        m = struct.unpack('<16f', uc.mem_read(matrix, 64))
        values = [f32(f32(f32(f32(p[0] * m[column]) + f32(p[1] * m[4 + column])) +
                            f32(p[2] * m[8 + column])) + m[12 + column]) for column in range(4)]
        uc.mem_write(result, struct.pack('<4f', *values))
        uc.reg_write(UC_X86_REG_EAX, result)
        uc.reg_write(UC_X86_REG_EIP, oracle.word(esp))
        uc.reg_write(UC_X86_REG_ESP, esp + 16)

    emulator.hook_add(UC_HOOK_CODE, transform_boundary, begin=stub, end=stub)

    def call(function, arguments):
        # Each test represents a caller consuming the x87 single result. Clear
        # its tag/top state before the next independent original invocation.
        emulator.reg_write(UC_X86_REG_FPSW, 0)
        emulator.reg_write(UC_X86_REG_FPTAG, 0xffff)
        oracle.call(function, arguments)
        mantissa, exponent = emulator.reg_read(UC_X86_REG_FP7)
        value = math.ldexp(mantissa / (1 << 63), (exponent & 0x7fff) - 16383)
        if exponent & 0x8000:
            value = -value
        return struct.pack('<f', value)

    outputs = bytearray()
    word = lambda value: struct.unpack('<I', struct.pack('<f', value))[0]
    for box, squared in thresholds:
        emulator.mem_write(bounds, struct.pack('<6f', *box))
        outputs.extend(call(0xa36b60, (bounds, word(math.sqrt(squared)))))
    for box, world_matrix, view_matrix, scale, projection_x, projection_y in metrics:
        emulator.mem_write(bounds, struct.pack('<6f', *box))
        emulator.mem_write(world, struct.pack('<16f', *world_matrix))
        emulator.mem_write(view, struct.pack('<16f', *view_matrix))
        outputs.extend(call(0xa36840, (view, view, word(scale), bounds, world,
                                      word(projection_x), word(projection_y))))
    assert transformed == 2 * sum(case[3] > 0 for case in metrics)
    return bytes(outputs)
