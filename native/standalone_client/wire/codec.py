"""Interpreted binary codec for the battle wire schema (Python 3, server side and tests)."""
import math
import struct

FRAME = struct.Struct('<IHH')
_SCALARS = {
    'u8': struct.Struct('<B'), 'u16': struct.Struct('<H'), 'u32': struct.Struct('<I'),
    'u64': struct.Struct('<Q'), 'i8': struct.Struct('<b'), 'i16': struct.Struct('<h'),
    'i32': struct.Struct('<i'), 'i64': struct.Struct('<q'), 'f32': struct.Struct('<f'),
    'f64': struct.Struct('<d'),
}
_COUNT = _SCALARS['u16']


class WireError(ValueError):
    pass


class Type(object):
    pass


class Scalar(Type):
    def __init__(self, code):
        self.code = code
        self.packer = _SCALARS[code]

    def __repr__(self):
        return self.code


class Bool(Type):
    def __repr__(self):
        return 'bool'


class Fixed(Type):
    """A server-rounded decimal: value * 10**digits stored as an exact integer."""

    def __init__(self, digits, base='i32'):
        self.digits = digits
        self.scale = 10 ** digits
        self.packer = _SCALARS[base]

    def __repr__(self):
        return 'fixed%d' % self.digits


class Str(Type):
    def __init__(self, maximum=65535):
        self.maximum = maximum


class List(Type):
    def __init__(self, item, maximum=65535):
        self.item = item
        self.maximum = maximum


class Tuple(Type):
    """Fixed-length positional list with one type per position."""

    def __init__(self, *items):
        self.items = items


class Map(Type):
    def __init__(self, item, maximum=65535):
        self.item = item
        self.maximum = maximum


class Enum(Type):
    def __init__(self, *values):
        self.values = values
        self.index = dict((repr(v), i) for i, v in enumerate(values))


class Any(Type):
    """Tagged value for open, low-frequency trees (effective params, outfits)."""


class Ref(Type):
    def __init__(self, name):
        self.name = name


class OneOf(Type):
    """First alternative that encodes; a u8 selects it on the wire."""

    def __init__(self, *items):
        self.items = items


class Field(object):
    def __init__(self, name, kind, optional=False, nullable=False):
        self.name = name
        self.kind = kind
        self.optional = optional
        self.nullable = nullable


class Record(object):
    def __init__(self, name, fields, variant=None):
        self.name = name
        self.fields = fields
        self.names = frozenset(f.name for f in fields)
        self.flagged = [f for f in fields if f.optional or f.nullable]
        self.variant = variant


class Variant(Type):
    """A dict whose ``key`` selects one record (event kinds, critical events)."""

    def __init__(self, key, records):
        self.key = key
        self.records = records
        self.order = list(records)


