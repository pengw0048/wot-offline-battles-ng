#!/usr/bin/env python3
"""Stage the pinned gameplay catalogs with #1513 raw WGDE row identities.

The pinned v1.1.1 bakers already use raw WGDE rows, including empty slots.
Verify catalog versions and digests, then preserve their bytes unchanged.
"""
import argparse
import hashlib
import json
from pathlib import Path

TABLE = Path(__file__).with_name('wgde_empty_rows.json')
TABLE_FORMAT = 'offline-lan-0922-wgde-empty-rows'
CONFIG_ROOT = Path('mods/configs/offline_lan_0922')
IDENTITY_RULE = 'wgde-raw-row'


def raw_row(index, empty):
    """The raw row of the index-th non-empty row, given sorted empty rows."""
    for row in empty:
        if row > index:
            break
        index += 1
    return index


def _rewire(rows, first, chunks):
    for row in rows:
        empty = chunks.get(str(row[first]))
        if empty:
            row[first + 1] = raw_row(row[first + 1], empty)


def rewire_destructibles(data, chunks):
    """instances[14:16], tree_instances[13:15] and excluded_instances[0:2]."""
    _rewire(data['instances'], 14, chunks)
    _rewire(data['tree_instances'], 13, chunks)
    _rewire(data['excluded_instances'], 0, chunks)
    return data


def rewire_foliage(data, chunks):
    """fallen_trees[0:2]."""
    _rewire(data['fallen_trees'], 0, chunks)
    return data


def encode(data):
    # The bakers' write_json: the manifests hash exactly these bytes.
    return (json.dumps(data, sort_keys=True, separators=(',', ':')) + '\n').encode('utf-8')


def load_table(path=TABLE):
    payload = Path(path).read_bytes()
    table = json.loads(payload.decode('utf-8'))
    if table.get('format') != TABLE_FORMAT or table.get('version') != 1:
        raise ValueError('Unrecognized WGDE empty-row table')
    return table


def _manifest(directory):
    return json.loads((directory / 'manifest.json').read_text(encoding='utf-8'))


def stage(source, destination, table_path=TABLE):
    """Write navgraphs, foliage and destructibles under destination/CONFIG_ROOT."""
    source, destination = Path(source), Path(destination)
    table = load_table(table_path)
    output = destination / CONFIG_ROOT
    destructibles = _manifest(source / 'destructibles')
    spaces = dict((record['map'], record['space_bin_sha256'])
                  for record in destructibles['maps'])
    if set(spaces) != set(table['maps']):
        raise ValueError('The WGDE empty-row table does not cover the pinned maps')
    for name, rewire in (('destructibles', rewire_destructibles), ('foliage', rewire_foliage)):
        manifest = destructibles if name == 'destructibles' else _manifest(source / name)
        expected_version = 10 if name == 'destructibles' else 5
        if manifest.get('version') != expected_version:
            raise ValueError('The pinned catalog identity version changed: ' + name)
        (output / name).mkdir(parents=True, exist_ok=True)
        for record in manifest['maps']:
            entry = table['maps'][record['map']]
            if entry['space_bin_sha256'] != spaces[record['map']]:
                raise ValueError('The WGDE empty-row table is for another space.bin: ' + record['map'])
            payload = (source / name / record['file']).read_bytes()
            if hashlib.sha256(payload).hexdigest() != record['sha256']:
                raise ValueError('Pinned catalog digest mismatch: ' + record['file'])
            data = json.loads(payload.decode('utf-8'))
            if name == 'destructibles' and data['source']['space_bin_sha256'] != entry['space_bin_sha256']:
                raise ValueError('Pinned catalog was baked from another space.bin: ' + record['file'])
            (output / name / record['file']).write_bytes(payload)
            record['sha256'] = hashlib.sha256(payload).hexdigest()
        manifest['identity'] = {'rule': IDENTITY_RULE}
        (output / name / 'manifest.json').write_bytes(encode(manifest))
    (output / 'navgraphs').mkdir(parents=True, exist_ok=True)
    for path in sorted((source / 'navgraphs').iterdir()):
        if path.is_file():
            (output / 'navgraphs' / path.name).write_bytes(path.read_bytes())


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('source', type=Path, help='Pinned gameplay source root')
    parser.add_argument('destination', type=Path)
    arguments = parser.parse_args()
    stage(arguments.source, arguments.destination)
