"""Execute the pinned #1513 decimal and hexadecimal parser instructions.

No floating-point parsing routine is substituted. Only imported errno storage
and x87 rounding-control queries are supplied. The hexadecimal run stops at
Py_BuildValue after the original instructions wrote their actual double argument;
it does not emulate the subsequent Python tuple allocation or constructor.
"""
import struct


class OriginalPythonNumericOracle:
    def __init__(self, executable):
        import pefile
        from unicorn import Uc, UC_ARCH_X86, UC_MODE_32, UC_HOOK_CODE
        from unicorn import x86_const as reg
        p = pefile.PE(str(executable))
        if p.FILE_HEADER.Machine != 0x14c or p.FILE_HEADER.TimeDateStamp != 0x5a6edca4:
            raise ValueError('The parser oracle requires the pinned Chinese HD #1513 PE')
        self.reg = reg
        self.u = u = Uc(UC_ARCH_X86, UC_MODE_32)
        image = p.get_memory_mapped_image()
        u.mem_map(p.OPTIONAL_HEADER.ImageBase, (len(image)+4095) & ~4095)
        u.mem_write(p.OPTIONAL_HEADER.ImageBase, image)
        for address in (0x10000000, 0x20000000, 0x30000000):
            u.mem_map(address, 65536)
        self.imports = imports = {}
        self.counts = {}
        self.capture = []
        self.hexadecimal = False
        for table in p.DIRECTORY_ENTRY_IMPORT:
            for entry in table.imports:
                address = 0x30001000 + len(imports)*8
                imports[address] = entry.name.decode() if entry.name else str(entry.ordinal)
                u.mem_write(entry.address, struct.pack('<I', address))
                u.mem_write(address, b'\xc3')
        self.stop = 0x30000000
        self.output = 0x20004000
        # Store the genuine x87 return value, without conversion by host Python.
        u.mem_write(self.stop, b'\xdd\x1d' + struct.pack('<I', self.output) + b'\x90')

        def hook(machine, address, unused_size, unused_data):
            sp = u.reg_read(reg.UC_X86_REG_ESP)
            if self.hexadecimal and address == 0xfe1920:  # Py_BuildValue("(d)", x)
                self.capture.append(struct.unpack('<Q', u.mem_read(sp+8, 8))[0])
                u.emu_stop()
                return
            if self.hexadecimal and address == 0xfc4320:  # PyErr_SetString
                self.capture.append(None)
                u.emu_stop()
                return
            if address not in imports:
                return
            name = imports[address]
            self.counts[name] = self.counts.get(name, 0) + 1
            arg = lambda i: self.word(sp+4+4*i)
            result = 0
            if name == '_errno':
                result = 0x20003000
            elif name == '__control87_2':
                # Round-to-nearest, 53-bit precision, masked exceptions. The
                # original wrapper reads and retains these exact settings.
                if arg(2): u.mem_write(arg(2), struct.pack('<I', 0x9001f))
                if arg(3): u.mem_write(arg(3), struct.pack('<I', 0x9001f))
            elif name == '__fpe_flt_rounds':
                result = 1
            else:
                raise RuntimeError('Unresolved original parser import: ' + name)
            u.reg_write(reg.UC_X86_REG_EAX, result)
            u.reg_write(reg.UC_X86_REG_EIP, self.word(sp))
            u.reg_write(reg.UC_X86_REG_ESP, sp+4)
        u.hook_add(UC_HOOK_CODE, hook)

    def word(self, address):
        return struct.unpack('<I', self.u.mem_read(address, 4))[0]

    def setup(self, arguments):
        reg, u = self.reg, self.u
        sp = 0x10008000
        u.mem_write(sp, struct.pack('<'+'I'*(len(arguments)+1), self.stop, *arguments))
        u.reg_write(reg.UC_X86_REG_ESP, sp)
        u.reg_write(reg.UC_X86_REG_FPCW, 0x27f)
        u.reg_write(reg.UC_X86_REG_MXCSR, 0x1f80)

    def decimal(self, text):
        self.hexadecimal = False
        string, end = 0x20000000, 0x20002000
        self.u.mem_write(string, text.encode('ascii')+b'\0')
        self.u.mem_write(end, bytes(4))
        self.setup([string, end])
        self.u.emu_start(0x104a590, self.stop+6, count=100000)
        return struct.unpack('<Q', self.u.mem_read(self.output, 8))[0], self.word(end)-string

    def fromhex(self, text):
        self.hexadecimal = True
        self.capture.clear()
        # A real PyString layout lets the original PyString_AsStringAndSize
        # execute unchanged. Its source buffer is the exact caller-owned bytes.
        obj = 0x20000000
        self.u.mem_write(obj, struct.pack('<IIiii', 1, 0x1a61400, len(text), -1, 0)
                         + text.encode('ascii')+b'\0')
        self.setup([0x1a64370, obj])
        self.u.emu_start(0xfdbb40, self.stop, count=100000)
        if len(self.capture) != 1:
            raise RuntimeError('Original hexadecimal parser did not reach its output boundary')
        return self.capture[0]
