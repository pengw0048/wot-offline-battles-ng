"""Execute exact #1513 battle animation bodies in Unicorn.

SwingingAnimator::update (0077f7b0) and receiveShotImpulse (0077f650),
LinkedNodesPitchAnimator::update (0078c070) and the SuspensionSound manager
update (00789630, with 0078a310) run unchanged. D3DX, memset, asinf, the
Svarog clock, compound node storage, std::function targets, MatrixProviders
and the Wwise post are hooked; D3DX/asinf call the host reference library
that the production kernel probe also uses.
"""
import ctypes
import math
import random
import struct

import pefile
import unicorn
from unicorn.x86_const import (UC_X86_REG_EAX, UC_X86_REG_ECX, UC_X86_REG_EIP, UC_X86_REG_ESP,
                               UC_X86_REG_FS, UC_X86_REG_GDTR, UC_X86_REG_SS, UC_X86_REG_XMM0)

SWING_UPDATE, SWING_IMPULSE, LINKED_UPDATE, SOUND_UPDATE = 0x77f7b0, 0x77f650, 0x78c070, 0x789630
D3DX = {'transform': (0x11c3bdc, 3), 'inverse': (0x11c3bb8, 3), 'normal': (0x11c3c06, 3),
        'ypr': (0x11c3be2, 4), 'multiply': (0x11c3bc4, 3), 'normalize': (0x11c3bbe, 2),
        'rotationX': (0x11c3bee, 2)}
MEMSET, ASINF, BAD_FUNCTION_CALL = 0x13dac1d, 0x13dad01, 0x13d7914
NODE_STORAGE, SOUND_POST, SVAROG_CLOCK = 0x8e0570, 0x124c310, 0x757920
STACK, HEAP, STUBS, GDT = 0x6000000, 0x6100000, 0x6200000, 0x6300000
SENTINEL, PROVIDER, FLOAT_A, FLOAT_B = STUBS + 0x200, STUBS + 0x210, STUBS + 0x220, STUBS + 0x230
VALUE_A, VALUE_B = STUBS + 0x300, STUBS + 0x304

# Native SwingingAnimator float fields in production probe order.
SWING_OFFSETS = ([0x04 + 4 * i for i in range(16)] + [0x44 + 4 * i for i in range(16)] +
                 [0xb8, 0xbc, 0xc0, 0xc4, 0xc8, 0xcc] + [0xd0 + 4 * i for i in range(6)] +
                 [0xe8 + 4 * i for i in range(6)] + [0x104 + 4 * i for i in range(16)])
assert len(SWING_OFFSETS) == 66


def f32(value):
    return struct.unpack('<f', struct.pack('<f', value))[0]


def bits(value):
    return struct.unpack('<I', struct.pack('<f', value))[0]


def from_bits(word):
    return struct.unpack('<f', struct.pack('<I', word))[0]


class Reference(object):
    """ctypes view of the host reference library (stock_battle_vehicle_animation_reference.hpp)."""

    def __init__(self, path):
        lib = ctypes.CDLL(str(path))
        P = ctypes.POINTER(ctypes.c_float)
        for name, args in (('xTransform', [P, P, P]), ('xTransformNormal', [P, P, P]), ('xNormalize', [P, P]),
                           ('xMultiply', [P, P, P]), ('xInverse', [P]),
                           ('xRotationYawPitchRoll', [P, ctypes.c_float, ctypes.c_float, ctypes.c_float]),
                           ('xRotationX', [P, ctypes.c_float])):
            getattr(lib, name).argtypes = args
            getattr(lib, name).restype = None
        lib.xAsin.argtypes = [ctypes.c_float]
        lib.xAsin.restype = ctypes.c_float
        self.lib = lib

    @staticmethod
    def array(values, size):
        return (ctypes.c_float * size)(*values)


