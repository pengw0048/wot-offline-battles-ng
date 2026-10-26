"""Actual WTau loader/binder instructions, with map/string/audio boundaries.

The first-array reader and binding loop run unmodified. Section mapping supplies
an explicit memory view; the StringTable lookup and voice-registration calls
record their real arguments. This does not execute Wwise or a Windows client.
"""
import struct


class OriginalSpaceSoundReader:
    def __init__(self, executable):
        import pefile
        from unicorn import Uc, UC_ARCH_X86, UC_MODE_32, UC_HOOK_CODE
        from unicorn import x86_const as reg
        self.reg = reg
        pe = pefile.PE(str(executable))
        assert pe.FILE_HEADER.Machine == 0x14c
        base, image = pe.OPTIONAL_HEADER.ImageBase, pe.get_memory_mapped_image()
        self.machine = Uc(UC_ARCH_X86, UC_MODE_32)
        self.machine.mem_map(base, (len(image)+4095)&~4095)
        self.machine.mem_write(base, image)
        self.machine.mem_map(0, 4096)
        self.machine.mem_map(0x20000000, 0x200000)
        self.owner, self.reader, self.feature = 0x20000000, 0x20000100, 0x20000200
        self.payload, self.text = 0x20010000, 0x20100000
        self.stack, self.stop = 0x201f0000, 0x201ff000
        self.machine.hook_add(UC_HOOK_CODE, self.hook)
        self.keys, self.output = [], bytearray()

    def word(self, address):
        return struct.unpack('<I', self.machine.mem_read(address, 4))[0]

    def put(self, address, *words):
        self.machine.mem_write(address, struct.pack('<'+'I'*len(words), *words))

    def returned(self, cleanup=0, value=0):
        m, r = self.machine, self.reg
        stack = m.reg_read(r.UC_X86_REG_ESP)
        destination = self.word(stack)
        m.reg_write(r.UC_X86_REG_ESP, stack+4+cleanup)
        m.reg_write(r.UC_X86_REG_EAX, value)
        m.reg_write(r.UC_X86_REG_EIP, destination)

    def hook(self, machine, address, size, unused):
        r = self.reg
        stack = machine.reg_read(r.UC_X86_REG_ESP)
        if address == 0x6cbd50:  # Existing mapped section, not its payload reader.
            self.returned(16, self.reader)
        elif address == 0xa49a80:  # Existing scene feature table.
            self.returned(value=self.feature)
        elif address == 0x6ce490:  # StringTable::entry(FNV).
            self.keys.append(self.word(stack+4))
            self.returned(4, self.text)
        elif address == 0x613c20:  # Copy the supplied bytes to native std::string.
            destination = machine.reg_read(r.UC_X86_REG_ECX)
            source, length = self.word(stack+4), self.word(stack+8)
            assert length == 5
            machine.mem_write(destination, bytes(machine.mem_read(source, length))+bytes(24-length))
            self.put(destination+16, length, 15)
            self.returned(8, destination)
        elif address == 0x124a700:  # Audio registration: capture event key/position.
            position = self.word(stack+8)
            self.output.extend(struct.pack('<I', self.keys[-1]))
            self.output.extend(machine.mem_read(position, 12))
            self.returned(8)
        elif address == 0x13d809e:  # Security cookie, no selection/reader logic.
            self.returned()
        elif address == 0x6cd049:  # Original loader failure, before log/cleanup.
            self.failed = True
            machine.emu_stop()

    def call(self, address, arguments=(), this=None):
        m, r = self.machine, self.reg
        self.put(self.stack, self.stop, *arguments)
        m.reg_write(r.UC_X86_REG_ESP, self.stack)
        m.reg_write(r.UC_X86_REG_ECX, self.owner if this is None else this)
        m.emu_start(address, self.stop, count=2000000)
        return m.reg_read(r.UC_X86_REG_EAX)

    def read(self, payload):
        assert len(payload) < 0xf0000
        self.keys, self.output, self.failed = [], bytearray(), False
        self.machine.mem_write(self.owner, bytes(256))
        self.machine.mem_write(self.payload, payload)
        self.put(self.reader, self.payload, len(payload), 0)
        self.machine.mem_write(self.text, b'event\0')
        self.machine.mem_write(0x215aa05, b'\0')
        self.put(self.feature+12, self.feature)
        accepted = self.call(0x6ccfc0, (0, 0, 0, 0, 0)) & 255
        cursor = self.word(self.reader+8)
        if self.failed:
            return None, cursor
        assert accepted == 1
        self.call(0x6cce90)
        return bytes(self.output), cursor

    def identities(self, payload, queries):
        self.machine.mem_write(self.owner, bytes(256))
        self.machine.mem_write(self.payload, payload)
        self.put(self.reader, self.payload, len(payload), 0)
        assert self.call(0x6ba4f0, (0,0,0,0,0)) & 255 == 1
        cursor=self.word(self.reader+8)
        self.machine.mem_write(0x1d26733,b'\x33')
        self.machine.mem_write(0x1d26743,b'\x43')
        output=self.feature+0x40
        results=[]
        for chunk,item in queries:
            self.call(0x6ba8f0,(output,chunk,item),this=self.owner+0x10)
            index=self.word(output)
            kind=self.machine.mem_read(output+4,1)[0]
            results.append(0xffffffff if index==0xffffffff else index|(0x80000000 if kind==0x43 else 0))
        return results,cursor


