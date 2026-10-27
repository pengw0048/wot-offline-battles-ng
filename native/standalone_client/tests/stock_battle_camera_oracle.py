"""Run original #1513 battle camera instructions over scripted engine owners.

The camera collider (007a34c0 family), its advanced volume-group collider
(007a52d0), the ThirdPersonProvider update (007a0960) and the HomingCamera
update (007a19e0) execute as x86. Space queries report a scripted list of
(obstacle, triangle, distance) hits into the original callbacks; D3DX and
libm use the float32 definitions shared with stock_battle_camera_engine.hpp.
This proves control and data flow, not engine traversal or rendering.
"""
import struct

from stock_cursor_camera_oracle import CameraMath, OriginalCursorCamera, bits, f32, unbits

OPERATOR_NEW = 0x13d7fe9
OPERATOR_DELETE = 0x13d8019
SIZED_DELETE = 0x13d801e
RENDER_CONTEXT = 0x1d380d4
CAMERA_SPACE = 0x1d59748
CAMERA_OVERRIDE = 0x1d2a904
IGNORE_DESTRUCTIBLES = 0x1a138c8
IGNORE_DESTRUCTIBLES_VTABLE = 0x14d998c

SEGMENT_SEGMENT = 0x7a7200
INSIDE_TRIANGLE = 0x7a7870
CLOSEST_ON_TRIANGLE = 0xf666e0
SEGMENT_TRIANGLE = 0x7a7590
PLANE_SEGMENT = 0x11bd260
TRIANGLE_RAY = 0x1172190
SPACE_BOUNDS = 0x7a3ed0
ADVANCED_CONSTRUCT = 0x7a50b0
ADVANCED_INIT = 0x7a5230
ADVANCED_ADD_GROUP = 0x7a51b0
ADVANCED_SCROLL = 0x7a5270
ADVANCED_COLLIDE = 0x7a52d0
SWEEP_COLLIDE = 0xab7cc0
OBJECT_BOUNDS = 0xab9e70
MATERIAL_DISABLER = 0xf64630
COLLIDER_COLLIDE = 0x7a34c0
TPP_UPDATE = 0x7a0960
TPP_STATIC_GUARD = 0x7a0c31
TPP_STATIC_DONE = 0x7a0c84
HOMING_UPDATE = 0x7a19e0
SPEEDTREE_UPDATE = 0x7978e0

ADVANCED_SIZE = 0x5c
APP = 0x1d1ff60
APP_GAME_DELTA = 0x57c
WATERS = 0xa2cd40
WATER_DISTANCE = 0xa302e0
SCENE_SPACE = 0x7a3770

# Branch targets the scenario sets must reach (0x7a5a65, a tangent sphere, is not).
BRANCHES = {
    0x7a5cc8: 'group without hits', 0x7a5cb6: 'hit beyond radius', 0x7a59f9: 'hit behind front',
    0x7a5a76: 'far sphere root', 0x7a5a7d: 'sphere candidate', 0x7a5ab9: 'hit ahead of front',
    0x7a5ce4: 'critical distance skip', 0x7a5ade: 'current group', 0x7a5afe: 'approach',
    0x7a5d42: 'rollback', 0x7a5feb: 'minimum distance', 0x7a6062: 'ceiling', 0x7a5f5d: 'negative offset',
    0x7a6c79: 'front volume', 0x7a6d8d: 'last group', 0x7a6f84: 'rear volume', 0x7a70e4: 'rear nearer',
    0x7a0d92: 'pivot at start', 0x7a0da4: 'pivot advanced', 0x7a0df8: 'pivot clear',
    0x7a0e56: 'no collision', 0x7a1b3b: 'zero aim', 0x7a1bd0: 'clip clamp', 0x7a1d52: 'camera moved',
    0x7a52c2: 'scroll ignored', 0x7a52bd: 'scroll clamp', 0x7a3507: 'advanced collider path',
    0x7a352d: 'space check path', 0x7a373d: 'water lift', 0x7a4104: 'space box clamp',
}


class BattleMath(CameraMath):
    @staticmethod
    def powf(base, exponent):
        import math
        try:
            return f32(math.pow(base, exponent))
        except OverflowError:
            return float('inf')
        except ValueError:
            return float('nan')

    @classmethod
    def atanf(cls, value):
        import math
        return cls._unary(math.atan, value)


def hex_bits(value):
    return '%08x' % bits(value)