class Machine(object):
    def __init__(self, executable, reference):
        pe = pefile.PE(str(executable))
        assert pe.FILE_HEADER.Machine == 0x14c
        image, base = pe.get_memory_mapped_image(), pe.OPTIONAL_HEADER.ImageBase
        self.u = u = unicorn.Uc(unicorn.UC_ARCH_X86, unicorn.UC_MODE_32)
        u.mem_map(base, (len(image) + 0xfff) & ~0xfff)
        u.mem_write(base, image)
        for region in (STACK, HEAP, STUBS, GDT):
            u.mem_map(region, 0x100000)
        teb = (0x1000 & 0xffff | (GDT + 0x1000 & 0xffffff) << 16 | 0x92 << 40 | 0x4 << 52 |
               ((GDT + 0x1000) >> 24 & 0xff) << 56)
        flat = 0xffff | 0x92 << 40 | 0xf << 48 | 0xc << 52
        u.mem_write(GDT, struct.pack('<QQQ', 0, teb, flat))
        u.reg_write(UC_X86_REG_GDTR, (0, GDT, 23, 0))
        u.reg_write(UC_X86_REG_FS, 1 << 3)
        u.reg_write(UC_X86_REG_SS, 2 << 3)
        # fld dword [value]; ret -- std::function<float()> and clock targets.
        u.mem_write(FLOAT_A, b'\xd9\x05' + struct.pack('<I', VALUE_A) + b'\xc3')
        u.mem_write(FLOAT_B, b'\xd9\x05' + struct.pack('<I', VALUE_B) + b'\xc3')
        self.ref = reference
        self.providers = {}
        self.posted = []
        self.thrown = False
        self.handlers = {SENTINEL: self._done, PROVIDER: self._provider, MEMSET: self._memset,
                         ASINF: self._asin, BAD_FUNCTION_CALL: self._bad, NODE_STORAGE: self._nodes,
                         SOUND_POST: self._post, SVAROG_CLOCK: self._clock}
        for name, (address, count) in D3DX.items():
            self.handlers[address] = (lambda n, c: lambda: self._d3dx(n, c))(name, count)
        for address in self.handlers:
            u.hook_add(unicorn.UC_HOOK_CODE, self._hook, begin=address, end=address)

    # -- memory helpers -----------------------------------------------------
    def word(self, address, value=None):
        if value is None:
            return struct.unpack('<I', self.u.mem_read(address, 4))[0]
        self.u.mem_write(address, struct.pack('<I', value & 0xffffffff))

    def floats(self, address, count, values=None):
        if values is None:
            return list(struct.unpack('<%df' % count, self.u.mem_read(address, 4 * count)))
        self.u.mem_write(address, struct.pack('<%df' % count, *values))

    # -- hooks --------------------------------------------------------------
    def _hook(self, u, address, size, data):
        self.handlers[address]()

    def _return(self, popped, eax=None):
        esp = self.u.reg_read(UC_X86_REG_ESP)
        target = self.word(esp)
        self.u.reg_write(UC_X86_REG_ESP, esp + 4 + 4 * popped)
        if eax is not None:
            self.u.reg_write(UC_X86_REG_EAX, eax)
        self.u.reg_write(UC_X86_REG_EIP, target)

    def arg(self, index):
        return self.word(self.u.reg_read(UC_X86_REG_ESP) + 4 + 4 * index)

    def _done(self):
        self.u.emu_stop()

    def _bad(self):
        self.thrown = True
        self.u.emu_stop()

    def _memset(self):
        destination, value, count = self.arg(0), self.arg(1), self.arg(2)
        self.u.mem_write(destination, bytes([value & 0xff]) * count)
        self._return(0, destination)

    def _asin(self):
        raw = self.u.reg_read(UC_X86_REG_XMM0)
        value = self.ref.lib.xAsin(from_bits(raw & 0xffffffff))
        self.u.reg_write(UC_X86_REG_XMM0, (raw & ~0xffffffff) | bits(value))
        self._return(0)

    def _provider(self):
        this = self.u.reg_read(UC_X86_REG_ECX)
        self.floats(self.arg(0), 16, self.providers[this])
        self._return(1)

    def _nodes(self):
        self._return(0, self.node_descriptor)

    def _post(self):
        self.posted.append(self.arg(0))
        self._return(1)

    def _clock(self):
        self._return(0, self.clock)

    def _d3dx(self, name, count):
        lib, A = self.ref.lib, Reference.array
        out = self.arg(0)
        if name in ('transform', 'normal'):
            v, m = A(self.floats(self.arg(1), 3), 3), A(self.floats(self.arg(2), 16), 16)
            result = A([0.] * 4, 4)
            (lib.xTransform if name == 'transform' else lib.xTransformNormal)(result, v, m)
            self.floats(out, 4 if name == 'transform' else 3, list(result)[:4 if name == 'transform' else 3])
        elif name == 'inverse':
            m = A(self.floats(self.arg(2), 16), 16)
            lib.xInverse(m)
            self.floats(out, 16, list(m))
        elif name == 'ypr':
            m = A([0.] * 16, 16)
            lib.xRotationYawPitchRoll(m, from_bits(self.arg(1)), from_bits(self.arg(2)), from_bits(self.arg(3)))
            self.floats(out, 16, list(m))
        elif name == 'multiply':
            a, b = A(self.floats(self.arg(1), 16), 16), A(self.floats(self.arg(2), 16), 16)
            m = A([0.] * 16, 16)
            lib.xMultiply(m, a, b)
            self.floats(out, 16, list(m))
        elif name == 'normalize':
            v, r = A(self.floats(self.arg(1), 3), 3), A([0.] * 3, 3)
            lib.xNormalize(r, v)
            self.floats(out, 3, list(r))
        elif name == 'rotationX':
            m = A([0.] * 16, 16)
            lib.xRotationX(m, from_bits(self.arg(1)))
            self.floats(out, 16, list(m))
        self._return(count, out)

    def call(self, address, ecx, args):
        esp = STACK + 0xff000 - 4 * len(args)
        for index, value in enumerate(args):
            self.word(esp + 4 * index, value)
        esp -= 4
        self.word(esp, SENTINEL)
        self.u.reg_write(UC_X86_REG_ESP, esp)
        self.u.reg_write(UC_X86_REG_ECX, ecx)
        self.thrown = False
        self.u.emu_start(address, SENTINEL, count=2000000)

    def provider(self, address, matrix):
        """A MatrixProvider object whose vtable slot 2 is the matrix() hook."""
        vtable = address + 0x40
        self.word(address, vtable)
        for slot in range(4):
            self.word(vtable + 4 * slot, PROVIDER)
        self.providers[address] = matrix
        return address

    def float_function(self, address, target):
        """std::function<float()> impl whose _Do_call (slot 2) loads a value."""
        vtable = address + 0x20
        self.word(address, vtable)
        for slot in range(6):
            self.word(vtable + 4 * slot, target)
        return address


