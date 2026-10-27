"""Execute exact #1513 BW::LeveredSuspension bodies in Unicorn.

The lever-limits record (007860c0), the lever sort (00785910), contact
projection (00788830), lever solve (007878d0), chain matrix (00787710), and the
manager frame (ground pass 00783a60, per-entry update 00787f50, node writes
00789060) run unchanged over fabricated native layouts. D3DX, cosf/sinf, the
collider, the camera space, LOD gates, node storage and std::function targets
are hooked; D3DX/cosf/sinf call the host reference library the kernel probe uses.
"""
import ctypes
import math
import random
import struct

import unicorn
from unicorn.x86_const import UC_X86_REG_EAX, UC_X86_REG_ECX, UC_X86_REG_XMM0

from stock_battle_vehicle_animation_oracle import (HEAP, STUBS, Machine, bits, f32, from_bits)

LIMITS, SORT, PROJECT, SOLVE, CHAIN = 0x7860c0, 0x785910, 0x788830, 0x7878d0, 0x787710
GROUND, UPDATE, WRITE, CONE_INIT = 0x783a60, 0x787f50, 0x789060, 0x5adf30
COSF, SINF, MEMMOVE, GATE, PUSH_BACK, CAMERA_SPACE = 0x13dad25, 0x13dad0d, 0x13dabf9, 0x781f70, 0x72f1a0, 0xac2f10
INTERLOCKED_SLOT, TLS_INDEX, GUARDS = 0x14913a0, 0x215e38c, (0x1d323fc, 0x1d32404)
COLLIDER_SLOT, POINTS = 0x1d32400, 0x1d323f0
CONE = 0x1d32450
PREPARE, RESET, QUERY, INTERLOCKED = STUBS + 0x400, STUBS + 0x410, STUBS + 0x420, STUBS + 0x430
TEB = 0x6300000 + 0x1000
ARENA = HEAP + 0x10000


