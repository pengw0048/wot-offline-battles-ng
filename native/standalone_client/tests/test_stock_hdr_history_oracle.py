"""Execute #1513's HDR history allocation and initialization sequence.

The render-target pool and render context are faked at their call boundaries;
the allocation call site, zero-vector construction and native colour packing
run from the pinned executable. The CRT float-to-integer conversion is faked
with its input checked. This does not execute the GPU command.
"""
import os
from pathlib import Path
import struct
import unittest


class OriginalHdrHistoryTest(unittest.TestCase):
    def test_history_targets_receive_zero_after_allocation(self):
        client = os.environ.get('WOT_0922_CLIENT')
        if not client:
            self.skipTest('WOT_0922_CLIENT is required')
        try:
            import pefile
            import unicorn
            from unicorn.x86_const import (UC_X86_REG_EAX, UC_X86_REG_EBP,
                                           UC_X86_REG_ECX, UC_X86_REG_EDI,
                                           UC_X86_REG_EIP, UC_X86_REG_ESP,
                                           UC_X86_REG_XMM0)
        except ImportError:
            self.skipTest('pefile and unicorn are required')
        pe = pefile.PE(str(Path(client) / 'WorldOfTanks.exe'))
        self.assertEqual(pe.FILE_HEADER.Machine, 0x14c)
        self.assertEqual(pe.OPTIONAL_HEADER.ImageBase, 0x400000)
        image = pe.get_memory_mapped_image()
        uc = unicorn.Uc(unicorn.UC_ARCH_X86, unicorn.UC_MODE_32)
        uc.mem_map(0x400000, (len(image) + 0xfff) & ~0xfff)
        uc.mem_write(0x400000, image)
        uc.mem_map(0x3000000, 0x10000)
        frame, owner, context = 0x3008000, 0x3002000, 0x3003000
        pool, manager = 0x3004000, 0x3005000
        targets = {7: 0x3006000, 9: 0x3006100}
        events = []


        def read32(address):
            return struct.unpack('<I', uc.mem_read(address, 4))[0]

        def write32(address, value):
            uc.mem_write(address, struct.pack('<I', value))

        def finish(eax, argbytes):
            esp = uc.reg_read(UC_X86_REG_ESP)
            uc.reg_write(UC_X86_REG_EAX, eax)
            uc.reg_write(UC_X86_REG_EIP, read32(esp))
            uc.reg_write(UC_X86_REG_ESP, esp + 4 + argbytes)

        def hook(u, address, size, data):
            esp = u.reg_read(UC_X86_REG_ESP)
            if address == 0x9288d0:  # Render context singleton.
                finish(pool, 0)
            elif address == 0x999fc0:  # Allocate a target and return its pool index.
                output, ctx, width, height, fmt, zero, mode, one = (
                    read32(esp + 4 + i * 4) for i in range(8))
                index = (7, 9)[len([e for e in events if e[0] == 'allocate'])]
                events.append(('allocate', index, ctx, width, height, fmt, zero, mode, one))
                u.mem_write(output, bytes([index]))
                finish(output, 32)
            elif address == 0x999f50:  # Resolve pool index to target wrapper.
                output, index = read32(esp + 4), read32(esp + 8) & 0xff
                write32(output, targets[index])
                finish(output, 8)
            elif address == 0x13d81d0:  # CRT float-to-int conversion for zero channels.
                value = struct.unpack('<f', struct.pack('<I', u.reg_read(UC_X86_REG_XMM0) & 0xffffffff))[0]
                self.assertEqual(value, 0.5)
                finish(0, 0)
            elif address == 0x998ad0:  # Observe target and packed clear colour.
                target = u.reg_read(UC_X86_REG_ECX)
                ctx, colour = read32(esp + 4), read32(esp + 8)
                events.append(('zero_operation', target, ctx, read32(colour)))
                finish(0, 8)

        for address in (0x9288d0, 0x999fc0, 0x999f50, 0x998ad0, 0x13d81d0):
            uc.hook_add(unicorn.UC_HOOK_CODE, hook, begin=address, end=address)
        write32(frame - 0x70, context)
        write32(frame - 0x68, owner)
        write32(context + 0x40, manager)
        uc.reg_write(UC_X86_REG_EBP, frame)
        uc.reg_write(UC_X86_REG_ESP, frame - 0x400)
        uc.reg_write(UC_X86_REG_EDI, owner)
        try:
            uc.emu_start(0xa725d8, 0xa726b8, count=100000)
        except unicorn.UcError as error:
            self.fail("%s at %#x; events=%r" % (error, uc.reg_read(UC_X86_REG_EIP), events))
        self.assertEqual(uc.reg_read(UC_X86_REG_EIP), 0xa726b8)
        self.assertEqual(events, [
            ('allocate', 7, context, 1, 1, 0x25, 0, 1, 1),
            ('allocate', 9, context, 1, 1, 0x25, 0, 2, 1),
            ('zero_operation', targets[7], context, 0),
            ('zero_operation', targets[9], context, 0),
        ])
        self.assertEqual(bytes(uc.mem_read(owner + 0x70, 2)), b'\x07\x09')


if __name__ == '__main__':
    unittest.main()
