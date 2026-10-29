"""Typed world effects, ordering and fixed-release kinetic-law parity.

The scene fixture supplies real typed failure/filter behavior. It does not
establish BigWorld presentation, native falling-body timing or Windows speed.
"""
import ast
import math
import os
from pathlib import Path
import random
import shutil
import subprocess
import sys
import tempfile
import textwrap
import types
import unittest
from unittest.mock import patch

from native_test_compile_flags import native_test_compile_flags

ROOT = Path(__file__).resolve().parents[1]


class NativeWorldPipelineTest(unittest.TestCase):
    def test_native_pipeline_and_fixed_kinetic_law(self):
        source = os.environ.get('WOT_FORK_0101_SOURCE')
        if not source:
            self.skipTest('Fixed v0.10.1 source input is required')
        compiler = shutil.which('c++')
        if not compiler:
            self.skipTest('C++ compiler unavailable')
        fork = Path(source)
        with tempfile.TemporaryDirectory(prefix='native-world-pipeline-') as folder:
            binary = Path(folder) / 'world-pipeline'
            big = ROOT / 'bigworld'
            command = [compiler, '-std=c++17', '-O1', '-Wall', '-Wextra', '-Werror',
                       *native_test_compile_flags(compiler), '-I' + str(fork / 'native'),
                       '-I' + str(big), str(ROOT / 'tests/test_stock_native_world_pipeline.cpp'),
                       str(big / 'stock_native_world_pipeline.cpp'),
                       str(big / 'stock_motion_rotation_batch.cpp'),
                       str(big / 'stock_offline_query_skin.cpp'),
                       str(fork / 'native/offline_destructible_geometry.cpp'),
                       str(fork / 'native/world_stage.cpp'), '-o', str(binary)]
            subprocess.run(command, check=True, capture_output=True, text=True)
            result = subprocess.run([str(binary)], check=True, capture_output=True, text=True)
            self.assertIn('Native world pipeline tests passed', result.stdout)

            sensor = fork / 'src/res/scripts/client/gui/mods/offline_lan_0922/destructibles_sensor.py'
            text = sensor.read_text()
            function = next(node for node in ast.parse(text).body
                            if isinstance(node, ast.FunctionDef) and node.name == '_stock_crushable_1513')
            area = types.SimpleNamespace(DESTR_TYPE_STRUCTURE=0, DESTR_TYPE_FRAGILE=1,
                DESTR_TYPE_FALLING_ATOM=2, g_cache=types.SimpleNamespace(unitVehicleMass=1.))
            cache = types.SimpleNamespace(scaledDestructibleHealth=lambda scale, health: health)
            environment = dict(_decode_mat_info_1513=lambda value: value,
                _runtime_material_descriptor_1513=lambda *args: environment['descriptor'],
                _descriptor_value=lambda value, name: value[name],
                _normalized_filename=lambda value: value,
                _isolate_destructible_1513=lambda *args, **kwargs: None)
            exec(textwrap.dedent(ast.get_source_segment(text, function)), environment)
            oracle = environment['_stock_crushable_1513']
            rng = random.Random(1513)
            rows, expected = [], []
            with patch.dict(sys.modules, AreaDestructibles=area, DestructiblesCache=cache):
                for index in range(180):
                    kind = index % 3
                    mass, unit = rng.uniform(100., 120000.), rng.uniform(100., 50000.)
                    speed, correction = rng.uniform(-35., 35.), rng.uniform(-2., 2.)
                    damage = .5 * mass * speed * speed * .00015
                    if kind:
                        damage *= math.pow(mass / unit, correction)
                    # Exercise strict equality plus both sides of every gate.
                    health = damage * (1. if index % 4 == 0 else
                                       .999999 if index % 4 == 1 else
                                       1.000001 if index % 4 == 2 else rng.uniform(0., 2.))
                    area.g_cache.unitVehicleMass = unit
                    environment['descriptor'] = dict(type=kind,
                        modules={73: dict(health=health)}, health=health,
                        kineticDamageCorrection=correction)
                    expected.append(bool(oracle((None, None, 1, 7, 73, 'exact/model'),
                                                speed, dict(physics=dict(weight=mass)), 1.)))
                    rows.append(' '.join(map(repr, (kind, mass, speed, health, correction, unit))))
            result = subprocess.run([str(binary), 'kinetic'], input='\n'.join(rows) + '\n',
                                    check=True, capture_output=True, text=True)
            self.assertEqual(expected, [bool(int(value)) for value in result.stdout.splitlines()])


if __name__ == '__main__':
    unittest.main()
