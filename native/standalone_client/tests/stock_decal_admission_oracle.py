"""Actual #1513 sort/cap and facade instructions; allocation/draw bridges only."""
import struct


class OriginalDecalAdmission:
    def __init__(self, executable):
        import pefile
        from unicorn import Uc, UC_ARCH_X86, UC_MODE_32, UC_HOOK_CODE
        from unicorn import x86_const as reg
        self.reg = reg
        pe = pefile.PE(str(executable))
        if pe.FILE_HEADER.Machine != 0x14c:
            raise ValueError('Original x86 client is required')
        self.image = pe.get_memory_mapped_image()
        self.base = pe.OPTIONAL_HEADER.ImageBase
        self.machine = Uc(UC_ARCH_X86, UC_MODE_32)
        self.machine.mem_map(self.base, (len(self.image) + 4095) & ~4095)
        self.machine.mem_write(self.base, self.image)
        self.machine.mem_map(0, 4096)
        self.machine.mem_map(0x20000000, 0x200000)
        self.owner, self.facade = 0x20000000, 0x20001000
        self.camera, self.limits = 0x20004000, 0x20005000
        self.stack, self.stop = 0x201f0000, 0x201ff000
        self.providers = [0x20006000 + i * 0x100 for i in range(5)]
        for i, provider in enumerate(self.providers):
            vtable = provider + 0x40
            self.put(provider, vtable)
            self.put(vtable + 0x30, self.stop + 0x100 + i * 0x10)
            self.put(vtable + 0x34, self.stop + 0x200 + i * 0x10)
        self.put(self.facade, self.owner)
        self.put(self.facade + 0x18, *self.providers)
        self.put(self.owner + 0x30, 0, self.limits)
        self.machine.hook_add(UC_HOOK_CODE, self.hook)
        self.prepared, self.emitted, self.phases, self.results = [], [], [], []
        self.cases = []
        self.draw_prefix = False

    def get(self, address, count=1):
        values = struct.unpack('<' + 'I' * count, self.machine.mem_read(address, count * 4))
        return values[0] if count == 1 else values

    def put(self, address, *values):
        self.machine.mem_write(address, struct.pack('<' + 'I' * len(values), *values))

    def return_call(self, arguments=0, result=0):
        m, r = self.machine, self.reg
        stack = m.reg_read(r.UC_X86_REG_ESP)
        destination = self.get(stack)
        m.reg_write(r.UC_X86_REG_ESP, stack + 4 + arguments)
        m.reg_write(r.UC_X86_REG_EAX, result)
        m.reg_write(r.UC_X86_REG_EIP, destination)

    def hook(self, machine, address, size, unused):
        r = self.reg
        stack = machine.reg_read(r.UC_X86_REG_ESP)
        if address == 0x73c5c0:  # Allocate the index vector; original iota/sort still execute.
            count = self.get(stack + 4)
            vector = machine.reg_read(r.UC_X86_REG_ECX)
            self.put(vector, 0x20010000, 0x20010000 + count * 4, 0x20010000 + count * 4, 0)
            self.return_call(8)
        elif address == 0x13dabf9:  # CRT memmove, with overlap preserved.
            destination, source, count = self.get(stack + 4, 3)
            machine.mem_write(destination, bytes(machine.mem_read(source, count)))
            self.return_call(result=destination)
        elif address == 0xa8f5b0:  # Allocate empty per-material-group work vectors.
            count = self.get(stack + 4)
            vector = machine.reg_read(r.UC_X86_REG_ECX)
            self.put(vector, 0x20020000, 0x20020000 + count * 16, 0x20020000 + count * 16)
            for group in range(count):
                data = 0x20030000 + group * 0x10000
                self.put(0x20020000 + group * 16, data, data + 0x10000, data + 0x10000, 0)
            self.return_call(8, vector)
        elif address == 0xa910dd:
            # Stop after the real sorted-prefix cap and real group validation.
            # Camera-kind conversion/96-byte packing are independently probed.
            frame = machine.reg_read(r.UC_X86_REG_EBP)
            workspace = self.get(frame - 0x4c)
            groups = self.get(frame - 0x1c)
            output = []
            for group in range(groups):
                data = self.get(workspace + group * 16)
                count = self.get(workspace + group * 16 + 12)
                output.append(list(self.get(data, count)) if count > 1 else ([self.get(data)] if count else []))
            self.results.append(output)
            machine.reg_write(r.UC_X86_REG_ESI, self.get(frame - 0xb0))
            machine.reg_write(r.UC_X86_REG_EDI, self.get(frame - 0xb4))
            machine.reg_write(r.UC_X86_REG_ESP, frame + 4)
            machine.reg_write(r.UC_X86_REG_EBP, self.get(frame))
            self.return_call(20)
        elif address == 0xf56eb0:  # Index-vector destruction after admission.
            self.return_call()
        elif address == 0x94b0e0:  # Borrow the camera, not an admission decision.
            self.return_call(result=self.camera)
        elif address == 0xa8fb90:  # GPU upload boundary, after all five providers.
            self.return_call(4)
        elif address == 0x96c240:
            self.return_call(result=0)  # Graphics-settings dirty flag, not admission.
        elif address == 0x6fb5d0:  # Append the material byte selected by the real phase switch.
            vector = machine.reg_read(r.UC_X86_REG_ECX)
            begin, end, capacity = self.get(vector, 3)
            if not begin:
                begin, end, capacity = 0x200f0000, 0x200f0000, 0x200f0100
            machine.mem_write(end, bytes(machine.mem_read(self.get(stack + 4), 1)))
            self.put(vector, begin, end + 1, capacity)
            self.return_call(4)
        elif address == 0xa90760 and not self.draw_prefix:  # Draw only: record phase and unchanged prepared-group base.
            context, groups, phase = self.get(stack + 4, 3)
            self.phases.append((context, groups, phase))
            self.return_call(12)
        elif self.stop + 0x100 <= address < self.stop + 0x150:
            provider = (address - self.stop - 0x100) // 16
            self.prepared.append(provider)
            self.return_call(4)
        elif self.stop + 0x200 <= address < self.stop + 0x250:
            provider = (address - self.stop - 0x200) // 16
            self.emitted.append(provider)
            context, metadata, records = self.get(stack + 4, 3)
            # Each real facade iteration must reset both scratch counts.
            if self.get(metadata + 12) != 0 or self.get(records + 12) != 0:
                raise AssertionError('Facade did not clear the previous provider')
            cap, groups, payload = self.cases[provider]
            self.configure(metadata, records, cap, groups, payload)
            self.return_call(12)

    def configure(self, metadata, records, cap, groups, payload):
        if len(payload) % 124 or len(payload) > 0x10000 or groups > 8:
            raise ValueError('Oracle workspace exceeded')
        self.put(self.limits, cap)
        self.put(metadata, 0x200c0000, 0x200c0000 + groups * 92, 0x200c0000 + groups * 92, groups)
        self.put(records, 0x200d0000, 0x200d0000 + len(payload), 0x200d0000 + len(payload), len(payload) // 124)
        if payload:
            self.machine.mem_write(0x200d0000, payload)

    def run(self, address, ecx, arguments):
        r = self.reg
        self.put(self.stack, self.stop, *arguments)
        self.machine.reg_write(r.UC_X86_REG_ESP, self.stack)
        self.machine.reg_write(r.UC_X86_REG_ECX, ecx)
        self.machine.reg_write(r.UC_X86_REG_MXCSR, 0x1f80)
        self.machine.emu_start(address, self.stop, count=3000000)
        if self.machine.reg_read(r.UC_X86_REG_EIP) != self.stop:
            raise AssertionError('Original instructions did not reach the return boundary')

    def phase_budget(self, cap, counts, phase):
        """Run the real phase selection and five-provider count/budget prefix."""
        if len(counts) != 5:
            raise ValueError('Exactly five prepared providers are required')
        groups = self.facade + 0x4c
        self.put(self.limits, cap)
        for provider, per_material in enumerate(counts):
            for material in range(8):
                self.put(groups + provider * 0x234 + 0x214 + material * 4,
                         per_material.get(material, 0))
        self.draw_prefix = True
        r, machine = self.reg, self.machine
        self.put(self.stack, self.stop, self.camera, groups, phase)
        machine.reg_write(r.UC_X86_REG_ESP, self.stack)
        machine.reg_write(r.UC_X86_REG_ECX, self.owner)
        machine.emu_start(0xa90760, 0xa90957, count=100000)
        self.draw_prefix = False
        if machine.reg_read(r.UC_X86_REG_EIP) != 0xa90957:
            raise AssertionError('Phase prefix did not reach the buffer budget boundary')
        frame = machine.reg_read(r.UC_X86_REG_EBP)
        begin, end = self.get(frame - 0x144, 2)
        return (self.get(frame - 0x158), self.get(frame - 0x15c),
                list(machine.mem_read(begin, end - begin)))

    def static_provider(self, inputs, fade_start, inverse_range):
        """Execute a4bcb0, including actual distance sort/fade/rect rejection/raw emission."""
        machine, r = self.machine, self.reg
        owner, cache, indices = 0x200b0000, 0x20080000, 0x200a0000
        subtexture = 0x200b1000
        if len(inputs) > 120:
            raise ValueError('Static provider workspace exceeded')
        machine.mem_write(owner, bytes(0x1000))
        self.put(owner + 4, cache)
        self.put(owner + 0x1c, indices, indices + len(inputs) * 4)
        machine.mem_write(owner + 0x8c, struct.pack('<f', fade_start))
        machine.mem_write(owner + 0x94, struct.pack('<f', inverse_range))
        machine.mem_write(subtexture + 10, struct.pack('<4H', 0, 0, 63, 63))
        for index, (distance, material, priority) in enumerate(inputs):
            entry = bytearray(88)
            struct.pack_into('<f', entry, 4, distance)
            struct.pack_into('<I', entry, 48, subtexture)
            struct.pack_into('<Ii', entry, 52, priority, material)
            # params[0] identifies the original source after provider distance sorting.
            struct.pack_into('<f', entry, 64, float(index))
            machine.mem_write(cache + index * 88, bytes(entry))
            self.put(indices + index * 4, index)
        self.put(0x20008000, 0x200c0000, 0x200c0000 + 92, 0x200c0000 + 92, 0)
        self.put(0x20008100, 0x200d0000, 0x200e0000, 0x200e0000, 0)
        self.put(self.stack, self.stop, self.camera, 0x20008000, 0x20008100)
        machine.reg_write(r.UC_X86_REG_ESP, self.stack)
        machine.reg_write(r.UC_X86_REG_ECX, owner)
        machine.reg_write(r.UC_X86_REG_MXCSR, 0x1f80)
        # The final cookie/return is outside the provider decisions under test.
        machine.emu_start(0xa4bcb0, 0xa4c029, count=3000000)
        if machine.reg_read(r.UC_X86_REG_EIP) != 0xa4c029:
            raise AssertionError('Static provider did not finish its emission loop')
        count = self.get(0x20008100 + 12)
        return bytes(machine.mem_read(0x200d0000, count * 124))

    def provider(self, cap, groups, payload):
        self.results = []
        self.configure(0x20008000, 0x20008100, cap, groups, payload)
        self.run(0xa91380, self.owner, [self.camera, 0x20008000, 0x20008100, 0x20009000])
        return self.results[0] if self.results else [[] for unused in range(groups)]

    def facade_collect_and_phases(self, cases):
        if len(cases) != 5:
            raise ValueError('The original facade has five providers')
        self.cases = cases
        self.prepared, self.emitted, self.results, self.phases = [], [], [], []
        self.put(self.facade + 0x38, 0)
        self.put(self.facade + 0x48, 0)
        self.run(0xa49990, self.facade, [self.camera, 0])
        for phase in range(4):
            self.run(0xa49a50, self.facade, [self.camera, phase, 0])
        return self.results