class OriginalSwinging(object):
    OBJECT, LOD, WORLD, COMPENSATION, DIRECTION = HEAP, HEAP + 0x200, HEAP + 0x300, HEAP + 0x400, HEAP + 0x500

    def __init__(self, machine):
        self.m = machine

    def load(self, state, reset, lod_bound=True):
        m = self.m
        m.u.mem_write(self.OBJECT, b'\0' * 0x148)
        for offset, word in zip(SWING_OFFSETS, state):
            m.word(self.OBJECT + offset, word)
        m.u.mem_write(self.OBJECT + 0x100, bytes([1 if reset else 0]))
        m.word(self.OBJECT + 0xac, m.float_function(self.LOD, FLOAT_A) if lod_bound else 0)

    def step(self, step):
        m = self.m
        world, compensation = step['world'], step['compensation']
        m.word(self.OBJECT + 0xb0, m.provider(self.WORLD, world) if world is not None else 0)
        m.word(self.OBJECT + 0xb4, m.provider(self.COMPENSATION, compensation) if compensation is not None else 0)
        if step['impulse'] is not None:
            m.floats(self.DIRECTION, 3, step['impulse'][0])
            m.call(SWING_IMPULSE, self.OBJECT, [self.DIRECTION, bits(step['impulse'][1])])
        m.word(VALUE_A, bits(step['lod']))
        before = bytes(m.u.mem_read(self.OBJECT + 0x44, 64))
        m.call(SWING_UPDATE, self.OBJECT, [bits(step['dt'])])
        state = [m.word(self.OBJECT + offset) for offset in SWING_OFFSETS]
        reset = m.u.mem_read(self.OBJECT + 0x100, 1)[0]
        return {'thrown': m.thrown, 'state': state, 'reset': reset,
                'output_written': bytes(m.u.mem_read(self.OBJECT + 0x44, 64)) != before}


