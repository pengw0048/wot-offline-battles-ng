"""Original #1513 constructor words, Reader store offsets and GroundSettings.

Constructor words come from executing f900a0's inline Tank::Settings and
Engine::Settings construction, f814d0 plus the TrackChassis tail, and the
f7ecd0 losses curve. Each Reader key's destination comes from the store that
follows its conversion call in f846c0/f8fe40/f822a0/f88700/f830d0/f7ecd0.
GroundSettings install (f968d0), count (f969b0) and lookup (f96860) run as
original code against the registry globals that f5fde0 writes. The probe's
"defaults" and "grounds" output is compared against all of these.
"""
import re
import struct
import sys
from pathlib import Path

COUNT, MAP = 0x1a5b968, 0x21316f8
# Reader, scan length, destination base inside its settings object.
READERS = {'body': (0xf846c0, 0x500, 0), 'shape': (0xf8fe40, 0x260, 0x94),
           'chassis': (0xf822a0, 0xe40, 0), 'aiming': (0xf88700, 0x500, 0),
           'surface': (0xf830d0, 0x270, 0), 'engine': (0xf7ecd0, 0x400, 0)}
CONVERSIONS = {0xf7e600: 1, 0xf81a60: 1, 0xf83d20: 3, 0xf80e20: 5, 0xf809e0: 3, 0xf80c00: 4, 0xf7e3e0: 2}
# Engine RPM keys are stored as rpm * 0.10471976f; the probe reports the
# production angular helpers for them.
ANGULAR = {'smplMinRPM': 'min', 'startRPM': 'start'}


def reader_fields(binding):
    """(group, key, field) for every Reader assignment in the binding source."""
    groups = {'readStockPhysicsBody': 'body', 'readStockPhysicsShape': 'shape',
              'readStockPhysicsChassisBase': 'chassis', 'readStockPhysicsChassis': 'aiming',
              'readStockPhysicsGroundSurface': 'surface', 'readStockPhysicsEngine': 'engine'}
    result = []
    for function, group in groups.items():
        body = re.search(r'\n\S[^\n]* ' + function + r'\(.*?\n\}', binding, re.S).group(0)
        for field, path in re.findall(r'result\.(\w+) = (?:value|pitch|yaw)((?:\.child\( "\w+" \))+)\.', body):
            result.append((group, re.findall(r'"(\w+)"', path)[-1], field))
    return sorted(result)


def store_offsets(executable):
    """{(group, key): (offset, count, byte)} from the original Reader stores."""
    import capstone
    import pefile
    from capstone.x86_const import X86_OP_MEM, X86_REG_EBX, X86_REG_EDI, X86_REG_ESI
    pe = pefile.PE(str(executable), fast_load=True)
    base, image = pe.OPTIONAL_HEADER.ImageBase, pe.get_memory_mapped_image()
    disassembler = capstone.Cs(capstone.CS_ARCH_X86, capstone.CS_MODE_32)
    disassembler.detail = True

    def text(address):
        offset = address - base
        if not 0 <= offset < len(image):
            return None
        data = image[offset:image.find(b'\0', offset)]
        return data.decode() if 0 < len(data) < 64 and all(32 <= c < 127 for c in data) else None
    result = {}
    for group, (start, length, origin) in READERS.items():
        key = count = None
        stores = []

        def finish():
            if key and count and stores:
                offset = min(disp for disp, unused in stores)
                result[(group, key)] = (offset + origin, count, stores[0][1] == 1)
        for instruction in disassembler.disasm(image[start - base:start - base + length], start):
            operands = instruction.operands
            if instruction.mnemonic == 'push' and operands[0].type == capstone.x86.X86_OP_IMM and \
                    text(operands[0].imm & 0xffffffff):
                finish()
                key, count, stores = text(operands[0].imm & 0xffffffff), None, []
            elif instruction.mnemonic == 'call' and stores:
                finish()  # the stores of one key end at the next call
                key = count = None
                stores = []
            elif instruction.mnemonic == 'call' and operands[0].type == capstone.x86.X86_OP_IMM:
                count = CONVERSIONS.get(operands[0].imm & 0xffffffff, count)
            elif count and operands and operands[0].type == X86_OP_MEM and \
                    instruction.mnemonic in ('mov', 'movss', 'movq', 'movups', 'fstp', 'movd') and \
                    operands[0].mem.base in (X86_REG_ESI, X86_REG_EDI, X86_REG_EBX) and not operands[0].mem.index:
                stores.append((operands[0].mem.disp, operands[0].size))
        finish()
    return result


