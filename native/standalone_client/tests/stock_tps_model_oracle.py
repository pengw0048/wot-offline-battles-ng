"""Execute original fa31c0 model controllers, base motion and real curve readers."""
import argparse
import json
from pathlib import Path
import random
import struct
import subprocess


def audit(client, probe, destination=None):
    import pefile
    from unicorn import Uc, UC_ARCH_X86, UC_MODE_32
    from unicorn.x86_const import UC_X86_REG_ESP, UC_X86_REG_ECX
    pe = pefile.PE(str(Path(client) / 'WorldOfTanks.exe'))
    base = pe.OPTIONAL_HEADER.ImageBase
    image = pe.get_memory_mapped_image()
    engine = Uc(UC_ARCH_X86, UC_MODE_32)
    engine.mem_map(base, (len(image) + 4095) // 4096 * 4096)
    engine.mem_write(base, image)
    engine.mem_map(0x30000000, 0x40000)
    owner, parent, context, stack, terminal, methods, height = (
        0x30000000, 0x30001000, 0x30003000, 0x30020000, 0x30030000, 0x30031000, 0x30032000)
    # Only platform consumers are replaced: recording the output matrix and
    # supplying the real height callback result. All controller/curve/motion
    # instructions execute unchanged in the original image.
    table = bytearray(engine.mem_read(0x174ee0c, 0x90))
    struct.pack_into('<I', table, 0x80, methods + 0x100)
    struct.pack_into('<I', table, 0x84, methods + 0x110)
    engine.mem_write(methods, bytes(table))
    engine.mem_write(methods + 0x100, b'\xc2\x0c\x00')
    engine.mem_write(methods + 0x110, b'\xd9\x05' + struct.pack('<I', height) + b'\xc2\x08\x00')
    scalar = (.2, -1., .4)
    for index, member in enumerate((0x7a8, 0x794, 0x798, 0x79c, 0x7a4)):
        curve = parent + 0x1000 + index * 0x100
        colour = index == 4
        engine.mem_write(curve, struct.pack('<I', 0x174e3b0 if colour else 0x174e3a0))
        engine.mem_write(curve + 8, struct.pack('<II', 3, curve + 0x40))
        engine.mem_write(curve + 0x14, struct.pack('<I', curve + 0x60))
        engine.mem_write(curve + 0x40, struct.pack('<3f', 0., .5, 1.))
        values = (.2, .4, .6, .8, 1., 2., 3., 4., -.1, -.2, -.3, -.4) if colour else scalar
        engine.mem_write(curve + 0x60, struct.pack('<%df' % len(values), *values))
    rng = random.Random(1513)
    f32 = lambda value: struct.unpack('<f', struct.pack('<f', value))[0]
    inputs, expected = [], []
    for index in range(4000):
        elapsed, ground = f32(rng.uniform(0, .25)), f32(rng.uniform(-20, 40))
        paused, has_source, stopped = int(index % 13 == 0), int(index % 3 != 0), int(index % 7 == 0)
        flags = rng.choice([0, 0x80, 0x40, 0x201, 1, 0x800, 0x4000, 0x10000,
                            0x2000, 0x200000, 0x216800])
        rotation_flags = rng.choice([0, 1, 2, 4, 7, 8, 15])
        own_identity, identity, children = (-1 if index % 5 else index), index + (index % 7), index % 2
        values = [f32(rng.uniform(-20, 20)) for unused in range(6)] + [
            f32(rng.uniform(.2, 2)), f32(rng.uniform(.2, 2)), f32(rng.uniform(-.1, .2)),
            f32(rng.uniform(.05, 10)), f32(rng.uniform(0, 10)), f32(rng.uniform(-1, 1)),
            f32(rng.uniform(-.2, 3))] + [f32(rng.uniform(-10, 10)) for unused in range(16)]
        engine.mem_write(owner, b'\0' * 0x178)
        engine.mem_write(owner, struct.pack('<I', methods))
        engine.mem_write(owner + 0x18, struct.pack('<I', children))
        engine.mem_write(owner + 0x20, struct.pack('<I', flags))
        engine.mem_write(owner + 0x34, struct.pack('<I', parent))
        engine.mem_write(owner + 0xa8, struct.pack('<q', own_identity))
        engine.mem_write(0x1a5beb0, struct.pack('<q', identity))
        engine.mem_write(0x2143681, bytes([paused]))
        engine.mem_write(owner + 0xd0, bytes([stopped]))
        engine.mem_write(owner + 0xf4, struct.pack('<I', rotation_flags))
        offsets = (0x38, 0x3c, 0x40, 0x48, 0x4c, 0x50, 0x44, 0xe0, 0x6c, 0x70, 0x74,
                   0x78, 0xb0, 0xb4, 0xb8, 0xbc, 0xd4, 0xd8, 0xdc,
                   0xe8, 0xec, 0xf0, 0xf8, 0xfc, 0x100, 0x104, 0x108, 0x10c, 0x110)
        for offset, value in zip(offsets, values):
            engine.mem_write(owner + offset, struct.pack('<f', value))
        engine.mem_write(height, struct.pack('<f', ground))
        for i, member in enumerate((0x7a8, 0x794, 0x798, 0x79c, 0x7a4)):
            engine.mem_write(parent + member, struct.pack('<I', parent + 0x1000 + i * 0x100 if has_source else 0))
        engine.mem_write(stack, struct.pack('<IfII', terminal, elapsed, 0, context))
        engine.reg_write(UC_X86_REG_ESP, stack)
        engine.reg_write(UC_X86_REG_ECX, owner)
        engine.emu_start(0xfa31c0, terminal, count=100000)
        state = [struct.unpack('<I', engine.mem_read(owner + offset, 4))[0] for offset in
                 (0x20, 0xf4)] + [engine.mem_read(owner + 0xd0, 1)[0]]
        state += [struct.unpack('<I', engine.mem_read(owner + offset, 4))[0] for offset in
                  (0x38, 0x3c, 0x40, 0x48, 0x4c, 0x50, 0x44, 0xe0, 0x6c, 0x74, 0xb0,
                   0xe8, 0xec, 0xf0, 0x104, 0x108, 0x10c, 0x110)]
        expected.append(' '.join(map(str, state)) + ' ')
        inputs.append(' '.join([format(elapsed, '.9g'), format(ground, '.9g')] + [str(value) for value in
            (paused, has_source, flags, own_identity, identity, children, rotation_flags, stopped)] +
            [format(value, '.9g') for value in values]))
    data = '\n'.join(inputs) + '\n'
    actual = subprocess.run([str(probe)], input=data, capture_output=True, text=True, check=True).stdout.splitlines()
    if actual != expected:
        failures = [(i, a, b, inputs[i]) for i, (a, b) in enumerate(zip(actual, expected)) if a != b]
        raise AssertionError('Original tPS model mismatch: %r' % failures[:3])
    if destination:
        destination = Path(destination)
        destination.mkdir(parents=True, exist_ok=True)
        (destination / 'model-inputs.txt').write_text(data)
        (destination / 'model-expected.txt').write_text('\n'.join(expected) + '\n')
    return {'originalFunctions': ['00fa31c0', '00fa3770', '00fa2e30'],
            'cases': len(inputs), 'bitwiseMismatches': 0, 'status': 'PASS'}


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('client', type=Path)
    parser.add_argument('probe', type=Path)
    parser.add_argument('--output', type=Path)
    args = parser.parse_args()
    print(json.dumps(audit(args.client, args.probe, args.output)))
