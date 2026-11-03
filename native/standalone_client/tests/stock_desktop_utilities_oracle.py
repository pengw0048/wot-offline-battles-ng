"""Execute original #1513 clipboard and locale wrappers with bounded OS calls.

No desktop clipboard or locale is read or changed. Win32 imports are explicit
in-memory boundaries; the original wrapper/control and string-copy instructions
run unchanged in the emulator.
"""
import struct
from unicorn import UC_HOOK_CODE
from unicorn import x86_const as r
from stock_tree_lod_oracle import OriginalTreeInstructionOracle


class OriginalDesktopUtilities:
    def __init__(self, executable):
        self.p = OriginalTreeInstructionOracle(executable)
        self.u = self.p.emulator
        self.operations = []
        self.blocks = {}
        self.opened = True
        self.locale_count = 2
        self.locale_day = 0
        self.arity = 0
        self.result = None
        imports = [(0x1491abc, 'open', 1), (0x1491948, 'empty', 0),
            (0x14913a4, 'allocate', 2), (0x14913a8, 'lock', 1),
            (0x1492094, 'copy', 3), (0x14913ac, 'unlock', 1),
            (0x149197c, 'set', 2), (0x1491980, 'close', 0),
            (0x14914ac, 'locale', 4)]
        self.imports = {}
        for index, (iat, name, count) in enumerate(imports):
            stub = 0x3000f400 + index * 8
            self.imports[stub] = (name, count)
            self.u.mem_write(iat, struct.pack('<I', stub))
        self.u.hook_add(UC_HOOK_CODE, self.hook)

    def returned(self, value=0, cleanup=0):
        stack = self.u.reg_read(r.UC_X86_REG_ESP)
        self.u.reg_write(r.UC_X86_REG_EAX, value & 0xffffffff)
        self.u.reg_write(r.UC_X86_REG_ESP, stack + 4 + cleanup)
        self.u.reg_write(r.UC_X86_REG_EIP, self.p.word(stack))

    def hook(self, unused, address, size, data):
        stack = self.u.reg_read(r.UC_X86_REG_ESP)
        if address in self.imports:
            name, count = self.imports[address]
            args = [self.p.word(stack + 4 + index * 4) for index in range(count)]
            self.operations.append((name, args))
            value = 1
            if name == 'open':
                assert args == [0]
                value = int(self.opened)
            elif name == 'allocate':
                assert args[0] == 0x2000  # Original GMEM_DDESHARE.
                value = self.p.allocate(args[1])
                self.blocks[value] = args[1]
            elif name == 'lock':
                assert args[0] in self.blocks
                value = args[0]
            elif name == 'copy':
                destination, source, chars = args
                assert self.blocks[destination] == chars * 2
                self.u.mem_write(destination, bytes(self.u.mem_read(source, chars * 2)))
                value = destination
            elif name == 'set':
                assert args[0] == 13 and args[1] in self.blocks
                self.result = bytes(self.u.mem_read(args[1], self.blocks[args[1]]))
                value = args[1]
            elif name == 'locale':
                assert args[0:2] == [0x400, 0x2000100c] and args[3] == 2
                self.u.mem_write(args[2], struct.pack('<i', self.locale_day))
                value = self.locale_count
            self.returned(value, 0 if name == 'copy' else 4 * count)
        elif address == 0xfb3610:  # PyTuple_Size
            self.returned(self.arity)
        elif address == 0xfe1200:  # PyInt_FromLong
            self.result = struct.unpack('<i', self.u.mem_read(stack + 4, 4))[0]
            self.returned(0x3000f800)
        elif address == 0x68e100:  # Original arity error helper.
            self.operations.append(('arity-error', []))
            self.returned(0)

    def copy(self, text, opened=True):
        self.operations = []
        self.result = None
        self.opened = opened
        data = text.encode('utf-16-le') + b'\0\0'
        chars = len(data) // 2 - 1
        owner = self.p.allocate(24)
        if chars < 8:
            self.u.mem_write(owner, data)
        else:
            source = self.p.allocate(len(data))
            self.u.mem_write(source, data)
            self.u.mem_write(owner, struct.pack('<I', source))
        self.u.mem_write(owner + 16, struct.pack('<II', chars, max(chars, 7)))
        self.p.call(0x6f5780, (owner,))
        return bool(self.u.reg_read(r.UC_X86_REG_EAX) & 255), self.result

    def first_day(self, day, count=2, arity=0):
        self.operations = []
        self.result = None
        self.locale_day = day
        self.locale_count = count
        self.arity = arity
        self.p.call(0x96f280, (0, 0))
        return self.result
