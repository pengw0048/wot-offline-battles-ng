"""Execute #1513 fashion wrappers and embedded Svarog dispatch instructions.

Tuple arity, Python error reporting and child virtual functions are observable
boundaries. The original factory initializes an empty native component owner;
seeded children only exercise that owner's original dispatch loop.
"""
import struct
from unicorn import UC_HOOK_CODE
from unicorn import x86_const as r
from stock_tree_lod_oracle import OriginalTreeInstructionOracle


class OriginalFashionLifecycle:
    def __init__(self, executable):
        self.p = OriginalTreeInstructionOracle(executable)
        self.u = self.p.emulator
        self.owner = self.p.allocate(0x1e8)
        self.args = self.p.allocate(16)
        self.activate_child = 0x3000d000
        self.deactivate_child = 0x3000d010
        self.children = {}
        self.receipts = []
        self.arity = 0
        self.u.mem_write(self.activate_child, b'\xc3')
        self.u.mem_write(self.deactivate_child, b'\xc3')
        self.u.hook_add(UC_HOOK_CODE, self.hook)

    def returned(self, value=0):
        stack = self.u.reg_read(r.UC_X86_REG_ESP)
        self.u.reg_write(r.UC_X86_REG_EAX, value)
        self.u.reg_write(r.UC_X86_REG_ESP, stack + 4)
        self.u.reg_write(r.UC_X86_REG_EIP, self.p.word(stack))

    def hook(self, unused, address, size, data):
        if address == 0xfb3610:  # PyTuple_Size
            self.returned(self.arity)
        elif address == 0x68e100:  # Original wrapper argument error.
            self.receipts.append('arity-error')
            self.returned()
        elif address in (self.activate_child, self.deactivate_child):
            self.receipts.append(self.children[self.u.reg_read(r.UC_X86_REG_ECX)])
            self.returned()

    def check_factories(self):
        # The real base constructor initializes ComponentSystem at +0x0c;
        # the vehicle factory calls that constructor before its vtable override.
        self.u.mem_write(self.owner, b'\xcc' * 0x1e8)
        self.p.call(0x13f0240, ecx=self.owner + 0xc)
        assert [self.p.word(self.owner + offset) for offset in (0x14, 0x18, 0x1c)] == [0, 0, 0]
        import capstone
        decoder = capstone.Cs(capstone.CS_ARCH_X86, capstone.CS_MODE_32)
        def instructions(address, count):
            return list(decoder.disasm(bytes(self.u.mem_read(address, count)), address))
        base = instructions(0xaecc60, 0x1a3)
        factory = instructions(0xaed680, 0x65)
        vehicle = instructions(0xaed6f0, 0x1ee)
        assert any(i.mnemonic == 'call' and i.op_str == '0x13f0240' for i in base)
        assert any(i.mnemonic == 'call' and i.op_str == '0xaecc60' for i in factory)
        assert any(i.mnemonic == 'call' and i.op_str == '0xaecc60' for i in vehicle)
        for table in (0x150e404, 0x150e4c4):
            assert self.p.word(table) == 0x13f0330
            assert self.p.word(table + 4) == 0x13f0410

    def dispatch(self, count, activate, arity=0):
        self.arity = arity
        self.receipts = []
        self.children.clear()
        self.u.mem_write(self.owner + 0xc, struct.pack('<I', 0x150e4c4))
        entries = self.p.allocate(max(count * 0x48, 1))
        self.u.mem_write(self.owner + 0x14, struct.pack('<III', entries, entries + count * 0x48,
                                                     entries + count * 0x48))
        table = self.p.allocate(8)
        self.u.mem_write(table, struct.pack('<II', self.activate_child, self.deactivate_child))
        for index in range(count):
            child = self.p.allocate(4)
            self.u.mem_write(child, struct.pack('<I', table))
            self.u.mem_write(entries + index * 0x48 + 0x28, struct.pack('<I', child))
            self.children[child] = index
        self.p.call(0xaed8e0 if activate else 0xaeda70, (self.owner + 4, self.args))
        return self.receipts
