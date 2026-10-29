"""Full native step: clock debt, planning, physics, combat ingress and publication."""
import ast
import os
import math
from pathlib import Path
import subprocess
import tempfile
import unittest
from native_test_compile_flags import native_test_compile_flags
ROOT = Path(__file__).resolve().parents[1]
LAW_NAMES = '''GRAVITY GRAVITY_FACTOR COHESION COH_DECAY_Y COH_DECAY_FACTOR
COH_DECAY_POW SLOPE_COH_DECAY_Y SLOPE_COH_DECAY COH_DECAY_BOUND POWER_FACTOR
BKWD_POWER_FRACTION ENGINE_MIN_V DRIVE_TRACTION SLOPE_GRIP_LNG_MIN_Y
SLOPE_GRIP_LNG_FULL_Y SLOPE_GRIP_LNG_MIN SLOPE_GRIP_LNG_FULL STEER_RESIST_MULT
SLIDE_HOLD_TAN SLIDE_KINETIC SLIP_THRESHOLD_TAN SLIP_DRAG COAST_BRAKE_SHARE
OVERSPEED_MAX_FACTOR ANG_ACCELERATION_TIME SPEED_AFFECT_ROT_DECREASE'''.split()
class RuntimeTest(unittest.TestCase):
    def test_complete_native_step(self):
        source = os.environ.get('WOT_FORK_0101_SOURCE')
        if not source:
            self.skipTest('Fixed gameplay source is required')
        fork = Path(source); big = ROOT/'bigworld'; native = fork/'native'
        names = '''runtime prewarm clock publication physics_host physics_world motion contacts step_state
            combat combat_runtime combat_mode weapon weapon_runtime artillery armor_law environment
            planning_runtime decision parking planner strategy navigation navigator navigation_runtime
            navigation_scene candidate motion_plan perception observation_runtime turret_obstacles'''.split()
        release = '''offline_math_batch_geometry offline_contact_roster offline_destructible_geometry
            offline_navigation offline_async_worker world_stage offline_simulation_control offline_visibility
            native_visibility_core navigation_query_stage offline_simulation_weapons'''.split()
        tree = ast.parse((fork/'src/res/scripts/client/gui/mods/offline_lan_0922/vehicle_physics.py').read_text())
        values = {"math": math}
        for node in tree.body:
            if isinstance(node, ast.Assign) and len(node.targets) == 1 and isinstance(node.targets[0], ast.Name):
                try:
                    values[node.targets[0].id] = eval(compile(ast.Expression(node.value), "fixed-drive-constants", "eval"), values)
                except (ValueError, TypeError, NameError, AttributeError):
                    pass
        with tempfile.TemporaryDirectory(prefix='native-runtime-') as directory:
            folder = Path(directory); binary = folder/'runtime'
            law = (native/'offline_release_drive_python.inc').read_text().split('PyObject *WOT_CDECL release_drive(')[0]
            (folder/'offline_release_drive_law.hpp').write_text('#pragma once\n#include <cmath>\n#include <algorithm>\n'+law)
            command = ['c++','-std=c++17','-O0','-ffp-contract=off','-Wall','-Wextra','-Werror',
                       *native_test_compile_flags('c++'),'-I'+str(folder),'-I'+str(big),'-I'+str(native)]
            command += [str(big/('stock_native_bot_'+name+'.cpp')) for name in names]
            command += [str(big/(name+'.cpp')) for name in ('stock_native_world_pipeline','stock_motion_rotation_batch','stock_offline_query_skin')]
            command += [str(native/(name+'.cpp')) for name in release]
            command += [str(ROOT/'tests/test_stock_native_bot_runtime.cpp'),'-o',str(binary)]
            built = subprocess.run(command, capture_output=True, text=True)
            self.assertEqual(built.returncode, 0, built.stderr)
            ran = subprocess.run([str(binary)], input=' '.join(str(values[name]) for name in LAW_NAMES), capture_output=True, text=True)
            self.assertEqual(ran.returncode, 0, ran.stderr)
            self.assertIn('integration passed', ran.stdout)
if __name__ == '__main__':
    unittest.main()
