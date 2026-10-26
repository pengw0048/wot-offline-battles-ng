"""Run unchanged #1513 curve readers over explicit MSVC map-node inputs.

No original arithmetic, lookup, or iterator code is replaced. The test creates
the documented 24-byte x86 tree nodes; allocation and tree insertion are outside
this scalar interpolation audit. It does not execute a Windows renderer.
"""
import math
import struct


def original_curve_outputs(executable, cases):
    from unicorn.x86_const import UC_X86_REG_FP7, UC_X86_REG_FPSW, UC_X86_REG_FPTAG
    from stock_tree_lod_oracle import OriginalTreeInstructionOracle
    oracle = OriginalTreeInstructionOracle(executable)
    emulator = oracle.emulator
    owner, head = oracle.allocate(16), oracle.allocate(24)
    output = bytearray()

    def call(function, args=()):
        emulator.reg_write(UC_X86_REG_FPSW, 0)
        emulator.reg_write(UC_X86_REG_FPTAG, 0xffff)
        oracle.call(function, args, ecx=owner)
        mantissa, exponent = emulator.reg_read(UC_X86_REG_FP7)
        value = math.ldexp(mantissa / (1 << 63), (exponent & 0x7fff) - 16383)
        if exponent & 0x8000:
            value = -value
        return struct.pack('<f', value)

    for frames, looping, duration, times in cases:
        # addKey uses last-write-wins assignment before loop() is enabled.
        keys = sorted(dict(frames).items())
        nodes = [oracle.allocate(24) for unused in keys]

        def build(first, last, parent):
            if first == last:
                return head
            middle = (first + last) // 2
            node = nodes[middle]
            left, right = build(first, middle, node), build(middle + 1, last, node)
            emulator.mem_write(node, struct.pack('<III4Bff', left, parent, right, 1, 0, 0, 0, *keys[middle]))
            return node

        root = build(0, len(nodes), head)
        emulator.mem_write(head, struct.pack('<III4Bff', nodes[0] if nodes else head, root,
            nodes[-1] if nodes else head, 1, 1, 0, 0, 0, 0))
        emulator.mem_write(owner, struct.pack('<IIIf', head, len(nodes), bool(looping), duration))
        output.extend(call(0x6cfd80))
        for time in times:
            bits, = struct.unpack('<I', struct.pack('<f', time))
            output.extend(call(0x6cfb50, (bits,)))
    return bytes(output)
