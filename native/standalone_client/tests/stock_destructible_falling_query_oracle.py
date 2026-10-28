"""Original6b4ad0; D3DX/atanf and ClientSpace collide are declared boundaries."""
import math
import ctypes
import os
import struct
import unicorn
from unicorn.x86_const import UC_X86_REG_EAX,UC_X86_REG_ECX,UC_X86_REG_EIP,UC_X86_REG_ESP,UC_X86_REG_XMM0
from stock_destructibles_oracle import OriginalStaticDestructibles
from stock_cursor_camera_oracle import CameraMath,f32
import stock_runtime_services_oracle as base


class OriginalFallingQuery(OriginalStaticDestructibles):
    def __init__(self,executable):
        super(OriginalFallingQuery,self).__init__(executable)
        self.distances=[];self.segments=[]
        self.native_atan=ctypes.CDLL('ucrtbase.dll' if os.name=='nt' else None).atanf
        self.native_atan.argtypes=[ctypes.c_float];self.native_atan.restype=ctypes.c_float
        self.native_hooks.update({0x11c3be2:self.rotation,0x11c3c06:self.transform,0xac2c90:self.collide,0x13dad1f:self.atan})
        for address in (0x11c3be2,0x11c3c06,0xac2c90,0x13dad1f):
            self.u.hook_add(unicorn.UC_HOOK_CODE,lambda u,a,s,d:self.native_hooks[a](),begin=address,end=address)

    def rotation(self):
        values=CameraMath.yaw_pitch_roll(self._float(2),self._float(3),self._float(4))
        self.u.mem_write(self._arg(1),struct.pack('<16f',*(v for row in values for v in row)))
        self.stdcall(self._arg(1),4)

    def transform(self):
        value=struct.unpack('<3f',self.u.mem_read(self._arg(2),12));flat=struct.unpack('<16f',self.u.mem_read(self._arg(3),64))
        result=CameraMath.transform(value,[flat[n*4:n*4+4]for n in range(4)])
        self.u.mem_write(self._arg(1),struct.pack('<3f',*result[:3]));self.stdcall(self._arg(1),3)

    def atan(self):
        raw=self.u.reg_read(UC_X86_REG_XMM0)&0xffffffff;value=struct.unpack('<f',struct.pack('<I',raw))[0]
        self.u.reg_write(UC_X86_REG_XMM0,struct.unpack('<I',struct.pack('<f',self.native_atan(value)))[0]);self._return(0)

    def collide(self):
        self.segments.append(bytes(self.u.mem_read(self._arg(1),12))+bytes(self.u.mem_read(self._arg(2),12)))
        assert self.distances,'Original collision requested an unscripted result'
        value=self.distances.pop(0);result=base.MARKS+0xf000;stub=result+0x10
        self.u.mem_write(result,struct.pack('<f',value))
        self.u.mem_write(stub,b'\xd9\x05'+struct.pack('<I',result)+b'\xc2\x0c\x00')
        self.u.reg_write(UC_X86_REG_EIP,stub)

    def query(self,origin,height,yaw,distances):
        self.distances=list(distances);self.segments=[]
        point,result=base.MARKS+0xf100,base.MARKS+0xf200
        self.u.mem_write(point,struct.pack('<3f',*origin));self.u.mem_write(result,b'\xa5'*8)
        stack=base.STACK+0x80000
        self.u.mem_write(stack,struct.pack('<4I2f',base.SENTINEL,result,0x12345678,point,height,yaw))
        self.u.reg_write(UC_X86_REG_ESP,stack);self.u.reg_write(UC_X86_REG_ECX,0)
        self.u.emu_start(0x6b4ad0,base.SENTINEL+1,count=100000)
        assert self.u.reg_read(UC_X86_REG_EIP)==base.SENTINEL
        return bytes(self.u.mem_read(result,8)),self.segments

    def filter(self,flags,distance,closest=100.):
        callback=base.MARKS+0xf300;triangle=callback+0x100
        self.u.mem_write(callback,struct.pack('<IfI',0x14bb7e0,closest,0xabababab))
        self.u.mem_write(triangle,bytes(0x24)+struct.pack('<H',flags))
        stack=base.STACK+0x80000;self.u.mem_write(stack,struct.pack('<IIIf',base.SENTINEL,0,triangle,distance))
        self.u.reg_write(UC_X86_REG_ESP,stack);self.u.reg_write(UC_X86_REG_ECX,callback)
        self.u.emu_start(0x6b47f0,base.SENTINEL+1,count=100000)
        return self.u.reg_read(UC_X86_REG_EAX),bytes(self.u.mem_read(callback+4,8))

    def wrapper(self,argc=4,fail_at=None):
        self.argc=argc;self.values=[41,0x8081,3,.25];self.fail_at=fail_at;self.calls=[];self.results={};self.backends=[]
        captured=[];marker=base.MARKS+0xfe00
        def convert_uint():
            index=self._arg(1)-base.ITEMS;self.calls.append(['setData','uint',index,self._cstr(self._arg(3))])
            if index==fail_at:self._return(0xffffffff)
            else:self.u.mem_write(self._arg(2),struct.pack('<I',self.values[index]));self._return(0)
        def query():
            captured.append([self._arg(1),self._arg(2),self._arg(3),self._float(4)]);self._return(marker)
        self.native_hooks[0x68fad0]=convert_uint;self.native_hooks[0x6b4fa0]=query
        if not getattr(self,'wrapper_hooks',False):
            for address in (0x68fad0,0x6b4fa0):
                self.u.hook_add(unicorn.UC_HOOK_CODE,lambda u,a,s,d:self.native_hooks[a](),begin=address,end=address)
            self.wrapper_hooks=True
        stack=base.STACK+0x80000;self.u.mem_write(stack,struct.pack('<3I',base.SENTINEL,0,base.ARGS));self.u.reg_write(UC_X86_REG_ESP,stack)
        self.u.emu_start(0x6b4840,base.SENTINEL+1,count=100000)
        assert self.u.reg_read(UC_X86_REG_EIP)==base.SENTINEL
        return self.u.reg_read(UC_X86_REG_EAX),list(self.calls),captured
