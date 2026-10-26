"""Compare the original Water constructor's float/divide/ceil grid producer."""
import argparse
import json
import math
from pathlib import Path
import random
import struct
import subprocess


def audit(client, probe, destination=None):
    import pefile
    from unicorn import Uc, UC_ARCH_X86, UC_MODE_32, UC_HOOK_CODE
    from unicorn.x86_const import UC_X86_REG_ESP, UC_X86_REG_EBP, UC_X86_REG_EBX, UC_X86_REG_EDI
    pe = pefile.PE(str(Path(client) / 'WorldOfTanks.exe'))
    base = pe.OPTIONAL_HEADER.ImageBase
    image = pe.get_memory_mapped_image()
    engine = Uc(UC_ARCH_X86, UC_MODE_32)
    engine.mem_map(base, (len(image) + 4095) // 4096 * 4096)
    engine.mem_write(base, image)
    engine.mem_map(0x30000000, 0x40000)
    state, owner, frame, stack, ceil_stub = 0x30000000, 0x30001000, 0x30002000, 0x30008000, 0x30030000
    imported = next(symbol.address for library in pe.DIRECTORY_ENTRY_IMPORT
                    for symbol in library.imports if symbol.name == b'ceil')
    engine.mem_write(imported, struct.pack('<I', ceil_stub))
    engine.mem_write(ceil_stub, b'\xdd\x44\x24\x04\xc3')

    def ceil(uc, unused_address, unused_size, unused_data):
        esp = uc.reg_read(UC_X86_REG_ESP)
        value = struct.unpack('<d', uc.mem_read(esp + 4, 8))[0]
        uc.mem_write(esp + 4, struct.pack('<d', math.ceil(value)))

    engine.hook_add(UC_HOOK_CODE, ceil, begin=ceil_stub, end=ceil_stub)
    rng = random.Random(1513)
    f32 = lambda value: struct.unpack('<f', struct.pack('<f', value))[0]
    inputs, expected = [], []
    for index in range(4000):
        tessellation = f32(rng.uniform(.01, 100))
        x, z = (f32(rng.uniform(0, 10000)) for unused in range(2))
        if index % 3 == 0:
            x = f32(tessellation * rng.randrange(1000))
            bits = struct.unpack('<I', struct.pack('<f', x))[0]
            x = struct.unpack('<f', struct.pack('<I', max(0, bits + index % 5 - 2)))[0]
        if index % 7 == 0:
            z = 0.
        engine.mem_write(state + 12, struct.pack('<2f', x, z))
        engine.mem_write(state + 24, struct.pack('<f', tessellation))
        engine.reg_write(UC_X86_REG_EBX, state)
        engine.reg_write(UC_X86_REG_EDI, owner)
        engine.reg_write(UC_X86_REG_EBP, frame)
        engine.reg_write(UC_X86_REG_ESP, stack - 16)
        # Stop after both original integer grid dimensions are stored.
        engine.emu_start(0xa260fe, 0xa26180, count=1000)
        gx, gz = struct.unpack('<2I', engine.mem_read(owner + 0x208, 8))
        expected.append('1 %d %d %d' % (gx, gz, gx * gz))
        inputs.append(' '.join(format(value, '.9g') for value in (x, z, tessellation)))
    data = '\n'.join(inputs) + '\n'
    actual = subprocess.run([str(probe)], input=data, capture_output=True, text=True, check=True).stdout.splitlines()
    if actual != expected:
        bad = [(i, a, b) for i, (a, b) in enumerate(zip(actual, expected)) if a != b]
        raise AssertionError('Original Water grid mismatch: %r' % bad[:3])
    if destination:
        destination = Path(destination)
        destination.mkdir(parents=True, exist_ok=True)
        (destination / 'grid-inputs.txt').write_text(data)
        (destination / 'grid-expected.txt').write_text('\n'.join(expected) + '\n')
    return {'originalFunction': '00a25ff0', 'instructionRange': '00a260fe-00a26180',
            'cases': len(expected), 'integerMismatches': 0, 'status': 'PASS'}


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('client', type=Path)
    parser.add_argument('probe', type=Path)
    parser.add_argument('--output', type=Path)
    args = parser.parse_args()
    print(json.dumps(audit(args.client, args.probe, args.output)))
