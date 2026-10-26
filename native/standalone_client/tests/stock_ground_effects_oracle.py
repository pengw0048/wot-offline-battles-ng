"""Execute the original complete 8b18f0 airstrike grid and schedule instructions.

Only imported CRT services, the external D3DX normalization result and the
terminal schedule consumer are bounded here. All point arithmetic, MT19937,
shuffle, CRT-rand call order, kinds and delays execute from the original PE.
The D3DX result is an explicit input, not validation of a Windows DLL.
"""
import math
import struct


def airstrikes(executable, cases):
    from stock_tree_lod_oracle import OriginalTreeInstructionOracle
    from unicorn import UC_HOOK_CODE
    from unicorn.x86_const import UC_X86_REG_ESP, UC_X86_REG_EAX, UC_X86_REG_EIP
    import pefile
    oracle = OriginalTreeInstructionOracle(executable)
    u = oracle.emulator
    origin, velocity = oracle.allocate(12), oracle.allocate(12)
    pe = pefile.PE(str(executable))
    for descriptor in pe.DIRECTORY_ENTRY_IMPORT:
        for imported in descriptor.imports:
            if imported.name == b'D3DXVec3Normalize':
                u.mem_write(imported.address, struct.pack('<I', 0x3000e000))
    u.mem_write(0x3000e000, b'\xc3')
    current = {}

    def finish(cleanup=0, value=0):
        esp = u.reg_read(UC_X86_REG_ESP)
        address = oracle.word(esp)
        u.reg_write(UC_X86_REG_EAX, value)
        u.reg_write(UC_X86_REG_ESP, esp + 4 + cleanup)
        u.reg_write(UC_X86_REG_EIP, address)

    def hook(uc, address, size, data):
        esp = u.reg_read(UC_X86_REG_ESP)
        if address == 0x3000e000:
            out, source = oracle.word(esp + 4), oracle.word(esp + 8)
            assert bytes(u.mem_read(source, 12)) == current['velocity']
            u.mem_write(out, current['direction'])
            finish(8, out)
        elif address == 0x8b0f20:
            kind, position = oracle.word(esp + 4), oracle.word(esp + 8)
            current['result'].append(bytes(u.mem_read(position, 12)) +
                                     bytes(u.mem_read(esp + 12, 4)) + struct.pack('<I', kind))
            finish(12)
        elif address == 0xb490e0:
            finish()  # Scope profiling, after all output has been consumed.

    u.hook_add(UC_HOOK_CODE, hook)
    results = []
    for case in cases:
        u.mem_write(oracle.owner, bytes(0x200))
        u.mem_write(origin, struct.pack('<3f', *case['origin']))
        current['velocity'] = struct.pack('<3f', *case['velocity'])
        current['direction'] = struct.pack('<3f', *case['direction'])
        current['result'] = []
        u.mem_write(velocity, current['velocity'])
        u.mem_write(oracle.owner + 0x1dc, struct.pack('<2f', case['variation'], case['offset']))
        randoms = iter(case['random'])
        oracle.random_source = lambda: next(randoms)
        dimensions = struct.unpack('<2I', struct.pack('<2f', case['width'], case['length']))
        oracle.call(0x8b18f0, (origin, velocity, *dimensions, case['across'], case['along']))
        results.append(b''.join(current['result']))
    return results


def decals(executable, cases):
    """Run complete 8b4a40 through its real 9319b0 argument submission."""
    from stock_tree_lod_oracle import OriginalTreeInstructionOracle
    from unicorn import UC_HOOK_CODE
    from unicorn.x86_const import UC_X86_REG_ESP, UC_X86_REG_EAX, UC_X86_REG_EIP
    import pefile
    oracle = OriginalTreeInstructionOracle(executable)
    u = oracle.emulator
    point = oracle.allocate(12)
    for descriptor in pefile.PE(str(executable)).DIRECTORY_ENTRY_IMPORT:
        for imported in descriptor.imports:
            if imported.name == b'D3DXVec3Normalize':
                u.mem_write(imported.address,struct.pack('<I',0x3000e000))
    u.mem_write(0x3000e000,b'\xc3')
    u.mem_write(0x3000e010,b'\xd9\x05'+struct.pack('<I',0x3000e100)+b'\xc3')
    u.mem_write(0x3000e100,struct.pack('<f',2.))
    current = {}
    def finish(cleanup=0,value=0):
        esp=u.reg_read(UC_X86_REG_ESP)
        address=oracle.word(esp)
        u.reg_write(UC_X86_REG_EAX,value)
        u.reg_write(UC_X86_REG_ESP,esp+4+cleanup)
        u.reg_write(UC_X86_REG_EIP,address)
    def hook(uc,address,size,data):
        esp=u.reg_read(UC_X86_REG_ESP)
        if address==0xa49a80:finish(value=0) # The caller's actual deferred pipeline state.
        elif address==0x3000e000:
            out,source=oracle.word(esp+4),oracle.word(esp+8)
            x,y,z=struct.unpack('<3f',u.mem_read(source,12))
            assert x==0. and z==0. and y<0.
            u.mem_write(out,struct.pack('<3f',0.,-1.,0.));finish(8,out)
        elif address==0x933040:
            u.reg_write(UC_X86_REG_EIP,0x3000e010) # Exact group-lifetime consumer input.
        elif address==0x9319b0:
            assert oracle.word(esp+64)==1, 'Ground must use the simple-collision branch'
            current['result']=bytes(u.mem_read(esp+4,4))
            for index,length in ((2,12),(3,12),(4,8)):
                current['result']+=bytes(u.mem_read(oracle.word(esp+index*4),length))
            current['result']+=bytes(u.mem_read(esp+20,4))
            finish()
    u.hook_add(UC_HOOK_CODE,hook)
    results=[]
    for case in cases:
        u.mem_write(oracle.owner,bytes(128))
        u.mem_write(oracle.owner+4,struct.pack('<2f',*case['size']))
        u.mem_write(oracle.owner+0x6c,struct.pack('<f',case['variation']))
        u.mem_write(oracle.owner+0x70,bytes([case['yaw']]))
        u.mem_write(point,struct.pack('<3f',*case['point']))
        randoms=iter(case['random']);oracle.random_source=lambda:next(randoms)
        oracle.call(0x8b4a40,(point,))
        results.append(current['result'])
    return results
