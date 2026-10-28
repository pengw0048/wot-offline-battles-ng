"""Original attachment descriptor writers and persistent-provider GPU records."""
import struct
from unicorn.x86_const import UC_X86_REG_EAX, UC_X86_REG_EBP, UC_X86_REG_ECX, UC_X86_REG_ESP, UC_X86_REG_EIP
import stock_runtime_services_oracle as base

OWNER=base.MARKS+0x10000
INSTANCE=base.MARKS+0x11000
TEXTURE=base.MARKS+0x12000
INPUT=base.MARKS+0x13000


def original_records(executable, inputs):
    original=base.OriginalRuntimeServices(executable)
    u=original.u
    # a64150 calls the camera view update and sorts the single visible index.
    original.hooks[0x94b0e0]=lambda:original._return(0)
    original.hooks[0xa63330]=lambda:original._return(0)
    import unicorn
    for address in (0x94b0e0,0xa63330):
        u.hook_add(unicorn.UC_HOOK_CODE,lambda u,a,s,d:original.hooks[a](),begin=address,end=address)
    u.mem_write(OWNER+4,struct.pack('<II',INSTANCE,INSTANCE+96))
    u.mem_write(OWNER+0x1c,struct.pack('<II',INPUT,INPUT+4))
    u.mem_write(INPUT,struct.pack('<I',0))
    u.mem_write(OWNER+0x94,struct.pack('<f',10000.))
    u.mem_write(OWNER+0x9c,struct.pack('<f',1./12500.))
    results=[]
    for data in inputs:
        material,priority,influence,distance,rotation,position,scale,rectangle=data
        native=bytearray(96)
        struct.pack_into('<3f',native,0,*position)
        struct.pack_into('<3f',native,12,*scale)
        struct.pack_into('<4f',native,24,*rotation)
        struct.pack_into('<4I',native,40,TEXTURE,priority,material,influence)
        struct.pack_into('<f',native,92,distance)
        u.mem_write(INSTANCE,bytes(native));u.mem_write(TEXTURE+10,struct.pack('<4H',*rectangle))
        u.mem_write(OWNER+0xb0,struct.pack('<I',0))
        u.mem_write(OWNER+0xb4,struct.pack('<I',INPUT+0x100))
        # Ensure the temporary material and output vectors have spare storage.
        u.mem_write(INPUT+0x200,struct.pack('<4I',INPUT+0x400,INPUT+0x800,0,0))
        u.mem_write(INPUT+0x300,struct.pack('<4I',INPUT+0x900,INPUT+0xa00,0,0))
        u.mem_write(OWNER+0x20,struct.pack('<I',INPUT+4))
        stack=base.STACK+0x80000
        u.mem_write(stack,struct.pack('<4I',base.SENTINEL,INPUT+0x50,INPUT+0x200,INPUT+0x300))
        u.reg_write(UC_X86_REG_ECX,OWNER);u.reg_write(UC_X86_REG_ESP,stack)
        u.emu_start(0xa64150,0xa6431f,count=10000)
        if u.reg_read(UC_X86_REG_EIP)!=0xa6431f:
            raise AssertionError('Record was not admitted: '+str(data))
        results.append(bytes(u.mem_read(u.reg_read(UC_X86_REG_EBP)-0x94,124)))
    return results


def original_descriptor(executable,shadow,priority,influence,material=6):
    original=base.OriginalRuntimeServices(executable);u=original.u
    import unicorn
    captured=[]
    def result(value,pop=0):
        esp=u.reg_read(UC_X86_REG_ESP);target=original._arg(0)
        u.reg_write(UC_X86_REG_EAX,value);u.reg_write(UC_X86_REG_EIP,target);u.reg_write(UC_X86_REG_ESP,esp+4+pop)
    def descriptor():
        ptr=original._arg(1)
        captured.append((bytes(u.mem_read(ptr,64)),original._cstr(ptr+64),
            struct.unpack('<3I',u.mem_read(ptr+88,12)),original._arg(2)))
        result(1,8)
    hooks={0x933160:descriptor,0x13dac1d:lambda:result(0),0x13dabf9:lambda:result(0),
           0x11e7c30:lambda:result(0),0x13d809e:lambda:result(0)}
    for address,callback in hooks.items():
        u.hook_add(unicorn.UC_HOOK_CODE,lambda u,a,s,d:hooks[a](),begin=address,end=address)
    u.mem_write(OWNER+0x24,struct.pack('<I',INSTANCE))
    def string(value):return value+b'\0'*(16-len(value))+struct.pack('<II',len(value),15)
    values=(string(b'')*3+string(b'ao')+struct.pack('<5I',priority,4,influence,0xffffffff,2)) if not shadow else (string(b'ao')+struct.pack('<3I',material,priority,influence))
    stack=base.STACK+0x80000;u.mem_write(stack,struct.pack('<I',base.SENTINEL)+values)
    u.reg_write(UC_X86_REG_ECX,OWNER);u.reg_write(UC_X86_REG_ESP,stack)
    # Copy/memset hooks implement their C memory boundaries, without replacing
    # the original descriptor field selection, ignored legacy args or enum.
    def memset():
        u.mem_write(original._arg(1),bytes([original._arg(2)&255])*original._arg(3));result(original._arg(1))
    def memcpy():
        u.mem_write(original._arg(1),bytes(u.mem_read(original._arg(2),original._arg(3))));result(original._arg(1))
    hooks[0x13dac1d]=memset;hooks[0x13dabf9]=memcpy
    u.emu_start(0xa13700 if shadow else 0xa12f70,base.SENTINEL+1,count=10000)
    assert u.reg_read(UC_X86_REG_EIP)==base.SENTINEL
    return captured


def wrapper_contract(executable):
    original=base.OriginalRuntimeServices(executable);u=original.u
    import unicorn
    methods=(('create',0xa12b70,9),('setup',0xa13580,4),('setLocalTransform',0xa12e90,1),
             ('collideWithScene',0xa13500,1),('WGOcclusionDecal',0xa12a30,0),('WGShadowForwardDecal',0xa133c0,0))
    hooks={0x68fad0:lambda:original._set_data('uint'),0x690130:lambda:original._set_data('Matrix'),
           0x11e7c30:lambda:original._return(0),0x746a60:lambda:original._return(0),0x13d809e:lambda:original._return(0),0x13dac1d:lambda:original._return(0)}
    for address in hooks:
        u.hook_add(unicorn.UC_HOOK_CODE,lambda u,a,s,d:hooks[a](),begin=address,end=address)
    rows=[]
    for name,address,count in methods:
        cases=[(n,None) for n in range(count+2) if n!=count]
        cases += [(count,index) for index in range(count)]
        for argc,fail in cases:
            original.argc=argc;original.fail_at=fail;original.values=[0]*10;original.calls=[]
            stack=base.STACK+0x80000;u.mem_write(stack,struct.pack('<III',base.SENTINEL,OWNER+4,base.ARGS))
            u.reg_write(UC_X86_REG_ECX,0);u.reg_write(UC_X86_REG_ESP,stack)
            try:u.emu_start(address,base.SENTINEL+1,count=10000)
            except Exception as error:raise AssertionError((name,argc,fail,hex(u.reg_read(UC_X86_REG_EIP)),original.calls)) from error
            assert u.reg_read(UC_X86_REG_EIP)==base.SENTINEL
            assert u.reg_read(UC_X86_REG_EAX)==0
            rows.append((name,argc,fail,list(original.calls)))
    return rows
