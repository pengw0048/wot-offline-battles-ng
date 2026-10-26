"""Replay oracle scenarios through the production binding (CPython 2.7 host)."""
import json
import os

import DataLinks
import Host
import Vehicular


class Recorder(object):
    def __init__(self):
        self.events = []

    def onWaterSplash(self, point, heavy):
        self.events.append(['splash', list(point), heavy])

    def onUnderWaterSwitch(self, underWater):
        self.events.append(['switch', underWater])


class Speed(object):
    value = 0.


traces = []
for sequence in json.load(open(os.environ['WATER_SENSOR_SCENARIOS'])):
    c = sequence['config']
    sensor = Vehicular.WaterSensor(((c[0], c[1], c[2]), (c[3], c[4]), c[5], c[6], c[7]))
    if sequence['initial'] is not None:
        Host.setHistory(sensor, *sequence['initial'])
    node, lod, speed, recorder = Host.node(), Host.lodLink(), Speed(), Recorder()
    link = DataLinks.createFloatLink(speed, 'value')
    Host.errors(), Host.waterQueries(), Host.transforms(), Host.segments()
    steps = []
    for step in sequence['steps']:
        Host.setNode(node, step['matrix'])
        sensor.sensorPlaneLink = node if step['plane'] else None
        speed.value = step['speed'] if step['speed'] is not None else 0.
        sensor.speedLink = link if step['speed'] is not None else None
        sensor.onWaterSplash = recorder.onWaterSplash if step['splash'] else None
        sensor.onUnderWaterSwitch = recorder.onUnderWaterSwitch if step['switch'] else None
        if step['lod'] is None:
            sensor.setLodLink(None)
        else:
            Host.setLod(lod, step['lod'][0], step['lod'][1], True)
            sensor.setLodLink(lod)
            sensor.setLodSettings((step['lod'][2], step['lod'][3]))
        Host.setWaters([step['height']])
        camera = step['camera']
        Host.setSpace(None if camera is None else camera[0], camera is not None and camera[1],
                      [] if camera is None else camera[2])
        reads, speeds = Host.nodeReads(node), Host.linkReads(link)
        active = Host.update(sensor)
        steps.append({'queries': Host.waterQueries(), 'transforms': Host.transforms(),
                      'segments': Host.segments(), 'events': list(recorder.events),
                      'errors': Host.errors(), 'history': list(Host.history(sensor)),
                      'planeReads': Host.nodeReads(node) - reads,
                      'speedReads': Host.linkReads(link) - speeds, 'active': active})
        del recorder.events[:]
    traces.append(steps)
json.dump(traces, open(os.environ['WATER_SENSOR_TRACES'], 'w'))
print('WaterSensor production trace: %d sequences' % len(traces))
