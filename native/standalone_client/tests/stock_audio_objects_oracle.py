"""Execute the original audio wrappers and provider/volume producers.

Python conversion/allocation and the final SDK calls are explicit boundaries.
The instructions between those boundaries are the pinned x86 executable, not
an alternate audio implementation. SDK acceptance is tested separately.
"""
import struct


class OriginalAudioObjects:
    def __init__(self, executable):
        from stock_tree_lod_oracle import OriginalTreeInstructionOracle
        from unicorn import UC_HOOK_CODE
        from unicorn import x86_const as reg
        self.oracle = OriginalTreeInstructionOracle(executable)
        self.machine = self.oracle.emulator
        self.reg = reg
        self.trace = []
        self.count = 1
        self.conversion = 0
        self.owner = self.oracle.allocate(128)
        self.provider = self.oracle.allocate(32)
        self.provider_vtable = self.oracle.allocate(32)
        self.provider_call = self.oracle.allocate(32)
        self.put(self.provider, self.provider_vtable, 100)
        self.put(self.provider_vtable+8, self.provider_call)
        self.put(self.provider_call, 0xc3)
        self.matrix = bytes(64)
        self.sampled_providers = []
        self.machine.hook_add(UC_HOOK_CODE, self.hook)

    def put(self, address, *words):
        self.machine.mem_write(address, struct.pack('<'+'I'*len(words), *words))

    def string(self, address):
        result = bytearray()
        while True:
            value = bytes(self.machine.mem_read(address, 1))
            if value == b'\0':
                return bytes(result)
            result.extend(value); address += 1

    def returned(self, value=0, cleanup=0):
        m, r = self.machine, self.reg
        stack = m.reg_read(r.UC_X86_REG_ESP)
        destination = self.oracle.word(stack)
        m.reg_write(r.UC_X86_REG_ESP, stack+4+cleanup)
        m.reg_write(r.UC_X86_REG_EAX, value & 0xffffffff)
        m.reg_write(r.UC_X86_REG_EIP, destination)

    def hook(self, machine, address, size, unused):
        p, r = self.oracle, self.reg
        stack = machine.reg_read(r.UC_X86_REG_ESP)
        arg = lambda n: p.word(stack+4+n*4)
        if address == 0xfb3610:  # PyTuple_Size
            self.trace.append(('arity', self.count)); self.returned(self.count)
        elif address == 0xfb3390:  # PyTuple_GetItem
            self.trace.append(('item', arg(1))); self.returned(self.owner)
        elif address == 0x68fe20:  # Actual conversion is covered by CPython probe.
            self.trace.append(('string-conversion', self.conversion)); self.returned(self.conversion)
        elif address == 0x68e100:
            self.trace.append(('arity-error', self.string(arg(0)), arg(1), arg(2)))
            self.returned()
        elif address == 0x11e7c30:  # Empty/converted temporary string lifetime.
            self.returned()
        elif address == 0x68ed00:
            self.trace.append(('float', arg(0)))
            self.returned(self.owner)
        elif address == 0xfcff60:  # PyList_New; every call gets a fresh object.
            value = p.allocate(16); self.put(value, 1)
            self.trace.append(('list', arg(0), value)); self.returned(value)
        elif address == 0xfc3720:
            self.trace.append(('exception', arg(0), self.string(arg(1))))
            self.returned()
        elif address == self.provider_call:
            self.trace.append(('provider',))
            self.sampled_providers.append(machine.reg_read(r.UC_X86_REG_ECX))
            machine.mem_write(arg(0), self.matrix)
            self.returned(cleanup=4)
        elif address == 0x12a7f00:  # SDK SetPosition
            self.trace.append(('position', arg(0), arg(1), bytes(machine.mem_read(arg(2), 36))))
            self.returned(1)
        elif address == 0x12a7470:  # SDK SetGameObjectOutputBusVolume
            self.trace.append(('volume', arg(0), arg(1), arg(2), arg(3), arg(4)))
            self.returned(1)
        elif address == 0x69be50:
            self.trace.append(('matrix-result', arg(0))); self.returned(0x1a5c798)

    def wrapper(self, address, count=1, conversion=0):
        self.trace = []; self.count = count; self.conversion = conversion
        self.oracle.call(address, (self.owner, self.owner+32))
        result = self.machine.reg_read(self.reg.UC_X86_REG_EAX)
        return result, list(self.trace)

    def provider_position(self, matrix, offset, present=True):
        self.trace = []
        self.matrix = struct.pack('<16f', *matrix)
        words = struct.unpack('<3I', struct.pack('<3f', *offset))
        self.put(self.provider+4, 100)
        self.oracle.call(0x1250e10, (0x87654321, 0x12345678,
            self.provider if present else 0, *words))
        return list(self.trace)

    def provider_setter(self, matrix, detach=False, old_present=True):
        self.trace = []; self.sampled_providers = []
        self.matrix = struct.pack('<16f', *matrix)
        p = self.oracle
        table, records, handle, incoming, replacement = (p.allocate(n) for n in (64, 0x50, 8, 4, 32))
        self.put(self.owner+8, table)
        self.put(table+12, records); self.put(table+24, 1)
        self.put(handle, 0, 7); self.put(records+0x44, 7)
        self.put(records+0x24, self.provider if old_present else 0)
        self.put(records+0x30, 0x87654321, 0x12345678)
        self.put(self.provider+4, 100)
        self.put(replacement, self.provider_vtable, 100)
        self.put(incoming, 0 if detach else replacement)
        self.oracle.call(0x1251080, (handle, incoming), ecx=self.owner)
        return list(self.trace), list(self.sampled_providers), p.word(records+0x24), replacement

    def volume(self, bits, index=2, generation=7):
        self.trace = []
        table, records, handle = (self.oracle.allocate(n) for n in (64, 0x50*3, 8))
        self.put(self.owner+8, table)
        self.put(table+12, records); self.put(table+24, 3)
        self.put(records+0x50*2+0x30, 0x87654321, 0x12345678)
        self.put(records+0x50*2+0x44, 7)
        self.put(handle, index, generation)
        self.put(0x215aa70, 0x12345678, 0x87654321)
        self.oracle.call(0x1251280, (handle, bits), ecx=self.owner)
        return list(self.trace)

    def null_matrix_getter(self):
        tls, block = self.oracle.allocate(16), self.oracle.allocate(0x4000)
        self.put(0x2c, tls); self.put(tls, block)
        self.put(0x215e38c, 0); self.put(block+0x32f0, 0)
        self.put(0x215aa84, 0); self.put(0x215aa80, 0)
        return self.wrapper(0x12525e0)
