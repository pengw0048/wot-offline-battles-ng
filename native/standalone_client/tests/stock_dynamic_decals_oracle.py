"""Execute the original dynamic provider's float path and 124-byte publication."""
import struct


def original_dynamic_records(executable, inputs):
    import pefile
    from unicorn import Uc, UC_ARCH_X86, UC_MODE_32, UC_HOOK_CODE
    from unicorn.x86_const import UC_X86_REG_EBP, UC_X86_REG_EBX, UC_X86_REG_EDI, UC_X86_REG_EDX, UC_X86_REG_ESP, UC_X86_REG_XMM1, UC_X86_REG_MXCSR
    pe = pefile.PE(str(executable))
    base = pe.OPTIONAL_HEADER.ImageBase
    mapped = pe.get_memory_mapped_image()
    machine = Uc(UC_ARCH_X86, UC_MODE_32)
    machine.mem_map(base, (len(mapped) + 4095) & ~4095)
    machine.mem_write(base, mapped)
    machine.mem_map(0x20000000, 0x10000)
    owner, record, rect, quality, indices, vector = [0x20000000 + i * 0x1000 for i in range(6)]
    frame = 0x2000d000
    machine.mem_write(owner + 8, struct.pack('<I', record))
    machine.mem_write(owner + 0x14, struct.pack('<I', indices))
    machine.mem_write(owner + 0xa8, struct.pack('<I', quality))
    machine.mem_write(record + 0x28, struct.pack('<I', rect))
    machine.mem_write(frame + 0x10, struct.pack('<I', vector))
    stopped = [None]
    def stop(current, address, size, unused):
        if address in (0xa5f290, 0xa5f2e8):
            stopped[0] = address
            current.emu_stop()
    machine.hook_add(UC_HOOK_CODE, stop)
    output = bytearray()
    if len(inputs) % 92:
        raise ValueError('Dynamic decal fixtures require exactly 92 bytes each')
    for offset in range(0, len(inputs), 92):
        case = bytes(inputs[offset:offset + 92])
        # The native 96-byte owner includes a real texture-handle pointer;
        # the fixture's semantic POD omits only that pointer and its bounds.
        machine.mem_write(record, case[:40])
        machine.mem_write(record + 0x2c, case[40:60])
        machine.mem_write(record + 0x5c, case[60:64])
        machine.mem_write(owner + 0x9c, case[64:68])
        machine.mem_write(owner + 0x8c, case[68:72])
        machine.mem_write(owner + 0x94, case[72:76])
        machine.mem_write(rect + 10, case[76:84])
        machine.mem_write(quality + 8, case[88:92])
        machine.reg_write(UC_X86_REG_EBP, frame)
        machine.reg_write(UC_X86_REG_EBX, owner)
        machine.reg_write(UC_X86_REG_EDX, 0)
        machine.reg_write(UC_X86_REG_EDI, struct.unpack_from('<I', case, 84)[0])
        machine.reg_write(UC_X86_REG_ESP, frame - 1024)
        machine.reg_write(UC_X86_REG_MXCSR, 0x1f80)
        stopped[0] = None
        machine.emu_start(0xa5f153, 0x2000f000, count=1000)
        if stopped[0] is None:
            raise RuntimeError('The original dynamic provider did not reach a publication boundary')
        accepted = stopped[0] == 0xa5f290
        alpha = machine.reg_read(UC_X86_REG_XMM1) & 0xffffffff
        output.extend(struct.pack('<II', accepted, alpha))
        output.extend(machine.mem_read(frame - 0x90, 124) if accepted else bytes(124))
    return bytes(output)
