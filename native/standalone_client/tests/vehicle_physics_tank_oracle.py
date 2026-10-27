"""Replay the original #1513 Tank construction graph against the Win32 probe.

vehicle_physics_native_probe.cpp runs production configure, setTransform and
publication with the actual D3DX9_43 and UCRT entries, records every SDK call,
and dumps the resulting state. This oracle builds the same native objects
with the original constructors, then executes original f8b8a0, f91450 and
f84be0. Each D3DX/libm call is answered from that run's recorded actual SDK
result; a call with no recorded result fails rather than being approximated.
D3DXMatrixScaling/Translation only assign elements and are written directly.
"""
import struct
import sys
from pathlib import Path

IAT = {0x1492120: 'inverse', 0x149214c: 'normal', 0x1492154: 'normalize',
       0x149215c: 'scaling', 0x1492164: 'translation', 0x149216c: 'coordinate',
       0x1491e00: 'tan'}
IDENTITY = [0x3f800000, 0, 0, 0, 0, 0x3f800000, 0, 0, 0, 0, 0x3f800000, 0, 0, 0, 0, 0x3f800000]
# Probe config word index -> (settings object, native offset). Index 41 is
# the useComplexForm byte; the others are words.
CONFIG = dict([(index, ('tank', 4 * index)) for index in range(14)] +
              list(zip(range(14, 21), [('tank', o) for o in (0xb0, 0xac, 0xa4, 0xa8, 0xa0, 0x9c, 0x98)])) +
              list(zip((21, 22, 23, 24, 25, 26, 27, 28, 29),
                       [('chassis', o) for o in (0x64, 0x68, 0x6c, 0, 0x60, 0xa4, 0xa8, 0x7c, 0x80)])) +
              [(31 + i, ('chassis', 4 + 4 * i)) for i in range(5)] +
              [(36 + i, ('chassis', 0x2c + 4 * i)) for i in range(5)] +
              [(30, ('engine', 0x18)), (41, ('tank', 0x94)), (42, ('chassis', 0x74))])


def f32(value):
    return struct.unpack('<I', struct.pack('<f', value))[0]


def parse_state(text):
    cases, case = [], None
    for line in text.splitlines():
        name, _, rest = line.partition(' ')
        tokens = [int(token) for token in rest.split()] if rest else []
        if name == 'case':
            case = {'seed': tokens[0], 'sections': [{}], 'config': None, 'pose': None}
            cases.append(case)
        elif name == 'config':
            case['config'] = tokens
        elif name == 'defaults':
            case['defaults'] = tokens
        elif name == 'pose':
            case['pose'] = tokens
            case['sections'].append({})
        elif name == 'reconfigure':
            case['sections'].append({})
        elif name == 'bounds':
            case['bounds'] = tokens
        elif name != 'end':
            case['sections'][-1][name] = tokens
    return cases


def parse_calls(text):
    cases, calls = {}, None
    for line in text.splitlines():
        name, _, rest = line.partition(' ')
        tokens = tuple(int(token) for token in rest.split())
        if name == 'case':
            calls = cases.setdefault(tokens[0], {})
        elif name in ('tan', 'normalize'):
            width = 1 if name == 'tan' else 3
            calls.setdefault(name, {})[tokens[:width]] = tokens[width:]
        elif name in ('coordinate', 'normal'):
            calls.setdefault(name, {})[tokens[:19]] = tokens[19:]
        elif name == 'inverse':
            calls.setdefault(name, {})[tokens[16:32]] = tokens[32:]
    return cases


