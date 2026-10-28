"""Run original AB1760 collection dispatch; CPython C API and GFx storage are boundaries."""
import struct
from unicorn import UC_HOOK_CODE
from unicorn import x86_const as r
from stock_tree_lod_oracle import OriginalTreeInstructionOracle


class OriginalCollections:
    def __init__(self, executable):
        self.p = OriginalTreeInstructionOracle(executable)
        self.u = self.p.emulator
        self.movie = self.p.allocate(0x30)
        self.u.mem_write(self.movie + 12, struct.pack('<I', self.movie + 16))
        self.set_type = 0x1a6e1d8
        self.child_type = self.p.allocate(0x100)
        self.other_type = self.p.allocate(0x100)
        self.values = {}
        self.iterator = 0
        self.iterations = []
        self.iter_index = 0
        self.iter_refs = []
        self.array_values = {}
        self.deallocated = []
        self.dealloc = 0x3000d000
        self.u.mem_write(self.dealloc, b'\xc3')
        self.iterator_type = self.p.allocate(0x100)
        self.u.mem_write(self.iterator_type + 0x18, struct.pack('<I', self.dealloc))
        self.u.hook_add(UC_HOOK_CODE, self.hook)

    def returned(self, value=0, cleanup=0):
        stack = self.u.reg_read(r.UC_X86_REG_ESP)
        self.u.reg_write(r.UC_X86_REG_EAX, value)
        self.u.reg_write(r.UC_X86_REG_ESP, stack + 4 + cleanup)
        self.u.reg_write(r.UC_X86_REG_EIP, self.p.word(stack))

    def object(self, typ, value=0):
        address = self.p.allocate(12)
        self.u.mem_write(address, struct.pack('<III', 10, typ, value))
        return address

    def result(self, address):
        typ = self.p.word(address + 12)
        if typ == 9:
            return ('array', self.array_values[address])
        if typ == 3:
            return ('number', self.p.word(address + 16))
        assert typ == 0, hex(typ)
        return ('undefined',)

    def hook(self, unused, address, size, data):
        stack = self.u.reg_read(r.UC_X86_REG_ESP)
        arg = lambda i: self.p.word(stack + 4 + i * 4)
        if address == 0xfbf700:  # PyType_IsSubtype
            self.returned(int(arg(0) == self.child_type and arg(1) == self.set_type))
        elif address == 0xb8d4f0:  # GFx::Movie::CreateArray
            output = arg(0)
            self.u.mem_write(output + 12, struct.pack('<I', 9))
            self.array_values[output] = []
            self.returned(cleanup=4)
        elif address == 0xfcb3b0:  # PyObject_GetIter
            self.iterator = self.object(self.iterator_type)
            self.u.mem_write(self.iterator, struct.pack('<I', 1))
            self.iter_index = 0
            self.returned(self.iterator)
        elif address == 0xfc9140:  # PyIter_Next, a new reference per yielded item
            assert arg(0) == self.iterator
            if self.iter_index == len(self.iterations):
                self.returned()
            else:
                item = self.iterations[self.iter_index]
                self.iter_index += 1
                self.u.mem_write(item, struct.pack('<I', self.p.word(item) + 1))
                self.iter_refs.append(item)
                self.returned(item)
        elif address == 0xfe0b10:  # PyInt_AsLong
            self.returned(self.p.word(arg(0) + 8))
        elif address == 0xab0c30:  # GFx::Value::SetElement
            output = self.u.reg_read(r.UC_X86_REG_ECX)
            assert arg(0) == len(self.array_values[output])
            self.array_values[output].append(self.result(arg(1)))
            self.returned(1, cleanup=8)
        elif address == 0xaa4f20:  # GFx::Value::~Value (storage boundary)
            self.returned()
        elif address == self.dealloc:
            self.deallocated.append(arg(0))
            self.returned()

    def convert(self, kind, sequence):
        self.iterations = [self.object(0x1a64bf0, n) for n in sequence]
        self.iter_refs = []
        self.deallocated = []
        source = self.object({'set': self.set_type, 'subclass': self.child_type,
                              'unsupported': self.other_type, 'frozenset': 0x1a6e2a0}[kind])
        output = self.p.allocate(32)
        self.p.call(0xab1760, (output, source, self.movie))
        if kind in ('set', 'subclass'):
            assert self.deallocated == [self.iterator]
            assert all(self.p.word(item) == 10 for item in self.iterations)
            assert self.iter_refs == self.iterations
        else:
            assert not self.iter_refs and not self.deallocated
        return self.result(output)
