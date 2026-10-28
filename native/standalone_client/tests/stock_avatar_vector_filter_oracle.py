"""Run the original #1513 AvatarFilter Vector3 sub-filter collection as x86.

The collection (AvatarFilter+0x2f8) sync 0065d850, get 0065d780, reset
0065d7f0 and setInterpolationType 0065d820 run with their channels, sample
vectors, interpolation (00680540, 011bb1a0, 00648be0) and the shared latency
globals (01d24210..20). ConnectionControl (01d20624) holds a scripted
connection whose lastMessageTime (slot 0x24) and serverTime (slot 0x30) are
returned through x87. libm and memmove use the shared host definitions.
"""
import struct

from stock_cursor_camera_oracle import OriginalCursorCamera, bits

OPERATOR_NEW = 0x13d7fe9
OPERATOR_DELETE = 0x13d8019
SIZED_DELETE = 0x13d801e
CONNECTION_CONTROL = 0x1d20624
CONNECTION_OFFSET = 0x8c
LATENCY = 0x1d24210
SYNC = 0x65d850
GET = 0x65d780
RESET = 0x65d7f0
INTERPOLATION = 0x65d820
CHANNELS = 10

# Branch targets the scenarios must reach.
BRANCHES = {
    0x65d877: 'default time', 0x65d660: 'create channel', 0x65d6ff: 'invalid identifier',
    0x680a51: 'prune newer samples', 0x680a9c: 'keep newest eight', 0x680b3a: 'latency update',
    0x680b40: 'fast latency weight', 0x680b4a: 'slow latency weight', 0x680566: 'no samples',
    0x6805be: 'before first sample', 0x6805ef: 'after last sample', 0x68064d: 'slerp',
    0x68067b: 'angles', 0x680854: 'spherical', 0x68099f: 'linear',
    0x11bb262: 'opposite yaw blend', 0x11bb2e1: 'slerp arc', 0x648bfe: 'wrap above 3 pi',
    0x648c71: 'wrap above pi', 0x648d0d: 'wrap below -pi', 0x648c9a: 'wrap below -3 pi',
}


def hex_bits(value):
    return '%08x' % bits(value)


def hex_double(value):
    return '%016x' % struct.unpack('<Q', struct.pack('<d', value))[0]


def double_words(value):
    low, high = struct.unpack('<II', struct.pack('<d', value))
    return [low, high]


class OriginalVectorChannels(OriginalCursorCamera):
    def __init__(self, executable):
        super(OriginalVectorChannels, self).__init__(executable)
        self.handle(OPERATOR_NEW, lambda e: e._return(e.alloc(max(e._arg(0), 4))))
        self.handle(OPERATOR_DELETE, lambda e: e._return())
        self.handle(SIZED_DELETE, lambda e: e._return())
        self.collection = self.alloc(8 * CHANNELS)
        self.control = self.alloc(0x100)
        self.put_word(CONNECTION_CONTROL, self.control)
        self.times = self.alloc(16)
        last, server = self.entry(), self.entry()
        self.handle(last, lambda e: self._double(self.times))
        self.handle(server, lambda e: self._double(self.times + 8))
        slots = [self.entry() for unused in range(16)]
        slots[9], slots[12] = last, server
        self.connection = self.alloc(0x80)
        self.put_word(self.connection, self.vtable(*slots))

    def _double(self, address):
        # fld qword [address]; ret, in a fresh stub so no translated block is reused.
        stub = self.alloc(8, b'\xdd\x05' + struct.pack('<I', address) + b'\xc3')
        self.uc.reg_write(self.x86.UC_X86_REG_EIP, stub)

    def connect(self, present, last, server):
        self.put_word(self.control + CONNECTION_OFFSET, self.connection if present else 0)
        self.uc.mem_write(self.times, struct.pack('<dd', last, server))

    def latency(self):
        return struct.unpack('<ddd', bytes(self.uc.mem_read(LATENCY, 24)))

    def samples(self, index):
        channel = self.word(self.collection + 8 * index)
        if not channel:
            return -1
        return (self.word(channel + 0x10) - self.word(channel + 0xc)) // 0x18


class Scenario(object):
    def __init__(self, executable, coverage=None):
        self.e = OriginalVectorChannels(executable)
        if coverage is not None:
            from unicorn import UC_HOOK_CODE
            for address in BRANCHES:
                self.e.uc.hook_add(UC_HOOK_CODE, lambda uc, a, size, data: coverage.add(a),
                                   begin=address, end=address)

    def run(self, commands):
        return [self.command(c) for c in commands]

    def command(self, c):
        e = self.e
        name = c[0]
        out = [name]
        if name == 'connect':
            e.connect(*c[1:])
        elif name == 'sync':
            identifier, value, time = c[1:]
            e.call(SYNC, [identifier & 0xffffffff] + [bits(v) for v in value] + double_words(time),
                   ecx=e.collection)
        elif name == 'get':
            identifier, time = c[1:]
            result = e.alloc(12)
            e.call(GET, [result, identifier & 0xffffffff] + double_words(time), ecx=e.collection)
            out += [hex_bits(v) for v in e.floats(result, 3)]
        elif name == 'reset':
            e.call(RESET, [c[1] & 0xffffffff], ecx=e.collection)
        elif name == 'interpolation':
            e.call(INTERPOLATION, [c[1] & 0xffffffff, c[2] & 0xffffffff], ecx=e.collection)
        else:
            raise RuntimeError('Unknown vector channel command ' + name)
        out += [hex_double(v) for v in e.latency()]
        out += [str(e.samples(i)) for i in range(CHANNELS)]
        return ' '.join(out)