class OriginalSettingsOracle:
    def __init__(self, graph, executable):
        from unicorn.x86_const import (UC_X86_REG_EAX, UC_X86_REG_EBP, UC_X86_REG_EBX,
                                       UC_X86_REG_EDI, UC_X86_REG_ESP)
        self.graph = graph
        self.oracle, self.uc = graph.oracle, graph.uc
        self.registers = dict(eax=UC_X86_REG_EAX, ebp=UC_X86_REG_EBP, ebx=UC_X86_REG_EBX,
                              edi=UC_X86_REG_EDI, esp=UC_X86_REG_ESP)
        self.offsets = store_offsets(executable)

    def words(self, address, count):
        return list(struct.unpack('<%uI' % count, self.uc.mem_read(address, count * 4)))

    def put(self, address, *values):
        self.uc.mem_write(address, struct.pack('<%uI' % len(values), *values))

    def zeroed(self, size):
        address = self.oracle.allocate(size)
        self.uc.mem_write(address, bytes(size))
        return address

    def run(self, begin, end, frame_words=(), **registers):
        """Execute an inline f900a0/f7ecd0 range on a private frame."""
        frame = self.oracle.allocate(0x800) + 0x600
        for offset, value in frame_words:
            self.put(frame + offset, value)
        self.uc.reg_write(self.registers['ebp'], frame)
        self.uc.reg_write(self.registers['esp'], frame - 0x400)
        for name, value in registers.items():
            self.uc.reg_write(self.registers[name], value)
        self.uc.emu_start(begin, end, count=1000000)

    def registry(self, count, groups):
        self.put(COUNT, count)
        self.put(MAP, *[g & 0xffffffff for g in groups])

    def tank_settings(self):
        settings = self.zeroed(0xe0)
        self.run(0xf905f0, 0xf90803, [(-0x13c, settings)], ebx=settings)
        return settings

    def chassis_settings(self):
        settings = self.zeroed(0x1d4)
        self.oracle.call(0xf814d0, ecx=settings)
        self.run(0xf90927, 0xf909a9, edi=settings)
        return settings

    def engine_settings(self):
        settings = self.zeroed(0x50)
        self.run(0xf90bb8, 0xf90cda, [(-0x130, settings)], eax=settings)
        return settings

    def curve(self, settings):
        begin, end = self.words(settings + 0x34, 2)
        return self.words(begin, (end - begin) // 4)

    def losses(self, settings, first, second):
        """f7ecd0's curve build: constructor x values, then the read losses."""
        self.run(0xf7ed31, 0xf7eda8, [(-0x18, first), (-0x14, second)], ebx=settings)
        return self.curve(settings)

    def eax(self):
        return self.uc.reg_read(self.registers['eax'])

    def table(self, settings):
        begin, end = self.words(settings + 0x54, 2)
        return begin, (end - begin) // 0x78

    def install(self, settings, kind, firm, medium, soft):
        surfaces = []
        for words in (firm, medium, soft):
            address = self.oracle.allocate(0x28)
            self.put(address, *words)
            surfaces.append(address)
        self.oracle.call(0xf968d0, (kind & 0xffffffff,) + tuple(surfaces), ecx=settings + 0x54)
        return self.eax() & 0xff

    def lookup(self, settings, kind, strength):
        self.oracle.call(0xf96860, (kind & 0xffffffff, strength & 0xffffffff), ecx=settings + 0x54)
        begin, unused = self.table(settings)
        entry, offset = divmod(self.eax() - begin, 0x78)
        return entry, offset // 0x28

    def count(self, settings):
        self.oracle.call(0xf969b0, ecx=settings + 0x54)
        return self.eax()


def parse(text):
    """Probe 'defaults' and 'grounds' output: a list of (name, tokens)."""
    rows = []
    for line in text.splitlines():
        if line.strip():
            name, _, rest = line.partition(' ')
            rows.append((name, rest.split()))
    return rows


def compare_defaults(oracle, rows):
    """Production default per Reader key against the original constructors."""
    g = oracle.graph
    g.prepare()
    oracle.registry(1, [0] * 256)
    objects = {'body': oracle.tank_settings(), 'chassis': oracle.chassis_settings(),
               'engine': oracle.engine_settings()}
    objects['shape'] = objects['body']
    objects['aiming'] = objects['chassis']
    table, entries = oracle.table(objects['chassis'])
    assert entries == 1 and oracle.count(objects['chassis']) == 1
    objects['surface'] = table
    compared, absent = 0, []
    for name, tokens in rows:
        if name == 'field':
            group, key, values = tokens[0], tokens[1], tokens[2:]
            if values == ['none']:
                absent.append((group, key))
                continue
            offset, count, byte = oracle.offsets[(group, key)]
            assert len(values) == count, (group, key, values)
            values = [int(value) for value in values]
            for surface in ((0, 0x28, 0x50) if group == 'surface' else (0,)):
                if byte:
                    original = list(bytearray(oracle.uc.mem_read(objects[group] + surface + offset, 1)))
                else:
                    original = oracle.words(objects[group] + surface + offset, count)
                assert original == values, (group, key, hex(offset), original, values)
                compared += 1
        elif name == 'angular':
            offset = oracle.offsets[('engine', {'min': 'smplMinRPM', 'start': 'startRPM'}[tokens[0]])][0]
            assert oracle.words(objects['engine'] + offset, 1) == [int(tokens[1])], tokens
            compared += 1
        elif name == 'torque':
            # build_vehicle_engine_torque_settings: curve points, power, start.
            values = [int(token) for token in tokens]
            assert oracle.curve(objects['engine']) == values[:6], (oracle.curve(objects['engine']), values)
            assert oracle.words(objects['engine'] + oracle.offsets[('engine', 'smplEnginePower')][0], 1) == values[6:7]
            assert oracle.words(objects['engine'] + oracle.offsets[('engine', 'startRPM')][0], 1) == values[7:8]
            compared += 1
        elif name == 'losses':
            values = [int(token) for token in tokens]
            engine = oracle.engine_settings()
            assert oracle.losses(engine, values[0], values[1]) == values[2:8], (values, oracle.curve(engine))
            compared += 1
    return compared, absent


def compare_grounds(oracle, rows):
    """Registry, f968d0 per item, the installed table and f96860 lookups."""
    g, compared = oracle.graph, 0
    settings = groups = None
    order = None
    for name, tokens in rows:
        values = [] if name == 'surface_order' else [int(token) for token in tokens]
        if name == 'surface_order':
            # Production member order as Reader keys; map to f830d0 offsets.
            order = [oracle.offsets[('surface', key)][0] // 4 for key in tokens]
            assert sorted(order) == list(range(10)), order
        elif name == 'registry':
            g.prepare()
            count, groups = values[0], [value - (1 << 32) if value >= 1 << 31 else value for value in values[1:]]
            oracle.registry(count, groups)
            settings = oracle.chassis_settings()
            assert oracle.count(settings) == count
        elif name == 'item':
            kind, installed = values[0], values[1]
            native = lambda words: [words[order.index(i)] for i in range(10)]
            surfaces = [native(values[2 + 10 * j:12 + 10 * j]) for j in range(3)]
            assert oracle.install(settings, kind - (1 << 32) if kind >= 1 << 31 else kind,
                                  *surfaces) == installed, (kind, installed)
            compared += 1
        elif name == 'table':
            begin, entries = oracle.table(settings)
            assert entries * 30 == len(values), (entries, len(values))
            original = oracle.words(begin, entries * 30)
            for index in range(entries * 3):
                production = values[index * 10:index * 10 + 10]
                assert original[index * 10:index * 10 + 10] == [production[order.index(i)] for i in range(10)], index
            compared += 1
        elif name == 'lookup':
            kind = values[0] - (1 << 32) if values[0] >= 1 << 31 else values[0]
            strength = values[1] - (1 << 32) if values[1] >= 1 << 31 else values[1]
            assert oracle.lookup(settings, kind, strength) == (values[2], values[3]), values
            compared += 1
    return compared


def main(executable, defaults_path, grounds_path):
    from vehicle_physics_tank_oracle import OriginalTankGraphOracle
    oracle = OriginalTankGraphOracle(executable).settings
    compared, absent = compare_defaults(oracle, parse(Path(defaults_path).read_text()))
    print('Original #1513 constructors matched %u production default entries; absent optionals keep '
          'native constructor state: %s' % (compared, ', '.join('%s.%s' % item for item in absent)))
    compared = compare_grounds(oracle, parse(Path(grounds_path).read_text()))
    print('Original f814d0/f968d0/f969b0/f96860 matched %u production GroundSettings sections' % compared)


if __name__ == '__main__':
    if sys.argv[1] == 'fields':
        for group, key, field in reader_fields(Path(sys.argv[2]).read_text()):
            print('FIELD(%s, "%s", %s)' % (group, key, field))
    else:
        main(*sys.argv[1:4])
