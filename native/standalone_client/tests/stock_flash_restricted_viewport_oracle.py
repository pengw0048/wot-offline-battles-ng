"""Execute #1513 AAF080 viewport selection; D3DX identity is a matrix boundary."""
import struct
from unicorn import UC_HOOK_CODE
from unicorn import x86_const as r
from stock_tree_lod_oracle import OriginalTreeInstructionOracle


class OriginalViewport:
    def __init__(self, executable):
        self.p=OriginalTreeInstructionOracle(executable)
        self.u=self.p.emulator
        self.rc=self.p.allocate(0x100)
        self.desc=self.p.allocate(0x100)
        self.outer=self.p.allocate(4)
        self.inner=self.p.allocate(0x80)
        self.clip=self.p.allocate(16)
        self.corners=self.p.allocate(32)
        self.output=self.p.allocate(64)
        self.rect=self.p.allocate(16)
        self.matrix=self.p.allocate(64)
        self.component=self.p.allocate(0x180)
        self.u.mem_write(self.outer,struct.pack('<I',self.inner))
        self.u.mem_write(self.desc+0x1c,struct.pack('<II',3840,720))
        self.u.mem_write(0x1d1fee0,struct.pack('<I',self.rc))
        self.u.hook_add(UC_HOOK_CODE,self.hook)

    def returned(self,value,cleanup=0):
        stack=self.u.reg_read(r.UC_X86_REG_ESP)
        self.u.reg_write(r.UC_X86_REG_EAX,value)
        self.u.reg_write(r.UC_X86_REG_ESP,stack+4+cleanup)
        self.u.reg_write(r.UC_X86_REG_EIP,self.p.word(stack))

    def hook(self,u,address,size,unused):
        if address in (0x9288d0,0x929570):self.returned(self.rc)
        elif address==0x923150:self.returned(self.desc)
        elif address==0x927e50:self.returned(self.outer)
        elif address==0x8bb0b0:self.returned(self.clip)
        elif address==0xaaf530:self.returned(self.matrix)
        elif address==0x68ee00:
            self.returned(int(bool(self.p.word(u.reg_read(r.UC_X86_REG_ESP)+4))))
        elif address==0x11c3bdc:
            stack=u.reg_read(r.UC_X86_REG_ESP)
            destination,source=self.p.word(stack+4),self.p.word(stack+8)
            u.mem_write(destination,bytes(u.mem_read(source,12)))
            self.returned(destination,12)

    def flag(self,value):
        self.u.mem_write(self.component+0x41,bytes([int(value)]))
        self.p.call(0x8c3c70,(self.component+4,))
        return self.u.reg_read(r.UC_X86_REG_EAX)

    def viewport(self,restricted,active,region,clip,corners):
        self.u.mem_write(self.inner+8,bytes([int(active)]))
        self.u.mem_write(self.inner+12,struct.pack('<4i',*region))
        self.u.mem_write(self.clip,struct.pack('<4f',*clip))
        self.u.mem_write(self.corners,struct.pack('<8f',*(v for c in corners for v in c)))
        self.p.call(0xaaf080,(self.matrix,self.corners,self.output,self.rect,int(restricted)))
        return list(struct.unpack('<6i',self.u.mem_read(self.output,24)))+list(
            struct.unpack('<4f',self.u.mem_read(self.rect,16)))
