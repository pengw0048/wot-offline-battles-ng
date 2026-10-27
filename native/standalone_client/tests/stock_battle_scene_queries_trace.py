"""Replay hit-record oracle cases through the production binding in real CPython 2.7."""
import json
import os
import sys
import types

import Host
import _BigWorld


class Vector3(tuple):
    def __new__(cls, x, y, z):
        return tuple.__new__(cls, (x, y, z))


Math = types.ModuleType('Math')
Math.Vector3 = Vector3
sys.modules['Math'] = Math

if os.environ.get('SCENE_QUERY_WATERS'):
    distances = []
    for case in json.load(open(os.environ['SCENE_QUERY_WATERS'])):
        Host.clearWaters()
        Host.addWater([1.0, 0, 0, 0, 0, 1.0, 0, 0, 0, 0, 1.0, 0, 0, 2.0, 0, 1.0], tuple(case['size']),
                      case['tess'], True, tuple(case['grid']), case['alpha'], tuple(case['bb']), (1, 1))
        distances.append(Host.waterDistance(0, (case['x'], 12.0, case['z']),
                                            (case['x'], -8.0, case['z']), 1))
    Host.clearWaters()
    json.dump(distances, open(os.environ['SCENE_QUERY_TRACES'], 'w'))
    sys.exit(0)

results = []
for case in json.load(open(os.environ['SCENE_QUERY_RECORDS'])):
    Host.removeSpace(1)
    Host.addSpace(1)
    Host.addSurface(1, case['flags'], case['distance'], case['vertices'],
                    [c for row in case['matrix'] for c in row], None)
    hit = _BigWorld.wg_collideSegment(1, (0.0, 10.0, 0.0), (0.0, -10.0, 0.0), 0)
    results.append({'point': list(hit[0]), 'normal': list(hit[1]), 'materialKind': hit[2],
                    'translation': list(hit[3]), 'itemIndex': hit[4], 'chunkID': hit[5]})
Host.removeSpace(1)
json.dump(results, open(os.environ['SCENE_QUERY_TRACES'], 'w'))
