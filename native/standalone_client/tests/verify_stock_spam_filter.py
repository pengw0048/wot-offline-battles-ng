#!/usr/bin/env python3
"""Compare the production kernel with #1513 x86 instructions, using explicit CRT fixtures.

The original string copy, ctype facet and wcsncmp calls are substituted. All
classification decisions, loops, mutation offsets and deletion code execute
from the original PE. This proves algorithm parity under the supplied facets;
it does not reproduce the Windows locale database or prove in-game chat UI.
"""
import argparse
import random
import struct
import subprocess

import pefile
from unicorn import Uc, UC_ARCH_X86, UC_MODE_32, UC_HOOK_CODE
from unicorn.x86_const import UC_X86_REG_EAX, UC_X86_REG_ECX, UC_X86_REG_EIP, UC_X86_REG_ESP


class Original:
    OWNER, LOCALES = 0x03000000, 0x03001000
    SOURCE, OUTPUT = 0x03002000, 0x03003000
    SOURCE_DATA, OUTPUT_DATA = 0x03010000, 0x03020000
    STACK, STOP = 0x0400f000, 0x05000000
    IS, LOWER, COMPARE = 0x05001000, 0x05001010, 0x05001020

    def __init__(self, executable):
        pe = pefile.PE(str(executable))
        image = pe.get_memory_mapped_image()
        base = pe.OPTIONAL_HEADER.ImageBase
        if base != 0x400000 or image[0x2e06b0:0x2e06b6] != bytes.fromhex('558bec6aff68'):
            raise AssertionError('Expected the inspected Chinese #1513 WGSpamFilter implementation')
        self.uc = Uc(UC_ARCH_X86, UC_MODE_32)
        self.uc.mem_map(base, (len(image) + 4095) & ~4095)
        self.uc.mem_write(base, image)
        for address, size in ((0, 4096), (self.OWNER, 0x40000), (0x04000000, 0x10000), (self.STOP, 0x2000)):
            self.uc.mem_map(address, size)
        for address, target in ((0x1491748, self.IS), (0x149174c, self.LOWER), (0x1492078, self.COMPARE)):
            self.write(address, target)
        for address in (0x687950, 0x6e0080, self.IS, self.LOWER, self.COMPARE, self.STOP):
            self.uc.hook_add(UC_HOOK_CODE, self.call, begin=address, end=address)

    def read(self, address):
        return struct.unpack('<I', self.uc.mem_read(address, 4))[0]

    def write(self, address, *values):
        self.uc.mem_write(address, struct.pack('<' + 'I' * len(values), *values))

    def unit(self, address):
        return struct.unpack('<H', self.uc.mem_read(address, 2))[0]

    def result(self, value, popped=0):
        stack = self.uc.reg_read(UC_X86_REG_ESP)
        self.uc.reg_write(UC_X86_REG_EAX, value & 0xffffffff)
        self.uc.reg_write(UC_X86_REG_EIP, self.read(stack))
        self.uc.reg_write(UC_X86_REG_ESP, stack + 4 + popped)

    def alpha(self, ch, index):
        return (65 <= ch <= 90 or 97 <= ch <= 122) if self.mode == 1 else ch == 65 and (self.mode != 3 or index == 1)

    def call(self, uc, address, unused_size, unused_data):
        stack = uc.reg_read(UC_X86_REG_ESP)
        if address == self.STOP:
            uc.emu_stop()
        elif address == 0x687950:
            output = uc.reg_read(UC_X86_REG_ECX)
            source = self.read(stack + 4)
            length = self.read(source + 16)
            uc.mem_write(self.OUTPUT_DATA, bytes(uc.mem_read(self.read(source), 2 * (length + 1))))
            self.write(output, self.OUTPUT_DATA, 0, 0, 0, length, 0x7ff)
            self.result(output, 4)
        elif address == 0x6e0080:
            self.result(self.read(stack + 4))
        elif address in (self.IS, self.LOWER):
            index = (uc.reg_read(UC_X86_REG_ECX) - self.LOCALES) // 8
            ch = self.read(stack + (8 if address == self.IS else 4)) & 0xffff
            if address == self.IS:
                mask = self.read(stack + 4)
                lower = (97 <= ch <= 122) if self.mode == 1 else self.mode == 2 and index == 0 and self.alpha(ch, index)
                self.result(int(self.alpha(ch, index) if mask == 0x103 else lower), 8)
            else:
                lowered = ch + 32 if self.mode == 1 and 65 <= ch <= 90 else ch if self.mode == 1 else ord('x') + index
                self.result(lowered, 4)
        elif address == self.COMPARE:
            first, second, width = (self.read(stack + offset) for offset in (4, 8, 12))
            result = 0
            for offset in range(width):
                a, b = self.unit(first + 2 * offset), self.unit(second + 2 * offset)
                if a != b:
                    result = a - b
                    break
                if a == 0:
                    break
            self.result(result)

    def apply(self, mode, units):
        self.mode = mode
        count = 0 if mode == 0 else 1 if mode == 1 else 2
        self.write(self.OWNER, 0, 0, self.LOCALES, self.LOCALES + count * 8, self.LOCALES + count * 8)
        self.uc.mem_write(self.SOURCE_DATA, struct.pack('<' + 'H' * (len(units) + 1), *units, 0))
        self.write(self.SOURCE, self.SOURCE_DATA, 0, 0, 0, len(units), 0x7ff)
        self.write(self.STACK, self.STOP, self.OUTPUT, self.SOURCE)
        self.uc.reg_write(UC_X86_REG_ESP, self.STACK)
        self.uc.reg_write(UC_X86_REG_ECX, self.OWNER)
        self.uc.emu_start(0x6e06b0, self.STOP + 1, count=5000000)
        if self.uc.reg_read(UC_X86_REG_EIP) != self.STOP:
            raise AssertionError('Original filter did not terminate')
        length = self.read(self.OUTPUT + 16)
        return tuple(struct.unpack('<' + 'H' * length, self.uc.mem_read(self.read(self.OUTPUT), length * 2)))


