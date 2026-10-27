"""Exact #1513 model/visual/primitives reader and brute-force BSP oracle.

The reader follows public BigWorld 14.4.1 Model::get -> Moo::Visual loading:
the .model's nodeless/nodefull visual, visual boundingBox, the primitives
"bsp2" section and "bsp2_materials" remapped by VisualLoader rules. The oracle
re-derives every hit from the raw triangle array in float32, independent of
the port's C++ and BSP traversal.
"""
import ctypes
import glob
import math
import re
import struct
import sys
import zipfile
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[3] / 'tools'))
import packed_xml  # noqa: E402


class Resources(object):
    def __init__(self, client):
        self.archives = []
        for path in sorted(glob.glob(str(Path(client) / 'res/packages/*.pkg'))):
            try:
                self.archives.append(zipfile.ZipFile(path))
            except zipfile.BadZipFile:
                pass

    def read(self, name):
        for archive in self.archives:
            if name in archive.NameToInfo:
                return archive.read(name)
        return None


def _children(element):
    for key, value in element.children:
        yield key.decode() if isinstance(key, bytes) else key, value


def _child(element, name):
    for key, value in _children(element):
        if key == name:
            return value
    return None


def _text(value):
    if value is None:
        return None
    if value.value_type == packed_xml.TYPE_ELEMENT:
        value = value.value.value
    raw = value.value
    if isinstance(raw, bytes):
        return raw.decode('latin1').strip()
    return raw


def _element(value):
    return value.value if value is not None and value.value_type == packed_xml.TYPE_ELEMENT else None


def _int(value, default=0):
    if value is None:
        return default
    if value.value_type == packed_xml.TYPE_ELEMENT:
        value = value.value.value
    if value.value_type == packed_xml.TYPE_INTEGER:
        return value.value
    text = value.value.decode('latin1').strip() if isinstance(value.value, bytes) else value.value
    return int(text) if text not in ('', None) else default


def _vector(value):
    if value.value_type == packed_xml.TYPE_ELEMENT:
        value = value.value.value
    if value.value_type == packed_xml.TYPE_VECTOR:
        return struct.unpack('<3f', value.value[:12])
    return tuple(float(v) for v in value.value.decode('latin1').split())


def primitive_sections(data):
    length = struct.unpack('<I', data[-4:])[0]
    table, offset, position, sections = data[-4 - length:-4], 4, 0, {}
    while position < len(table):
        size = struct.unpack('<I', table[position:position + 4])[0]
        position += 20
        name_size = struct.unpack('<I', table[position:position + 4])[0]
        position += 4
        name = table[position:position + name_size].decode()
        position = (position + name_size + 3) & ~3
        sections[name] = data[offset:offset + size]
        offset = (offset + size + 3) & ~3
    return sections


def pack_flags(collision, kind):
    return (collision | ((kind << 8) & 0xff00)) & 0xffff


def load_model(resources, name):
    """(bbox min, bbox max, bsp2 bytes or None, remap flags) or None."""
    data = resources.read(name)
    if data is None:
        return None
    model = packed_xml.read_packed_xml(data)
    visual_name = _text(_child(model, 'nodelessVisual')) or _text(_child(model, 'nodefullVisual'))
    if not visual_name:
        return None
    visual_data = resources.read(visual_name + '.visual_processed')
    if visual_data is None:
        return None
    visual = packed_xml.read_packed_xml(visual_data)
    box = _element(_child(visual, 'boundingBox'))
    low, high = _vector(_child(box, 'min')), _vector(_child(box, 'max'))
    materials = {}
    for key, render_set in _children(visual):
        if key != 'renderSet':
            continue
        for key2, geometry in _children(_element(render_set)):
            if key2 != 'geometry':
                continue
            for key3, group in _children(_element(geometry)):
                if key3 != 'primitiveGroup':
                    continue
                material = _element(_child(_element(group), 'material'))
                if material is not None:
                    identifier = _text(_child(material, 'identifier'))
                    materials.setdefault(identifier, (_int(_child(material, 'collisionFlags')),
                                                      _int(_child(material, 'materialKind'))))
    object_kind = _int(_child(visual, 'materialKind'))
    primitives = resources.read(visual_name + '.primitives_processed')
    sections = primitive_sections(primitives) if primitives else {}
    bsp = sections.get('bsp2')
    flags = []
    if bsp is not None:
        ids = re.findall(rb'<id>\s*(.*?)\s*</id>', sections.get('bsp2_materials', b''))
        for identifier in ids:
            material = materials.get(identifier.decode('latin1'))
            if material is None:
                flags.append(pack_flags(0, object_kind))
            else:
                collision, kind = material
                flags.append(pack_flags(collision, kind if kind != 0 else object_kind))
        if not flags:
            flags.append(pack_flags(0, object_kind))
    return list(low), list(high), bsp, flags


