"""Execute exact #1513 PyTimedWarplaneMotor trajectory bodies in Unicorn.

00734c90 (init), 00733590 -> 00734230 (addTrajectoryPoint without a
callback) and 007348e0 -> 00734520 (rev update) run unchanged. D3DX,
memset and __libm_sse2_atanf call the host reference library that the
production probe also uses; crossed-point callbacks are recorded through a
real std::map lookup.
"""
import ctypes
import math
import random
import struct

import unicorn
from unicorn.x86_const import UC_X86_REG_XMM0

import stock_battle_vehicle_animation_oracle as base

INIT, ADD, ADVANCE = 0x734c90, 0x733590, 0x7348e0
LOOK_AT, INVERSE, NORMALIZE2, ROTATION_Z, MULTIPLY = 0x11c3be8, 0x11c3bb8, 0x11c3c00, 0x11c3bfa, 0x11c3bc4
ATANF = 0x13dad1f
HEAP = base.HEAP
MOTOR = HEAP + 0x1000
STATE = MOTOR + 0x10
POINTS, MAP_HEAD, NODES, CALLBACK = HEAP + 0x2000, HEAP + 0x4000, HEAP + 0x4100, base.STUBS + 0x240
ARGS = HEAP + 0x8000
CAPACITY = 96


class Reference(object):
    def __init__(self, path):
        lib = ctypes.CDLL(str(path))
        P = ctypes.POINTER(ctypes.c_float)
        for name, args in (('xLookAtLH', [P, P, P, P]), ('xInverse', [P]), ('xNormalize2', [P, P]),
                           ('xRotationZ', [P, ctypes.c_float]), ('xMultiply', [P, P, P])):
            getattr(lib, name).argtypes = args
            getattr(lib, name).restype = None
        lib.xAtan.argtypes = [ctypes.c_float]
        lib.xAtan.restype = ctypes.c_float
        self.lib = lib


class Machine(base.Machine):
    def __init__(self, executable, reference):
        base.Machine.__init__(self, executable, None)
        self.motor_ref = reference
        self.callbacks = []
        extra = {LOOK_AT: self._look, INVERSE: self._inverse, NORMALIZE2: self._normalize2,
                 ROTATION_Z: self._rotation_z, MULTIPLY: self._multiply, ATANF: self._atan,
                 CALLBACK: self._callback}
        for address, handler in extra.items():
            self.handlers[address] = handler
            if address not in (INVERSE, MULTIPLY):
                self.u.hook_add(unicorn.UC_HOOK_CODE, self._hook, begin=address, end=address)

    def _array(self, address, count):
        return (ctypes.c_float * count)(*self.floats(address, count))

    def _look(self):
        out = (ctypes.c_float * 16)()
        self.motor_ref.lib.xLookAtLH(out, self._array(self.arg(1), 3), self._array(self.arg(2), 3),
                                     self._array(self.arg(3), 3))
        self.floats(self.arg(0), 16, list(out))
        self._return(4, self.arg(0))

    def _inverse(self):
        m = self._array(self.arg(2), 16)
        self.motor_ref.lib.xInverse(m)
        self.floats(self.arg(0), 16, list(m))
        self._return(3, self.arg(0))

    def _normalize2(self):
        out = (ctypes.c_float * 2)()
        self.motor_ref.lib.xNormalize2(out, self._array(self.arg(1), 2))
        self.floats(self.arg(0), 2, list(out))
        self._return(2, self.arg(0))

    def _rotation_z(self):
        out = (ctypes.c_float * 16)()
        self.motor_ref.lib.xRotationZ(out, base.from_bits(self.arg(1)))
        self.floats(self.arg(0), 16, list(out))
        self._return(2, self.arg(0))

    def _multiply(self):
        out = (ctypes.c_float * 16)()
        self.motor_ref.lib.xMultiply(out, self._array(self.arg(1), 16), self._array(self.arg(2), 16))
        self.floats(self.arg(0), 16, list(out))
        self._return(3, self.arg(0))

    def _atan(self):
        raw = self.u.reg_read(UC_X86_REG_XMM0)
        value = self.motor_ref.lib.xAtan(base.from_bits(raw & 0xffffffff))
        self.u.reg_write(UC_X86_REG_XMM0, (raw & ~0xffffffff) | base.bits(value))
        self._return(0)

    def _callback(self):
        this = self.u.reg_read(base.UC_X86_REG_ECX)
        key = self.word(this + 4)
        self.callbacks.append([key] + [self.word(self.arg(0) + 4 * i) for i in range(3)] +
                              [self.word(self.arg(1) + 4 * i) for i in range(3)])
        self._return(2)


