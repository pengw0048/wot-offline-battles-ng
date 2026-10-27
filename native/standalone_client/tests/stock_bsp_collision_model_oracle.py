"""Execute exact #1513 WGBspCollisionModel instructions in Unicorn and compare.

Unchanged original code: comparator 00f43e30, segment collector 00f43ee0 with
WorldTriangle::intersects 01172190, sphere collector 00f43fd0 with closest
point 00f666e0, the collideSegment normal/cosine block 00f45130..00f451de, the
getBoundingBox radius block 00f44b12..00f44b66 and the collideSphere sweep
set-up in 00f452b0. Set insertion 00f43a90, vector push_back 00f618c0, the
BSP sweep 01179d70 and D3DXVec3Normalize are hooked; BSP traversal is not
executed here.
"""
import random
import struct
import subprocess

import pefile
import unicorn
from unicorn.x86_const import (UC_X86_REG_EAX, UC_X86_REG_EBP, UC_X86_REG_EBX, UC_X86_REG_ECX,
                               UC_X86_REG_EDI, UC_X86_REG_EIP, UC_X86_REG_ESI, UC_X86_REG_ESP,
                               UC_X86_REG_FS, UC_X86_REG_GDTR, UC_X86_REG_SS, UC_X86_REG_XMM0)

COMPARE, SEGMENT, SPHERE, CLOSEST = 0xf43e30, 0xf43ee0, 0xf43fd0, 0xf666e0
SET_INSERT, PUSH_BACK, SWEEP, NORMALISE = 0xf43a90, 0xf618c0, 0x1179d70, 0x11c3bbe
SPHERE_SETUP, LIST_NEW = 0xf452b0, 0xfcff60
FACED_START, FACED_END, RADIUS_START, RADIUS_END = 0xf45130, 0xf451de, 0xf44b12, 0xf44b66
STACK, DATA, GDT, SENTINEL = 0x6000000, 0x6200000, 0x6300000, 0x6400000


def bits(value):
    return struct.unpack('<I', struct.pack('<f', value))[0]


def unbits(value):
    return struct.unpack('<f', struct.pack('<I', value & 0xffffffff))[0]


