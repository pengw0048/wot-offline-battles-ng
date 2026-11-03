"""Execute #1513 9cded0 with mathematical D3DX import boundaries.

The instruction body owns PCA, hull traversal, area selection and axis order.
Only Vec2Normalize, MatrixTranspose, Vec4Transform and the security-cookie
check are adapted; this is an arithmetic oracle, not native D3DX/GPU evidence.
"""
import math
from pathlib import Path
import random
import struct


def binary32(value):
    return struct.unpack('<f', struct.pack('<f', value))[0]


def projected_focus_boxes(count=250):
    generator = random.Random(1513)
    for unused in range(count):
        x, y, z = [generator.uniform(-1, 1) for unused_axis in range(3)]
        length = math.sqrt(x*x + y*y + z*z)
        x, y, z = x/length, y/length, z/length
        right = (z, 0, -x)
        length = math.hypot(right[0], right[2])
        right = tuple(value/length for value in right)
        up = (y*right[2], z*right[0]-x*right[2], -y*right[0])
        half_x, half_y, half_z = (generator.uniform(1, 30),
                                  generator.uniform(.5, 3), generator.uniform(1, 30))
        center_x, center_y = generator.uniform(-100, 100), generator.uniform(-100, 100)
        yield [(binary32(center_x + sx*half_x*right[0] + sy*half_y*right[1] + sz*half_z*right[2]),
                binary32(center_y + sx*half_x*up[0] + sy*half_y*up[1] + sz*half_z*up[2]))
               for sz in (1, -1) for sy in (1, -1) for sx in (1, -1)]


def original_rectangles(client, cases):
    import pefile
    from unicorn import Uc, UC_ARCH_X86, UC_MODE_32, UC_HOOK_CODE
    from unicorn.x86_const import (UC_X86_REG_ESP, UC_X86_REG_EIP,
                                   UC_X86_REG_EAX, UC_X86_REG_MXCSR)

    pe = pefile.PE(str(Path(client) / 'WorldOfTanks.exe'))
    if pe.FILE_HEADER.Machine != 0x14c:
        raise ValueError('The original rectangle oracle requires the pinned x86 client')
    image = pe.get_memory_mapped_image()
    base = pe.OPTIONAL_HEADER.ImageBase
    uc = Uc(UC_ARCH_X86, UC_MODE_32)
    uc.mem_map(base, (len(image)+4095) & ~4095)
    uc.mem_write(base, image)
    memory, stack, end = 0x30000000, 0x40000000, 0x30008000
    uc.mem_map(memory, 65536)
    uc.mem_map(stack, 0x100000)

    def words(address, count):
        return struct.unpack('<%dI' % count, uc.mem_read(address, count*4))

    def floats(address, count):
        return struct.unpack('<%df' % count, uc.mem_read(address, count*4))

    def put(address, values):
        uc.mem_write(address, struct.pack('<%df' % len(values), *values))

    def imported(unused_uc, address, unused_size, unused_context):
        if address not in (0x11c3c00, 0x11c3c24, 0x11c3c3c, 0x13d809e):
            return
        pointer = uc.reg_read(UC_X86_REG_ESP)
        returned = words(pointer, 1)[0]
        if address == 0x13d809e:
            arguments = 0
        elif address == 0x11c3c00:
            output, source = words(pointer+4, 2)
            x, y = floats(source, 2)
            length = binary32(math.sqrt(binary32(binary32(x*x)+binary32(y*y))))
            put(output, (binary32(x/length), binary32(y/length)) if length else (0, 0))
            arguments = 2
            uc.reg_write(UC_X86_REG_EAX, output)
        elif address == 0x11c3c3c:
            output, source = words(pointer+4, 2)
            matrix = floats(source, 16)
            put(output, [matrix[column*4+row] for row in range(4) for column in range(4)])
            arguments = 2
            uc.reg_write(UC_X86_REG_EAX, output)
        else:
            output, source, matrix_address = words(pointer+4, 3)
            vector, matrix = floats(source, 4), floats(matrix_address, 16)
            result = []
            for column in range(4):
                value = binary32(binary32(vector[0]*matrix[column]) +
                                 binary32(vector[1]*matrix[4+column]))
                value = binary32(value + binary32(vector[2]*matrix[8+column]))
                result.append(binary32(value + binary32(vector[3]*matrix[12+column])))
            put(output, result)
            arguments = 3
            uc.reg_write(UC_X86_REG_EAX, output)
        uc.reg_write(UC_X86_REG_ESP, pointer+4+arguments*4)
        uc.reg_write(UC_X86_REG_EIP, returned)

    uc.hook_add(UC_HOOK_CODE, imported)
    result = []
    for points in cases:
        put(memory, [value for point in points for value in point])
        pointer = stack+0x80000
        minimum_extent = struct.unpack('<I', struct.pack('<f', .001))[0]
        uc.mem_write(pointer, struct.pack('<5I', end, memory+0x1000, len(points), memory, minimum_extent))
        uc.reg_write(UC_X86_REG_ESP, pointer)
        uc.reg_write(UC_X86_REG_MXCSR, 0x1f80)
        uc.emu_start(0x9cded0, end, count=150000)
        if uc.reg_read(UC_X86_REG_EIP) != end:
            raise RuntimeError('The original rectangle oracle did not reach its return boundary')
        result.append(bytes(uc.mem_read(memory+0x1000, 24)))
    return result
