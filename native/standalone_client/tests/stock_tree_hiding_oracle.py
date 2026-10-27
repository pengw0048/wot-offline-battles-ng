"""Run the #1513 old-ST4 tree-hiding instructions under Unicorn.

The emulated x87 control word is the MSVC CRT default (53-bit precision).
D3DX calls use the float32 definitions of stock_cursor_camera_oracle; the
host production probe uses the same definitions.
"""
import struct

from stock_cursor_camera_oracle import OriginalCursorCamera

LINE_IN_ELLIPSOID = 0x11bdf60
COMPUTE_DRAW_DATA = 0xafe460
SELECT_LOD = 0xafe910
TICK = 0xb035a0
RENDER_CONTEXT = 0x1d380d4
CAMERA_STATE = 0x374
EYE = 0x188
PASS = 0x1d59f18
HIDE_CLOSEST, SQUARED_RADIUS, RADIUS, ALPHA_RADIUS = 0x1d59f08, 0x1d59f0c, 0x1d59f10, 0x1d59f14
TARGET, TRANSPARENCY = 0x1d59f74, 0x1a20ebc
FRAME_CAMERA, FRAME_TARGET, FRAME_DISTANCE = 0x1d59f80, 0x1d59f8c, 0x1d59f1c
MSVC_X87 = 0x27f
ENABLE_HIDING, SET_RADIUS, ENABLE_TRANSPARENCY = 0x6b1800, 0x6b3d00, 0x6b1880
TUPLE_SIZE, TUPLE_ITEM, SET_BOOL, SET_FLOAT, ARG_COUNT_ERROR = 0xfb3610, 0xfb3390, 0x690570, 0x68fba0, 0x68e100
SRT_ENABLE_HIDING, SRT_SET_RADIUS, SRT_SYSTEM = 0x748190, 0x7481d0, 0x1d211b4
PY_NONE = 0x1a5c798


