"""Execute #1513 AppType's exact six-name enum selection instructions."""
import struct


def content_types(executable, names):
    from stock_tree_lod_oracle import OriginalTreeInstructionOracle
    from unicorn.x86_const import UC_X86_REG_EBP,UC_X86_REG_ESP
    oracle=OriginalTreeInstructionOracle(executable)
    u=oracle.emulator
    descriptor=oracle.allocate(28)
    result=[]
    for name in names:
        value=name.encode('utf-8')
        u.mem_write(descriptor,bytes(28))
        if len(value)<16:
            u.mem_write(descriptor+4,value+b'\x00')
            u.mem_write(descriptor+24,struct.pack('<I',15))
        else:
            text=oracle.allocate(len(value)+1);u.mem_write(text,value+b'\x00')
            u.mem_write(descriptor+4,struct.pack('<I',text))
            u.mem_write(descriptor+24,struct.pack('<I',len(value)))
        u.mem_write(descriptor+20,struct.pack('<I',len(value)))
        u.mem_write(oracle.stack-0x44,struct.pack('<I',descriptor))
        u.reg_write(UC_X86_REG_EBP,oracle.stack)
        u.reg_write(UC_X86_REG_ESP,oracle.stack-1024)
        u.emu_start(0x11a4ef4,0x11a5012,count=3000)
        result.append(oracle.word(descriptor))
    return result
