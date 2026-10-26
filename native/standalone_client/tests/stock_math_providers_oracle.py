"""Execute #1513 Bound/Combined/Smooth provider instructions without copied math.

Clamp/blend/replay/copy instructions execute directly from the original PE.
Input sampling and factory tuple/base allocation services are explicit harness
boundaries. D3DX cases capture the real caller arguments and stop before its
algorithm; the scaling output is an opaque sentinel for checking multiply order.
"""
import argparse
import json
from pathlib import Path
import random
import struct
import subprocess


def bits(value):
    return struct.unpack('<I', struct.pack('<f', value))[0]


class ProviderInstructionOracle:
    def __init__(self, executable):
        from stock_tree_lod_oracle import OriginalTreeInstructionOracle
        from unicorn import UC_HOOK_CODE
        from unicorn.x86_const import UC_X86_REG_ECX, UC_X86_REG_EIP, UC_X86_REG_ESP
        self.oracle = oracle = OriginalTreeInstructionOracle(executable)
        self.uc = uc = oracle.emulator
        self.owner = oracle.allocate(64)
        self.result = oracle.allocate(64)
        self.sources = [oracle.allocate(80), oracle.allocate(80)]
        self.replay = oracle.allocate(0x200)
        self.camera = oracle.allocate(64)
        vtable = oracle.allocate(16)
        self.write(vtable + 8, 0x3000e000)
        for source in self.sources:
            self.write(source, vtable)
        self.samples = []
        self.factory_count = None
        self.factory_record = None
        self.boundary_kind = None
        self.boundary_words = None
        self.scaling_words = []

        def sample(uc, address, size, context):
            stack = uc.reg_read(UC_X86_REG_ESP)
            source = uc.reg_read(UC_X86_REG_ECX)
            assert source in self.sources
            self.samples.append(self.sources.index(source))
            uc.mem_write(oracle.word(stack + 4), bytes(uc.mem_read(source + 16, 64)))
            uc.reg_write(UC_X86_REG_EIP, oracle.word(stack))
            uc.reg_write(UC_X86_REG_ESP, stack + 8)
        uc.hook_add(UC_HOOK_CODE, sample, begin=0x3000e000, end=0x3000e000)

        def factory_boundary(uc, address, size, context):
            if self.factory_count is None:
                return
            from unicorn.x86_const import UC_X86_REG_EAX
            stack = uc.reg_read(UC_X86_REG_ESP)
            cleanup = 0
            result = 0
            if address == 0xfb3610:
                result = self.factory_count
            elif address == 0x13d7fe9:
                size = oracle.word(stack + 4)
                result = oracle.allocate(size)
                uc.mem_write(result, bytes(size))
                self.factory_record['allocation_size'] = size
                self.factory_record['object'] = result
            elif address == 0x692ee0:
                self.factory_record['auto_tick'] = bool(oracle.word(stack + 4))
                self.factory_record['type'] = hex(oracle.word(stack + 8))
                cleanup = 8
            else:
                assert address == 0x68e100
                self.factory_record['argument_error'] = True
                assert oracle.word(stack + 8) == oracle.word(stack + 12) == 0
            uc.reg_write(UC_X86_REG_EAX, result)
            uc.reg_write(UC_X86_REG_EIP, oracle.word(stack))
            uc.reg_write(UC_X86_REG_ESP, stack + 4 + cleanup)
        for address in (0xfb3610, 0x13d7fe9, 0x692ee0, 0x68e100):
            uc.hook_add(UC_HOOK_CODE, factory_boundary, begin=address, end=address)

        def d3dx_boundary(uc, address, size, context):
            if self.boundary_kind is None:
                return
            from unicorn.x86_const import UC_X86_REG_EAX
            stack = uc.reg_read(UC_X86_REG_ESP)
            args = [oracle.word(stack + 4 + index * 4) for index in range(4)]
            if address in (0x9288d0, 0x9288e0):
                uc.reg_write(UC_X86_REG_EAX, self.camera)
                uc.reg_write(UC_X86_REG_EIP, oracle.word(stack))
                uc.reg_write(UC_X86_REG_ESP, stack + 4)
                return
            if address == 0x11c3bca:
                self.scaling_words = args[1:4]
                self.write(args[0], *[bits(100. + index) for index in range(16)])
                uc.reg_write(UC_X86_REG_EAX, args[0])
                uc.reg_write(UC_X86_REG_EIP, oracle.word(stack))
                uc.reg_write(UC_X86_REG_ESP, stack + 20)
                return
            if address == 0x11c3bd0:
                self.boundary_words = args[1:4]
            elif address == 0x11c3bb8:
                self.boundary_words = list(struct.unpack('<16I', uc.mem_read(args[2], 64)))
                self.boundary_words += [int(args[0] == args[2]), int(args[1] == 0)]
            elif address == 0x11c3bc4:
                self.boundary_words = self.scaling_words + list(struct.unpack('<16I', uc.mem_read(args[1], 64)))
                self.boundary_words += list(struct.unpack('<16I', uc.mem_read(args[2], 64)))
                self.boundary_words += [int(args[0] == args[2])]
            else:
                assert address == 0x11c3bbe
                self.boundary_words = list(struct.unpack('<3I', uc.mem_read(args[1], 12)))
                self.boundary_words += [int(args[0] == args[1])]
            uc.emu_stop()
        for address in (0x9288d0, 0x9288e0, 0x11c3bca, 0x11c3bd0,
                        0x11c3bb8, 0x11c3bc4, 0x11c3bbe):
            uc.hook_add(UC_HOOK_CODE, d3dx_boundary, begin=address, end=address)

    def write(self, address, *words):
        self.uc.mem_write(address, struct.pack('<' + 'I' * len(words), *words))

    def run(self, kind, flags, words):
        owner = self.owner
        self.uc.mem_write(owner, bytes(64))
        self.uc.mem_write(self.result, bytes([0xa5]) * 64)
        self.samples = []
        if kind == 0:
            assert len(words) == 22
            self.write(self.sources[0] + 16, *words[:16])
            self.write(owner + 16, self.sources[0] if flags & 1 else 0, *words[16:])
            self.oracle.call(0x6df150, (self.result,), ecx=owner)
            result = bytes(self.uc.mem_read(self.result, 64))
        elif kind == 1:
            assert len(words) == 32
            for index in range(2):
                self.write(self.sources[index] + 16, *words[index * 16:(index + 1) * 16])
            self.write(owner + 16, self.sources[0] if flags & 1 else 0,
                       self.sources[1] if flags & 2 else 0)
            self.oracle.call(0x6df230, (self.result,), ecx=owner)
            result = bytes(self.uc.mem_read(self.result, 64))
        elif kind < 4:
            assert kind in (2, 3) and len(words) == 8
            self.write(owner + 16, self.sources[0] if flags & 1 else 0, *words[:6])
            matrix = [0] * 16
            matrix[12:15] = words[3:6]
            self.write(self.sources[0] + 16, *matrix)
            self.write(0x1d2a448, self.replay if flags & 2 else 0)
            self.write(self.replay + 0x1c0, words[6])
            self.oracle.call(0x6dfbf0 if kind == 2 else 0x6dfaa0, (words[7],), ecx=owner)
            result = bytes(self.uc.mem_read(owner + 20, 12))
        else:
            assert kind in (4, 5, 6, 7) and len(words) == (20 if kind == 6 else 16)
            self.write(self.sources[0] + 16, *words[:16])
            self.write(owner + 16, self.sources[0] if flags & 1 else 0)
            if kind == 6:
                self.write(owner + 20, *words[16:19])
                self.write(self.camera + 12, words[19])
            self.boundary_kind = kind
            self.boundary_words = None
            self.scaling_words = []
            self.oracle.call({4: 0x6df440, 5: 0x6912c0, 6: 0x6df350, 7: 0x6dd860}[kind],
                             (self.result,), ecx=owner)
            self.boundary_kind = None
            if flags & 1:
                assert self.boundary_words is not None
                return [1] + self.boundary_words + [len(self.samples)] + self.samples
            assert self.boundary_words is None
            return [0] + list(struct.unpack('<16I', self.uc.mem_read(self.result, 64))) + [0]
        count = len(result) // 4
        return list(struct.unpack('<' + 'I' * count, result)) + [len(self.samples)] + self.samples

    def factories(self):
        from unicorn.x86_const import UC_X86_REG_EAX
        records = []
        entries = [('WGYawOnlyTransform', 0x6de7f0), ('WGInverseTransform', 0x6de2a0),
                   ('WGBoundMatrixProvider', 0x6de0e0), ('WGCombinedMP', 0x6de1d0),
                   ('WGTranslationOnlyMP', 0x6de620), ('WGStrategicAreaViewMP', 0x6de540),
                   ('WGSmoothTranslationOnlyMP', 0x6de450),
                   ('WGReplayAwaredSmoothTranslationOnlyMP', 0x6de360)]
        for name, address in entries:
            for count in (0, 1, 2, 5):
                self.factory_count = count
                self.factory_record = {'name': name, 'address': hex(address), 'arguments': count}
                self.oracle.call(address, (0, 0), ecx=self.owner)
                result = self.uc.reg_read(UC_X86_REG_EAX)
                if count:
                    assert result == 0 and self.factory_record.get('argument_error')
                    assert 'allocation_size' not in self.factory_record
                else:
                    assert result == self.factory_record.pop('object') + 4
                    assert self.factory_record['auto_tick'] == ('Smooth' in name)
                records.append(self.factory_record)
        self.factory_count = None
        self.factory_record = None
        return records