def cases():
    corpus = [(), (0,), (0xd83d, 0xde00), (0xd83d,), (0xde00,)]
    for text in ('AAaa', 'AAAa', 'AB  ', 'ABC  ', 'hello', 'ABCdef', 'AAAAAA'):
        corpus.append(tuple(map(ord, text)))
    for width in range(1, 8):
        for chunk in (tuple(range(97, 97 + width)), (0, *range(97, 96 + width)),
                      (13, *range(97, 96 + width)), (9, *range(97, 96 + width)),
                      (10, *range(97, 96 + width)), (32, *range(97, 96 + width)),
                      tuple((0xd83d, 0xde00, 0x4e16, 0x754c)[i % 4] for i in range(width))):
            for repeats in (3, 4, 5):
                for tail in ((), (120,)):
                    corpus.append(chunk * repeats + tail)
    rng = random.Random(1513)
    alphabet = (0, 9, 10, 13, 32, 65, 66, 67, 97, 98, 99, 0x4e16, 0x754c, 0xd83d, 0xde00)
    for unused in range(250):
        chunk = tuple(rng.choice(alphabet) for unused in range(rng.randrange(1, 9)))
        corpus.append(chunk * rng.randrange(1, 8) + tuple(rng.choice(alphabet) for unused in range(rng.randrange(9))))
    return [(mode, units) for mode in range(4) for units in corpus]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('probe', help='Compiled probe_stock_spam_filter executable')
    parser.add_argument('client', help='Inspected #1513 client directory')
    args = parser.parse_args()
    from pathlib import Path
    original = Original(Path(args.client) / 'WorldOfTanks.exe')
    inputs = cases()
    request = ''.join('%d %s\n' % (mode, ''.join('%04x' % ch for ch in units)) for mode, units in inputs)
    lines = subprocess.run([args.probe], input=request, text=True, capture_output=True, check=True).stdout.splitlines()
    assert len(lines) == len(inputs), (len(lines), len(inputs))
    for (mode, units), line in zip(inputs, lines):
        actual = tuple(int(line[offset:offset + 4], 16) for offset in range(0, len(line), 4))
        expected = original.apply(mode, units)
        assert actual == expected, (mode, units, actual, expected)
    print('Production UTF-16 kernel matched original #1513 instructions for %d cases; CRT facets were explicit fixtures' % len(inputs))


if __name__ == '__main__':
    main()
