"""Authored #1513 vehicle sections consumed by the battle animation readers.

Reads every packed item_defs vehicle XML in the exact scripts.pkg and returns
the raw swinging, drivenJoints, hullAiming and leveredSuspension sections,
converted the way their exact Python readers convert them.
"""
import math
from pathlib import Path
import sys
import zipfile

ROOT = Path(__file__).resolve().parents[3]
sys.path.insert(0, str(ROOT / 'tools'))
from packed_xml import TYPE_ELEMENT, TYPE_VECTOR, PackedValue, read_packed_xml  # noqa: E402
import struct


def _text(value):
    if value.value_type == TYPE_ELEMENT:
        value = value.value.value
    raw = value.value
    return raw.decode('latin-1').strip() if isinstance(raw, bytes) else raw


def _children(value):
    if value is None or value.value_type != TYPE_ELEMENT:
        return []
    return [(key.decode('latin-1') if isinstance(key, bytes) else key, item)
            for key, item in value.value.children]


def _child(value, name):
    for key, item in _children(value):
        if key == name:
            return item
    return None


def _walk(value, path):
    for key, item in _children(value):
        yield path + '/' + key, key, item
        for entry in _walk(item, path + '/' + key):
            yield entry


def _floats(value):
    if value.value_type == TYPE_ELEMENT:
        value = value.value.value
    if value.value_type == TYPE_VECTOR:
        return list(struct.unpack('<%df' % (len(value.value) // 4), value.value))
    text = _text(value)
    return [float(text)] if isinstance(text, (int, float)) else [float(part) for part in text.split()]


def _driven_joints(section):
    # vehicles._readDrivenJoints: [[masterNode, radians...], [slaveNode, radians...], ...] per master.
    groups = []
    for key, master in _children(section):
        rows = [[_text(_child(master, 'node'))] +
                [math.radians(_floats(row)[0]) for unused, row in _children(_child(master, 'table'))]]
        for unused, slave in _children(_child(master, 'slaves')):
            rows.append([_text(_child(slave, 'node'))] +
                        [math.radians(_floats(row)[0]) for unused2, row in _children(_child(slave, 'table'))])
        groups.append(rows)
    return groups


def _hull_aiming(section):
    audio = _child(section, 'audio')
    sounds = []
    for state, item in _children(_child(audio, 'sounds')):
        under, over = _child(item, 'underLimitSounds'), _child(item, 'overLimitSounds')
        sounds.append((state, (_text(_child(under, 'wwsoundPC')), _text(_child(under, 'wwsoundNPC'))),
                       (_text(_child(over, 'wwsoundPC')), _text(_child(over, 'wwsoundNPC')))))
    return {'angleLimit': math.radians(_floats(_child(audio, 'angleLimitValue'))[0]), 'sounds': sounds}


def _wheel_names(section):
    # chassis_readers.readWheelsAndGroups: named wheels plus template groups.
    names = []
    for key, item in _children(section):
        if key == 'wheel':
            names.append(_text(_child(item, 'name')))
        elif key == 'group':
            template = _text(_child(item, 'template'))
            start, count = int(_text(_child(item, 'startIndex'))), int(_text(_child(item, 'count')))
            names += ['%s%d' % (template, index) for index in range(start, start + count)]
    return names


def _levered(section, wheels=None):
    levers = []
    for key, item in _children(section):
        if key != 'lever':
            continue
        limits = _floats(_child(item, 'limits'))
        levers.append((_text(_child(item, 'startNode')), _text(_child(item, 'jointNode')),
                       _text(_child(item, 'trackNode')), math.radians(limits[0]), math.radians(limits[1])))
    speed = _child(section, 'interpolationSpeedMul')
    return {'levers': levers, 'interpolationSpeedMul': _floats(speed)[0] if speed is not None else 10.0,
            'wheels': _wheel_names(wheels) if wheels is not None else []}


def collect(client):
    found = {'swinging': [], 'drivenJoints': [], 'hullAiming': [], 'leveredSuspension': []}
    with zipfile.ZipFile(Path(client) / 'res/packages/scripts.pkg') as archive:
        for member in sorted(archive.namelist()):
            if not (member.startswith('scripts/item_defs/vehicles/') and member.endswith('.xml')):
                continue
            data = archive.read(member)
            if not data.startswith(b'\x45\x4e\xa1\x62'):
                continue
            root = read_packed_xml(data)
            sections = dict((path, item) for path, key, item in _walk(PackedValue(TYPE_ELEMENT, root), ''))
            for path, key, item in _walk(PackedValue(TYPE_ELEMENT, root), ''):
                where = (member, path)
                if key == 'swinging' and path.startswith('/hull/'):
                    found['swinging'].append((where, _floats(_child(item, 'pitchParams')),
                                              _floats(_child(item, 'rollParams')),
                                              _floats(_child(item, 'sensitivityToImpulse'))[0]))
                elif key == 'drivenJoints':
                    found['drivenJoints'].append((where, _driven_joints(item)))
                elif key == 'hullAiming':
                    found['hullAiming'].append((where, _hull_aiming(item)))
                elif key == 'leveredSuspension':
                    wheels = sections.get(path.rsplit('/', 1)[0] + '/wheels')
                    found['leveredSuspension'].append((where, _levered(item, wheels)))
    return found