class LeveredMachine(Machine):
    def __init__(self, executable, reference):
        Machine.__init__(self, executable, reference)
        lib = reference.lib
        for name in ('xCos', 'xSin'):
            getattr(lib, name).argtypes = [ctypes.c_float]
            getattr(lib, name).restype = ctypes.c_float
        extra = {COSF: lambda: self._unary(lib.xCos), SINF: lambda: self._unary(lib.xSin),
                 MEMMOVE: self._memmove, GATE: self._gate, PUSH_BACK: self._push_back,
                 CAMERA_SPACE: self._space, PREPARE: self._prepare, RESET: self._reset, QUERY: self._query,
                 INTERLOCKED: self._interlocked}
        for address, handler in extra.items():
            self.handlers[address] = handler
            self.u.hook_add(unicorn.UC_HOOK_CODE, self._hook, begin=address, end=address)
        self.word(INTERLOCKED_SLOT, INTERLOCKED)
        self.call(CONE_INIT, 0, [])
        self.cone = self.word(CONE)

    # -- hooks --------------------------------------------------------------
    def _unary(self, function):
        raw = self.u.reg_read(UC_X86_REG_XMM0)
        value = function(from_bits(raw & 0xffffffff))
        self.u.reg_write(UC_X86_REG_XMM0, (raw & ~0xffffffff) | bits(value))
        self._return(0)

    def _memmove(self):
        destination, source, count = self.arg(0), self.arg(1), self.arg(2)
        self.u.mem_write(destination, bytes(self.u.mem_read(source, count)))
        self._return(0, destination)

    def _gate(self):
        self._return(0, 1 if self.gates[self.u.reg_read(UC_X86_REG_ECX)] else 0)

    def _push_back(self):
        end = self.word(POINTS + 4)
        self.u.mem_write(end, bytes(self.u.mem_read(self.arg(0), 12)))
        self.word(POINTS + 4, end + 12)
        self._return(1)

    def _space(self):
        self.word(self.arg(0), self.space)
        if self.space:
            self.word(self.space + 4, self.word(self.space + 4) + 1)
        self._return(0)

    def _interlocked(self):
        value = (self.word(self.arg(0)) - 1) & 0xffffffff
        self.word(self.arg(0), value)
        self._return(1, value)

    def _prepare(self):
        self.calls.append(('prepare', self.floats(self.arg(0), 6), self.arg(1), self.arg(2)))
        self._return(5, 1)

    def _reset(self):
        self.calls.append(('reset',))
        self._return(0)

    def _query(self):
        distance, normal, ray = self.arg(0), self.arg(1), self.arg(4)
        self.calls.append(('query', [self.word(ray + 4 * k) for k in range(7)]))
        hit, value, vector = self.answers.pop(0)
        if hit:
            self.word(distance, value)
            for k in range(3):
                self.word(normal + 4 * k, vector[k])
        self._return(6, 1 if hit else 0)

    # -- arena --------------------------------------------------------------
    def reset_arena(self):
        self.next = ARENA
        self.u.mem_write(ARENA, b'\0' * 0x40000)

    def alloc(self, size):
        address = self.next
        self.next += (size + 15) & ~15
        assert self.next < ARENA + 0x40000
        return address

    def put(self, words):
        address = self.alloc(4 * max(len(words), 1))
        for index, word in enumerate(words):
            self.word(address + 4 * index, word)
        return address

    def vector(self, address, words, stride_words, capacity=None):
        """Write a std::vector {begin, end, cap} at `address` holding `words`."""
        count = len(words) // stride_words if stride_words else 0
        capacity = count if capacity is None else capacity
        begin = self.alloc(4 * stride_words * max(capacity, 1))
        for index, word in enumerate(words):
            self.word(begin + 4 * index, word)
        self.word(address, begin)
        self.word(address + 4, begin + 4 * len(words))
        self.word(address + 8, begin + 4 * stride_words * capacity)
        return begin

    def function(self, result):
        """std::function impl whose _Do_call (slot 2) returns `result` in eax."""
        impl = self.alloc(0x30)
        code = impl + 0x20
        self.word(impl, impl + 0x10)
        for slot in range(4):
            self.word(impl + 0x10 + 4 * slot, code)
        self.u.mem_write(code, b'\xb8' + struct.pack('<I', result) + b'\xc3')
        return impl

    def cover(self, addresses):
        """Count executions of branch targets so tests can prove coverage."""
        for handle in getattr(self, 'coverage_hooks', []):
            self.u.hook_del(handle)
        self.coverage = dict((address, 0) for address in addresses)

        def count(u, address, size, data):
            self.coverage[address] += 1
        self.coverage_hooks = [self.u.hook_add(unicorn.UC_HOOK_CODE, count, begin=address, end=address)
                               for address in addresses]

    def words(self, address, count):
        return list(struct.unpack('<%dI' % count, self.u.mem_read(address, 4 * count)))


# -- single-function runners ---------------------------------------------
def run_limits(m, minimum, maximum):
    m.reset_arena()
    out = m.alloc(0x20)
    m.call(LIMITS, out, [0, 0, minimum, maximum])
    words = m.words(out, 7)
    return words[2:6] + [words[6] & 0xff]


def run_sort(m, starts):
    """starts: (x, z) bit pairs; returns the sorted index permutation."""
    m.reset_arena()
    records = m.alloc(0xcc * len(starts))
    for index, (x, z) in enumerate(starts):
        m.word(records + 0xcc * index + 0x34, x)
        m.word(records + 0xcc * index + 0x3c, z)
    holder = m.put([records])
    pointer = m.put([holder])
    order = m.put(list(range(len(starts))))
    m.call(SORT, 0, [order, order + 4 * len(starts), len(starts), pointer])
    return m.words(order, len(starts))


def frame_words(world, inverse):
    """Native 0xa4-byte frame block of 00787f50 (all bit patterns)."""
    side = [bits(-from_bits(world[k])) for k in range(3)]
    return list(world) + list(inverse) + list(world[8:11]) + side + list(world[4:7])


def run_project(m, frame, anchor, contact):
    m.reset_arena()
    block, a, c = m.put(frame), m.put(anchor), m.put(contact)
    m.call(PROJECT, 0, [block, a, c])
    return m.words(c, 7)


def run_solve(m, frame, lever, anchor, contact):
    m.reset_arena()
    block, a, c, record = m.put(frame), m.put(anchor), m.put(contact), m.put(lever)
    m.call(SOLVE, 0, [c, record, a, block])
    return m.words(c, 7) + m.words(record, 5)