def cases(count=4000):
    rng = random.Random(1513)
    edges = [0, 0x80000000, bits(.01), bits(-.01), bits(.03), bits(.5),
             bits(1), bits(-1), 1, 0x80000001, 0x7f800000, 0xff800000,
             0x7fc00001, 0xffc00042, 0x7f7fffff, 0xff7fffff]
    def value(index):
        return edges[index % len(edges)] if index % 3 == 0 else bits(rng.uniform(-500, 500))
    for kind in range(4):
        for index in range(count):
            flags = index % (2 if kind == 0 else 4)
            length = 22 if kind == 0 else 32 if kind == 1 else 8
            words = [value(index + column) for column in range(length)]
            if kind >= 2:
                words[6] = edges[index % len(edges)]
                words[7] = bits(rng.uniform(-10, 10))
                if index % 11 == 0:
                    words[3:6] = words[:3]
                elif index % 11 == 1:
                    words[:6] = list(map(bits, [1, 2, 3, 1.001, 2.001, 3.001]))
            yield kind, flags, words
    for kind in range(4, 8):
        for index in range(count):
            words = [value(index + column) for column in range(16)]
            if kind == 6:
                words[13] = bits(rng.uniform(.001, 2000))
                words += [bits(rng.uniform(.001, 10)), bits(rng.uniform(.001, 10)),
                          bits(rng.uniform(.001, 2000)), bits(rng.uniform(.1, 5))]
            yield kind, index % 2, words


