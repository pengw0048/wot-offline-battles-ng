"""Run the exact #1513 model_assembler PYC over every pinned chassis descriptor.

The reviewed native parser contracts are enforced by CPython 2.7 fakes; this
proves caller shapes and descriptor coverage, not native rendering.
"""
from pathlib import Path
import json
import os
import subprocess
import sys
import tempfile
import unittest

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE))
from stock_vehicle_wheel_tracks_inventory import chassis_rows  # noqa: E402


class WheelTrackConsumersTest(unittest.TestCase):
    def test_every_chassis_partition_over_the_exact_pyc(self):
        client = os.environ.get('WOT_0922_CLIENT')
        if not client:
            self.skipTest('WOT_0922_CLIENT is required for the original PYC and descriptors')
        python = Path(os.environ.get('WOT_PYTHON27', '/Users/peng/.pyenv/versions/2.7.18/bin/python2.7'))
        if not python.exists():
            self.skipTest('The pinned CPython 2.7 audit interpreter is unavailable')
        rows = chassis_rows(client)
        splines = [row for row in rows if row['splineDesc']]
        # The shipped descriptor classes this batch must cover.
        self.assertEqual(len(rows), 1015)
        self.assertEqual(len(splines), 996)
        self.assertEqual(sum(1 for row in rows if row['trackNodes'] is None and not row['splineDesc']), 19)
        self.assertEqual(sum(1 for row in splines if row['splineDesc']['segment2ModelLeft']), 301)
        self.assertEqual(sum(1 for row in splines if row['splineDesc']['segmentModelLeft'] !=
                             row['splineDesc']['segmentModelRight']), 67)
        with tempfile.TemporaryDirectory(prefix='wheel-track-consumers-') as directory:
            inventory = Path(directory) / 'chassis.json'
            inventory.write_text(json.dumps(rows))
            result = subprocess.run(
                [str(python), str(HERE / 'stock_vehicle_wheel_tracks_consumer.py'),
                 str(Path(client) / 'res/packages/scripts.pkg'), str(inventory)],
                capture_output=True, text=True, env=dict(os.environ, PYTHONDONTWRITEBYTECODE='1'))
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        report = json.loads(result.stdout.strip().splitlines()[-1])
        self.assertEqual(report['chassis'], 1015)
        # Both sides of every spline chassis reach wg_createSplineTrack.
        self.assertEqual(report['spline_calls'], 2 * len(splines))
        print('Exact model_assembler PYC over %d chassis, %d shape partitions' % (
            report['chassis'], report['partitions']))

    def test_absent_spline_manager_branch_is_the_only_spline_path(self):
        bigworld = HERE.parent / 'bigworld'
        spline = (bigworld / 'stock_vehicle_spline_tracks.cpp').read_text()
        body = spline[spline.index('static PyObject * py_wg_createSplineTrack'):
                      spline.index('PY_MODULE_FUNCTION( wg_createSplineTrack, BigWorld )')]
        # 0065834b returns None before parsing; no fabricated track exists.
        self.assertIn('Py_RETURN_NONE;', body)
        self.assertNotIn('PyArg_ParseTuple', body)
        self.assertIn('return false;', spline[spline.index('bool isStockSplineTrack'):])
        fashion = (bigworld / 'stock_vehicle_fashions.cpp').read_text()
        self.assertIn('"OO:setSplineTrack"', fashion)
        self.assertIn('PY_METHOD( setSplineTrack )', fashion)
        self.assertIn('must be set to a WGSplineTrack or None', fashion)


if __name__ == '__main__':
    unittest.main()