class Schema(object):
    def __init__(self, records, messages):
        self.records = records
        self.messages = messages
        self.message_ids = dict((name, i + 1) for i, name in enumerate(messages))
        self.message_names = dict((i, n) for n, i in self.message_ids.items())

    # ---- encoding -------------------------------------------------------
    def encode(self, message):
        kind = message.get('type')
        if kind not in self.message_ids:
            raise WireError('message type %r is not in the schema' % (kind,))
        out = bytearray()
        layout = self.messages[kind]
        if layout is None:
            _any(out, dict((k, v) for k, v in message.items() if k != 'type'), kind)
        elif isinstance(layout, tuple):
            errors = []
            for index, name in enumerate(layout):
                trial = bytearray([index])
                try:
                    self._record(trial, self.records[name], message, 'type')
                except WireError as error:
                    errors.append('%s: %s' % (name, error))
                    continue
                out += trial
                break
            else:
                raise WireError('no %s layout matched: %s' % (kind, ' | '.join(errors)))
        else:
            self._record(out, self.records[layout], message, 'type')
        return FRAME.pack(len(out), self.message_ids[kind], 0) + bytes(out)

    def _record(self, out, record, value, skip=None, path=''):
        if not isinstance(value, dict):
            raise WireError('%s: %s expects an object' % (path, record.name))
        extra = set(value) - record.names - (set([skip]) if skip else set())
        if extra:
            raise WireError('%s: %s has unknown keys %s' % (path, record.name, sorted(extra)))
        bits = 0
        for i, f in enumerate(record.flagged):
            present = f.name in value
            if not present and not f.optional:
                raise WireError('%s.%s is required' % (path, f.name))
            if present:
                bits |= 1 << (2 * i)
                if value[f.name] is None:
                    if not f.nullable:
                        raise WireError('%s.%s is not nullable' % (path, f.name))
                    bits |= 1 << (2 * i + 1)
        nbytes = (2 * len(record.flagged) + 7) // 8
        out += bits.to_bytes(nbytes, 'little')
        for f in record.fields:
            if f.name not in value:
                if f.optional:
                    continue
                raise WireError('%s.%s is required' % (path, f.name))
            item = value[f.name]
            if item is None and f.nullable:
                continue
            self._value(out, f.kind, item, path + '.' + f.name)

    def _value(self, out, kind, value, path):
        if isinstance(kind, Scalar):
            if kind.code[0] == 'f':
                if isinstance(value, bool) or not isinstance(value, float):
                    raise WireError('%s: expected float, got %r' % (path, value))
                if kind.code == 'f32' and struct.unpack('<f', struct.pack('<f', value))[0] != value:
                    raise WireError('%s: %r is not exact in f32' % (path, value))
            elif isinstance(value, bool) or not isinstance(value, int):
                raise WireError('%s: expected int, got %r' % (path, value))
            try:
                out += kind.packer.pack(value)
            except struct.error:
                raise WireError('%s: %r out of range for %s' % (path, value, kind.code))
        elif isinstance(kind, Bool):
            if not isinstance(value, bool):
                raise WireError('%s: expected bool, got %r' % (path, value))
            out.append(1 if value else 0)
        elif isinstance(kind, Fixed):
            if isinstance(value, bool) or not isinstance(value, float):
                raise WireError('%s: expected float, got %r' % (path, value))
            quantum = round(value * kind.scale)
            if quantum / kind.scale != value or (value == 0.0 and math.copysign(1.0, value) < 0):
                raise WireError('%s: %r is not a %d-digit decimal' % (path, value, kind.digits))
            try:
                out += kind.packer.pack(quantum)
            except struct.error:
                raise WireError('%s: %r out of fixed range' % (path, value))
        elif isinstance(kind, Str):
            if not isinstance(value, str):
                raise WireError('%s: expected str, got %r' % (path, value))
            data = value.encode('utf-8')
            if len(value) > kind.maximum or len(data) > 65535:
                raise WireError('%s: string too long' % path)
            out += _COUNT.pack(len(data)) + data
        elif isinstance(kind, List):
            if not isinstance(value, (list, tuple)) or len(value) > kind.maximum:
                raise WireError('%s: expected list <= %d' % (path, kind.maximum))
            out += _COUNT.pack(len(value))
            for i, item in enumerate(value):
                self._value(out, kind.item, item, '%s[%d]' % (path, i))
        elif isinstance(kind, Tuple):
            if not isinstance(value, (list, tuple)) or len(value) != len(kind.items):
                raise WireError('%s: expected %d-tuple' % (path, len(kind.items)))
            for i, (item_kind, item) in enumerate(zip(kind.items, value)):
                self._value(out, item_kind, item, '%s[%d]' % (path, i))
        elif isinstance(kind, Map):
            if not isinstance(value, dict) or len(value) > kind.maximum:
                raise WireError('%s: expected map <= %d' % (path, kind.maximum))
            out += _COUNT.pack(len(value))
            for key, item in value.items():
                self._value(out, Str(), key, path + '{}')
                self._value(out, kind.item, item, '%s[%r]' % (path, key))
        elif isinstance(kind, Enum):
            index = kind.index.get(repr(value))
            if index is None:
                raise WireError('%s: %r is not one of %r' % (path, value, kind.values))
            out.append(index)
        elif isinstance(kind, Ref):
            self._record(out, self.records[kind.name], value, path=path)
        elif isinstance(kind, OneOf):
            errors = []
            for index, item in enumerate(kind.items):
                trial = bytearray()
                try:
                    self._value(trial, item, value, path)
                except WireError as error:
                    errors.append(str(error))
                    continue
                out.append(index)
                out += trial
                return
            raise WireError('%s: no alternative matched: %s' % (path, ' | '.join(errors)))
        elif isinstance(kind, Variant):
            if not isinstance(value, dict):
                raise WireError('%s: expected object' % path)
            tag = value.get(kind.key)
            if tag not in kind.records:
                raise WireError('%s: unknown %s %r' % (path, kind.key, tag))
            out.append(kind.order.index(tag))
            self._record(out, self.records[kind.records[tag]], value, kind.key, path)
        elif isinstance(kind, Any):
            _any(out, value, path)
        else:
            raise WireError('%s: unsupported schema type %r' % (path, kind))

    # ---- decoding -------------------------------------------------------
    def decode(self, data):
        length, kind, flags = FRAME.unpack_from(data, 0)
        if length + FRAME.size != len(data):
            raise WireError('frame length mismatch')
        name = self.message_names.get(kind)
        if name is None:
            raise WireError('unknown message id %d' % kind)
        layout = self.messages[name]
        if layout is None:
            value, offset = _read_any(data, FRAME.size)
        elif isinstance(layout, tuple):
            value, offset = self._read_record(data, FRAME.size + 1, self.records[layout[data[FRAME.size]]])
        else:
            value, offset = self._read_record(data, FRAME.size, self.records[layout])
        if offset != len(data):
            raise WireError('trailing bytes in %s' % name)
        result = {'type': name}
        result.update(value)
        return result

    def _read_record(self, data, offset, record):
        nbytes = (2 * len(record.flagged) + 7) // 8
        bits = int.from_bytes(data[offset:offset + nbytes], 'little')
        offset += nbytes
        state = {}
        for i, f in enumerate(record.flagged):
            state[f.name] = ((bits >> (2 * i)) & 1, (bits >> (2 * i + 1)) & 1)
        result = {}
        for f in record.fields:
            if f.name in state:
                present, null = state[f.name]
                if not present:
                    continue
                if null:
                    result[f.name] = None
                    continue
            result[f.name], offset = self._read(data, offset, f.kind)
        return result, offset

    def _read(self, data, offset, kind):
        if isinstance(kind, Scalar):
            return kind.packer.unpack_from(data, offset)[0], offset + kind.packer.size
        if isinstance(kind, Bool):
            return data[offset] != 0, offset + 1
        if isinstance(kind, Fixed):
            quantum = kind.packer.unpack_from(data, offset)[0]
            return quantum / kind.scale, offset + kind.packer.size
        if isinstance(kind, Str):
            n = _COUNT.unpack_from(data, offset)[0]
            offset += 2
            return bytes(data[offset:offset + n]).decode('utf-8'), offset + n
        if isinstance(kind, List):
            n = _COUNT.unpack_from(data, offset)[0]
            offset += 2
            items = []
            for _ in range(n):
                item, offset = self._read(data, offset, kind.item)
                items.append(item)
            return items, offset
        if isinstance(kind, Tuple):
            items = []
            for item_kind in kind.items:
                item, offset = self._read(data, offset, item_kind)
                items.append(item)
            return items, offset
        if isinstance(kind, Map):
            n = _COUNT.unpack_from(data, offset)[0]
            offset += 2
            items = {}
            for _ in range(n):
                key, offset = self._read(data, offset, Str())
                items[key], offset = self._read(data, offset, kind.item)
            return items, offset
        if isinstance(kind, Enum):
            return kind.values[data[offset]], offset + 1
        if isinstance(kind, Ref):
            return self._read_record(data, offset, self.records[kind.name])
        if isinstance(kind, OneOf):
            return self._read(data, offset + 1, kind.items[data[offset]])
        if isinstance(kind, Variant):
            tag = kind.order[data[offset]]
            value, offset = self._read_record(data, offset + 1, self.records[kind.records[tag]])
            result = {kind.key: tag}
            result.update(value)
            return result, offset
        if isinstance(kind, Any):
            return _read_any(data, offset)
        raise WireError('unsupported schema type %r' % (kind,))