def prepare(client, output, count=4000, probe=None):
    oracle = ProviderInstructionOracle(Path(client) / 'WorldOfTanks.exe')
    output = Path(output)
    output.mkdir(parents=True, exist_ok=True)
    inputs, expected = [], []
    totals = [0] * 8
    for kind, flags, words in cases(count):
        inputs.append(' '.join(map(str, [kind, flags] + words)))
        expected.append(' '.join(map(str, oracle.run(kind, flags, words))))
        totals[kind] += 1
    input_path = output / 'math-provider-inputs.txt'
    input_path.write_text('\n'.join(inputs) + '\n')
    (output / 'math-provider-expected.txt').write_text('\n'.join(expected) + '\n')
    summary = {'original_instruction_cases': totals, 'source': 'Chinese HD #1513 x86',
               'boundary': 'Input sampling; exact D3DX caller arguments, not D3DX algorithm results'}
    summary['factories'] = oracle.factories()
    (output / 'math-provider-oracle.json').write_text(json.dumps(summary, indent=2) + '\n')
    if probe:
        actual = subprocess.run([str(probe), '--fixtures'], input=input_path.read_text(),
                                capture_output=True, text=True, check=True).stdout.splitlines()
        mismatches = [(i, a, e) for i, (a, e) in enumerate(zip(actual, expected)) if a.split() != e.split()]
        assert len(actual) == len(expected) and not mismatches, (len(actual), len(expected), mismatches[:3])
    print(json.dumps({'cases': totals, 'factory_argument_cases': len(summary['factories'])}))


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('client', type=Path)
    parser.add_argument('output', type=Path)
    parser.add_argument('--count', type=int, default=4000)
    parser.add_argument('--probe', type=Path)
    args = parser.parse_args()
    prepare(args.client, args.output, args.count, args.probe)