def run_chain(m, contacts, i, j, fallback, matrix):
    m.reset_arena()
    holder = m.alloc(12)
    words = []
    for contact in contacts:
        words += contact + [0]
    m.vector(holder, words, 8)
    out, a, b = m.alloc(64), m.put(fallback), m.put(matrix)
    m.call(CHAIN, 0, [out, holder, i, j, a, b])
    return m.words(out, 16)


# -- manager frame ----------------------------------------------------------
class OriginalLevered(object):
    """Native manager with one entry per built suspension, driven frame by frame."""

    def __init__(self, machine, built, locals_, node_count):
        m = self.m = machine
        m.reset_arena()
        m.calls, m.answers, m.gates = [], [], {}
        self.count, self.nodes = len(built), node_count
        self.tls = m.alloc(0x3300)
        m.word(self.tls + 0x32f0, 0x7fffffff)
        m.word(TEB + 0x2c, m.put([self.tls] * 8))
        assert m.word(TLS_INDEX) < 8 and all(m.word(g) == 0 for g in GUARDS)
        self.collider = m.alloc(0x40)
        m.word(self.collider, self.collider + 0x10)
        for slot, stub in enumerate((PREPARE, RESET, 0, 0, 0, QUERY)):
            m.word(self.collider + 0x10 + 4 * slot, stub)
        m.word(COLLIDER_SLOT, self.collider)
        points = m.alloc(12 * 64)
        m.word(POINTS, points)
        m.word(POINTS + 4, points)
        m.word(POINTS + 8, points + 12 * 64)
        self.space = m.alloc(16)
        m.word(self.space, 0)
        m.word(self.space + 4, 1)
        # Compound node storage {base, ?, N}: [N indices][N locals][N worlds].
        self.descriptor = m.alloc(0x30)
        self.storage = m.alloc(node_count * (4 + 128))
        m.word(self.descriptor, self.storage)
        m.word(self.descriptor + 8, node_count)
        m.node_descriptor = self.descriptor
        for index, matrix in enumerate(locals_):
            m.u.mem_write(self.local(index), struct.pack('<16I', *matrix))
        self.mgr = m.alloc(0x100)
        self.entries, self.flags = m.alloc(0x60 * self.count), m.alloc(0x10 * self.count)
        self.lods, self.states = m.alloc(0x30 * self.count), m.alloc(0x40 * self.count)
        for offset, value in ((0, self.entries), (0xc, self.count), (0x38, self.flags), (0x70, self.lods),
                              (0xa8, self.states)):
            m.word(self.mgr + offset, value)
        self.frames, self.boxes = [], []
        for index, b in enumerate(built):
            entry, flags, state = self.entry(index), self.flag(index), self.state(index)
            frame, box = m.alloc(64), m.alloc(24)
            self.frames.append(frame)
            self.boxes.append(box)
            m.word(entry + 0x24, m.function(frame))
            m.word(entry + 0x4c, m.function(box))
            n = len(b['levers'])
            m.vector(entry + 0x50, sum(b['probes'], []), 3)
            m.word(entry + 0x5c, b['rayLength'])
            m.u.mem_write(flags, bytes([1]))
            m.vector(flags + 4, [0] * (8 * n), 8)
            m.vector(state, sum(b['levers'], []), 12)
            m.vector(state + 0xc, [], 8, capacity=n)
            m.vector(state + 0x18, sum(b['anchors'], []), 3)
            authored = []
            for record in b['authored']:
                for node, matrix in record:
                    authored += [node & 0xffffffff] + list(matrix)
            m.vector(state + 0x24, authored, 51)
            m.word(state + 0x34, b['speedMul'])
            m.word(state + 0x38, 0)
            m.word(state + 0x3c, b['split'])

    def entry(self, i):
        return self.entries + 0x60 * i

    def flag(self, i):
        return self.flags + 0x10 * i

    def state(self, i):
        return self.states + 0x40 * i

    def local(self, node):
        return self.storage + 4 * self.nodes + 64 * node

    def world(self, node):
        return self.storage + 68 * self.nodes + 64 * node

    def step(self, frame):
        m = self.m
        for i in range(self.count):
            m.u.mem_write(self.frames[i], struct.pack('<16I', *frame['frames'][i]))
            m.u.mem_write(self.boxes[i], struct.pack('<6I', *frame['boxes'][i]))
            m.gates[self.lods + 0x30 * i] = frame['gates'][i]
        for node, matrix in enumerate(frame['worlds']):
            m.u.mem_write(self.world(node), struct.pack('<16I', *matrix))
        m.space = self.space if frame['space'] else 0
        m.calls = []
        m.answers = [tuple(a) for a in frame['answers']]
        m.call(GROUND, 0, [self.mgr])
        assert not m.thrown and not m.answers, 'ground pass consumed %d answers' % len(m.answers)
        for i in range(self.count):
            m.call(UPDATE, 0, [self.mgr, i, frame['dt']])
            assert not m.thrown
        for i in range(self.count):
            m.call(WRITE, 0, [self.mgr, i])
            assert not m.thrown
        return self.snapshot()

    def contacts(self, address):
        begin, end = self.m.words(address, 2)
        rows = []
        for at in range(begin, end, 32):
            words = self.m.words(at, 8)
            rows.append(words[:7] + [words[7] & 0xff])
        return rows

    def snapshot(self):
        m, result = self.m, []
        for i in range(self.count):
            state, entry = self.state(i), self.entry(i)
            levers = m.words(state, 2)
            probes = m.words(entry + 0x50, 2)
            result.append({
                'history': m.u.mem_read(self.flag(i), 1)[0],
                'contacts': self.contacts(self.flag(i) + 4),
                'smoothed': self.contacts(state + 0xc),
                'levers': [m.words(at, 5) for at in range(levers[0], levers[1], 0x30)],
                'probes': [m.words(at, 3) for at in range(probes[0], probes[1], 12)]})
        locals_ = [m.words(self.local(node), 16) for node in range(self.nodes)]
        return {'entries': result, 'locals': locals_, 'calls': list(m.calls)}