class OriginalWarplane(object):
    """Fabricates the exact 0x94-byte motor with its +0x10 trajectory state."""

    def __init__(self, machine):
        self.m = machine

    def create(self, roll_speed, keys):
        m = self.m
        m.u.mem_write(HEAP + 0x1000, b'\0' * 0x8000)
        m.word(STATE + 0x00, 0)
        m.floats(STATE + 0x04, 16, base.identity())
        m.word(STATE + 0x4c, base.bits(roll_speed))
        # std::map<unsigned, std::function<void(Vector3, Vector3)>>: a right
        # chain of nodes under the nil head; each value records its key.
        nodes = [NODES + 0x40 * i for i in range(len(keys))]
        m.word(MAP_HEAD, nodes[0] if nodes else MAP_HEAD)
        m.word(MAP_HEAD + 4, nodes[0] if nodes else MAP_HEAD)
        m.word(MAP_HEAD + 8, nodes[-1] if nodes else MAP_HEAD)
        m.u.mem_write(MAP_HEAD + 0xc, b'\x01\x01')
        for i, (node, key) in enumerate(zip(nodes, keys)):
            impl = NODES + 0x1000 + 0x40 * i
            vtable = impl + 0x10
            m.word(impl, vtable)
            m.word(impl + 4, key)
            for slot in range(6):
                m.word(vtable + 4 * slot, CALLBACK)
            m.word(node, MAP_HEAD)
            m.word(node + 4, MAP_HEAD)
            m.word(node + 8, nodes[i + 1] if i + 1 < len(nodes) else MAP_HEAD)
            m.u.mem_write(node + 0xc, b'\x01\x00')
            m.word(node + 0x10, key)
            m.word(node + 0x3c, impl)
        m.word(STATE + 0x50, MAP_HEAD)
        m.word(STATE + 0x54, len(keys))
        m.word(STATE + 0x70, POINTS)
        m.word(STATE + 0x74, POINTS)
        m.word(STATE + 0x78, POINTS + 0x1c * CAPACITY)

    def _point(self, address, point):
        self.m.floats(address, 7, list(point[0]) + list(point[1]) + [point[2]])

    def init(self, now, first, second):
        self._point(ARGS, first)
        self._point(ARGS + 0x20, second)
        self.m.call(INIT, STATE, [base.bits(now), ARGS, ARGS + 0x20])

    def add(self, point):
        self.m.floats(ARGS, 3, point[0])
        self.m.floats(ARGS + 0x10, 3, point[1])
        self.m.call(ADD, MOTOR, [ARGS, ARGS + 0x10, base.bits(point[2]), 0])

    def advance(self, now, elapsed):
        self.m.callbacks = []
        self.m.call(ADVANCE, STATE, [base.bits(now), base.bits(elapsed)])
        return list(self.m.callbacks)

    def state(self):
        m = self.m
        count = (m.word(STATE + 0x74) - m.word(STATE + 0x70)) // 0x1c
        words = [m.word(STATE + offset) for offset in range(0, 0x4c, 4)]
        words += [m.word(STATE + offset) for offset in range(0x58, 0x70, 4)]
        words += [m.word(STATE + 0x7c), m.word(STATE + 0x80), count]
        words += [m.word(POINTS + 4 * i) for i in range(7 * count)]
        return words


def scenarios(seed=1513):
    """FlockExotic-shaped flights plus boundary and insertion edge cases."""
    rng = random.Random(seed)
    result = []
    for number in range(120):
        now = base.f32(rng.choice((0., 12.5, 431.25, 3600.75)) + rng.uniform(0, 5))
        start = [base.f32(rng.uniform(-200, 200)), base.f32(rng.uniform(0, 30)), base.f32(rng.uniform(-200, 200))]
        velocity = [base.f32(rng.uniform(-12, 12)), base.f32(rng.uniform(-2, 8)), base.f32(rng.uniform(-12, 12))]
        if number % 10 == 0:
            velocity = [0., base.f32(rng.uniform(1, 6)), 0.]  # vertical flight (dirLength == 0)
        initial = [base.f32(v * rng.uniform(.2, .6)) for v in velocity]
        acceleration = base.f32(rng.choice((0.5, 1., 2., 3.)))
        flight = base.f32(rng.uniform(1., 15.))
        middle = [base.f32(s + (i + v) / 2.) for s, i, v in zip(start, initial, velocity)]
        end = [base.f32(s + v * flight) for s, v in zip(start, velocity)]
        first = (start, initial, now)
        second = (middle, velocity, base.f32(now + acceleration))
        ops = [('add', (end, velocity, base.f32(now + flight)))]
        clock = now
        for step in range(rng.randint(10, 50)):
            dt = base.f32(rng.choice((1 / 60., 1 / 30., .1, .25, 1., 0.)))
            clock = base.f32(clock + dt)
            ops.append(('rev', clock if rng.random() < .8 else base.f32(clock + rng.uniform(-.02, .02)), dt))
            if rng.random() < .08:
                when = base.f32(clock + rng.choice((-1., 0., .5, 3.)))
                ops.append(('add', ([base.f32(rng.uniform(-200, 200)), base.f32(rng.uniform(0, 40)),
                                     base.f32(rng.uniform(-200, 200))],
                                    [base.f32(rng.uniform(-10, 10)) for _ in range(3)], when)))
        roll = base.f32(rng.choice((0., 0., .349066, 1.5)))
        result.append({'roll': roll, 'now': now, 'first': first, 'second': second, 'ops': ops})
    return result
