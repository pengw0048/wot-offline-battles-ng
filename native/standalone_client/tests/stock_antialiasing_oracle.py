"""Run original #1513 AA selection and D3D9 vendor/sample branches in x86."""
import struct


def antialiasing_cases(executable, cases):
    import pefile
    from unicorn import Uc, UC_ARCH_X86, UC_MODE_32, UC_HOOK_CODE
    from unicorn.x86_const import UC_X86_REG_EAX, UC_X86_REG_ECX, UC_X86_REG_ESP, UC_X86_REG_EIP
    pe=pefile.PE(str(executable));base=pe.OPTIONAL_HEADER.ImageBase
    uc=Uc(UC_ARCH_X86,UC_MODE_32)
    uc.mem_map(base,(pe.OPTIONAL_HEADER.SizeOfImage+4095)&~4095);uc.mem_write(base,pe.get_memory_mapped_image())
    stack=0x30000000;owner=0x40000000;stop=owner+0xf000;check=owner+0xe000
    uc.mem_map(stack,0x2000);uc.mem_map(owner,0x10000)
    state={}
    def word(p):return struct.unpack('<I',uc.mem_read(p,4))[0]
    def put(p,*values):uc.mem_write(p,struct.pack('<'+'I'*len(values),*values))
    def ret(value,arguments=0):
        sp=uc.reg_read(UC_X86_REG_ESP);pc=word(sp)
        uc.reg_write(UC_X86_REG_EAX,value&0xffffffff);uc.reg_write(UC_X86_REG_ESP,sp+4+arguments);uc.reg_write(UC_X86_REG_EIP,pc)
    def intercept(machine,address,size,unused):
        sp=uc.reg_read(UC_X86_REG_ESP)
        if address==stop:state['stopped']=True;uc.emu_stop()
        elif address==0x99def0:ret(int(state['temporal']))
        elif address==0x9c8e40:state['fxaa']=word(sp+4);ret(0,4)
        elif address==0x99ef30:
            params=struct.unpack('<3I',uc.mem_read(word(sp+4),12));state['params']=params
            put(owner+0x2000+0x88,*params);ret(0,4)
        elif address==0x99ea30:state['scalar_enabled']=word(sp+4);ret(0,4)
        elif address==check:
            state['query']=struct.unpack('<7I',uc.mem_read(sp+4,28))
            ret(0 if state['check_result'] else 0x8876086c,28)
    uc.hook_add(UC_HOOK_CODE,intercept)
    outputs=[]
    for desired,quality,nvidia,temporal,check_result in cases:
        uc.mem_write(owner,bytes(0x10000));uc.mem_write(stack,bytes(0x2000))
        state.clear();state.update(temporal=temporal,check_result=check_result)
        put(owner,owner+0x1000,owner+0x2000,0)
        put(0x1d20c70,owner+0x3000);put(owner+0x3000+0x4c,owner+0x4000)
        put(stack+0x1000,stop,desired)
        uc.reg_write(UC_X86_REG_ECX,owner);uc.reg_write(UC_X86_REG_ESP,stack+0x1000)
        uc.emu_start(0x9519b0,0,count=10000);assert state.get('stopped')
        mode=word(owner+8);params=state['params'];fxaa=state['fxaa'];enabled=state['scalar_enabled']
        vendor=1 if nvidia else 0
        def cdecl(address,args):
            state.pop('stopped',None);put(stack+0x1000,stop,*args)
            uc.reg_write(UC_X86_REG_ESP,stack+0x1000);uc.emu_start(address,0,count=10000)
            assert state.get('stopped');return uc.reg_read(UC_X86_REG_EAX)
        samples=cdecl(0xb2ea00,[desired,vendor])
        actual_quality=cdecl(0xb2e9b0,[desired,quality,vendor])
        put(owner+0x10,owner+0x2000);put(owner+0x18,0);put(owner+0x20,1)
        put(owner+0x3c,owner+0x1000);put(owner+0x48,vendor)
        put(owner+0x1000+0x80,7);put(owner+0x2000,owner+0x3000);put(owner+0x3000+0x2c,check)
        state.pop('query',None);state.pop('stopped',None)
        put(stack+0x1000,stop,2,0,desired,0)
        uc.reg_write(UC_X86_REG_ECX,owner);uc.reg_write(UC_X86_REG_ESP,stack+0x1000)
        uc.emu_start(0xb26d10,0,count=10000);assert state.get('stopped')
        supported=bool(uc.reg_read(UC_X86_REG_EAX)&255)
        outputs.append([mode,fxaa,*params,enabled,samples,actual_quality,supported,'query'in state])
    return outputs


