"""Execute the original #1513 version/status branch and shadow conversion."""
import struct
from pathlib import Path


def preference_versions(executable, cases):
    import pefile
    from unicorn import Uc, UC_ARCH_X86, UC_MODE_32, UC_HOOK_CODE
    from unicorn.x86_const import UC_X86_REG_EAX, UC_X86_REG_EBP, UC_X86_REG_ESP, UC_X86_REG_EIP

    pe = pefile.PE(str(executable))
    base = pe.OPTIONAL_HEADER.ImageBase
    machine = Uc(UC_ARCH_X86, UC_MODE_32)
    machine.mem_map(base, (pe.OPTIONAL_HEADER.SizeOfImage + 4095) & ~4095)
    machine.mem_write(base, pe.get_memory_mapped_image())
    stack, owner = 0x30000000, 0x40000000
    machine.mem_map(stack, 0x2000)
    machine.mem_map(owner, 0x1000)
    frame = stack + 0x1000
    state = {}

    def intercept(uc, address, size, unused):
        if address == 0x91daf4:
            state['stopped'] = True
            uc.emu_stop()
            return
        if address not in (0x9288d0, 0x9268e0, 0x935d60):
            return
        pointer = uc.reg_read(UC_X86_REG_ESP)
        destination = struct.unpack('<I', uc.mem_read(pointer, 4))[0]
        if address == 0x935d60:
            state['detected'] = True
        uc.reg_write(UC_X86_REG_EAX, owner)
        uc.reg_write(UC_X86_REG_ESP, pointer + 4 + (16 if address == 0x935d60 else 0))
        uc.reg_write(UC_X86_REG_EIP, destination)

    machine.hook_add(UC_HOOK_CODE, intercept)
    result = []
    for major, minor, maintenance, status in cases:
        state.clear()
        machine.mem_write(stack, bytes(0x2000))
        machine.mem_write(owner, bytes(0x1000))
        machine.mem_write(0x1d3805c, struct.pack('<4i', major, minor, maintenance, 0))
        machine.reg_write(UC_X86_REG_EAX, status & 0xffffffff)
        machine.reg_write(UC_X86_REG_EBP, frame)
        machine.reg_write(UC_X86_REG_ESP, frame - 0x100)
        # Start immediately after the real DataSection version/status reads;
        # no reimplementation of their comparisons or branches is executed.
        machine.emu_start(0x91d933, 0, count=10000)
        assert state.get('stopped'), 'Original preference branch did not terminate'
        values = struct.unpack('<4i', machine.mem_read(0x1d3805c, 16))
        refreshed = machine.mem_read(owner + 0x559, 1) != b'\0'
        result.append(list(values) + [bool(state.get('detected')), refreshed])
    return result


if __name__ == '__main__':
    import json
    import sys
    print(json.dumps(preference_versions(Path(sys.argv[1]), json.load(sys.stdin))))
