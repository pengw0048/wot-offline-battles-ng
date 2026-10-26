"""Export vehicle physics sections from the pinned client's packed XML as JSON."""
import json
import struct
import sys
import zipfile
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[3] / 'tools'))
import packed_xml  # noqa: E402


def scalar(value):
    if value.value_type == packed_xml.TYPE_VECTOR:
        raw = value.value.encode('latin1') if isinstance(value.value, str) else value.value
        return list(struct.unpack('<%df' % (len(raw) // 4), raw))
    return value.value.decode('latin1') if isinstance(value.value, bytes) else value.value


def node(name, value):
    if isinstance(value, packed_xml.PackedValue) and isinstance(value.value, packed_xml.PackedElement):
        value = value.value
    if isinstance(value, packed_xml.PackedElement):
        return {'n': name, 't': value.value.value_type, 'v': scalar(value.value),
                'c': [node(key.decode(), child) for key, child in value.children]}
    return {'n': name, 't': value.value_type, 'v': scalar(value), 'c': []}


def main(client, output, *paths):
    archive = zipfile.ZipFile(Path(client) / 'res/packages/scripts.pkg')
    result = {}
    for path in paths:
        root = packed_xml.read_packed_xml(archive.read(path))
        result[path] = [node(key.decode(), child) for key, child in root.children if key == b'physics'][0]
    Path(output).write_text(json.dumps(result))


if __name__ == '__main__':
    main(*sys.argv[1:])
