"""Execute the exact #1513 GridVertexBuffer producer without a game process."""
import struct


def native_grid(client, resolution_x, resolution_z):
    import pefile
    from unicorn import Uc, UC_ARCH_X86, UC_MODE_32, UC_HOOK_CODE
    from unicorn.x86_const import UC_X86_REG_ECX, UC_X86_REG_EIP, UC_X86_REG_ESP, UC_X86_REG_EAX
    pe = pefile.PE(str(client / 'WorldOfTanks.exe'))
    assert pe.FILE_HEADER.Machine == 0x14c
    image = pe.get_memory_mapped_image()
    base = pe.OPTIONAL_HEADER.ImageBase
    assert pe.get_data(0xaeae70 - base, 6) == bytes.fromhex('55 8b ec 83 ec 30')
    uc = Uc(UC_ARCH_X86, UC_MODE_32)
    uc.mem_map(base, (len(image) + 4095) & ~4095)
    uc.mem_write(base, image)
    job = 0x30000000
    uc.mem_map(job, 0x20000)
    holder, buffer, stack, stop = job + 0x40, job + 0x100, job + 0x10000, job + 0x1f000
    # aeb19f..aeb1ca computes this flag before passing period4 to the job.
    morph = (resolution_x - 1) % 4 == 0 and (resolution_z - 1) % 4 == 0
    data = bytearray(0x50)
    struct.pack_into('<HHI', data, 8, resolution_x, resolution_z, 4)
    struct.pack_into('<II', data, 0x10, job + 0x30, job + 0x31)
    data[0x31] = morph
    struct.pack_into('<I', data, 0x40, buffer)
    uc.mem_write(job, bytes(data))
    uc.mem_write(stack, struct.pack('<III', stop, holder, 0))
    calls = [0]

    def code(machine, address, unused_size, unused):
        if address == stop:
            machine.emu_stop()
        elif address == 0x11c3c60:
            # Only external D3DXFloat32To16Array is modelled. The original x86
            # chooses every source value, output slot, table entry and branch.
            sp = machine.reg_read(UC_X86_REG_ESP)
            returned, dst, src, count = struct.unpack('<4I', machine.mem_read(sp, 16))
            assert count == 1
            value = struct.unpack('<f', machine.mem_read(src, 4))[0]
            machine.mem_write(dst, struct.pack('<e', value))
            machine.reg_write(UC_X86_REG_EAX, dst)
            machine.reg_write(UC_X86_REG_ESP, sp + 16)
            machine.reg_write(UC_X86_REG_EIP, returned)
            calls[0] += 1

    uc.hook_add(UC_HOOK_CODE, code)
    uc.reg_write(UC_X86_REG_ECX, job)
    uc.reg_write(UC_X86_REG_ESP, stack)
    uc.emu_start(0xaeae70, stop, count=1000000)
    assert uc.reg_read(UC_X86_REG_EIP) == stop
    assert calls[0] == resolution_x * resolution_z * 4
    return bytes(uc.mem_read(buffer, resolution_x * resolution_z * 8))


def shader_inputs(effect):
    """Read ARIE/ARIS and only reviewed input-reading SM3 token operations."""
    word = lambda at: struct.unpack_from('<I', effect, at)[0]
    assert effect[:12] == b'ARIEDX9 \x04\0\0\0'
    table = word(0x3c)
    result = []
    for shader in range(word(table)):
        start, size = struct.unpack_from('<II', effect, table + 4 + shader * 8)
        blob = effect[start:start + size]
        local = lambda at: struct.unpack_from('<I', blob, at)[0]
        assert blob[:12] == b'ARISDX9 \x03\0\0\0'
        if local(0x1c) != 1:
            continue
        begin, size = local(0x28) + 0x20, local(0x2c)
        tokens = struct.unpack('<' + 'I' * (size // 4), blob[begin:begin + size])
        cursor, declarations, uses = 1, {}, {}
        while cursor < len(tokens):
            op, length = tokens[cursor] & 0xffff, tokens[cursor] >> 24 & 15
            if op == 0xffff:
                break
            if op == 0xfffe:
                cursor += 1 + (tokens[cursor] >> 16 & 0x7fff)
                continue
            args = tokens[cursor + 1:cursor + 1 + length]
            if op == 31:
                reg = args[1]
                if ((reg >> 28 & 7) | (reg >> 8 & 0x18)) == 1:
                    declarations[reg & 0x7ff] = (args[0] & 15, args[0] >> 16 & 15)
            elif op not in (81, 47, 48):
                for src in args[1:]:
                    if ((src >> 28 & 7) | (src >> 8 & 0x18)) != 1:
                        continue
                    if op in (1, 2, 3, 4, 5, 10, 11, 12, 13, 18, 19, 35, 88):
                        indices = [q for q in range(4) if args[0] >> (16 + q) & 1]
                    elif op in (8, 9):
                        indices = range(3 if op == 8 else 4)
                    elif op in (6, 7, 14, 15):
                        indices = [0]
                    else:
                        raise AssertionError('Unreviewed terrain input opcode %s' % op)
                    uses.setdefault(src & 0x7ff, set()).update('xyzw'[src >> (16 + q * 2) & 3] for q in indices)
            cursor += 1 + length
        result.append((shader, declarations, uses))
    return result