class Original(object):
    def __init__(self, executable):
        pe = pefile.PE(str(executable))
        assert pe.FILE_HEADER.Machine == 0x14c
        image, base = pe.get_memory_mapped_image(), pe.OPTIONAL_HEADER.ImageBase
        self.u = u = unicorn.Uc(unicorn.UC_ARCH_X86, unicorn.UC_MODE_32)
        u.mem_map(base, (len(image) + 0xfff) & ~0xfff)
        u.mem_write(base, image)
        for region in (STACK, DATA, GDT, SENTINEL):
            u.mem_map(region, 0x100000)
        teb = (0x1000 | (GDT + 0x1000 & 0xffffff) << 16 | 0x92 << 40 | 0x4 << 52 | ((GDT + 0x1000) >> 24 & 0xff) << 56)
        flat = 0xffff | 0x92 << 40 | 0xf << 48 | 0xc << 52
        u.mem_write(GDT, struct.pack('<QQQ', 0, teb, flat))
        u.reg_write(UC_X86_REG_GDTR, (0, GDT, 23, 0))
        u.reg_write(UC_X86_REG_FS, 8)
        u.reg_write(UC_X86_REG_SS, 16)
        self.hooks = {SENTINEL: lambda: u.emu_stop(), SET_INSERT: self._insert, PUSH_BACK: self._push,
                      NORMALISE: self._normalise, SWEEP: self._sweep, LIST_NEW: lambda: u.emu_stop()}
        for address in self.hooks:
            u.hook_add(unicorn.UC_HOOK_CODE, lambda uc, a, s, d: self.hooks[a](), begin=address, end=address)

    def arg(self, index):
        return struct.unpack('<I', self.u.mem_read(self.u.reg_read(UC_X86_REG_ESP) + 4 * index, 4))[0]

    def ret(self, value, pop):
        esp = self.u.reg_read(UC_X86_REG_ESP)
        if value is not None:
            self.u.reg_write(UC_X86_REG_EAX, value)
        self.u.reg_write(UC_X86_REG_EIP, self.arg(0))
        self.u.reg_write(UC_X86_REG_ESP, esp + 4 + pop)

    def floats(self, address, count):
        return list(struct.unpack('<%df' % count, self.u.mem_read(address, 4 * count)))

    def words(self, address, count):
        return list(struct.unpack('<%dI' % count, self.u.mem_read(address, 4 * count)))

    def put(self, address, fmt, *values):
        self.u.mem_write(address, struct.pack(fmt, *values))

    def call(self, address, ecx, args):
        esp = STACK + 0xf0000
        self.put(esp, '<%dI' % (len(args) + 1), SENTINEL, *args)
        self.u.reg_write(UC_X86_REG_ESP, esp)
        self.u.reg_write(UC_X86_REG_ECX, ecx)
        self.u.emu_start(address, 0, count=200000)
        return self.u.reg_read(UC_X86_REG_EAX)

    def _insert(self):
        self.inserted.append(self.words(self.arg(3), 1) + self.floats(self.arg(3) + 4, 4))
        self.ret(self.arg(1), 12)

    def _push(self):
        record = bytes(self.u.mem_read(self.arg(1), 32))
        begin, end = self.words(self.u.reg_read(UC_X86_REG_ECX), 2)
        self.u.mem_write(end, record)
        self.put(self.u.reg_read(UC_X86_REG_ECX) + 4, '<I', end + 32)
        self.ret(None, 4)

    def _normalise(self):
        # Inputs are already unit length; production normalises before this block.
        self.ret(self.arg(1), 8)

    def _sweep(self):
        self.sweeps.append((self.floats(self.arg(2), 9), self.floats(self.arg(3), 3), self.arg(6)))
        self.ret(0, 0)

    def compare(self, a, b):
        self.put(DATA, '<If3f', a[0], a[1], *a[2])
        self.put(DATA + 0x20, '<If3f', b[0], b[1], *b[2])
        return self.call(COMPARE, 0, [DATA, DATA + 0x20]) & 0xff

    def segment(self, triangle, flags, start, direction, stop):
        collector = DATA + 0x100
        self.put(collector, '<3I6fB', 0, 0, 0, *(start + direction), stop)
        self.put(DATA + 0x200, '<9fHH', *(triangle + [flags, 0]))
        self.inserted = []
        result = self.call(SEGMENT, collector, [DATA + 0x300, DATA + 0x200, bits(0.5)])
        return result, self.inserted

    def sphere(self, triangle, flags, centre, r2, outside, existing):
        collector, array = DATA + 0x100, DATA + 0x1000
        for index, record in enumerate(existing):
            self.put(array + 32 * index, '<7fI', *record)
        end = array + 32 * len(existing)
        self.put(collector, '<4I4fB', 0, array, end, array + 0x800, *(centre + [r2]), outside)
        self.put(DATA + 0x200, '<9fHH', *(triangle + [flags, 0]))
        result = self.call(SPHERE, collector, [DATA + 0x300, DATA + 0x200, 0])
        begin, end = self.words(collector + 4, 2)
        return result, [self.floats(at, 7) + self.words(at + 28, 1) for at in range(begin, end, 32)]

    def faced(self, normal, direction):
        ebp = STACK + 0x80000
        node = DATA + 0x400
        self.put(node + 0x18, '<3f', *normal)
        self.put(ebp - 0x38, '<3f', *direction)
        self.u.reg_write(UC_X86_REG_EBP, ebp)
        self.u.reg_write(UC_X86_REG_ESP, ebp - 0x100)
        self.u.reg_write(UC_X86_REG_ESI, node)
        self.u.emu_start(FACED_START + 0x0, FACED_END)
        return self.floats(ebp - 0x1c, 3), self.floats(ebp - 0x54, 1)[0]

    def radius(self, low, high):
        ebp = STACK + 0x80000
        self.put(ebp + 8, '<f', low[0])
        self.put(ebp - 4, '<f', low[1])
        self.put(ebp - 8, '<f', low[2])
        self.put(ebp - 0x14, '<3f', high[2], high[1], high[0])
        self.u.reg_write(UC_X86_REG_EBP, ebp)
        self.u.reg_write(UC_X86_REG_ESP, ebp - 0x100)
        self.u.emu_start(RADIUS_START, RADIUS_END)
        return struct.unpack('<f', self.u.reg_read(UC_X86_REG_XMM0).to_bytes(16, 'little')[:4])[0]

    def sweep_setup(self, centre, radius):
        model = DATA + 0x600
        self.put(model + 0x88, '<I', DATA + 0x700)
        self.put(DATA + 0x500, '<3f', *centre)
        self.sweeps = []
        self.call(SPHERE_SETUP, model, [DATA + 0x500, bits(radius), 1])
        state = self.sweeps[0][2]
        collector = self.words(state, 1)[0]
        return self.sweeps, self.floats(collector + 0x10, 4), self.u.mem_read(collector + 0x20, 1)[0]


def f32(value):
    return unbits(bits(value))


def vector(rng, scale=2.0):
    return [f32(rng.uniform(-scale, scale)) for _ in range(3)]


def hexes(values):
    return ' '.join('%x' % bits(v) for v in values)