# -- scenario helpers ---------------------------------------------------------
def identity():
    return [1., 0., 0., 0., 0., 1., 0., 0., 0., 0., 1., 0., 0., 0., 0., 1.]


def pose(yaw, pitch, roll, position):
    sy, cy, sp, cp, sr, cr = (math.sin(yaw), math.cos(yaw), math.sin(pitch), math.cos(pitch),
                              math.sin(roll), math.cos(roll))
    rows = [cr * cy + sr * sp * sy, sr * cp, sr * sp * cy - cr * sy, 0.,
            cr * sp * sy - sr * cy, cr * cp, sr * sy + cr * sp * cy, 0.,
            cp * sy, -sp, cp * cy, 0.] + list(position) + [1.]
    return [f32(v) for v in rows]


def unit(rng, up_bias):
    v = [rng.uniform(-1, 1), rng.uniform(-1, 1) + up_bias, rng.uniform(-1, 1)]
    length = math.sqrt(sum(c * c for c in v)) or 1.
    return [f32(c / length * rng.choice((1., 1., 2.5))) for c in v]


def suspension_layout(rng, levers):
    """Synthetic compound locals for authored levers (start/joint/track per lever)."""
    count = len(levers)
    per_side = (count + 1) // 2
    nodes, inputs = [], []
    for index, lever in enumerate(levers):
        minimum, maximum = lever[-2], lever[-1]
        side = 1. if index < per_side else -1.
        z = (index % per_side - per_side / 2.) * rng.uniform(.6, .8)
        start = pose(0., rng.uniform(-.2, .2), 0., (f32(side * rng.uniform(1.2, 1.6)), f32(rng.uniform(.3, .5)),
                                                    f32(z)))
        joint = pose(0., rng.uniform(-.4, .4), 0., (0., f32(-rng.uniform(.05, .2)),
                                                     f32(rng.choice((-1, 1)) * rng.uniform(.25, .5))))
        track = pose(0., 0., 0., (0., f32(-rng.uniform(.05, .3)), f32(rng.uniform(-.05, .05))))
        inputs.append((len(nodes), len(nodes) + 1, len(nodes) + 2, minimum, maximum))
        nodes += [start, joint, track]
    return nodes, inputs