class OriginalSpaceSoundGroups:
    """Actual native map insertion and LSstartAll iteration, with voice leases
    bridged to inert invalid handles. No container operations are replaced.
    """
    def __init__(self,executable):
        from stock_tree_lod_oracle import OriginalTreeInstructionOracle
        from unicorn import UC_HOOK_CODE
        from unicorn import x86_const as reg
        self.reg=reg
        self.oracle=OriginalTreeInstructionOracle(executable)
        self.machine=self.oracle.emulator
        self.invalid=self.oracle.allocate(12)
        self.machine.mem_write(self.invalid,struct.pack('<III',0xffffffff,0xffffffff,0))
        manager=self.oracle.allocate(256)
        self.machine.mem_write(manager+0xe5,b'\1')
        self.machine.mem_write(0x1d207a8,struct.pack('<I',manager))
        self.machine.hook_add(UC_HOOK_CODE,self.hook)
        self.order=[]

    def returned(self,cleanup=0,value=0):
        m,r=self.machine,self.reg
        stack=m.reg_read(r.UC_X86_REG_ESP);destination=self.oracle.word(stack)
        m.reg_write(r.UC_X86_REG_ESP,stack+4+cleanup)
        m.reg_write(r.UC_X86_REG_EAX,value);m.reg_write(r.UC_X86_REG_EIP,destination)

    def string(self,address,text):
        destination=address
        if len(text)>15:
            destination=self.oracle.allocate(len(text)+1)
            self.machine.mem_write(address,struct.pack('<I',destination))
        self.machine.mem_write(destination,text+b'\0')
        self.machine.mem_write(address+16,struct.pack('<II',len(text),max(15,len(text))))

    def hook(self,machine,address,size,unused):
        if address==0x124bcf0:
            destination=machine.reg_read(self.reg.UC_X86_REG_ECX)
            machine.mem_write(destination,struct.pack('<III',0xffffffff,0xffffffff,0))
            self.returned(value=destination)
        elif address==0x124be20:
            self.returned()
        elif address in (0x124f1b0,0x124f3d0):
            self.returned(4)
        elif address==0x124bbd0:
            self.returned(16,self.invalid)
        elif address==0x124c470:
            self.returned(8)
        elif address==0x124c210:
            key=machine.reg_read(self.reg.UC_X86_REG_ECX)-0x30
            length=self.oracle.word(key+16)
            data=self.oracle.word(key) if self.oracle.word(key+20)>15 else key
            self.order.append(bytes(machine.mem_read(data,length)))
            self.returned()

    def start_order(self,names):
        p,m=self.oracle,self.machine
        feature,scene=p.allocate(16),p.allocate(1024)
        m.mem_write(0x215aa04,b'\0')
        p.call(0x1249d60,(scene,),ecx=feature)
        owner=p.word(feature+12)
        for index,name in enumerate(names):
            definition=p.allocate(0x50);event=p.allocate(24);position=p.allocate(12)
            self.string(definition+0x24,name);self.string(event,b'event')
            m.mem_write(definition+0x3c,struct.pack('<III',event,event+24,event+24))
            m.mem_write(position,struct.pack('<3f',1,2,3))
            p.call(0x124a450,(definition,index,0,position),ecx=owner)
        self.order=[]
        p.call(0x124ac80,ecx=feature)
        return self.order