def temporal_frame_cases(executable, cases):
    import pefile
    from unicorn import Uc, UC_ARCH_X86, UC_MODE_32, UC_HOOK_CODE
    from unicorn.x86_const import UC_X86_REG_ECX, UC_X86_REG_ESP
    pe=pefile.PE(str(executable));base=pe.OPTIONAL_HEADER.ImageBase
    uc=Uc(UC_ARCH_X86,UC_MODE_32)
    uc.mem_map(base,(pe.OPTIONAL_HEADER.SizeOfImage+4095)&~4095);uc.mem_write(base,pe.get_memory_mapped_image())
    stack=0x30000000;owner=0x40000000;stop=owner+0xf000
    uc.mem_map(stack,0x2000);uc.mem_map(owner,0x10000)
    def intercept(machine,address,size,unused):
        if address==stop:uc.emu_stop()
    uc.hook_add(UC_HOOK_CODE,intercept)
    outputs=[]
    for frame,samples,supported,accumulation,history in cases:
        uc.mem_write(owner,bytes(0x100));uc.mem_write(stack+0x1000,struct.pack('<I',stop))
        uc.mem_write(owner+0x88,struct.pack('<I',samples))
        uc.mem_write(owner+0xa8,struct.pack('<I',frame))
        uc.mem_write(owner+0xb0,struct.pack('<I',history))
        uc.mem_write(owner+0xcc,bytes([supported,accumulation]))
        uc.reg_write(UC_X86_REG_ECX,owner);uc.reg_write(UC_X86_REG_ESP,stack+0x1000)
        uc.emu_start(0x99ff20,stop,count=10000)
        index,actual_frame,previous,current=struct.unpack('<4I',uc.mem_read(owner+0xa4,16))
        x,y=struct.unpack('<2I',uc.mem_read(owner+0x54,8))
        outputs.append([actual_frame,index,previous,current,x,y])
    return outputs


def temporal_kernel_cases(executable, cases):
    # The #1513 kernel instructions are executed unchanged. Its imported
    # api-ms-win-crt-math-l1-1-0.__libm_sse2_expf is the sole boundary
    # replaced with this host's float expf,
    # also used by the production C++ probe. This is not a CRT equivalence test.
    import ctypes
    import pefile
    from unicorn import Uc, UC_ARCH_X86, UC_MODE_32, UC_HOOK_CODE
    from unicorn.x86_const import UC_X86_REG_ECX, UC_X86_REG_ESP, UC_X86_REG_EIP, UC_X86_REG_XMM0
    expf=ctypes.CDLL(None).expf;expf.argtypes=[ctypes.c_float];expf.restype=ctypes.c_float
    pe=pefile.PE(str(executable));base=pe.OPTIONAL_HEADER.ImageBase
    uc=Uc(UC_ARCH_X86,UC_MODE_32)
    uc.mem_map(base,(pe.OPTIONAL_HEADER.SizeOfImage+4095)&~4095);uc.mem_write(base,pe.get_memory_mapped_image())
    stack=0x30000000;owner=0x40000000;stop=owner+0xf000
    uc.mem_map(stack,0x2000);uc.mem_map(owner,0x10000)
    def intercept(machine,address,size,unused):
        if address==stop:uc.emu_stop()
        elif address==0x13daddf:
            bits=uc.reg_read(UC_X86_REG_XMM0)&0xffffffff
            argument=struct.unpack('<f',struct.pack('<I',bits))[0]
            value=struct.unpack('<I',struct.pack('<f',expf(argument)))[0]
            uc.reg_write(UC_X86_REG_XMM0,value)
            sp=uc.reg_read(UC_X86_REG_ESP);pc=struct.unpack('<I',uc.mem_read(sp,4))[0]
            uc.reg_write(UC_X86_REG_ESP,sp+4);uc.reg_write(UC_X86_REG_EIP,pc)
    uc.hook_add(UC_HOOK_CODE,intercept)
    outputs=[]
    for width,height,x,y in cases:
        uc.mem_write(owner,bytes(0x1000));uc.mem_write(owner+0x54,struct.pack('<2f',x,y))
        uc.mem_write(stack+0x1000,struct.pack('<I2f3I',stop,width,height,owner+0x100,owner+0x200,owner+0x300))
        uc.reg_write(UC_X86_REG_ECX,owner);uc.reg_write(UC_X86_REG_ESP,stack+0x1000)
        uc.emu_start(0x99ecd0,stop,count=10000)
        outputs.append(list(struct.unpack('<20I',uc.mem_read(owner+0x100,80)))+
            list(struct.unpack('<12I',uc.mem_read(owner+0x200,48)))+
            list(struct.unpack('<12I',uc.mem_read(owner+0x300,48))))
    return outputs


if __name__=='__main__':
    import json,sys
    function={'temporal':temporal_frame_cases,'kernel':temporal_kernel_cases}.get(
        sys.argv[2] if len(sys.argv)>2 else '',antialiasing_cases)
    print(json.dumps(function(sys.argv[1],json.load(sys.stdin))))