class TreeHidingOracle(OriginalCursorCamera):
    def __init__(self, executable):
        super().__init__(executable)
        self.lod_distance = 1e6

    def _import(self, uc, address, size, data):
        if self.imports.get(address) == 'D3DXVec4Transform':
            # b035a0 projects the target into 1d59f78, which nothing reads.
            v, m = self.floats(self._arg(1), 4), self.matrix(self._arg(2))
            self.put_floats(self._arg(0), [sum(v[r] * m[r][c] for r in range(4)) for c in range(4)])
            self._return(self._arg(0), 12)
            return
        super()._import(uc, address, size, data)

    def run(self, function, args=(), ecx=0):
        x86 = self.x86
        stack = self.STACK + 0xf0000
        self.uc.mem_write(stack, struct.pack('<' + 'I' * (1 + len(args)), self.STOP, *args))
        self.uc.reg_write(x86.UC_X86_REG_ESP, stack)
        self.uc.reg_write(x86.UC_X86_REG_EBP, stack + 0x400)
        self.uc.reg_write(x86.UC_X86_REG_ECX, ecx)
        self.uc.reg_write(x86.UC_X86_REG_FPCW, MSVC_X87)
        self.uc.reg_write(x86.UC_X86_REG_MXCSR, 0x1f80)
        self.uc.emu_start(function, self.STOP, count=2000000)

    def line_in_ellipsoid(self, size, start, end):
        """11bdf60's unrounded st(0), stored by a caller stub as a double."""
        if not hasattr(self, 'lie_stub'):
            self.lie_stub = self.STUBS + 0x400
            self.lie_result = self.STUBS + 0x480
            code = bytes.fromhex('8b4424048b4c24088b54240c525150')
            after = self.lie_stub + len(code) + 5
            code += b'\xe8' + struct.pack('<i', LINE_IN_ELLIPSOID - after)
            code += bytes.fromhex('83c40c') + b'\xdd\x1d' + struct.pack('<I', self.lie_result) + b'\xc3'
            self.uc.mem_write(self.lie_stub, code)
        pointers = [self.alloc(12, struct.pack('<3f', *v)) for v in (size, start, end)]
        self.run(self.lie_stub, pointers)
        return struct.unpack('<d', self.uc.mem_read(self.lie_result, 8))[0]

    def compute_fade(self, case):
        """afe460 with afe910 supplying the case's LOD distance."""
        if SELECT_LOD not in self.handlers:
            def select(e):
                e.put_floats(e._arg(3), [self.lod_distance])
                e._return(0, 16)
            self.handle(SELECT_LOD, select)
        self.lod_distance = case['lod']
        settings = case.get('settings')
        if settings is not None:
            self.uc.mem_write(HIDE_CLOSEST, bytes([int(settings['hideClosest'])]))
            self.put_floats(SQUARED_RADIUS, [settings['squared'], settings['radius'], settings['alphaRadius']])
            self.uc.mem_write(TRANSPARENCY, bytes([int(settings['transparency'])]))
        self.put_word(TARGET, 0x1234 if case['frame'] else 0)
        if case['frame']:
            self.put_floats(FRAME_CAMERA, case['frame']['camera'])
            self.put_floats(FRAME_TARGET, case['frame']['target'])
            self.put_floats(FRAME_DISTANCE, [case['frame']['distance']])
        self.put_word(PASS, 0)
        tree_type = self.alloc(0x200)
        self.put_floats(tree_type + 0x164, list(case['min']) + list(case['max']))
        self.uc.mem_write(tree_type + 0x1e4, bytes([int(case['bush'])]))
        record = self.alloc(0x100)
        self.uc.mem_write(record, b'\x01')
        self.uc.mem_write(record + 0x2c, bytes([int(case['model3d'])]))
        camera = self.alloc(0x200)
        self.put_floats(camera + EYE, case['eye'])
        world = self.alloc(64, struct.pack('<16f', *case['world']))
        self.run(COMPUTE_DRAW_DATA, (camera, world, 0, 0, record), ecx=tree_type)
        return self.floats(record + 0x38, 2)

    def tick(self, matrix, eye, delta=0.016):
        """b035a0 with a scripted MatrixProvider target."""
        provider = self.alloc(16)
        slot = self.entry()
        self.put_word(provider, self.vtable(0, 0, slot))

        def provide(e):
            e.put_floats(e._arg(0), matrix)
            e._return(0, 4)
        self.handle(slot, provide)
        self.put_word(TARGET, provider)
        context = self.alloc(0x1000)
        self.put_word(RENDER_CONTEXT, context)
        self.put_floats(context + CAMERA_STATE + EYE, eye)
        self.run(TICK, (0, struct.unpack('<I', struct.pack('<f', delta))[0]))
        return (self.floats(FRAME_CAMERA, 3), self.floats(FRAME_TARGET, 3), self.floats(FRAME_DISTANCE, 1)[0])

    def state(self):
        """The renderer state the three BigWorld functions write."""
        return [self.byte(HIDE_CLOSEST)] + self.floats(SQUARED_RADIUS, 3) + [self.byte(TRANSPARENCY)]

    def call_script(self, function, values):
        """Call a BigWorld module function with scripted tuple items.

        values are ('bool', b), ('float', f) or ('bad', None). The engine's
        Script::setData converters and argCountError are call boundaries.
        """
        if not getattr(self, 'script_ready', False):
            self.script_ready = True
            self.events = []

            def size(e):
                e._return(e.word(e._arg(0) + 8))

            def item(e):
                e._return(e.word(e._arg(0) + 12 + 4 * e._arg(1)))

            def convert(kind):
                def handler(e):
                    obj, out, name = e._arg(0), e._arg(1), self.text(e._arg(2))
                    tag, raw = e.word(obj), e.word(obj + 4)
                    if tag != kind:
                        self.events.append(['error', name])
                        e._return(0xffffffff)
                        return
                    if kind == 1:
                        e.uc.mem_write(out, bytes([raw & 1]))
                    else:
                        e.put_word(out, raw)
                    e._return(0)
                return handler

            def count(e):
                name, low, high = self.text(e._arg(0)), e._arg(1), e._arg(2)
                types = [self.text(e._arg(3 + i)) for i in range(high)]
                self.events.append(['count', name, low, high] + types)
                e._return(0)

            def srt(name, cleanup, floats):
                def handler(e):
                    words = [e._arg(i) for i in range(cleanup // 4)]
                    self.events.append([name] + ([struct.unpack('<f', struct.pack('<I', w))[0] for w in words]
                                                 if floats else [w & 255 for w in words]))
                    e._return(0, cleanup)
                return handler
            self.handle(TUPLE_SIZE, size)
            self.handle(TUPLE_ITEM, item)
            self.handle(SET_BOOL, convert(1))
            self.handle(SET_FLOAT, convert(2))
            self.handle(ARG_COUNT_ERROR, count)
            self.handle(SRT_ENABLE_HIDING, srt('srt_hiding', 4, False))
            self.handle(SRT_SET_RADIUS, srt('srt_radius', 8, True))
            self.put_word(SRT_SYSTEM, 0x5150)
        self.events = []
        items = []
        for kind, value in values:
            raw = struct.unpack('<I', struct.pack('<f', value))[0] if kind == 'float' else int(bool(value))
            items.append(self.alloc(8, struct.pack('<2I', {'bool': 1, 'float': 2}.get(kind, 3), raw)))
        args = self.alloc(12 + 4 * len(items), struct.pack('<%dI' % (3 + len(items)), 1, 0, len(items), *items))
        none = self.word(PY_NONE)
        self.run(function, (0, args))
        result = self.uc.reg_read(self.x86.UC_X86_REG_EAX)
        return ['none' if result == PY_NONE else result, self.word(PY_NONE) - none, self.events, self.state()]

    def text(self, address):
        out = bytearray()
        while True:
            byte = self.uc.mem_read(address + len(out), 1)[0]
            if not byte:
                return out.decode()
            out.append(byte)
