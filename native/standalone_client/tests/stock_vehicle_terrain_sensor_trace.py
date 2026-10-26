"""Run oracle scenarios through the production TerrainMatKindSensor in real CPython 2.7."""
import json
import os
import struct

import Host
import Vehicular

NOT_WRITTEN = 0xffffffff


def bits(value):
    return struct.unpack('<I', struct.pack('<f', value))[0]


traces = []
for case in json.load(open(os.environ['TERRAIN_SENSOR_SCENARIOS'])):
    node = Host.node()
    Host.setNode(node, case['matrix'])
    sensor = Vehicular.TerrainMatKindSensor(node, [tuple(p) for p in case['points']], case['scanLength'])
    lod = None
    if case['lod']:
        lod = Host.lodLink()
        Host.setLod(lod, case['lod'][0], case['lod'][1], True)
        sensor.setLodLink(lod)
        sensor.setLodSettings((case['lod'][2], case['lod'][3]))
    camera = case['camera']
    if camera is None:
        Host.setSpace(None, False, [])
    else:
        Host.setSpace(camera[0], camera[1], camera[2])
    Host.setKinds(sensor, [NOT_WRITTEN] * len(case['points']))
    Host.transforms()
    Host.segments()
    # setLodLink validates the native link type by sampling it once.
    before = Host.lodReads(lod) if lod else 0
    Host.update(sensor)
    traces.append({
        'kinds': [int(k) for k in Host.kinds(sensor)],
        'segments': [[space, [bits(v) for v in start], [bits(v) for v in end], [r & 0xffffffff for r in results]]
                     for space, start, end, results in Host.segments()],
        'transforms': [[bits(v) for v in point] for point in Host.transforms()],
        'matrixReads': Host.nodeReads(node), 'lodReads': Host.lodReads(lod) - before if lod else 0})
    sensor.destroy()
    assert Host.errors() == [], case
json.dump(traces, open(os.environ['TERRAIN_SENSOR_TRACES'], 'w'))
