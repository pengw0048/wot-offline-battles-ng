"""Execute the original #1513 quality records and mip arithmetic without D3D."""
import struct


class TextureQualityOracle:
    def __init__(self, executable):
        import pefile
        from unicorn import Uc, UC_ARCH_X86, UC_MODE_32
        from unicorn.x86_const import UC_X86_REG_ESP
        self.registers = __import__('unicorn.x86_const', fromlist=[''])
        pe = pefile.PE(str(executable))
        image = pe.get_memory_mapped_image()
        self.uc = Uc(UC_ARCH_X86, UC_MODE_32)
        self.uc.mem_map(pe.OPTIONAL_HEADER.ImageBase, (len(image) + 4095) & ~4095)
        self.uc.mem_write(pe.OPTIONAL_HEADER.ImageBase, image)
        self.uc.mem_map(0x10000000, 0x100000)
        self.uc.mem_map(0x20000000, 0x1000)
        self.uc.reg_write(UC_X86_REG_ESP, 0x10080000)
        self.frame = 0x10080000
        self.object = 0x10010000

    def register(self, name, value=None):
        key = getattr(self.registers, 'UC_X86_REG_' + name)
        if value is not None:
            self.uc.reg_write(key, value)
        return self.uc.reg_read(key)

    def word(self, address, value=None):
        if value is not None:
            self.uc.mem_write(address, struct.pack('<I', value & 0xffffffff))
        return struct.unpack('<I', self.uc.mem_read(address, 4))[0]

    def options(self, anisotropy):
        self.register('EAX', anisotropy)
        self.uc.emu_start(0x94d9bd, 0x94d9ca)
        self.register('EBP', self.frame)
        self.register('ESP', self.frame - 0x200)
        self.uc.emu_start(0x94dbc8, 0x94dc70)
        self.word(self.frame - 0x108, 0)
        self.register('ESI', self.object)
        self.uc.emu_start(0x94dcb9, 0x94dd3b)
        stack = self.register('ESP')
        values = []
        for i, offset in enumerate((0x140, 0x134, 0x128, 0x11c)):
            record = self.frame - offset
            values.append((self.word(record), self.word(record + 4),
                           bool(self.uc.mem_read(record + 8, 1)[0]),
                           bool(self.word(stack + i * 24 + 8))))
        return values

    def logical_skip(self, quality_skip, lod_mode):
        stack = self.frame - 0x200
        self.uc.mem_write(stack, struct.pack('<III', 0x20000000, quality_skip, lod_mode))
        self.register('ESP', stack)
        self.uc.emu_start(0x94e900, 0x20000000)
        return self.register('EAX')

    def base_skip(self, logical_skip, split_count, logical_mips):
        self.register('EBP', self.frame)
        self.register('EDI', self.object)
        self.word(self.object + 0xa4, logical_mips)
        self.word(self.object + 0x24, logical_skip)
        self.word(self.frame - 0x1c, 0)
        self.word(self.frame - 0x18, split_count * 24)
        self.uc.emu_start(0x9b4395, 0x9b43cb)
        # Skip only the InputOptions constructor. The next raw instructions
        # consume the real clamp and split-count results from the first block.
        self.uc.emu_start(0x9b43d0, 0x9b43e2)
        return self.register('EAX')
