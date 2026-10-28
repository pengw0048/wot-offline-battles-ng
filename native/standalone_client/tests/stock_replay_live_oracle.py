"""Execute #1513 replay live wrappers, with explicit conversion/owner seams."""
import math
import struct
from stock_cursor_camera_oracle import OriginalCursorCamera, bits, unbits

NONE = 0x1a5c798


class OriginalReplayLive(OriginalCursorCamera):
    def _import(self, uc, address, size, data):
        if self.imports[address] == '_isnan':
            raw = self.word(self.uc.reg_read(self.x86.UC_X86_REG_ESP) + 4)
            high = self.word(self.uc.reg_read(self.x86.UC_X86_REG_ESP) + 8)
            self._return(int(math.isnan(struct.unpack('<d', struct.pack('<II', raw, high))[0])))
            return
        return super()._import(uc, address, size, data)

    def __init__(self, executable):
        super().__init__(executable)
        self.values = {NONE: None}
        self.owner = self.alloc(0x220)
        self.codec = self.alloc(0xb0)
        self.put_word(self.owner + 0x194, self.codec)
        self.entities = {}
        self.events = []
        self.error = False
        self.handle(0xfb3610, lambda e: e._return(len(self.values[e._arg(0)])))
        self.handle(0xfb3390, lambda e: e._return(self.object(self.values[e._arg(0)][e._arg(1)])))
        self.handle(0x68e100, lambda e: (setattr(self, 'error', True), e._return()))
        for address, kind in ((0x68fa20, 'int'), (0x68fad0, 'uint'), (0x68fba0, 'float'),
                              (0x690570, 'bool'), (0x690190, 'vec2'), (0x690240, 'vec3')):
            self.handle(address, lambda e, k=kind: self.convert(k))
        for address, kind in ((0x68ed00, 'float'), (0x68ee00, 'bool'), (0x68ece0, 'uint'),
                              (0xfe1200, 'int'), (0x68eb40, 'vec2'), (0x68ebd0, 'vec3')):
            self.handle(address, lambda e, k=kind: self.get_value(k))
        self.handle(0xfb3420, lambda e: e._return(self.object([None] * e._arg(0))))
        self.handle(0xfb3560, self.tuple_set)
        self.handle(0x68e560, self.callback)
        self.handle(0x6410a0, lambda e: e._return(0x12340000))
        self.handle(0x7d5b50, lambda e: e._return(self.entities.get(e._arg(0), 0), 4))
        for address, name, width in ((0x70c070, 'serverAim', 4), (0x70c170, 'slot', 4),
                                     (0x70c670, 'reload', 8), (0x70bda0, 'reset', 0),
                                     (0x70a1e0, 'ammo', 4), (0x70b7f0, 'lock', 8),
                                     (0x70c470, 'cruise', 4), (0x70c570, 'equipment', 4),
                                     (0x70c880, 'sniper', 4), (0x70c270, 'length', 4),
                                     (0x70c370, 'period', 4)):
            self.handle(address, lambda e, n=name, w=width:
                        (self.events.append(('packet', n)), e._return(0, w)))
        for offset in (0x170, 0x180, 0x184, 0x188, 0x18c):
            self.put_word(self.owner + offset, self.object('callback-%x' % offset))
        # Reproduce the recovered constructor defaults used by these leaves.
        self.put_word(self.owner + 0x1d8, bits(.01))
        for offset in (0x1dc, 0x15c, 0x160):
            self.put_word(self.owner + offset, bits(-1))
        for offset in (0x1d0, 0x1d4):
            self.put_word(self.owner + offset, bits(-100000))
        self.put_word(self.owner + 0x1f0, -1)

    def object(self, value):
        result = self.alloc(16)
        self.put_word(result, 100)
        self.values[result] = value
        return result

    def convert(self, kind):
        value = self.values[self._arg(0)]
        destination = self._arg(1)
        try:
            if kind in ('int', 'uint'):
                if not isinstance(value, int):
                    raise ValueError('int')
                if not (-2**31 <= value < 2**31 if kind == 'int' else 0 <= value < 2**32):
                    raise ValueError('domain')
                self.put_word(destination, value)
            elif kind == 'float':
                if not isinstance(value, (int, float)):
                    raise ValueError('float')
                self.put_word(destination, bits(value))
            elif kind == 'bool':
                self.uc.mem_write(destination, bytes([int(bool(value))]))
            else:
                width = int(kind[-1])
                if not isinstance(value, tuple) or len(value) != width:
                    raise ValueError('vector')
                self.put_floats(destination, value)
            self._return()
        except (ValueError, TypeError, OverflowError):
            self.error = True
            self._return(-1)

    def get_value(self, kind):
        raw = self._arg(0)
        if kind == 'float':
            value = unbits(raw)
        elif kind == 'bool':
            value = bool(raw & 255)
        elif kind == 'int':
            value = raw if raw < 2**31 else raw - 2**32
        elif kind == 'uint':
            value = raw
        else:
            value = tuple(self.floats(raw, int(kind[-1])))
        self._return(self.object(value))

    def tuple_set(self, e):
        self.values[e._arg(0)][e._arg(1)] = self.values[e._arg(2)]
        e._return()

    def callback(self, e):
        self.events.append(('callback', self.values[e._arg(0)], self.values[e._arg(1)]))
        e._return()

    def method(self, function, *args):
        self.error = False
        result = self.call(function, (self.owner, self.object(tuple(args)), 0))
        if self.error:
            return 'error'
        return self.values.get(result, result)

    def setter(self, function, value):
        self.error = False
        result = self.call(function, (self.owner, self.object(value), 0))
        return 'error' if self.error or result else None

    def mode(self, playing=False, recording=False, now=0):
        self.uc.mem_write(self.owner + 0x210, bytes([int(playing)]))
        self.uc.mem_write(self.owner + 0x212, bytes([int(recording)]))
        self.uc.mem_write(self.owner + 0x21a, b'\x01')
        self.put_word(self.owner + 0x1c4, bits(now))
