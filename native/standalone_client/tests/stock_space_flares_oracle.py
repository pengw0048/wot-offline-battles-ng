"""Execute the unchanged original flare tint instruction range."""
import struct


def colours(executable, cases):
    from stock_tree_lod_oracle import OriginalTreeInstructionOracle
    from unicorn.x86_const import UC_X86_REG_ECX, UC_X86_REG_EDI, UC_X86_REG_ESI, UC_X86_REG_EBP, UC_X86_REG_ESP
    oracle = OriginalTreeInstructionOracle(executable)
    u = oracle.emulator
    definition, instance = oracle.allocate(48), oracle.allocate(80)
    result = []
    for colour, tint in cases:
        u.mem_write(definition + 0x1c, struct.pack('<3f', *tint))
        u.mem_write(instance + 0x38, struct.pack('<I', colour))
        u.reg_write(UC_X86_REG_ECX, instance)
        u.reg_write(UC_X86_REG_EDI, definition)
        u.reg_write(UC_X86_REG_ESI, instance + 4)
        u.reg_write(UC_X86_REG_EBP, oracle.stack)
        u.reg_write(UC_X86_REG_ESP, oracle.stack - 4096)
        u.emu_start(0x6be3df, 0x6be3ef, count=10)
        u.emu_start(0x6be413, 0x6be563, count=1000)
        result.append(struct.unpack('<I', u.mem_read(instance + 0x38, 4))[0])
    return result