_ANY_NULL, _ANY_FALSE, _ANY_TRUE, _ANY_INT, _ANY_FLOAT, _ANY_STR, _ANY_LIST, _ANY_MAP, _ANY_UINT = range(9)


def _any(out, value, path):
    if value is None:
        out.append(_ANY_NULL)
    elif value is True:
        out.append(_ANY_TRUE)
    elif value is False:
        out.append(_ANY_FALSE)
    elif isinstance(value, int):
        if value > 0x7fffffffffffffff:
            if value > 0xffffffffffffffff:
                raise WireError('%s: %r exceeds u64' % (path, value))
            out.append(_ANY_UINT)
            out += _SCALARS['u64'].pack(value)
        elif value < -0x8000000000000000:
            raise WireError('%s: %r below i64' % (path, value))
        else:
            out.append(_ANY_INT)
            out += _SCALARS['i64'].pack(value)
    elif isinstance(value, float):
        out.append(_ANY_FLOAT)
        out += _SCALARS['f64'].pack(value)
    elif isinstance(value, str):
        data = value.encode('utf-8')
        out.append(_ANY_STR)
        out += _SCALARS['u32'].pack(len(data)) + data
    elif isinstance(value, (list, tuple)):
        out.append(_ANY_LIST)
        out += _SCALARS['u32'].pack(len(value))
        for i, item in enumerate(value):
            _any(out, item, '%s[%d]' % (path, i))
    elif isinstance(value, dict):
        out.append(_ANY_MAP)
        out += _SCALARS['u32'].pack(len(value))
        for key, item in value.items():
            if not isinstance(key, str):
                raise WireError('%s: non-string key %r' % (path, key))
            data = key.encode('utf-8')
            out += _SCALARS['u32'].pack(len(data)) + data
            _any(out, item, '%s[%r]' % (path, key))
    else:
        raise WireError('%s: %r is not a wire value' % (path, value))


