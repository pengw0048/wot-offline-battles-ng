"""Read original ABC and replay its bounded scheduler instruction subset.

This is an ABC consumer probe, not a replacement AVM2. Each authored method entry
uses the compiled SDK AddFrame. Array storage, wall-clock reads, Timer start/stop,
and the task handler are explicit boundaries. No scheduler control flow is rewritten.
"""
import struct
import zlib


class Reader:
    def __init__(self, data): self.data, self.pos = data, 0
    def byte(self):
        value = self.data[self.pos]; self.pos += 1; return value
    def raw(self, size):
        value = self.data[self.pos:self.pos+size]; self.pos += size; return value
    def uint(self):
        value = shift = 0
        while True:
            byte = self.byte(); value |= (byte & 127) << shift
            if byte < 128: return value
            shift += 7
    def jump(self):
        value = int.from_bytes(self.raw(3), 'little')
        return value - 0x1000000 if value & 0x800000 else value


class ABC:
    def __init__(self, data):
        r = Reader(data); r.raw(4)
        for kind in range(3):
            for unused in range(max(0, r.uint()-1)):
                r.raw(8) if kind == 2 else r.uint()
        self.strings = [''] + [r.raw(r.uint()).decode('utf-8') for unused in range(max(0, r.uint()-1))]
        namespaces = [None] + [(r.byte(), r.uint()) for unused in range(max(0, r.uint()-1))]
        for unused in range(max(0, r.uint()-1)):
            for item in range(r.uint()): r.uint()
        self.names = ['']
        for unused in range(max(0, r.uint()-1)):
            kind = r.byte()
            if kind in (7, 13):
                ns, name = r.uint(), r.uint()
                self.names.append(self.strings[name])
            elif kind in (15, 16): self.names.append(self.strings[r.uint()])
            elif kind in (17, 18): self.names.append('[late]')
            elif kind in (9, 14):
                name = r.uint(); r.uint(); self.names.append(self.strings[name])
            elif kind in (27, 28): r.uint(); self.names.append('[late]')
            elif kind == 29:
                base = r.uint()
                for item in range(r.uint()): r.uint()
                self.names.append(self.names[base])
            else: raise AssertionError('Unsupported multiname kind ' + str(kind))
        for unused in range(r.uint()):
            count = r.uint(); r.uint()
            for item in range(count): r.uint()
            r.uint(); flags = r.byte()
            if flags & 8:
                for item in range(r.uint()): r.uint(); r.byte()
            if flags & 128:
                for item in range(count): r.uint()
        for unused in range(r.uint()):
            r.uint()
            for item in range(r.uint()): r.uint(); r.uint()
        self.methods, self.parents, self.getters = {}, {}, {}
        count = r.uint()
        for unused in range(count):
            name, parent, flags = self.names[r.uint()], self.names[r.uint()], r.byte()
            self.parents[name] = parent
            if flags & 8: r.uint()
            for item in range(r.uint()): r.uint()
            r.uint()
            self.traits(r, name)
        for unused in range(count): r.uint(); self.traits(r, None)
        for unused in range(r.uint()): r.uint(); self.traits(r, None)
        self.bodies = {}
        for unused in range(r.uint()):
            method = r.uint()
            for item in range(4): r.uint()
            self.bodies[method] = r.raw(r.uint())
            for item in range(r.uint()):
                for field in range(5): r.uint()
            self.traits(r, None)
        assert r.pos == len(data)

    def traits(self, r, owner):
        for unused in range(r.uint()):
            name, flags = self.names[r.uint()], r.byte()
            kind = flags & 15
            if kind in (0, 6):
                r.uint(); r.uint()
                if r.uint(): r.byte()
            elif kind == 4: r.uint(); r.uint()
            elif kind == 5: r.uint(); r.uint()
            else:
                r.uint(); method = r.uint()
                if owner:
                    self.methods[owner, name] = method
                    if kind == 2: self.getters[owner, name] = method
            if flags & 64:
                for item in range(r.uint()): r.uint()

    def lookup(self, kind, name, getter=False):
        mapping = self.getters if getter else self.methods
        while kind:
            if (kind, name) in mapping: return mapping[kind, name]
            kind = self.parents.get(kind)
        raise AssertionError('Unknown original method ' + name)


def read_scheduler(swf):
    body = zlib.decompress(swf[8:]) if swf[:3] in (b'CWS', b'CFX') else swf[8:]
    reader = Reader(body)
    reader.pos = (5 + 4 * (body[0] >> 3) + 7) // 8 + 4
    while reader.pos < len(body):
        tag = int.from_bytes(reader.raw(2), 'little'); size = tag & 63; kind = tag >> 6
        if size == 63: size = int.from_bytes(reader.raw(4), 'little')
        data = reader.raw(size)
        if kind == 82:
            abc = data[data.index(b'\0', 4)+1:]
            if b'Scheduler' in abc and b'TimedTaskStack' in abc:
                return ABC(abc)
    raise AssertionError('Original lobby scheduler ABC not found')


class Object:
    def __init__(self, kind, **values): self.kind, self.values = kind, values


