"""Behavioral comparisons against the pinned tree sensor function."""
import importlib.util
import os
from pathlib import Path
import sys
import types
import unittest
from unittest.mock import patch

ROOT = Path(__file__).resolve().parents[1]
spec = importlib.util.spec_from_file_location(
    'stock_tree_registration_bindings', ROOT / 'bigworld/stock_tree_registration_bindings.py')
adapter = importlib.util.module_from_spec(spec)
spec.loader.exec_module(adapter)
SENSOR = 'src/res/scripts/client/gui/mods/offline_lan_0922/destructibles_sensor.py'


def function(source, namespace):
    text = source[source.index(adapter._START):source.index(adapter._END)]
    exec(compile(text, SENSOR, 'exec'), namespace)
    return namespace['_fell_trees_near']


class Vector:
    def __init__(self, x, y, z):
        self.x, self.y, self.z = x, y, z


class Descriptor:
    def __init__(self, bbox):
        self.bbox = bbox


def exercise(source, registration, bbox, native, count, receipt_hit=False,
             catalog=True, priority=(7,), existing=False):
    calls = []
    state = {'chunks': {}, 'felled': set(), 'spaceID': None}
    if existing:
        state['chunks'][10] = {'native_count': 2, 'count': 1}

    def record(name, result=None):
        def call(*args, **kwargs):
            calls.append((name, args, kwargs))
            return result
        return call

    class Manager:
        def getSpaceID(self):
            calls.append(('getSpaceID', (), {}))
            return 1
        def startSpace(self, value):
            calls.append(('startSpace', (value,), {}))

    class Geometry:
        def neighbourhood(self, sensor, area, pos, sin_y, cos_y, vel, enabled):
            calls.append(('neighbourhood', (pos, sin_y, cos_y, vel, enabled), {}))
            return (10, 11, [(12, 1.0, -1.0)]) if native else None

    area = types.ModuleType('AreaDestructibles')
    area.g_destructiblesManager = Manager()
    area.chunkIDFromPosition = lambda p: (calls.append(('chunk', (p.x, p.z), {})) or 10)
    math_module = types.ModuleType('Math')
    math_module.Vector3 = Vector
    bw = types.ModuleType('BigWorld')
    bw.wg_getChunkMatrix = lambda *args: types.SimpleNamespace(translation=Vector(0, 0, 0))
    hull = lambda td: (calls.append(('hull', (), {})) or td.bbox)
    def native_count(manager, cid):
        calls.append(('native_count', (manager, cid), {}))
        return (2 if cid == 10 else None) if existing else count
    namespace = {
        '_clear_runtime_registry': record('clear'),
        '_retry_catalog_publications_1513': record('retry'),
        '_tree_runtime_state_1513': lambda space: (calls.append(('state', (space,), {})) or state),
        '_vehicle_hull_bbox': hull,
        '_vehicle_swept_box': record('swept', ('vehicle_box',)),
        '_proximity_receipt_key_1513': record('key', (2, 10, 0, 0)),
        '_empty_proximity_receipt_valid_1513': record('receipt_valid', receipt_hit),
        '_native_geometry': Geometry(), '_native_sensor': object(),
        '_destructible_catalog': {} if catalog else None,
        '_DESTRUCTIBLE_CHUNK_METRES_1513': 100.0,
        'combat_count': record('count'),
        '_destructible_isolated_1513': record('isolated', False),
        '_native_chunk_destructible_count_1513': native_count,
        '_diagnostic_chunk_pending_1513': record('pending'),
        '_chunk_native_name_list_1513': record('name_list', ({}, 'ready')),
        '_chunk_native_names_1513': record('names', ({}, 'ready')),
        'observed_call': lambda name, fn, *args: (calls.append(('observed', (name,) + args, {})) or fn(*args)),
        'xrange': range, '_bump_spatial_revision_1513': record('bump'),
        '_diagnostic_chunk_1513': record('diagnostic'), 'LOG_DEBUG': record('log'),
        '_flush_spatial_mutation_batch_1513': record('flush'),
        '_loaded_chunk_signature_1513': record('stream_signature', None),
    }
    fn = function(source, namespace)
    with patch.dict(sys.modules, {'AreaDestructibles': area, 'BigWorld': bw, 'Math': math_module}):
        try:
            result = fn(2, Vector(3, 0, 5), 0.3, 2, Descriptor(bbox), registration, priority)
            error = None
        except Exception as exc:
            result, error = None, (type(exc).__name__, str(exc))
    return result, error, calls, state


def stable_calls(calls):
    return [name for name, args, kwargs in calls]