class OriginalBattleCamera(OriginalCursorCamera):
    """The #1513 image with scripted space, render context and heap."""

    def __init__(self, executable):
        super(OriginalBattleCamera, self).__init__(executable)
        self.rehandle(OPERATOR_NEW, lambda e: e._return(e.alloc(max(e._arg(0), 4))))
        self.handle(OPERATOR_DELETE, lambda e: e._return())
        self.handle(SIZED_DELETE, lambda e: e._return())
        self.handle(MATERIAL_DISABLER, lambda e: e._return(1))
        self.handle(OBJECT_BOUNDS, self._object_bounds)
        self.handle(SWEEP_COLLIDE, self._sweep)
        self.sweep = []
        self.rear = []
        self.segment = []
        self.object_bounds = {}
        self.space_bounds = ((0.0,) * 3, (0.0,) * 3)
        self.sweep_calls = 0
        self.segment_calls = 0
        self.events = []
        self.context = self.alloc(0x400)
        self.put_word(RENDER_CONTEXT, self.context)
        self.put_word(CAMERA_OVERRIDE, 0)
        bounds_slot, segment_slot = self.entry(), self.entry()
        self.handle(bounds_slot, self._space_bounds)
        self.handle(segment_slot, self._segment)
        slots = [self.entry() for unused in range(16)]
        slots[12], slots[15] = bounds_slot, segment_slot
        self.space = self.alloc(0x40)
        self.put_word(self.space, self.vtable(*slots))
        self.put_word(self.space + 4, 1000)
        self.put_word(CAMERA_SPACE, self.space)
        self.app = self.alloc(0x600)
        self.put_word(APP, self.app)
        self.waters = []
        self.scene_check = (False, (0.0, 0.0, 0.0))
        self.rehandle(WATERS, self._waters)
        self.rehandle(SCENE_SPACE, self._scene_space)
        self.rehandle(WATER_DISTANCE, self._water_distance)
        self.result = self.alloc(16)
        self.stop_x87 = self.STOP - 6
        self.uc.mem_write(self.stop_x87, b'\xd9\x1d' + struct.pack('<I', self.result))

    def rehandle(self, address, function):
        previous = self.handlers.get(address)
        if previous is not None:
            self.uc.hook_del(previous)
        self.handle(address, function)

    # Imports the CursorCamera oracle does not script.
    def _import(self, uc, address, size, data):
        name = self.imports.get(address)
        m = BattleMath
        if name == 'D3DXMatrixTranslation':
            matrix = [[1.0, 0.0, 0.0, 0.0], [0.0, 1.0, 0.0, 0.0], [0.0, 0.0, 1.0, 0.0],
                      [unbits(self._arg(1)), unbits(self._arg(2)), unbits(self._arg(3)), 1.0]]
            self.put_matrix(self._arg(0), matrix)
            self._return(self._arg(0), 16)
            return
        if name in ('D3DXMatrixRotationX', 'D3DXMatrixRotationY'):
            angle = unbits(self._arg(1))
            s, c = m.sinf(angle), m.cosf(angle)
            matrix = [[1.0, 0.0, 0.0, 0.0], [0.0, 1.0, 0.0, 0.0], [0.0, 0.0, 1.0, 0.0],
                      [0.0, 0.0, 0.0, 1.0]]
            if name == 'D3DXMatrixRotationX':
                matrix[1][1], matrix[1][2], matrix[2][1], matrix[2][2] = c, s, -s, c
            else:
                matrix[0][0], matrix[0][2], matrix[2][0], matrix[2][2] = c, -s, s, c
            self.put_matrix(self._arg(0), matrix)
            self._return(self._arg(0), 8)
            return
        if name == '__libm_sse2_powf':
            self._set_xmm0(m.powf(self._xmm(self.x86.UC_X86_REG_XMM0),
                                  self._xmm(self.x86.UC_X86_REG_XMM1)))
            self._return()
            return
        if name == '__libm_sse2_atanf':
            self._set_xmm0(m.atanf(self._xmm(self.x86.UC_X86_REG_XMM0)))
            self._return()
            return
        super(OriginalBattleCamera, self)._import(uc, address, size, data)

    # Scripted engine owners.
    def _object_bounds(self, e):
        low, high = self.object_bounds[e._arg(2)]
        self.put_floats(e._arg(0), list(low) + list(high))
        e._return(e._arg(0), 12)

    def _space_bounds(self, e):
        self.put_floats(e._arg(0), list(self.space_bounds[0]) + list(self.space_bounds[1]))
        e._return(e._arg(0), 4)

    def _waters(self, e):
        pointers = []
        for depth in self.waters:
            water = self.alloc(16)
            self.put_floats(water + 4, [depth])
            pointers.append(water)
        array = self.alloc(4 * max(len(pointers), 1), struct.pack('<%dI' % len(pointers), *pointers))
        vector = self.alloc(12)
        self.uc.mem_write(vector, struct.pack('<III', array, array + 4 * len(pointers),
                                              array + 4 * len(pointers)))
        e._return(vector)

    def _water_distance(self, e):
        water = e.uc.reg_read(e.x86.UC_X86_REG_ECX)
        e._return_x87(self.floats(water + 4, 1)[0], 12)

    def _scene_space(self, e):
        moved, offset = self.scene_check
        if moved:
            position = e._arg(0)
            current = self.floats(position, 3)
            self.put_floats(position, [f32(current[i] + offset[i]) for i in range(3)])
        e._return(1 if moved else 0)

    def _hit_memory(self, hit):
        identifier, matrix, triangle, flags, distance = hit
        transform = self.alloc(64)
        self.put_matrix(transform, matrix)
        obstacle = self.alloc(0x14)
        self.uc.mem_write(obstacle, struct.pack('<IIIII', 0, transform, transform, 1, identifier))
        face = self.alloc(0x28)
        self.put_floats(face, [c for v in triangle for c in v])
        self.uc.mem_write(face + 0x24, struct.pack('<HH', flags, 0))
        return obstacle, face, distance

    def _visits(self, callback, segment):
        """x86 that offers every scripted hit to the original callback."""
        code = b''
        hits = self.segment if segment else (self.sweep if self.sweep_calls <= 2 else self.rear)
        for hit in hits:
            obstacle, face, distance = self._hit_memory(hit)
            code += (b'\x68' + struct.pack('<I', bits(distance)) + b'\x68' + struct.pack('<I', face) +
                     b'\x68' + struct.pack('<I', obstacle) + b'\xb9' + struct.pack('<I', callback) +
                     b'\x8b\x01\xff\x50\x10')
            if segment:
                code += (b'\x83\xf8\x03\x74\x0a\xc7\x05' + struct.pack('<I', self.result) +
                         struct.pack('<I', bits(distance)))
        return code

    def _run_visits(self, callback, segment, cleanup):
        self.put_floats(self.result, [-1.0])
        code = self._visits(callback, segment) + b'\xd9\x05' + struct.pack('<I', self.result)
        code += b'\xc3' if cleanup == 0 else b'\xc2' + struct.pack('<H', cleanup)
        stub = self.alloc(len(code), code)
        self.uc.reg_write(self.x86.UC_X86_REG_EIP, stub)

    def _sweep(self, e):
        self.sweep_calls += 1
        self._run_visits(e._arg(3), False, 0)

    def _segment(self, e):
        self.segment_calls += 1
        self._run_visits(e._arg(2), True, 12)

    # Calls.
    def vector(self, values):
        address = self.alloc(12)
        self.put_floats(address, list(values))
        return address

    def call_x87(self, function, args=(), ecx=0):
        x86 = self.x86
        stack = self.STACK + 0xf0000
        self.uc.mem_write(stack, struct.pack('<' + 'I' * (1 + len(args)), self.stop_x87, *args))
        self.uc.reg_write(x86.UC_X86_REG_ESP, stack)
        self.uc.reg_write(x86.UC_X86_REG_EBP, stack + 0x400)
        self.uc.reg_write(x86.UC_X86_REG_ECX, ecx)
        self.uc.reg_write(x86.UC_X86_REG_FPCW, 0x37f)
        self.uc.reg_write(x86.UC_X86_REG_MXCSR, 0x1f80)
        self.uc.emu_start(function, self.STOP, count=20000000)
        return self.floats(self.result, 1)[0]


