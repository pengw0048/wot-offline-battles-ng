"""Execute #1513's complete zone camera classifier and transition callback.

Only the native SoundManager event/aux consumers are boundaries. Timer return,
float geometry, first-match ordering and current-zone writes run as x86 code.
"""
import struct


class OriginalSoundZones:
    def __init__(self, executable):
        from stock_tree_lod_oracle import OriginalTreeInstructionOracle
        from unicorn import UC_HOOK_CODE
        from unicorn import x86_const as r
        self.p = OriginalTreeInstructionOracle(executable)
        self.u = self.p.emulator
        self.r = r
        self.owner = self.p.allocate(24)
        self.closure = self.p.allocate(8)
        self.sound = self.p.allocate(256)
        self.output = self.p.allocate(8)
        self.records = self.p.allocate(44 * 128)
        self.put(0x1d207a8, self.sound)
        self.put(self.closure, 0, self.owner)
        self.put(self.owner, self.records, self.records, self.records + 44 * 128, 0, 0, 0x3dcccccd)
        self.trace = []
        self.wrapper_mode = False
        self.u.hook_add(UC_HOOK_CODE, self.hook)

    def put(self, address, *words):
        self.u.mem_write(address, struct.pack('<' + 'I' * len(words), *words))

    def hook(self, u, address, size, unused):
        if self.wrapper_mode and self.wrapper_hook(u, address): return
        if address == 0x12a319b:  # Explicit CRT strncpy_s primitive boundary.
            stack = u.reg_read(self.r.UC_X86_REG_ESP)
            destination, size, source, count = [self.p.word(stack + 4*i) for i in range(4)]
            data = bytes(u.mem_read(source, count))
            assert count < size
            u.mem_write(destination, data + b'\0')
            u.reg_write(self.r.UC_X86_REG_EAX, 0)
            u.reg_write(self.r.UC_X86_REG_EIP, 0x12a31a1)
            return
        if address not in (0x1248210, 0x12485a0, 0x1248570): return
        stack = u.reg_read(self.r.UC_X86_REG_ESP)
        word = self.p.word
        if address == 0x1248210:
            self.trace.append(('event', word(stack + 4)))
            cleanup = 4
        elif address == 0x12485a0:
            self.trace.append(('aux', word(stack + 4), word(stack + 8)))
            cleanup = 8
        else:
            self.trace.append(('default',))
            cleanup = 0
        u.reg_write(self.r.UC_X86_REG_ESP, stack + 4 + cleanup)
        u.reg_write(self.r.UC_X86_REG_EIP, word(stack))

    def load(self, records):
        assert len(records) <= 128
        self.u.mem_write(self.records, b''.join(records))
        self.put(self.owner + 4, self.records + 44 * len(records))
        self.put(self.owner + 16, 0)

    def tick(self, camera):
        self.trace = []
        self.u.mem_write(self.sound + 0x84, struct.pack('<3f', *camera))
        self.p.call(0x8a36f0, (self.output,), ecx=self.closure)
        delay = struct.unpack('<d', self.u.mem_read(self.output, 8))[0]
        assert delay == struct.unpack('<f', struct.pack('<f', .1))[0]
        current = self.p.word(self.owner + 16)
        return (current - self.records) // 44 if current else -1, list(self.trace)

    def name_id(self, text):
        source = self.p.allocate(len(text) + 1)
        self.u.mem_write(source, text + b'\0')
        self.p.call(0x124b9b0, (source,))
        return self.u.reg_read(self.r.UC_X86_REG_EAX)

    def returned(self, value=0, cleanup=0):
        stack = self.u.reg_read(self.r.UC_X86_REG_ESP)
        self.u.reg_write(self.r.UC_X86_REG_ESP, stack + 4 + cleanup)
        self.u.reg_write(self.r.UC_X86_REG_EAX, value & 0xffffffff)
        self.u.reg_write(self.r.UC_X86_REG_EIP, self.p.word(stack))

    def wrapper_hook(self, u, address):
        stack = u.reg_read(self.r.UC_X86_REG_ESP)
        arg = lambda n:self.p.word(stack+4+4*n)
        if address == 0xfb3610:
            self.trace.append(('arity', self.count)); self.returned(self.count)
        elif address == 0xfb3390:
            self.returned(arg(1) + 1)
        elif address in (0x690190,0x68fba0,0x68fe20):
            index = arg(0)-1
            self.trace.append(('convert',index,{0x690190:'Vector2',0x68fba0:'float',0x68fe20:'string'}[address]))
            if self.fail == index:
                self.returned(-1)
            else:
                value = self.arguments[index]
                if address == 0x690190:
                    u.mem_write(arg(1),struct.pack('<2f',*value))
                elif address == 0x68fba0:
                    u.mem_write(arg(1),struct.pack('<f',value))
                else:
                    raw = value.encode('ascii')
                    destination = arg(1)
                    if len(raw)<16: u.mem_write(destination,raw+b'\0')
                    else:
                        allocated=self.p.allocate(len(raw)+1);u.mem_write(allocated,raw+b'\0')
                        self.put(destination,allocated)
                    self.put(destination+16,len(raw),max(len(raw),15))
                self.returned()
        elif address == 0x8a3450:
            self.created = bytes(u.mem_read(arg(0),44)); self.returned(cleanup=4)
        elif address == 0x11e7c30:
            self.returned()
        elif address == 0x68e100:
            self.trace.append(('arity-error',)); self.returned()
        elif address in (0x13dad0d,0x13dad25):
            import ctypes
            function = getattr(ctypes.CDLL(None),'sinf' if address==0x13dad0d else 'cosf')
            function.argtypes=[ctypes.c_float];function.restype=ctypes.c_float
            value=struct.unpack('<f',struct.pack('<I',u.reg_read(self.r.UC_X86_REG_XMM0)&0xffffffff))[0]
            output=struct.unpack('<I',struct.pack('<f',function(value)))[0]
            u.reg_write(self.r.UC_X86_REG_XMM0,output)
            self.returned()
        else: return False
        return True

    def wrapper(self, arguments, count=7, fail=-1):
        self.wrapper_mode=True;self.arguments=arguments;self.count=count;self.fail=fail
        self.created=None;self.trace=[]
        self.put(0x1d34ea0,self.owner)
        try:
            self.p.call(0x8a3840,(0,0))
            return self.u.reg_read(self.r.UC_X86_REG_EAX),list(self.trace),self.created
        finally: self.wrapper_mode=False
