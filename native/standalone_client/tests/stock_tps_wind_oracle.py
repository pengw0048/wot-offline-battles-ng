"""Compare the original weather setter and Main wind/random expiry instructions."""
import argparse
import json
from pathlib import Path
import random
import struct
import subprocess


def audit(client, probe, destination=None):
    import pefile
    from unicorn import Uc, UC_ARCH_X86, UC_MODE_32
    from unicorn.x86_const import UC_X86_REG_ESP, UC_X86_REG_EBP
    pe = pefile.PE(str(Path(client) / 'WorldOfTanks.exe'))
    base = pe.OPTIONAL_HEADER.ImageBase
    image = pe.get_memory_mapped_image()
    engine = Uc(UC_ARCH_X86, UC_MODE_32)
    engine.mem_map(base, (len(image) + 4095) // 4096 * 4096)
    engine.mem_write(base, image)
    engine.mem_map(0x30000000, 0x10000)
    stack, terminal = 0x30001000, 0x30009000
    rng = random.Random(1513)
    f32 = lambda value: struct.unpack('<f', struct.pack('<f', value))[0]
    values = [f32(rng.randrange(32768) * f32(1 / 32767)) for unused in range(4096)]
    engine.mem_write(0x21436a0, struct.pack('<4096f', *values))
    inputs = [' '.join(format(value, '.9g') for value in values)]
    expected = []
    for index in range(4000):
        elapsed = f32(rng.uniform(0, 2))
        weather = [f32(rng.uniform(-50, 50)) for unused in range(3)]
        if index % 3 == 0: weather[1] = 0.
        remaining = 0. if index % 7 == 0 else elapsed if index % 11 == 0 else f32(rng.uniform(-1, 3))
        cursor = rng.randrange(4096)
        engine.mem_write(stack, struct.pack('<I3f', terminal, *weather))
        engine.reg_write(UC_X86_REG_ESP, stack)
        engine.emu_start(0xfa8100, terminal)
        engine.mem_write(0x21476a0, struct.pack('<I', cursor))
        engine.mem_write(0x2148918, struct.pack('<f', remaining))
        engine.mem_write(stack - 0xb4, struct.pack('<f', elapsed))
        engine.reg_write(UC_X86_REG_EBP, stack)
        # Stop before the unrelated epsilon/frame-budget portion of Main.
        engine.emu_start(0xfa4178, 0xfa4278)
        result = list(struct.unpack('<3I', engine.mem_read(0x1a5bf04, 12)))
        result += [struct.unpack('<I', engine.mem_read(address, 4))[0] for address in (0x2148918, 0x21476a0)]
        expected.append(' '.join(map(str, result)))
        inputs.append(' '.join(format(value, '.9g') for value in (elapsed, *weather, remaining)) + ' ' + str(cursor))
    data = '\n'.join(inputs) + '\n'
    actual = subprocess.run([str(probe)], input=data, text=True, capture_output=True, check=True).stdout.splitlines()
    if actual != expected:
        failures = [(i, a, b) for i, (a, b) in enumerate(zip(actual, expected)) if a != b]
        raise AssertionError('Original Main wind mismatch: %r' % failures[:3])
    if destination:
        destination = Path(destination)
        destination.mkdir(parents=True, exist_ok=True)
        (destination / 'wind-inputs.txt').write_text(data)
        (destination / 'wind-expected.txt').write_text('\n'.join(expected) + '\n')
    return {'originalFunctions': ['00fa8100', '00fa4178-00fa4278'], 'cases': len(expected),
            'bitwiseMismatches': 0, 'status': 'PASS'}


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('client', type=Path)
    parser.add_argument('probe', type=Path)
    parser.add_argument('--output', type=Path)
    args = parser.parse_args()
    print(json.dumps(audit(args.client, args.probe, args.output)))