def normalize(value):
    if value is None or isinstance(value, (bool, int, float, str)):
        return value
    if isinstance(value, Vector):
        return ('Vector', value.x, value.y, value.z)
    if isinstance(value, (tuple, list)):
        return tuple(normalize(item) for item in value)
    if isinstance(value, set):
        return ('set', tuple(sorted(normalize(item) for item in value)))
    if isinstance(value, dict):
        return tuple(sorted((normalize(key), normalize(item)) for key, item in value.items()))
    return ('object', type(value).__name__)


def meaningful_arguments(calls):
    # Preserve values and order while discarding per-run object identities.
    return [(name, normalize(args), normalize(kwargs)) for name, args, kwargs in calls]


class TreeRegistrationTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        root = os.environ.get('WOT_FORK_0101_SOURCE')
        if not root:
            raise unittest.SkipTest('WOT_FORK_0101_SOURCE is required')
        cls.original = (Path(root) / SENSOR).read_text()
        cls.patched = adapter.sensor(cls.original)

    def test_anchor_and_idempotence(self):
        self.assertEqual(adapter.sensor(self.patched), self.patched)
        self.assertNotEqual(self.original, self.patched)
        with self.assertRaises(ValueError):
            adapter.sensor(self.original.replace('vehicle_box = _vehicle_swept_box(pos, yaw, vel, bbox)',
                                                 'vehicle_box = changed(pos, yaw, vel, bbox)', 1))
        with self.assertRaises(ValueError):
            adapter.sensor(self.original.replace(adapter._START, adapter._START * 2, 1))

    def test_valid_registration_preserves_work_and_results(self):
        bboxes = [None, ((-1.2, -1, -2), (1.7, 1, 3), None),
                  ((-8, -4, -1), (3, 7, 5), None)]
        for bbox in bboxes:
            for native in (False, True):
                for count in (None, 0):
                    for catalog in (False, True):
                        with self.subTest(bbox=bbox, native=native, count=count, catalog=catalog):
                            before = exercise(self.original, True, bbox, native, count, catalog=catalog)
                            after = exercise(self.patched, True, bbox, native, count, catalog=catalog)
                            self.assertIsNone(before[1]); self.assertIsNone(after[1])
                            self.assertEqual(before[0], after[0])
                            self.assertEqual(before[3], after[3])
                            self.assertEqual([n for n in stable_calls(before[2]) if n not in ('hull', 'swept', 'key')],
                                             stable_calls(after[2]))
                            self.assertEqual(meaningful_arguments([row for row in before[2]
                                if row[0] not in ('hull', 'swept', 'key')]), meaningful_arguments(after[2]))
                            self.assertIn('retry', stable_calls(after[2]))
                            self.assertIn('neighbourhood', stable_calls(after[2]))
                            self.assertIn('native_count', stable_calls(after[2]))

    def test_normal_path_and_invalid_descriptor(self):
        for bbox in (None, ((-2, -1, -3), (2, 1, 3), None), ((), ())):
            for receipt_hit in (False, True):
                before = exercise(self.original, False, bbox, True, 0, receipt_hit)
                after = exercise(self.patched, False, bbox, True, 0, receipt_hit)
                self.assertEqual((before[0], before[1], stable_calls(before[2]), before[3]),
                                 (after[0], after[1], stable_calls(after[2]), after[3]))
                self.assertEqual(meaningful_arguments(before[2]), meaningful_arguments(after[2]))
                if bbox != ((), ()):
                    self.assertIsNone(before[1])
                    self.assertIsNone(after[1])
                    self.assertIn('receipt_valid', stable_calls(after[2]))
                    self.assertEqual('neighbourhood' in stable_calls(after[2]), not receipt_hit)
                    if not receipt_hit:
                        self.assertIn('stream_signature', stable_calls(after[2]))
        old = exercise(self.original, True, ((), ()), True, 0)
        new = exercise(self.patched, True, ((), ()), True, 0)
        self.assertEqual(old[1], ('RuntimeError', '#1513 hull hit tester bbox is invalid'))
        self.assertIsNone(new[1])
        self.assertEqual(new[0]['status'], 'ready')

    def test_existing_nonempty_chunk_and_priority_order(self):
        args = (True, ((-2, -1, -3), (2, 1, 3), None), True, 2)
        before = exercise(self.original, *args, priority=(12, 7), existing=True)
        after = exercise(self.patched, *args, priority=(12, 7), existing=True)
        self.assertIsNone(before[1]); self.assertIsNone(after[1])
        self.assertEqual(before[0], after[0])
        self.assertEqual(before[3], after[3])
        self.assertEqual(after[0]['ready_chunks'], (10,))
        self.assertEqual(after[0]['pending_chunks'], (7, 11, 12))
        self.assertEqual([args[1] for name, args, kw in after[2] if name == 'native_count'],
                         [12, 7, 10, 11])
        self.assertEqual(meaningful_arguments([row for row in before[2]
            if row[0] not in ('hull', 'swept', 'key')]), meaningful_arguments(after[2]))


if __name__ == '__main__':
    unittest.main()