class OriginalLinkedNodes(object):
    ENTRY, GROUP, SLAVES, TABLE, NODES, DESCRIPTOR = (HEAP, HEAP + 0x100, HEAP + 0x400, HEAP + 0x1000,
                                                      HEAP + 0x2000, HEAP + 0x3000)

    def __init__(self, machine):
        self.m = machine

    def run(self, knots, rows, table, master, slaves):
        m = self.m
        m.u.mem_write(HEAP, b'\0' * 0x4000)
        m.node_descriptor = self.DESCRIPTOR
        m.word(self.DESCRIPTOR, self.NODES)
        m.floats(self.NODES, 16, master)
        m.word(self.ENTRY, HEAP + 0x3800)
        m.word(self.ENTRY + 4, self.GROUP)
        m.word(self.ENTRY + 8, self.GROUP + 0x7c)
        m.word(self.GROUP + 0x18, 0)
        m.word(self.GROUP + 0x5c, self.SLAVES)
        m.word(self.GROUP + 0x60, self.SLAVES + 0x5c * len(slaves))
        m.word(self.GROUP + 0x68, self.TABLE)
        m.word(self.GROUP + 0x74, knots)
        m.word(self.GROUP + 0x78, rows)
        m.floats(self.TABLE, len(table), [from_bits(word) for word in table])
        for index, authored in enumerate(slaves):
            record = self.SLAVES + 0x5c * index
            m.word(record + 0x18, index + 1)
            m.floats(record + 0x1c, 16, authored)
        m.call(LINKED_UPDATE, self.ENTRY, [self.ENTRY])
        return {'table': [m.word(self.TABLE + 4 * i) for i in range(len(table))],
                'poses': [[m.word(self.NODES + 64 * (index + 1) + 4 * k) for k in range(16)]
                          for index in range(len(slaves))]}


class OriginalSuspensionSound(object):
    MANAGER, RECORD, LOD, CLOCK, CLOCK_IFACE, BODY, VEHICLE = (HEAP, HEAP + 0x100, HEAP + 0x200, HEAP + 0x300,
                                                               HEAP + 0x380, HEAP + 0x400, HEAP + 0x500)

    def __init__(self, machine):
        self.m = machine

    def run(self, scenario):
        m = self.m
        m.u.mem_write(HEAP, b'\0' * 0x800)
        m.clock = self.CLOCK
        m.word(self.CLOCK + 0x50, m.float_function(self.CLOCK_IFACE, FLOAT_B))
        m.word(VALUE_B, scenario['dt'])
        m.word(VALUE_A, scenario['lod'])
        m.word(self.MANAGER + 8, self.RECORD)
        m.word(self.MANAGER + 0x14, 1)
        r = self.RECORD
        m.word(r + 0x24, m.float_function(self.LOD, FLOAT_A))
        for offset, key in ((0x28, 'lodSetting'), (0x2c, 'angleLimit'), (0x68, 'angle'), (0x6c, 'speed'),
                            (0x70, 'start')):
            m.word(r + offset, scenario[key])
        m.word(r + 0x74, scenario['motion'])
        m.u.mem_write(r + 0x78, struct.pack('<H', scenario['vehicleState']))
        for slot in range(9):
            m.word(r + 0x30 + 4 * slot, slot + 1 if slot < 8 else 0)
        m.word(r + 0x60, m.provider(self.BODY, scenario['body']) if scenario['body'] is not None else 0)
        m.word(r + 0x64, m.provider(self.VEHICLE, scenario['vehicle']) if scenario['vehicle'] is not None else 0)
        m.posted = []
        m.call(SOUND_UPDATE, self.MANAGER, [])
        return {'angle': m.word(r + 0x68), 'speed': m.word(r + 0x6c), 'start': m.word(r + 0x70),
                'motion': m.word(r + 0x74), 'posted': list(m.posted)}


# -- scenarios -------------------------------------------------------------
def identity():
    return [1., 0., 0., 0., 0., 1., 0., 0., 0., 0., 1., 0., 0., 0., 0., 1.]


def pose(rng, yaw, pitch, roll, position):
    sy, cy, sp, cp, sr, cr = (math.sin(yaw), math.cos(yaw), math.sin(pitch), math.cos(pitch),
                              math.sin(roll), math.cos(roll))
    rows = [cr * cy + sr * sp * sy, sr * cp, sr * sp * cy - cr * sy, 0.,
            cr * sp * sy - sr * cy, cr * cp, sr * sy + cr * sp * cy, 0.,
            cp * sy, -sp, cp * cy, 0.] + list(position) + [1.]
    return [f32(v) for v in rows]


