"""Partition every pinned #1513 chassis by its wheel/track/spline descriptor shape."""
import struct
import sys
import zipfile
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[3] / 'tools'))
from packed_xml import read_packed_xml, TYPE_ELEMENT, TYPE_VECTOR  # noqa: E402


def _text(value):
    if value.value_type in (1, 5):
        return value.value.decode('latin1').strip()
    if value.value_type == 4:
        return 'true' if value.value else 'false'
    if value.value_type == TYPE_VECTOR:
        # Packed float scalars/vectors are little-endian float32 values.
        floats = struct.unpack('<%df' % (len(value.value) // 4), value.value)
        return ' '.join(repr(item) for item in floats)
    return str(value.value)


class _Section(object):
    def __init__(self, element):
        self.element = element

    def children(self):
        for name, child in self.element.children:
            if child.value_type == TYPE_ELEMENT:
                yield name.decode('latin1'), _Section(child.value)
            else:
                yield name.decode('latin1'), _Leaf(child)

    def get(self, path):
        current = self
        for part in path.split('/'):
            current = next((child for name, child in current.children() if name == part), None)
            if current is None:
                return None
        return current

    def all(self, name):
        return [child for key, child in self.children() if key == name]

    def text(self):
        return _text(self.element.value)


class _Leaf(object):
    def __init__(self, value):
        self.value = value

    def children(self):
        return iter(())

    def get(self, path):
        return None

    def all(self, name):
        return []

    def text(self):
        return _text(self.value)


def _value(section, path, default=None):
    child = section.get(path)
    return default if child is None else child.text()


def chassis_rows(client):
    """Return one row per vehicle chassis section with the raw authored fields."""
    archive = zipfile.ZipFile(Path(client) / 'res/packages/scripts.pkg')
    rows = []
    for member in sorted(archive.namelist()):
        parts = member.split('/')
        if (len(parts) != 5 or parts[:3] != ['scripts', 'item_defs', 'vehicles'] or
                not member.endswith('.xml') or parts[4] in ('list.xml', 'customization.xml')):
            continue
        chassis = _Section(read_packed_xml(archive.read(member))).get('chassis')
        if chassis is None:
            continue
        for name, section in chassis.children():
            wheels = section.get('wheels')
            nodes = section.get('trackNodes')
            spline = section.get('splineDesc')
            rows.append({
                'vehicle': parts[3] + '/' + parts[4][:-4], 'chassis': name,
                'wheels': {
                    'lodDist': _value(wheels, 'lodDist'),
                    'syncAngle': _value(wheels, 'leadingWheelSyncAngle'),
                    'groups': [dict(isLeft=_value(g, 'isLeft'), template=_value(g, 'template'),
                                    count=_value(g, 'count', '1'), startIndex=_value(g, 'startIndex', '0'),
                                    radius=_value(g, 'radius')) for g in wheels.all('group')],
                    'wheels': [dict(isLeft=_value(w, 'isLeft'), radius=_value(w, 'radius'),
                                    name=_value(w, 'name'), isLeading=_value(w, 'isLeading'),
                                    syncAngle=_value(w, 'syncAngle')) for w in wheels.all('wheel')]},
                'trackNodes': None if nodes is None else {
                    'defaults': {key: _value(nodes, key) for key in (
                        'elasticity', 'damping', 'forwardElastK', 'backwardElastK', 'offset',
                        'maxAmplitude', 'maxOffset', 'gravity')},
                    'nodes': [{key: _value(node, key) for key in (
                        'name', 'isLeft', 'offset', 'leftSibling', 'rightSibling', 'damping',
                        'elasticity', 'forwardElastK', 'backwardElastK')} for node in nodes.all('node')]},
                'trackThickness': _value(section, 'trackThickness'),
                'splineDesc': None if spline is None else {key: _value(spline, path) for key, path in (
                    ('segmentModelLeft', 'segmentModelLeft'), ('segmentModelRight', 'segmentModelRight'),
                    ('segmentLength', 'segmentLength'), ('leftDesc', 'left'), ('rightDesc', 'right'),
                    ('segmentOffset', 'segmentOffset'), ('segment2ModelLeft', 'segment2ModelLeft'),
                    ('segment2ModelRight', 'segment2ModelRight'), ('segment2Offset', 'segment2Offset'),
                    ('atlasUTiles', 'atlas/UTiles'), ('atlasVTiles', 'atlas/VTiles'), ('lodDist', 'lodDist'))},
            })
    return rows