class Scenario(object):
    """Runs one command list and renders each line like the host harness."""

    def __init__(self, executable, coverage=None):
        self.e = OriginalBattleCamera(executable)
        self.advanced = None
        if coverage is not None:
            from unicorn import UC_HOOK_CODE
            for address in BRANCHES:
                self.e.uc.hook_add(UC_HOOK_CODE, lambda uc, a, size, data: coverage.add(a),
                                   begin=address, end=address)

    def run(self, commands):
        return [self.command(c) for c in commands]

    def command(self, c):
        e = self.e
        name, args = c[0], c[1:]
        out = [name]
        if name == 'camera':
            e.put_floats(e.context + 0x374, list(args))
        elif name == 'space':
            e.put_word(CAMERA_SPACE, e.space if args[0] else 0)
        elif name == 'dtime':
            e.put_floats(e.app + APP_GAME_DELTA, [args[0]])
        elif name == 'spacebounds':
            e.space_bounds = (tuple(args[0]), tuple(args[1]))
        elif name == 'waters':
            e.waters = list(args[0])
        elif name == 'scenecheck':
            e.scene_check = (bool(args[0]), tuple(args[1]))
        elif name == 'collide':
            collider = e.alloc(0x68 + 0x10)
            e.uc.mem_write(collider + 0xc, bytes(e.uc.mem_read(self.advanced, ADVANCED_SIZE)))
            position = e.vector(args[0])
            hit = e.call(COLLIDER_COLLIDE, [position, e.vector(args[1]), e.vector(args[2]),
                                            e.vector(args[3]), 1 if args[4] else 0], ecx=collider) & 0xff
            e.uc.mem_write(self.advanced, bytes(e.uc.mem_read(collider + 0xc, ADVANCED_SIZE)))
            out += [str(hit)] + [hex_bits(v) for v in e.floats(position, 3)] + self.state()
            e.sweep_calls = e.segment_calls = 0
        elif name in ('sweep', 'rear', 'segment'):
            setattr(e, name, list(args[0]))
        elif name == 'objbounds':
            e.object_bounds[args[0]] = (tuple(args[1]), tuple(args[2]))
        elif name == 'segseg':
            c1, c2 = e.alloc(12), e.alloc(12)
            d = e.call_x87(SEGMENT_SEGMENT, [e.vector(v) for v in args] + [c1, c2])
            out += [hex_bits(d)] + [hex_bits(v) for v in e.floats(c1, 3) + e.floats(c2, 3)]
        elif name == 'inside':
            r = e.alloc(12)
            face = e.alloc(0x28)
            e.put_floats(face, [x for v in args[1:] for x in v])
            hit = e.call(INSIDE_TRIANGLE, [e.vector(args[0]), face, r]) & 0xff
            out += [str(hit)] + [hex_bits(v) for v in e.floats(r, 3)]
        elif name == 'closest':
            r = e.alloc(12)
            e.call(CLOSEST_ON_TRIANGLE, [r] + [e.vector(v) for v in args])
            out += [hex_bits(v) for v in e.floats(r, 3)]
        elif name == 'segtri':
            face = e.alloc(0x28)
            e.put_floats(face, [x for v in args[2:] for x in v])
            s, t = e.alloc(12), e.alloc(12)
            d = e.call_x87(SEGMENT_TRIANGLE, [e.vector(args[0]), e.vector(args[1]), face, s, t])
            out += [hex_bits(d)] + [hex_bits(v) for v in e.floats(s, 3) + e.floats(t, 3)]
        elif name == 'plane':
            plane = e.alloc(16)
            e.put_floats(plane, list(args[0]) + [args[1]])
            r = e.alloc(12)
            hit = e.call(PLANE_SEGMENT, [r, e.vector(args[2]), e.vector(args[3])], ecx=plane) & 0xff
            out += [str(hit)] + [hex_bits(v) for v in e.floats(r, 3)]
        elif name == 'triray':
            face = e.alloc(0x28)
            e.put_floats(face, [x for v in args[:3] for x in v])
            length = e.alloc(4)
            e.put_floats(length, [args[5]])
            hit = e.call(TRIANGLE_RAY, [e.vector(args[3]), e.vector(args[4]), length], ecx=face) & 0xff
            out += [str(hit), hex_bits(e.floats(length, 1)[0])]
        elif name == 'spacebox':
            position = e.vector(args[0])
            copy = e.vector(args[0])
            hit = e.call(SPACE_BOUNDS, [position, e.vector(args[1]), e.vector(args[2]), copy]) & 0xff
            out += [str(hit)] + [hex_bits(v) for v in e.floats(position, 3)]
        elif name == 'advinit':
            self.advanced = e.alloc(ADVANCED_SIZE + 0x10)
            zero = e.vector((0.0, 0.0, 0.0))
            e.call(ADVANCED_CONSTRUCT, [zero, 1], ecx=self.advanced)
            e.call(ADVANCED_INIT, [bits(v) for v in args], ecx=self.advanced)
        elif name == 'advgroup':
            group = e.alloc(0x20)
            e.put_floats(group, list(args[:7]))
            e.uc.mem_write(group + 0x1c, bytes([1 if args[7] else 0, 0, 0, 0]))
            e.call(ADVANCED_ADD_GROUP, [group], ecx=self.advanced)
        elif name == 'advoffset':
            e.put_floats(self.advanced + 0x2c, [args[0], args[1]])
        elif name == 'scroll':
            e.call(ADVANCED_SCROLL, [bits(args[0])], ecx=self.advanced)
            out += self.state()
        elif name == 'advcollide':
            position = e.vector(args[1])
            hit = e.call(ADVANCED_COLLIDE, [bits(args[0]), position, e.vector(args[2]),
                                            e.vector((0.0, 0.0, 1.0)), e.vector(args[3])],
                         ecx=self.advanced) & 0xff
            out += [str(hit)] + [hex_bits(v) for v in e.floats(position, 3)] + self.state()
            out += [str(e.sweep_calls), str(e.segment_calls)]
            e.sweep_calls = e.segment_calls = 0
        elif name == 'tpp':
            out += self.tpp(*args)
        elif name == 'homing':
            out += self.homing(*args)
        else:
            raise RuntimeError('Unknown oracle command ' + name)
        return ' '.join(out)

    def state(self):
        e, a = self.e, self.advanced
        values = e.floats(a + 0x18, 9)
        return [hex_bits(v) for v in values] + [str(e.word(a + 0x50)), str(e.word(a + 0x54))]

    def _collider_hook(self, events, result, output):
        e = self.e

        def collide(x):
            events.append('collide')
            for i in range(4):
                events.extend(hex_bits(v) for v in e.floats(x._arg(i), 3))
            events.append(str(x._arg(4) & 0xff))
            if result:
                e.put_floats(x._arg(0), list(output))
            x._return(1 if result else 0, 20)
        return collide

    def _matrix_provider(self, matrix):
        e = self.e
        slot = e.entry()

        def provide(x):
            e.put_matrix(x._arg(0), matrix)
            x._return(0, 4)
        e.handle(slot, provide)
        provider = e.alloc(0x20)
        e.put_word(provider, e.vtable(e.entry(), e.entry(), slot))
        return provider

    def _vector4_provider(self, value):
        e = self.e
        slot = e.entry()

        def output(x):
            e.put_floats(x._arg(0), list(value) + [1.0])
            x._return(0, 4)
        e.handle(slot, output)
        provider = e.alloc(0x40)
        e.put_word(provider, e.vtable(e.entry(), e.entry(), slot))
        return provider

    def tpp(self, base, height, radius, distance, yaw, pitch, up, result, output, frames, collide):
        e = self.e
        events = []
        e.rehandle(COLLIDER_COLLIDE, self._collider_hook(events, result, output))
        e.put_word(IGNORE_DESTRUCTIBLES, IGNORE_DESTRUCTIBLES_VTABLE)
        e.rehandle(TPP_STATIC_GUARD,
                 lambda x: x.uc.reg_write(x.x86.UC_X86_REG_EIP, TPP_STATIC_DONE))
        t = e.alloc(0x160)
        identity = [[1.0 if i == j else 0.0 for j in range(4)] for i in range(4)]
        e.put_word(t + 0xc, self._matrix_provider(base) if base is not None else 0)
        e.put_floats(t + 0x10, [height, radius] + list(up) + [distance, yaw, pitch])
        for offset in (0x30, 0x70, 0xb0):
            e.put_matrix(t + offset, identity)
        e.uc.mem_write(t + 0xf0, b'\x01')
        basic = e.alloc(0x28)
        e.put_word(t + 0xf4, basic)
        for unused in range(frames):
            e.call(TPP_UPDATE, [1 if collide else 0], ecx=t)
            events += [hex_bits(v) for v in e.floats(t + 0x70, 16) + e.floats(t + 0xb0, 16) +
                       e.floats(t + 0x30, 16) + e.floats(basic + 0x18, 4)]
        return events

    def homing(self, camera, aim, pivot, up, clip, result, output, dtime):
        e = self.e
        events = []
        e.rehandle(COLLIDER_COLLIDE, self._collider_hook(events, result, output))
        e.rehandle(SPEEDTREE_UPDATE, lambda x: (events.append('speedtree'), x._return(0, 4)))
        h = e.alloc(0x130)
        for offset, value in ((0xac, camera), (0xb0, aim), (0xa8, pivot)):
            e.put_word(h + offset, self._vector4_provider(value) if value is not None else 0)
        e.put_floats(h + 0xb4, list(up) + list(clip))
        e.call(HOMING_UPDATE, [bits(dtime)], ecx=h)
        events += [hex_bits(v) for v in e.floats(h + 0x18, 16) + e.floats(h + 0x58, 16)]
        return events