def swing_scenarios(descriptors, seed=1513):
    """Descriptor tuples drive realistic motion; edges cover the branch set."""
    rng = random.Random(seed)
    multipliers = (0.9, 1.88, 0.3, 4.0, 1.0, 1.0)
    scenarios = []
    for index, (pitch, roll, shot) in enumerate(descriptors):
        state = [0.] * 66
        basis = pose(rng, rng.uniform(-.1, .1), 0., 0., (0., rng.uniform(.3, 1.), rng.uniform(-.5, .5)))
        state[0:16] = basis
        state[16:32] = identity()
        state[32:36] = [rng.choice((0., 0., 2., 1.)), rng.choice((0., 1., -1.)), 100., rng.choice((10., 20., 0.5))]
        state[36] = roll[0]
        state[37] = shot
        state[38:44] = [f32(p * k) for p, k in zip(pitch, multipliers)]
        state[44:50] = list(roll[1:])
        steps = []
        position, yaw, speed = [rng.uniform(-50, 50), 0., rng.uniform(-50, 50)], rng.uniform(-3, 3), 0.
        for number in range(rng.randint(4, 14)):
            dt = rng.choice((1 / 60., 1 / 30., .016, .05, .12, .29, .3, .31, .5, 0., 1e-6, 2e-5))
            speed += rng.uniform(-6, 6) * max(dt, .01)
            position[0] += math.sin(yaw) * speed * dt
            position[2] += math.cos(yaw) * speed * dt
            if rng.random() < .1:
                position[0] += rng.uniform(5, 30)  # teleport beyond maxMovementSpeed
            world = pose(rng, yaw, rng.uniform(-.2, .2), rng.uniform(-.2, .2), position)
            steps.append({
                'dt': f32(dt), 'lod': f32(rng.choice((10., 50., 99.9, 100., 100.01, 400.))),
                'world': None if rng.random() < .08 else world,
                'compensation': None if rng.random() < .5 else pose(rng, 0., rng.uniform(-.1, .1),
                                                                       rng.uniform(-.1, .1), (0., 0., 0.)),
                'impulse': None if rng.random() < .7 else ([f32(rng.uniform(-1, 1)) for _ in range(3)],
                                                           f32(rng.uniform(0, 3000)))})
        scenarios.append({'state': [bits(v) for v in state], 'reset': 1 if index % 3 else rng.randint(0, 1),
                          'steps': steps})
    return scenarios


def linked_scenarios(groups, seed=1513):
    """Every authored table shape, sampled at, between and beyond its knots."""
    rng = random.Random(seed)
    scenarios = []
    for rows in groups:
        knots = min(len(row) for row in rows)
        table = []
        for row in rows:
            table += [f32(v) for v in row[:knots]] + [0.]
        candidates = list(rows[0][:knots])
        candidates += [(a + b) / 2. for a, b in zip(rows[0], rows[0][1:knots])]
        candidates += [min(rows[0]) - .2, max(rows[0]) + .2, 0.]
        for angle in candidates + [rng.uniform(-.5, .5) for _ in range(3)]:
            angle = max(-1.4, min(1.4, angle))
            master = pose(rng, 0., -angle, 0., (0., 0., 0.))
            slaves = [pose(rng, rng.uniform(-.3, .3), rng.uniform(-.3, .3), 0.,
                           (rng.uniform(-1, 1), rng.uniform(0, 1), rng.uniform(-1, 1))) for _ in rows[1:]]
            current = rng.choice((0., f32(angle), table[knots]))
            scenario_table = list(table)
            scenario_table[knots] = current
            scenarios.append({'knots': knots, 'rows': len(rows), 'table': [bits(v) for v in scenario_table],
                              'master': master, 'slaves': slaves})
    return scenarios


def sound_scenarios(limits, seed=1513):
    rng = random.Random(seed)
    scenarios = []
    for number in range(400):
        limit = rng.choice(limits)
        body = pose(rng, rng.uniform(-3, 3), rng.uniform(-.3, .3), rng.uniform(-.1, .1), (0., 0., 0.))
        vehicle = pose(rng, rng.uniform(-3, 3), rng.uniform(-.3, .3), 0., (0., 0., 0.))
        scenarios.append({
            'lodSetting': bits(rng.choice((50., 100.))), 'angleLimit': bits(limit),
            'angle': bits(rng.uniform(-.3, .3)), 'speed': bits(rng.choice((0., .03, -.04, .2, -.6))),
            'start': bits(rng.uniform(-.3, .3)), 'motion': rng.randint(0, 4),
            'vehicleState': rng.choice((2, 2, 2, 0, 1, 3)), 'lod': bits(rng.choice((10., 50., 75., 100.))),
            'dt': bits(rng.choice((0., 1 / 60., .05, .2))),
            'body': None if rng.random() < .1 else body, 'vehicle': None if rng.random() < .1 else vehicle})
    return scenarios