class OriginalTankGraphOracle:
    def __init__(self, executable):
        from stock_tree_lod_oracle import OriginalTreeInstructionOracle
        from unicorn import UC_HOOK_CODE
        from unicorn.x86_const import UC_X86_REG_ESP, UC_X86_REG_EIP, UC_X86_REG_XMM0
        self.oracle = OriginalTreeInstructionOracle(executable)
        self.uc = self.oracle.emulator
        self.word = self.oracle.word
        self.calls = None
        self.missing = []
        stubs = {}
        for index, (address, name) in enumerate(sorted(IAT.items())):
            stub = 0x3000c000 + index * 16
            self.uc.mem_write(stub, b'\xc3')
            stubs[stub] = name
        self.stubs = stubs
        self.random = 0

        def rand():
            self.random = (self.random * 214013 + 2531011) & 0xffffffff
            return (self.random >> 16) & 0x7fff
        self.oracle.random_source = rand

        def words(address, count):
            return list(struct.unpack('<%uI' % count, self.uc.mem_read(address, count * 4)))

        def hook(uc, address, size, data):
            name = stubs.get(address)
            if name is None:
                return
            stack = uc.reg_read(UC_X86_REG_ESP)
            arg = lambda index: self.word(stack + 4 + index * 4)
            if name == 'tan':
                key = (uc.reg_read(UC_X86_REG_XMM0) & 0xffffffff,)
                result = self.lookup(name, key)
                uc.reg_write(UC_X86_REG_XMM0, result[0])
                uc.reg_write(UC_X86_REG_EIP, self.word(stack))
                uc.reg_write(UC_X86_REG_ESP, stack + 4)
                return
            if name == 'scaling':
                matrix = list(IDENTITY)
                matrix[0], matrix[5], matrix[10] = arg(1), arg(2), arg(3)
                uc.mem_write(arg(0), struct.pack('<16I', *matrix))
                cleanup, value = 16, arg(0)
            elif name == 'translation':
                matrix = list(IDENTITY)
                matrix[12], matrix[13], matrix[14] = arg(1), arg(2), arg(3)
                uc.mem_write(arg(0), struct.pack('<16I', *matrix))
                cleanup, value = 16, arg(0)
            elif name == 'normalize':
                result = self.lookup(name, tuple(words(arg(1), 3)))
                uc.mem_write(arg(0), struct.pack('<3I', *result))
                cleanup, value = 8, arg(0)
            elif name in ('coordinate', 'normal'):
                key = tuple(words(arg(1), 3) + words(arg(2), 16))
                result = self.lookup(name, key)
                uc.mem_write(arg(0), struct.pack('<3I', *result))
                if name == 'coordinate':
                    # The production adapter discards w; record what D3DX would
                    # leave there only if a caller reads it.
                    uc.mem_write(arg(0) + 12, struct.pack('<I', 0x7fbadbad))
                cleanup, value = 12, arg(0)
            else:
                source = tuple(words(arg(2), 16))
                if source == tuple(IDENTITY) and source not in self.calls.get('inverse', {}):
                    # Constructor-only identity inverses are overwritten before use.
                    result = IDENTITY
                else:
                    result = self.lookup(name, source)
                uc.mem_write(arg(0), struct.pack('<16I', *result))
                cleanup, value = 12, arg(0)
            from unicorn.x86_const import UC_X86_REG_EAX
            uc.reg_write(UC_X86_REG_EAX, value)
            uc.reg_write(UC_X86_REG_EIP, self.word(stack))
            uc.reg_write(UC_X86_REG_ESP, stack + 4 + cleanup)
        self.uc.hook_add(UC_HOOK_CODE, hook, begin=0x3000c000, end=0x3000cfff)
        self.words = words
        from vehicle_physics_defaults_oracle import OriginalSettingsOracle
        self.settings = OriginalSettingsOracle(self, executable)
        self.defaults_checked = 0

    def prepare(self):
        """Restore the image, then the TLS slot and IAT entries it reverts."""
        self.oracle.restore()
        self.uc.mem_write(0x2131dcc, struct.pack('<i', -1))
        tls = self.oracle.allocate(0x4000)
        slots = self.oracle.allocate(4096)
        self.uc.mem_write(0x2c, struct.pack('<I', slots))
        self.uc.mem_write(slots, struct.pack('<I', tls))
        for stub, name in self.stubs.items():
            address = next(key for key, value in IAT.items() if value == name)
            self.uc.mem_write(address, struct.pack('<I', stub))

    def lookup(self, name, key):
        result = self.calls.get(name, {}).get(key)
        if result is None:
            self.missing.append((name, key))
            raise AssertionError(('No recorded actual SDK result', name, key))
        return result

    def put(self, address, *values):
        self.uc.mem_write(address, struct.pack('<%uI' % len(values), *values))

    def build(self, config, force, seed, defaults=()):
        """Construct Tank, TrackChassis and Engine as f4fd30/f900a0 do.

        Settings objects come from the original constructor instructions;
        a config word the probe marks as a default must equal that word."""
        o, put = self.oracle, self.put
        self.random = seed
        tank = o.allocate(0xaf4)
        record = o.allocate(0x80)
        o.call(0xf8a8e0, (0, record), ecx=tank)
        chassis = o.allocate(0x148)
        o.call(0xf86a50, (tank + 0x1c8, tank + 0x308), ecx=chassis)
        engine = o.allocate(0x33c)
        self.uc.mem_write(engine, bytes(0x33c))
        o.call(0xf6f800, ecx=engine + 4)
        put(engine + 0x2f8, f32(0.05))
        put(engine + 0x310, f32(1.0))
        put(engine + 0x318, 0xffffffff)
        put(engine + 0x324, 0xffffffff)
        put(tank + 0x1b8, chassis, 0, engine, 0)
        put(chassis + 0x60, tank)
        put(chassis + 0x8, 0)
        objects = {'tank': self.settings.tank_settings(), 'chassis': self.settings.chassis_settings(),
                   'engine': self.settings.engine_settings()}
        for index in defaults:
            target, offset = CONFIG[index]
            width = 1 if index == 41 else 4
            original = int.from_bytes(bytes(self.uc.mem_read(objects[target] + offset, width)), 'little')
            assert original == config[index], ('constructor default', index, hex(original), hex(config[index]))
            self.defaults_checked += 1
        for index, (target, offset) in CONFIG.items():
            if index == 41:
                self.uc.mem_write(objects[target] + offset, bytes([config[index]]))
            else:
                put(objects[target] + offset, config[index])
        settings, chassis_settings, engine_settings = objects['tank'], objects['chassis'], objects['engine']
        vector = o.allocate(8)
        put(vector, settings, 0)
        put(tank + 0x638, 0, vector, vector + 8, vector + 8)
        vector = o.allocate(8)
        put(vector, chassis_settings, 0)
        put(chassis + 0xc0, 0, vector, vector + 8, vector + 8)
        vector = o.allocate(8)
        put(vector, engine_settings, 0)
        put(engine + 0x32c, 0, vector, vector + 8, vector + 8)
        put(chassis + 0x64, force)
        return tank, chassis, engine

    def vertical_force(self, tank):
        """Run f84e80's tail 0xf8554c..0xf8558c: chassis +0x64 from settings."""
        from unicorn.x86_const import UC_X86_REG_EDI, UC_X86_REG_EBP, UC_X86_REG_ESP
        frame = self.oracle.allocate(0x400) + 0x200
        self.uc.reg_write(UC_X86_REG_EDI, tank)
        self.uc.reg_write(UC_X86_REG_EBP, frame)
        self.uc.reg_write(UC_X86_REG_ESP, frame - 0x100)
        self.uc.emu_start(0xf8554c, 0xf85591)

    def body(self, sections, name, address, mass_address=None):
        words = self.words
        sections[name + '.mass'] = words(address, 2)
        sections[name + '.inertia'] = words(address + 8, 16) + words(address + 0x48, 16) + \
            words(address + 0x88, 16)
        sections[name + '.pose'] = words(address + 0x140, 16) + words(address + 0x180, 16) + \
            words(address + 0x1d8, 1)

    def vector(self, header, stride):
        begin, end = self.words(header, 2)
        return [begin + offset for offset in range(0, end - begin, stride)]

    def polyhedron(self, sections, name, address):
        words = self.words
        local = []
        for offset in (0x24, 0x0c, 0x18):
            items = self.vector(address + offset, 12)
            local += [len(items)] + [w for item in items for w in words(item, 3)]
        faces = self.vector(address, 0x14)
        local.append(len(faces))
        for face in faces:
            indices = self.vector(face, 4)
            local += [len(indices)] + [words(item, 1)[0] for item in indices] + words(face + 12, 2)
        adjacency = self.vector(address + 0x54, 12)
        local.append(len(adjacency))
        for entry in adjacency:
            indices = self.vector(entry, 4)
            local += [len(indices)] + [words(item, 1)[0] for item in indices]
        local += words(address + 0x138, 6)
        sections[name + '.local'] = local
        world = []
        for offset in (0x48, 0x3c, 0x30):
            for item in self.vector(address + offset, 12):
                world += words(item, 3)
        world += words(address + 0x60, 16) + words(address + 0xa0, 6) + \
            words(address + 0xb8, 16) + words(address + 0xf8, 16)
        sections[name + '.world'] = world

    def box(self, sections, name, address):
        sections[name] = self.words(address, 0x11c // 4)

    def bytes_at(self, address, count):
        return list(bytearray(self.uc.mem_read(address, count)))

    def state(self, tank, chassis, engine):
        words, sections = self.words, {}
        sections['identity.components'] = [words(item, 1)[0] - tank
                                           for item in self.vector(tank + 0x654, 4)]
        sections['identity.tank'] = words(tank + 0xa30, 48) + words(chassis + 0x50, 4) + \
            words(chassis + 0x13c, 3) + words(tank + 0x6a8, 3) + words(tank + 0x700, 3) + \
            words(tank + 0x720, 3) + words(engine + 0x314, 6) + self.bytes_at(tank + 0x1a8, 1)
        sections['identity.input'] = words(tank + 8, 2) + self.bytes_at(tank + 0x14, 13) + \
            words(tank + 0x24, 1)
        for index, wheel in enumerate(self.vector(chassis + 0xa4, 0x4b4)):
            sections['wheel%u.identity' % index] = self.bytes_at(wheel + 0x10, 1) + \
                words(wheel + 0x428, 2) + words(wheel + 0x430, 33)
        self.body(sections, 'hull', tank + 0x1c8)
        self.body(sections, 'engine', engine + 4)
        sections['chassis.force'] = words(chassis + 0x64, 1)
        for index, wheel in enumerate(self.vector(chassis + 0xa4, 0x4b4)):
            name = 'wheel%u' % index
            sections[name + '.record'] = words(wheel, 4) + words(wheel + 0x14, 1) + \
                words(wheel + 0x348, 2) + words(wheel + 0x308, 16)
            self.body(sections, name, wheel + 0x1c)
        for index, track in enumerate(self.vector(chassis + 0xb4, 0x1c)):
            sections['track%u' % index] = [words(item, 1)[0] for item in self.vector(track + 0x10, 4)]
        self.polyhedron(sections, 'embedded0', tank + 0x28)
        self.polyhedron(sections, 'embedded1', tank + 0x4b4)
        for collection, base in enumerate((0x178, 0x190)):
            for index, item in enumerate(self.vector(tank + base + 0xc, 4)):
                self.polyhedron(sections, 'collection%u.poly%u' % (collection, index), words(item, 1)[0])
            for index, item in enumerate(self.vector(tank + base, 4)):
                self.box(sections, 'collection%u.box%u' % (collection, index), words(item, 1)[0])
        self.body(sections, 'snapshot', tank + 0x740)
        return sections

    def run(self, case, calls):
        self.prepare()
        self.calls = calls
        tank, chassis, engine = self.build(case['config'], 0, case['seed'],
                                           [int(index) for index in case.get('defaults', ())])
        self.vertical_force(tank)
        self.oracle.call(0xf8b8a0, ecx=tank)
        before = self.state(tank, chassis, engine)
        pose = self.oracle.allocate(64)
        self.put(pose, *case['pose'])
        self.oracle.call(0xf91450, (pose,), ecx=tank)
        bounds = self.oracle.allocate(24)
        self.oracle.call(0xf84be0, (bounds,), ecx=tank)
        after = self.state(tank, chassis, engine)
        # Same settings again: f900a0 reinstalls identical slots, then f8b8a0
        # rebuilds on the existing owner at its current hull pose.
        self.oracle.call(0xf8b8a0, ecx=tank)
        again = self.state(tank, chassis, engine)
        return before, after, again, self.words(bounds, 6)


def compare(executable, state_path, calls_path):
    cases = parse_state(Path(state_path).read_text(encoding='utf-8', errors='strict'))
    calls = parse_calls(Path(calls_path).read_text())
    oracle = OriginalTankGraphOracle(executable)
    # The config mapping must agree with the original Reader stores.
    stores = oracle.settings.offsets
    for (group, key), index in (
            (('body', 'gravity'), 0), (('body', 'hullSize'), 5), (('body', 'turretTopWidth'), 13),
            (('shape', 'auxClearance'), 14), (('shape', 'terrAftChamferFraction'), 20),
            (('shape', 'useComplexForm'), 41), (('chassis', 'chassisMass'), 21),
            (('chassis', 'trackGaugeFactor'), 24), (('chassis', 'wheelRadius'), 25),
            (('chassis', 'stiffness1'), 29), (('chassis', 'roadWheelPositions'), 31),
            (('chassis', 'stiffnessFactors'), 36), (('chassis', 'wheelPenetration'), 42),
            (('engine', 'engineInertia'), 30)):
        assert stores[(group, key)][0] == CONFIG[index][1], (group, key, stores[(group, key)], CONFIG[index])
    sections = 0
    for case in cases:
        before, after, again, bounds = oracle.run(case, calls[case['seed']])
        for label, expected, actual in (('configure', before, case['sections'][0]),
                                        ('transform', after, case['sections'][1]),
                                        ('reconfigure', again, case['sections'][2])):
            assert sorted(expected) == sorted(actual), (case['seed'], label,
                sorted(set(expected) ^ set(actual)))
            for name in sorted(expected):
                original, production = expected[name], actual[name]
                if name.startswith('collection') and '.box' in name:
                    original = original[:0x11c // 4]
                assert original == production, (case['seed'], label, name,
                    next(((i, a, b) for i, (a, b) in enumerate(zip(original, production)) if a != b),
                         (len(original), len(production))))
                sections += 1
        assert bounds == case['bounds'], (case['seed'], bounds, case['bounds'])
    fallback = sum(1 for case in cases if case.get('defaults'))
    print('Original #1513 Tank graph replay matched %u production sections across %u cases '
          '(%u with Reader fallback settings; %u defaulted config words equal the original constructors)'
          % (sections, len(cases), fallback, oracle.defaults_checked))


if __name__ == '__main__':
    compare(*sys.argv[1:4])