class OriginalModelSoundMatrices:
    """Run the complete 6c24f0 selector; record its exact D3DX operands."""
    def __init__(self,executable):
        import pefile
        from stock_tree_lod_oracle import OriginalTreeInstructionOracle
        from unicorn import UC_HOOK_CODE
        from unicorn import x86_const as reg
        self.reg=reg;self.oracle=OriginalTreeInstructionOracle(executable)
        self.machine=self.oracle.emulator;self.stub=0x3000e000
        pe=pefile.PE(str(executable))
        imports=[v.address for d in pe.DIRECTORY_ENTRY_IMPORT for v in d.imports
                 if v.name and b'D3DXMatrixMultiply' in v.name]
        assert len(imports)==1
        self.multiplyImport=imports[0];self.operands=[]
        self.machine.hook_add(UC_HOOK_CODE,self.hook)

    def hook(self,m,address,size,unused):
        if address!=self.stub:return
        p,r=self.oracle,self.reg
        stack=m.reg_read(r.UC_X86_REG_ESP)
        out,left,right=(p.word(stack+n)for n in (4,8,12))
        self.operands.append(bytes(m.mem_read(left,64))+bytes(m.mem_read(right,64)))
        # Selection is complete. Do not replace D3DX with a guessed matrix
        # formula: the actual Windows production TU uses the original DLL.
        m.reg_write(r.UC_X86_REG_ESP,stack+16)
        m.reg_write(r.UC_X86_REG_EAX,out)
        m.reg_write(r.UC_X86_REG_EIP,p.word(stack))

    def arguments(self,instanceArrays,modelArrays,indices):
        p,m=self.oracle,self.machine;p.restore()
        m.mem_write(self.multiplyImport,struct.pack('<I',self.stub))
        handler,state,models,out=p.allocate(0x80),p.allocate(0xb0),p.allocate(0xb0),p.allocate(64)
        def data(encoded):
            value=p.allocate(max(1,len(encoded)))
            if encoded:m.mem_write(value,encoded)
            return value
        m.mem_write(handler+0x5c,struct.pack('<I',state))
        m.mem_write(state+8,struct.pack('<I',data(instanceArrays[0])))
        m.mem_write(state+0x20,struct.pack('<I',data(instanceArrays[3])))
        m.mem_write(state+0x98,struct.pack('<I',models))
        m.mem_write(models+0x28,struct.pack('<I',data(modelArrays[4])))
        m.mem_write(models+0x88,struct.pack('<I',data(modelArrays[13])))
        m.mem_write(models+0xa0,struct.pack('<I',data(modelArrays[15])))
        result=bytearray()
        for index in indices:
            self.operands=[]
            # An unreadable hardPoint string must not be dereferenced by this
            # actual native handler; only the authored fragile index is used.
            p.call(0x6c24f0,(index,0xdeadbeef,out),ecx=handler)
            assert len(self.operands)==1
            result.extend(struct.pack('<I',index)+self.operands[0])
        return bytes(result)
