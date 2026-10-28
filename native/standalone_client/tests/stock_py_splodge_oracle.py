"""Original #1513 factory and 128-byte draw receipt; CPython/D3DX/render singleton are boundaries."""
import struct
from stock_cursor_camera_oracle import OriginalCursorCamera, CameraMath, bits

class OriginalSplodge(OriginalCursorCamera):
    def __init__(self, executable):
        super().__init__(executable)
        self.values={};self.error=False;self.forward=True
        self.handle(0xfb3610,lambda e:e._return(len(self.values[e._arg(0)])))
        self.handle(0xfb3390,lambda e:e._return(self.object(self.values[e._arg(0)][e._arg(1)])))
        self.handle(0x68e100,lambda e:(setattr(self,'error',True),e._return()))
        self.handle(0x690130,lambda e:self.matrix_convert())
        self.handle(0x68fba0,lambda e:self.float_convert())
        self.handle(0xa49a80,lambda e:e._return(int(self.forward)))
        self.handle(0x13d7fe9,lambda e:e._return(self.alloc(e._arg(0))))
        self.handle(0x8f62b0,lambda e:e._return(e.uc.reg_read(e.x86.UC_X86_REG_ECX),4))
        self.handle(0x11c3c06,lambda e:self.transform_normal())
        self.renderer=self.alloc(0x40)
        self.buffer=self.alloc(128*4)
        self.put_word(self.renderer+4,self.buffer)
        self.put_word(self.renderer+8,self.buffer+128*4)
        table=self.alloc(0x10c)
        singleton=self.alloc(4);self.put_word(singleton,table)
        slot=self.entry();self.put_word(table+0x108,slot)
        self.handle(0x941a10,lambda e:e._return(singleton))
        self.handle(slot,lambda e:e._return(self.renderer))
        self.uc.mem_write(0x1ce4794,b'\x01')
    def object(self,value):
        a=self.alloc(16);self.values[a]=value;return a
    def matrix_convert(self):
        v=self.values[self._arg(0)]
        if not isinstance(v,list) or len(v)!=16:self.error=True;self._return(-1);return
        self.uc.mem_write(self._arg(1),struct.pack('<16f',*v));self._return()
    def float_convert(self):
        v=self.values[self._arg(0)]
        if not isinstance(v,(int,float)):self.error=True;self._return(-1);return
        self.put_word(self._arg(1),bits(v));self._return()
    def factory(self,*args):
        self.error=False
        r=self.call(0x13f2a60,(0,self.object(args),0))
        if self.error:return 'error'
        return None if r==0x1a5c798 else r-4
    def transform_normal(self):
        v=self.floats(self._arg(1),3);flat=self.floats(self._arg(2),16)
        value=CameraMath.transform_normal(v,[flat[i:i+4] for i in range(0,16,4)])
        self.put_floats(self._arg(0),value);self._return(self._arg(0),12)
    def draw(self,owner,world,lod=0,outside=True,enabled=True):
        self.put_word(owner+0x64,bits(lod));self.uc.mem_write(owner+0x70,bytes([int(outside)]))
        self.uc.mem_write(0x1ce4794,bytes([int(enabled)]));self.put_word(self.renderer+16,0)
        pointer=self.alloc(64);self.put_floats(pointer,world)
        self.call(0x13f30e0,(0,pointer),ecx=owner)
        return None if not self.word(self.renderer+16) else self.floats(self.buffer,32)
