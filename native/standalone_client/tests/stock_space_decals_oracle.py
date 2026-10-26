"""Run unmodified #1513 a8fd60 normal GPU packing, without imported-call bridges."""
import struct


def original_pack(executable, inputs):
    import pefile
    from unicorn import Uc, UC_ARCH_X86, UC_MODE_32
    from unicorn.x86_const import UC_X86_REG_ECX, UC_X86_REG_ESP, UC_X86_REG_MXCSR
    pe = pefile.PE(str(executable))
    if pe.FILE_HEADER.Machine != 0x14c:
        raise ValueError('The decal oracle requires the original x86 executable')
    base = pe.OPTIONAL_HEADER.ImageBase
    mapped = pe.get_memory_mapped_image()
    machine = Uc(UC_ARCH_X86, UC_MODE_32)
    machine.mem_map(base, (len(mapped) + 4095) & ~4095)
    machine.mem_write(base, mapped)
    machine.mem_map(0x20000000, 0x10000)
    owner, material, record, output = 0x20000000, 0x20001000, 0x20002000, 0x20003000
    stack, stop = 0x20008000, 0x2000f000
    result = bytearray()
    if len(inputs) % 216:
        raise ValueError('Decal oracle input requires 92-byte material and 124-byte render records')
    for cursor in range(0, len(inputs), 216):
        machine.mem_write(material, inputs[cursor:cursor + 92])
        machine.mem_write(record, inputs[cursor + 92:cursor + 216])
        machine.mem_write(stack, struct.pack('<4I', stop, material, record, output))
        machine.reg_write(UC_X86_REG_ECX, owner)
        machine.reg_write(UC_X86_REG_ESP, stack)
        machine.reg_write(UC_X86_REG_MXCSR, 0x1f80)
        machine.emu_start(0xa8fd60, stop, count=10000)
        result.extend(machine.mem_read(output, 96))
    return bytes(result)


def original_distance(executable, inputs):
    """Execute a4c810 through its distance store, before vector publication."""
    import pefile
    from capstone import Cs, CS_ARCH_X86, CS_MODE_32
    from unicorn import Uc, UC_ARCH_X86, UC_MODE_32
    from unicorn.x86_const import UC_X86_REG_ECX, UC_X86_REG_ESP, UC_X86_REG_MXCSR
    pe = pefile.PE(str(executable))
    base = pe.OPTIONAL_HEADER.ImageBase
    mapped = pe.get_memory_mapped_image()
    code = mapped[0xa4c810 - base:0xa4c810 - base + 256]
    stop = next(instruction.address for instruction in Cs(CS_ARCH_X86, CS_MODE_32).disasm(code, 0xa4c810)
                if instruction.mnemonic == 'call')
    machine = Uc(UC_ARCH_X86, UC_MODE_32)
    machine.mem_map(base, (len(mapped) + 4095) & ~4095)
    machine.mem_write(base, mapped)
    machine.mem_map(0x20000000, 0x10000)
    owner, cache, camera, stack = 0x20000000, 0x20001000, 0x20002000, 0x20008000
    machine.mem_write(owner + 4, struct.pack('<I', cache))
    result = bytearray()
    if len(inputs) % 28:
        raise ValueError('Distance inputs require position/camera/zoom: seven float32s')
    for cursor in range(0, len(inputs), 28):
        machine.mem_write(cache + 8, inputs[cursor:cursor + 12])
        machine.mem_write(camera, inputs[cursor + 12:cursor + 24])
        machine.mem_write(stack, struct.pack('<2I', 0x2000f000, camera) +
                          inputs[cursor + 24:cursor + 28] + struct.pack('<I', 0))
        machine.reg_write(UC_X86_REG_ECX, owner)
        machine.reg_write(UC_X86_REG_ESP, stack)
        machine.reg_write(UC_X86_REG_MXCSR, 0x1f80)
        machine.emu_start(0xa4c810, stop, count=1000)
        result.extend(machine.mem_read(cache + 4, 4))
    return bytes(result)


def original_mip_levels(executable, inputs):
    """Execute the original level-count writer, stopping before allocation."""
    import pefile
    from unicorn import Uc, UC_ARCH_X86, UC_MODE_32
    from unicorn.x86_const import UC_X86_REG_ECX, UC_X86_REG_ESP
    pe = pefile.PE(str(executable))
    base, mapped = pe.OPTIONAL_HEADER.ImageBase, pe.get_memory_mapped_image()
    machine = Uc(UC_ARCH_X86, UC_MODE_32)
    machine.mem_map(base, (len(mapped) + 4095) & ~4095); machine.mem_write(base, mapped)
    machine.mem_map(0, 4096)  # Original Win32 SEH prologue reads fs:[0].
    machine.mem_map(0x20000000, 0x10000)
    owner, stack = 0x20000000, 0x20008000
    output = bytearray()
    if len(inputs) % 8:
        raise ValueError('Mip inputs require two uint32 values')
    for offset in range(0, len(inputs), 8):
        machine.mem_write(owner + 0x48, inputs[offset:offset + 4])
        machine.mem_write(owner + 0x44, inputs[offset + 4:offset + 8])
        machine.mem_write(stack, struct.pack('<II', 0x2000f000, 0))
        machine.reg_write(UC_X86_REG_ECX, owner); machine.reg_write(UC_X86_REG_ESP, stack)
        machine.emu_start(0x13f5450, 0x13f54d9, count=1000)
        output.extend(machine.mem_read(owner + 0x38, 4))
    return bytes(output)
