"""Execute the #1513 compositor's original per-pixel x86 instructions.

Image decoding/scaling, resource allocation and the CRT float-to-integer
conversion (finite inputs in [0, 255.5]) are explicit boundaries here.
The native blend order, floating-point arithmetic, clamping, quantization and
SetPixel byte writes and row addressing all execute unchanged.
"""
import struct
import unicorn
from unicorn.x86_const import (UC_X86_REG_EBP, UC_X86_REG_ESI, UC_X86_REG_EDI,
                              UC_X86_REG_ESP, UC_X86_REG_XMM1, UC_X86_REG_XMM2,
                              UC_X86_REG_MXCSR, UC_X86_REG_XMM0, UC_X86_REG_EAX,
                              UC_X86_REG_EIP, UC_X86_REG_ECX)


class OriginalCamouflagePixel:
    def __init__(self, executable):
        self.u = unicorn.Uc(unicorn.UC_ARCH_X86, unicorn.UC_MODE_32)
        top = max(address + size for address, size, raw in executable.sections)
        self.u.mem_map(executable.base, (top + 4095) & ~4095)
        for address, size, raw in executable.sections:
            self.u.mem_write(executable.base + address, executable.data[raw:raw + size])
        self.stack, self.pixels = 0x5000000, 0x6100000
        self.u.mem_map(self.stack, 0x100000)
        self.u.mem_map(self.pixels, 0x40000)
        self.result = None
        self.image_output = None
        self.u.hook_add(unicorn.UC_HOOK_CODE, self._output, begin=0xb64770, end=0xb64770)
        self.u.hook_add(unicorn.UC_HOOK_CODE, self._integer, begin=0x13d81d0, end=0x13d81d0)

    def _integer(self, u, address, size, unused):
        value = struct.unpack('<f', int(u.reg_read(UC_X86_REG_XMM0) & 0xffffffff).to_bytes(4, 'little'))[0]
        assert 0.0 <= value <= 255.5
        stack = u.reg_read(UC_X86_REG_ESP)
        target = struct.unpack('<I', u.mem_read(stack, 4))[0]
        u.reg_write(UC_X86_REG_EAX, int(value))
        u.reg_write(UC_X86_REG_ESP, stack + 4)
        u.reg_write(UC_X86_REG_EIP, target)

    def _output(self, u, address, size, unused):
        stack = u.reg_read(UC_X86_REG_ESP)
        self.result = struct.unpack('<I', u.mem_read(stack + 12, 4))[0]
        if self.image_output is not None:
            unused_target, x, y = struct.unpack('<3I', u.mem_read(stack, 12))
            assert x < self.image_width and y < self.image_height
            index = y * self.image_width + x
            assert self.image_output[index] is None
            self.image_output[index] = self.result
            self.image_remaining -= 1
            return
        u.emu_stop()

    def prepare(self, colors, weights):
        frame = self.stack + 0x80000
        inverse = struct.unpack('<f', struct.pack('<f', 1.0 / 255.0))[0]
        palettes = []
        for i, color in enumerate(colors):
            destination = frame - 0x110 - i * 16
            self.u.mem_write(self.stack + 0x40000, struct.pack('<II', 0x738f5d, color))
            self.u.reg_write(UC_X86_REG_ESP, self.stack + 0x40000)
            self.u.reg_write(UC_X86_REG_ECX, destination)
            self.u.emu_start(0x738eb0, 0x738f5d, count=1000)
            values = struct.unpack('<4f', self.u.mem_read(destination, 16))
            palettes.append(values)
        for i, value in enumerate(weights):
            self.u.mem_write(frame - 0xbc - i * 4, struct.pack('<f', value))
        self.u.mem_write(frame - 0x98, b'\0' * 4)
        self.u.reg_write(UC_X86_REG_ESP, self.stack + 0x40000)
        self.u.reg_write(UC_X86_REG_EBP, frame)
        self.u.reg_write(UC_X86_REG_ESI, self.pixels)
        self.u.reg_write(UC_X86_REG_EDI, 0)
        self.u.reg_write(UC_X86_REG_XMM1, int.from_bytes(struct.pack('<f', inverse), 'little'))
        self.u.reg_write(UC_X86_REG_XMM2, int.from_bytes(struct.pack('<4f', *palettes[0]), 'little'))
        self.u.reg_write(UC_X86_REG_MXCSR, 0x1f80)
        self.result = None
        return frame

    def pixel(self, mask, background, colors, weights):
        return struct.unpack('<I', self.image(struct.pack('<I', mask), struct.pack('<I', background),
                                              1, 1, colors, weights))[0]

    def image(self, mask, background, width, height, colors, weights):
        assert len(mask) == len(background) == width * height * 4
        assert len(mask) + len(background) <= 0x40000
        frame = self.prepare(colors, weights)
        self.u.mem_write(self.pixels, mask + background)
        for offset, value in ((0x94, self.pixels + len(mask)), (0x74, self.pixels),
                              (0x78, width), (0x7c, height)):
            self.u.mem_write(frame - offset, struct.pack('<I', value))
        self.image_width, self.image_height = width, height
        self.image_remaining = width * height
        self.image_output = [None] * self.image_remaining
        output = self.pixels + 0x20000
        # Original RawImage::Create(1,1,...) and ImageData layout. Its SetPixel
        # implementation runs unchanged, including the format-1 channel map.
        self.u.mem_write(frame - 0xf0, struct.pack('<5I', 1, 0, 0x10000, frame - 0xdc, 0))
        self.u.mem_write(frame - 0xdc, struct.pack('<5I', width, height, width * 4,
                                                 width * height * 4, output))
        self.u.mem_write(output, b'\xcd' * width * height * 4)
        try:
            self.u.emu_start(0xaac5c0, 0xaacb36, count=width * height * 2000)
            if self.image_remaining:
                raise AssertionError('Original compositor did not finish its image loop')
            return bytes(self.u.mem_read(output, width * height * 4))
        finally:
            self.image_output = None