def compare(executable, directory):
    from test_stock_bsp_collision_model import ROOT, build_host
    probe = build_host(None, directory, ROOT / 'tests/stock_bsp_collision_model_probe.cpp')
    process = subprocess.Popen([str(probe)], stdin=subprocess.PIPE, stdout=subprocess.PIPE, text=True)

    def ask(line):
        process.stdin.write(line + '\n')
        process.stdin.flush()
        return process.stdout.readline().split()

    original, rng, counts = Original(executable), random.Random(1513), {}

    def check(name, expected, actual):
        assert expected == actual, (name, expected, actual)
        counts[name] = counts.get(name, 0) + 1

    for _ in range(4000):
        p, a, b, c = vector(rng, 3), vector(rng), vector(rng), vector(rng)
        if rng.random() < 0.1:
            c = list(b)
        put = DATA + 0x800
        original.put(put, '<12f', *(p + a + b + c))
        original.call(CLOSEST, 0, [put + 0x40, put, put + 12, put + 24, put + 36])
        check('closestPoint', ['%x' % bits(v) for v in original.floats(put + 0x40, 3)],
              ask('closest ' + hexes(p + a + b + c)))
    for _ in range(4000):
        base = f32(rng.uniform(0, 5))
        first = [rng.randrange(3), base, vector(rng)]
        second = [rng.randrange(3), f32(base + rng.choice([0.0, 0.0001, 0.00039, 0.0004, 0.01, -0.0002])),
                  list(first[2]) if rng.random() < 0.4 else vector(rng)]
        if rng.random() < 0.3:
            second[2][rng.randrange(3)] = first[2][rng.randrange(3)]
        check('comparator', [str(original.compare(first, second))],
              ask('less %x %s %x %s' % (first[0], hexes([first[1]] + first[2]), second[0],
                                        hexes([second[1]] + second[2]))))
    for _ in range(6000):
        triangle = vector(rng) + vector(rng) + vector(rng)
        flags = rng.choice([0x100, 0xc00, 0x1c00, 0x4983, 0x110, 0x140, 0x0])
        start, direction = vector(rng, 4), vector(rng)
        if rng.random() < 0.7:
            # Aim through a barycentric point so most rays reach the intersection.
            u, v = rng.random(), rng.random()
            u, v = (1 - u, 1 - v) if u + v > 1 else (u, v)
            target = [triangle[i] + u * (triangle[3 + i] - triangle[i]) + v * (triangle[6 + i] - triangle[i])
                      for i in range(3)]
            direction = [f32(t - s) for t, s in zip(target, start)]
        length = sum(d * d for d in direction) ** 0.5
        direction = [f32(d / length) for d in direction]
        stop = rng.randrange(2)
        result, inserted = original.segment(triangle, flags, start, direction, stop)
        reply = ask('segment %s %x %x' % (hexes(triangle + start + direction), flags, stop))
        expected = [str(result)] + (['-'] if not inserted else
                                    ['%x' % inserted[0][0]] + ['%x' % bits(v) for v in inserted[0][1:]])
        check('segmentCollector', expected, reply)
        counts['segmentInserted'] = counts.get('segmentInserted', 0) + bool(inserted)
    for _ in range(6000):
        triangle = vector(rng) + vector(rng) + vector(rng)
        flags = rng.choice([0x100, 0xc00, 0x1c00, 0x140])
        centre, r2 = vector(rng), f32(rng.uniform(0, 6))
        outside = rng.randrange(2)
        existing = []
        if rng.random() < 0.5:
            existing.append(vector(rng) + vector(rng) + [f32(rng.uniform(0, 3)), (flags >> 8) & 0xff])
        if rng.random() < 0.3:
            existing.append(vector(rng) + vector(rng) + [f32(rng.uniform(0, 3)), 0x55])
        result, records = original.sphere(triangle, flags, centre, r2, outside, existing)
        line = 'sphere %s %x %x %x' % (hexes(triangle + centre + [r2]), flags, outside, len(existing))
        for record in existing:
            line += ' %s %x' % (hexes(record[:7]), record[7])
        reply = ask(line)
        expected = [str(len(records))]
        for record in records:
            expected += ['%x' % bits(v) for v in record[:7]] + ['%x' % record[7]]
        assert result == 3
        check('sphereCollector', expected, reply)
        counts['sphereAppended'] = counts.get('sphereAppended', 0) + (len(records) > len(existing))
        counts['sphereUpdated'] = counts.get('sphereUpdated', 0) + any(
            r[:7] != e[:7] for r, e in zip(records, [[f32(v) for v in x[:7]] + x[7:] for x in existing]))
    for _ in range(3000):
        normal = vector(rng)
        length = sum(v * v for v in normal) ** 0.5
        normal = [f32(v / length) for v in normal]
        direction = vector(rng)
        length = sum(v * v for v in direction) ** 0.5
        direction = [f32(v / length) for v in direction]
        faced, cosine = original.faced(normal, direction)
        check('facedCosine', ['%x' % bits(v) for v in faced + [cosine]],
              ask('faced ' + hexes(normal + direction)))
    for _ in range(2000):
        low, high = vector(rng, 5), vector(rng, 5)
        check('boundingRadius', ['%x' % bits(original.radius(low, high))], ask('radius ' + hexes(low + high)))
    for _ in range(1000):
        centre, radius = vector(rng, 5), f32(rng.uniform(0, 3))
        sweeps, collector, outside = original.sweep_setup(centre, radius)
        expected = ['%x' % bits(v) for v in sweeps[0][0] + sweeps[1][0] + sweeps[0][1] + [collector[3]]]
        assert sweeps[0][1] == sweeps[1][1] and collector[:3] == centre and outside == 1
        check('sphereSweep', expected, ask('sweep ' + hexes(centre + [radius])))
    process.stdin.close()
    process.wait()
    return counts