class Probe:
    def __init__(self, abc, gate):
        self.abc, self.gate, self.now = abc, gate, 10000
        self.invoked, self.timer_calls, self.max_depth = 0, [], 0
        self.clock = Object('clock')

    def get(self, obj, name):
        if isinstance(obj, list):
            return len(obj) if name == 'length' else obj[int(name)]
        if name in obj.values: return obj.values[name]
        return self.execute(obj, self.abc.lookup(obj.kind, name, True))

    def call(self, obj, name, args):
        if isinstance(obj, list):
            if name == 'pop': return obj.pop()
            if name == 'splice':
                at, count = int(args[0]), int(args[1]); removed = obj[at:at+count]
                obj[at:at+count] = args[2:]; return removed
        if obj.kind == 'clock' and name == 'getTimer': return self.now
        if obj.kind == 'Timer':
            self.timer_calls.append((name, obj.values['delay'])); return None
        if name == '_handler': self.invoked += 1; return None
        return self.execute(obj, self.abc.lookup(obj.kind, name), args)

    def execute(self, obj, method, args=()):
        if not self.gate.enter(): raise OverflowError('AS error 1023')
        self.max_depth = max(self.max_depth, self.gate.depth())
        try:
            r, stack, local = Reader(self.abc.bodies[method]), [], [obj] + list(args) + [None]*12
            while r.pos < len(r.data):
                op = r.byte()
                if op == 0x47: return None
                if op == 0x48: return stack.pop()
                if op in (0x02, 0x09): continue
                if op == 0x30: stack.pop()
                elif 0xd0 <= op <= 0xd3: stack.append(local[op-0xd0])
                elif 0xd4 <= op <= 0xd7: local[op-0xd4] = stack.pop()
                elif op == 0x62: stack.append(local[r.uint()])
                elif op == 0x63: local[r.uint()] = stack.pop()
                elif op == 0x08: local[r.uint()] = None
                elif op == 0x20: stack.append(None)
                elif op == 0x24:
                    value = r.byte(); stack.append(value-256 if value & 128 else value)
                elif op in (0x26, 0x27, 0x28): stack.append({0x26:True, 0x27:False, 0x28:float('nan')}[op])
                elif op == 0x29: stack.pop()
                elif op == 0x2a: stack.append(stack[-1])
                elif op == 0x80: r.uint()  # Object coerce is a boundary, never changes these original instances.
                elif op in (0x70, 0x75, 0x76):
                    value = stack.pop(); stack.append(float(value) if op == 0x75 else bool(value) if op == 0x76 else str(value))
                elif op in (0x93, 0xc1): stack.append(stack.pop()-1)
                elif op == 0x96: stack.append(not stack.pop())
                elif op in (0xa0, 0xa1, 0xa2, 0xab, 0xad, 0xaf):
                    b, a = stack.pop(), stack.pop()
                    stack.append({0xa0:lambda:a+b, 0xa1:lambda:a-b, 0xa2:lambda:a*b,
                                  0xab:lambda:a==b, 0xad:lambda:a<b, 0xaf:lambda:a>b}[op]())
                elif op in (0x5d, 0x5e):
                    name = self.abc.names[r.uint()]; assert name == 'getTimer'; stack.append(self.clock)
                elif op == 0x66:
                    name = self.abc.names[r.uint()]
                    if name == '[late]': name = stack.pop()
                    stack.append(self.get(stack.pop(), name))
                elif op in (0x61, 0x68):
                    name = self.abc.names[r.uint()]; value, target = stack.pop(), stack.pop(); target.values[name] = value
                elif op in (0x46, 0x4f):
                    name, count = self.abc.names[r.uint()], r.uint()
                    values = [stack.pop() for unused in range(count)][::-1]
                    result = self.call(stack.pop(), name, values)
                    if op == 0x46: stack.append(result)
                elif op in range(0x0c, 0x1b):
                    offset = r.jump()
                    if op == 0x10: take = True
                    elif op in (0x11, 0x12): take = bool(stack.pop()) == (op == 0x11)
                    else:
                        b, a = stack.pop(), stack.pop()
                        take = {0x0c:lambda:not a<b, 0x0d:lambda:not a<=b, 0x0e:lambda:not a>b,
                                0x0f:lambda:not a>=b, 0x13:lambda:a==b, 0x14:lambda:a!=b,
                                0x15:lambda:a<b, 0x16:lambda:a<=b, 0x17:lambda:a>b, 0x18:lambda:a>=b}[op]()
                    if take: r.pos += offset
                else: raise AssertionError('Unsupported executed ABC opcode ' + hex(op))
        finally: self.gate.leave()

    def run(self, count=8, repeats=20):
        # Tasks were scheduled at t=0 and the next frame arrives at t=10000ms.
        # This is a finite authored catch-up queue; no timer callback is invoked recursively.
        tasks = [Object('TimedTask', _finishTime=40.0, _interval=40, _count=repeats,
                        _maxCount=repeats, _startTime=0, _args=[], _handler=object()) for unused in range(count)]
        scheduler = Object('Scheduler', _disposed=False,
                           _taskStack=Object('TimedTaskStack', _tasks=tasks),
                           _timer=Object('Timer', delay=40.0))
        self.execute(scheduler, self.abc.lookup('Scheduler', 'updateTimer'))
        return scheduler
