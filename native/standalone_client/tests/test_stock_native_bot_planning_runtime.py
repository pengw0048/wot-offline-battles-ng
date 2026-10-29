"""Native facade links real planners, queues, world proofs and perception owners."""
import os
from pathlib import Path
import subprocess
import tempfile
import unittest
from native_test_compile_flags import native_test_compile_flags
ROOT=Path(__file__).resolve().parents[1]
class PlanningRuntimeTest(unittest.TestCase):
    def test_native_owner_lifecycle_and_selected_receipt_reuse(self):
        source=os.environ.get('WOT_FORK_0101_SOURCE')
        if not source:self.skipTest('Fixed v0.10.1 source input is required')
        fork=Path(source);big=ROOT/'bigworld'
        owned=['planning_runtime','decision','parking','planner','strategy','navigation','navigator','navigation_runtime','navigation_scene','candidate','motion_plan','perception']
        release=['offline_navigation','offline_async_worker','offline_destructible_geometry','world_stage','offline_simulation_control','offline_visibility','native_visibility_core','navigation_query_stage']
        with tempfile.TemporaryDirectory(prefix='native-planning-runtime-') as folder:
            binary=Path(folder)/'host'
            command=['c++','-std=c++17','-O1','-ffp-contract=off','-Wall','-Wextra','-Werror',*native_test_compile_flags('c++'),'-I'+str(big),'-I'+str(fork/'native')]
            command += [str(big/('stock_native_bot_'+name+'.cpp')) for name in owned]
            command += [str(big/(name+'.cpp')) for name in ('stock_native_world_pipeline','stock_motion_rotation_batch','stock_offline_query_skin')]
            command += [str(fork/'native'/(name+'.cpp')) for name in release]
            command += [str(ROOT/'tests/test_stock_native_bot_planning_runtime.cpp'),'-o',str(binary)]
            built=subprocess.run(command,capture_output=True,text=True);self.assertEqual(built.returncode,0,built.stderr)
            ran=subprocess.run([str(binary)],capture_output=True,text=True);self.assertEqual(ran.returncode,0,ran.stderr);self.assertIn('integration passed',ran.stdout)
if __name__=='__main__':unittest.main()