def f32(value):
    return ctypes.c_float(value).value


class F32(object):
    @staticmethod
    def sub(a, b):
        return tuple(f32(x - y) for x, y in zip(a, b))

    @staticmethod
    def dot(a, b):
        return f32(f32(f32(a[0] * b[0]) + f32(a[1] * b[1])) + f32(a[2] * b[2]))

    @staticmethod
    def cross(a, b):
        return (f32(f32(a[1] * b[2]) - f32(a[2] * b[1])), f32(f32(a[2] * b[0]) - f32(a[0] * b[2])),
                f32(f32(a[0] * b[1]) - f32(a[1] * b[0])))

    @classmethod
    def normalise(cls, v):
        length = f32(math.sqrt(cls.dot(v, v)))
        if length == 0.0:
            return (0.0, 0.0, 0.0)
        return tuple(f32(c / length) for c in v)


def bsp_triangles(bsp, flags):
    count = struct.unpack('<I', bsp[16:20])[0]
    result = []
    for index in range(count):
        at = 60 + index * 40
        values = struct.unpack('<9fHH', bsp[at:at + 40])
        result.append(((values[0:3], values[3:6], values[6:9]), flags[values[9] % len(flags)]))
    return result


def intersect(triangle, start, direction, length):
    """Public WorldTriangle::intersects in float32 (also #1513 01172190)."""
    v0, v1, v2 = triangle
    edge1, edge2 = F32.sub(v1, v0), F32.sub(v2, v0)
    p = F32.cross(direction, edge2)
    det = F32.dot(edge1, p)
    epsilon = f32(0.000001)
    if -epsilon < det < epsilon:
        return None
    inv = f32(1.0 / det) if det != 0 else math.copysign(float('inf'), det)
    t = F32.sub(start, v0)
    u = f32(F32.dot(t, p) * inv)
    if u < 0.0 or 1.0 < u:
        return None
    q = F32.cross(t, edge1)
    v = f32(F32.dot(direction, q) * inv)
    if v < 0.0 or 1.0 < f32(u + v):
        return None
    distance = f32(F32.dot(edge2, q) * inv)
    return distance if 0.0 < distance < length else None


def raw_normal(triangle):
    v0, v1, v2 = triangle
    e, f = F32.sub(v2, v0), F32.sub(v2, v1)
    return (f32(f32(f[2] * e[1]) - f32(f[1] * e[2])), f32(f32(f[0] * e[2]) - f32(f[2] * e[0])),
            f32(f32(f[1] * e[0]) - f32(f[0] * e[1])))


def segment_hits(triangles, start, end):
    """Every accepted hit (distance, raw normal, kind); order and seams unmerged."""
    start = tuple(f32(c) for c in start)
    direction = F32.normalise(F32.sub(tuple(f32(c) for c in end), start))
    segment = F32.sub(tuple(f32(c) for c in end), start)
    hits = []
    for triangle, flags in triangles:
        if flags & 0xff == 0xff or flags & 0x50:
            continue
        if intersect(triangle, start, segment, 1.0) is None:
            continue
        distance = intersect(triangle, start, direction, f32(1e10))
        if distance is not None:
            hits.append((distance, raw_normal(triangle), (flags >> 8) & 0xff))
    return hits, direction