def _read_any(data, offset):
    tag = data[offset]
    offset += 1
    if tag == _ANY_NULL:
        return None, offset
    if tag in (_ANY_FALSE, _ANY_TRUE):
        return tag == _ANY_TRUE, offset
    if tag == _ANY_INT:
        return _SCALARS['i64'].unpack_from(data, offset)[0], offset + 8
    if tag == _ANY_UINT:
        return _SCALARS['u64'].unpack_from(data, offset)[0], offset + 8
    if tag == _ANY_FLOAT:
        return _SCALARS['f64'].unpack_from(data, offset)[0], offset + 8
    n = _SCALARS['u32'].unpack_from(data, offset)[0]
    offset += 4
    if tag == _ANY_STR:
        return bytes(data[offset:offset + n]).decode('utf-8'), offset + n
    if tag == _ANY_LIST:
        items = []
        for _ in range(n):
            item, offset = _read_any(data, offset)
            items.append(item)
        return items, offset
    if tag == _ANY_MAP:
        items = {}
        for _ in range(n):
            k = _SCALARS['u32'].unpack_from(data, offset)[0]
            key = bytes(data[offset + 4:offset + 4 + k]).decode('utf-8')
            items[key], offset = _read_any(data, offset + 4 + k)
        return items, offset
    raise WireError('bad any tag %d' % tag)


def same(a, b):
    """Exact JSON-model equality: types (int/float/bool) and float bits must match."""
    if type(a) is not type(b):
        if isinstance(a, (list, tuple)) and isinstance(b, (list, tuple)):
            return len(a) == len(b) and all(same(x, y) for x, y in zip(a, b))
        if isinstance(a, dict) and isinstance(b, dict):
            return a.keys() == b.keys() and all(same(a[k], b[k]) for k in a)
        return False
    if isinstance(a, float):
        return struct.pack('<d', a) == struct.pack('<d', b) or (math.isnan(a) and math.isnan(b))
    if isinstance(a, dict):
        return a.keys() == b.keys() and all(same(a[k], b[k]) for k in a)
    if isinstance(a, (list, tuple)):
        return len(a) == len(b) and all(same(x, y) for x, y in zip(a, b))
    return a == b
