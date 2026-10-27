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
        settings = case['settings']
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

