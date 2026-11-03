"""Audit all four Bot plans on exact resources and cold #1513 hit testers.

Reuses the existing production BSP binding host. Its Model/physics2 seam reads
real model/visual/BSP data, but remains host evidence rather than Windows
Model::get, resource streaming or complete VehicleDescr installation evidence.
"""
import argparse
import json
import os
from pathlib import Path
import runpy
import subprocess
import sys
import tempfile
import unittest
from unittest.mock import patch

ROOT = Path(__file__).resolve().parents[1]
sys.path[:0] = [str(ROOT / 'tests'), str(ROOT.parents[1] / 'tools')]
from bake_navigation_0922 import PackageVFS
from inspect_client import inspect_client
from prepare_native_vehicle import xml_root, vector, normalized
from stock_bsp_collision_model_resources import load_model
from test_stock_bsp_collision_model import build_host


class Resources(PackageVFS):
    def read(self, name):
        try:
            return super().read(normalized(name))
        except KeyError:
            return None


def collect(fork, resources):
    package = Path(fork) / 'src/res/scripts/client/gui/mods/offline_lan_0922'
    plans = runpy.run_path(str(package / 'bot_fitting_data.py'))['PLANS']
    siege = runpy.run_path(str(package / 'siege_mechanics.py'))['VEHICLE_PARAMS']
    rows = []
    for vehicle, tiers in sorted(plans.items()):
        nation, name = vehicle.split(':')
        for tier in ('rookie', 'regular', 'veteran', 'elite'):
            modules = tiers[tier]['modules']
            modes = []
            for mode in ('travel', 'siege') if vehicle in siege else ('travel',):
                suffix = '_siege_mode' if mode == 'siege' else ''
                definition = xml_root(resources.read('scripts/item_defs/vehicles/%s/%s%s.xml' % (nation,name,suffix)))
                chassis = definition.find('chassis/' + modules['chassis'])
                hull = definition.find('hull')
                turret = definition.find('turrets0/' + modules['turret'])
                gun = turret.find('guns/' + modules['gun'])
                # Native vehicle gun locals can override shared properties.
                shared = xml_root(resources.read('scripts/item_defs/vehicles/%s/components/guns.xml' % nation))
                common_gun = shared.find('shared/' + modules['gun'])
                parts = {}
                for key, component in (('chassis',chassis),('hull',hull),('turret',turret),('gun',gun)):
                    section = component.find('hitTester')
                    if section is None and key == 'gun' and common_gun is not None:
                        section = common_gun.find('hitTester')
                    if section is None:
                        raise ValueError('Missing hit tester: %s/%s/%s/%s' % (vehicle,tier,mode,key))
                    parts[key] = {child.tag: child.text.strip() for child in section if child.text and child.tag.startswith('collisionModelClient')}
                    if not parts[key].get('collisionModelClient'):
                        raise ValueError('Missing client collision model: %s/%s/%s/%s' % (vehicle,tier,mode,key))
                modes.append(dict(mode=mode, parts=parts, hull_position=vector(chassis,'hullPosition'),
                                  turret_position=vector(hull,'turretPositions/turret'),
                                  gun_position=vector(turret,'gunPosition')))
            rows.append(dict(vehicle=vehicle,tier=tier,modules=modules,modes=modes))
    return rows


def audit(client, fork, python27, batch_size=64):
    from test_stock_native_bot_geometry_startup import build_geometry_host
    inspect_client(Path(client))
    resources = Resources(str(Path(client) / 'res/packages'))
    try:
        rows = collect(fork, resources)
        totals = dict(vehicles=len({row['vehicle'] for row in rows}), plans=len(rows),
                      descriptors=0, cold_testers=0, siege_descriptors=0, batches=0)
        with tempfile.TemporaryDirectory(prefix='bot-geometry-cold-') as directory:
            folder = Path(directory)
            build_geometry_host(folder, Path(fork), python27)
            with patch.dict(os.environ, WOT_PYTHON27=python27):
                host = build_host(unittest.TestCase(), folder, ROOT / 'tests/stock_bsp_collision_model_host.cpp')
            for start in range(0, len(rows), batch_size):
                batch = rows[start:start+batch_size]
                names = {model for row in batch for mode in row['modes']
                         for part in mode['parts'].values() for model in part.values()}
                models = {}
                for name in sorted(names):
                    model = load_model(resources, name)
                    if model is None or model[2] is None:
                        raise ValueError('Missing real collision model/BSP: ' + name)
                    low, high, bsp, flags = model
                    models[name] = [low, high, bsp.hex(), flags]
                data = folder / 'resources.json'
                data.write_text(json.dumps(dict(rows=batch, models=models)))
                output = folder / 'result.json'
                result = subprocess.run([str(host), str(ROOT / 'tests/stock_bot_geometry_cold_consumer.py'),
                    str(data), str(Path(client) / 'res/packages/scripts.pkg'), str(output), str(folder),
                    str(Path(fork) / 'src/res/scripts/client/gui/mods/offline_lan_0922/tank_collision.py')],
                    text=True, capture_output=True, timeout=120)
                if result.returncode:
                    raise AssertionError(result.stdout + result.stderr)
                counts = json.loads(output.read_text())
                for key, value in counts.items():
                    totals[key] += value
                totals['batches'] += 1
        totals['evidence'] = 'Host production BSP binding, exact PYC lifecycle and resource-selected geometry; Windows engine loading not asserted'
        return totals
    finally:
        resources.close()


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--client', required=True)
    parser.add_argument('--fork-source', required=True)
    parser.add_argument('--python27', required=True)
    args = parser.parse_args()
    print(json.dumps(audit(args.client, args.fork_source, args.python27), indent=2))
