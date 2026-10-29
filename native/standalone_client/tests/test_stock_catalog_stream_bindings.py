"""Compare prepared catalog admission with the fixed 144619 consumer."""
import collections
import importlib.util
import os
from pathlib import Path
import runpy
import sys
import textwrap
import types
import unittest
from unittest.mock import patch

ROOT = Path(__file__).resolve().parents[1]
SPEC = importlib.util.spec_from_file_location(
    'stock_catalog_stream_bindings', ROOT / 'bigworld/stock_catalog_stream_bindings.py')
adapter = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(adapter)
REL = Path('src/res/scripts/client/gui/mods/offline_lan_0922')


def extract(source, name, namespace, indent=''):
    start = source.index(indent + 'def ' + name + '(')
    end = source.find('\n' + indent + 'def ', start + 1)
    text = source[start:end if end >= 0 else len(source)]
    # Remove the next function's decorators, which belong outside this body.
    while text.rstrip().splitlines()[-1].lstrip().startswith('@'):
        text = '\n'.join(text.rstrip().splitlines()[:-1]) + '\n'
    exec(compile(textwrap.dedent(text), name, 'exec'), namespace)
    return namespace[name]


def environment(identities, registered, boxes=None, budget=4, container=list):
    boxes = boxes or {}
    calls = []
    counts = collections.Counter()
    instances = dict((key, {'live': key}) for key in registered)
    cache = dict((key, ('old',)) for key in set(identities))
    remaining = [budget]
    catalog = {'has_instance_index': True, 'baked_instances': dict(
        (key, {'boxes': boxes.get(key, ('far',))}) for key in set(identities))}

    def intersections(values, vehicle):
        calls.append(('intersection', values))
        if not isinstance(values, tuple):
            raise TypeError('malformed baked boxes')
        return values if 'contact' in values else ()

    def admit(space, key):
        calls.append(('admit', key))
        if remaining[0] <= 0:
            return None
        remaining[0] -= 1
        instances[key] = {'live': key}
        return instances[key]

    ns = {
        '_destructible_catalog': catalog,
        '_native_geometry': types.SimpleNamespace(
            baked_candidates=lambda *args: container(identities)),
        '_native_sensor': object(),
        'g_offh_destr_instances': instances,
        'g_offh_destr_unresolved_obstacles': cache,
        '_catalog_intersections': intersections,
        '_stream_baked_shot_instance_1513': admit,
        '_confirmed_unresolved_obstacle_1513': lambda space, key, **kw:
            (calls.append(('unresolved', key)) or ('solid',)),
        'combat_count': lambda name: counts.update((name,)),
    }
    return ns, calls, counts


class CatalogStreamTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        root = os.environ.get('WOT_FORK_0101_SOURCE')
        if not root:
            raise unittest.SkipTest('WOT_FORK_0101_SOURCE is required')
        cls.original = (Path(root) / REL / 'destructibles_sensor.py').read_text()
        cls.patched = adapter.sensor(cls.original)
        cls.battle = (Path(root) / REL / 'battle_runtime.py').read_text()

    def run_stream(self, source, identities, registered, **kwargs):
        ns, calls, counts = environment(identities, registered, **kwargs)
        fn = extract(source, '_stream_baked_motion_instances_1513', ns)
        result = fn(1, 'vehicle')
        return result, ns, calls, counts

    def test_anchor_idempotence_and_source_compile(self):
        self.assertEqual(adapter.sensor(self.patched), self.patched)
        with self.assertRaises(ValueError):
            adapter.sensor(self.original.replace(adapter._OLD, '\tchanged = True\n', 1))
        with self.assertRaises(ValueError):
            adapter.sensor(self.original.replace(adapter._START, adapter._START * 2, 1))
        # Runtime insertion uses no Python 3 syntax; the coordinator also
        # compiles the prepared payload with its pinned Python 2 interpreter.
        compile(self.patched, 'prepared_sensor', 'exec')

    def test_existing_query_and_registration_adapters_compose(self):
        query = runpy.run_path(str(ROOT / 'bigworld/stock_offline_query_bindings.py'))['sensor']
        tree = runpy.run_path(str(ROOT / 'bigworld/stock_tree_registration_bindings.py'))['sensor']
        self.assertEqual(adapter.sensor(tree(query(self.original))),
                         tree(query(adapter.sensor(self.original))))

    def test_all_registered_skips_only_unused_geometry(self):
        keys = [(1, 3), (1, 1), (1, 2)]
        for container in (list, tuple, set):
            original = self.run_stream(self.original, keys, keys, container=container)
            prepared = self.run_stream(self.patched, keys, keys, container=container)
            self.assertEqual(original[0], prepared[0])
            self.assertEqual(original[1]['g_offh_destr_instances'],
                             prepared[1]['g_offh_destr_instances'])
            self.assertEqual(original[1]['g_offh_destr_unresolved_obstacles'], {})
            self.assertEqual(prepared[1]['g_offh_destr_unresolved_obstacles'], {})
            self.assertEqual(original[3], prepared[3])
            self.assertEqual(prepared[2], [])
            self.assertEqual(len(original[2]), 3)

    def test_mixed_priority_ties_and_name_budget_are_unchanged(self):
        keys = [(1, 4), (1, 2), (1, 1), (1, 3), (1, 5), (1, 6)]
        boxes = {(1, 1): ('contact',), (1, 2): ('contact',)}
        results = [self.run_stream(source, keys, [(1, 4)], boxes=boxes, budget=2)
                   for source in (self.original, self.patched)]
        self.assertEqual(results[0][0], results[1][0])
        self.assertEqual(results[0][2:], results[1][2:])
        self.assertEqual(results[0][1]['g_offh_destr_instances'],
                         results[1][1]['g_offh_destr_instances'])
        admitted = [row[1] for row in results[1][2] if row[0] == 'admit']
        self.assertEqual(admitted[:2], [(1, 1), (1, 2)])

    def test_single_use_iterators_keep_original_path(self):
        keys = [(1, 2), (1, 1)]
        results = [self.run_stream(source, keys, keys, container=iter)
                   for source in (self.original, self.patched)]
        self.assertEqual(results[0][0], results[1][0])
        self.assertEqual(results[0][2:], results[1][2:])
        self.assertEqual(results[1][3]['destructible_stream_candidates'], 2)

    def test_no_registration_result_survives_the_next_call(self):
        ns, calls, counts = environment([(1, 1)], [(1, 1)])
        fn = extract(self.patched, '_stream_baked_motion_instances_1513', ns)
        self.assertEqual(fn(1, 'vehicle'), ())
        self.assertEqual(calls, [])
        ns['g_offh_destr_instances'].clear()
        self.assertEqual(fn(1, 'vehicle'), ())
        self.assertEqual([name for name, value in calls], ['intersection', 'admit'])
        self.assertEqual(counts['destructible_stream_candidates'], 2)
        self.assertEqual(counts['destructible_stream_missing'], 1)

    def test_custom_cache_retains_original_pop_order(self):
        outputs = []
        for source in (self.original, self.patched):
            keys = [(1, 2), (1, 1)]
            ns, calls, counts = environment(keys, keys)
            class Cache(dict):
                def pop(self, key, default=None):
                    calls.append(('pop', key))
                    return super().pop(key, default)
            ns['g_offh_destr_unresolved_obstacles'] = Cache(
                ns['g_offh_destr_unresolved_obstacles'])
            result = extract(source, '_stream_baked_motion_instances_1513', ns)(1, 'vehicle')
            outputs.append((result, calls, counts))
        self.assertEqual(outputs[0], outputs[1])

    def test_unused_malformed_baked_boxes_no_longer_block_registered_instances(self):
        keys = [(1, 1)]
        with self.assertRaisesRegex(TypeError, 'malformed baked boxes'):
            self.run_stream(self.original, keys, keys, boxes={keys[0]: None})
        result = self.run_stream(self.patched, keys, keys, boxes={keys[0]: None})
        self.assertEqual(result[0], ())
        self.assertEqual(result[3]['destructible_stream_candidates'], 1)
        self.assertEqual(result[1]['g_offh_destr_instances'][keys[0]], {'live': keys[0]})
        # A missing item still needs the original baked proof. This narrow
        # branch never swallows malformed geometry in mixed admission.
        for source in (self.original, self.patched):
            with self.assertRaisesRegex(TypeError, 'malformed baked boxes'):
                self.run_stream(source, keys + [(1, 2)], keys,
                                boxes={keys[0]: None})

    def test_mixed_repair_can_invalidate_a_registered_item(self):
        keys = [(1, 2), (1, 1)]
        outputs = []
        for source in (self.original, self.patched):
            ns, calls, counts = environment(keys, [(1, 2)],
                                           boxes={(1, 1): ('contact',)})
            admit = ns['_stream_baked_shot_instance_1513']
            def repair(space, key):
                if key == (1, 1):
                    ns['g_offh_destr_instances'].pop((1, 2), None)
                return admit(space, key)
            ns['_stream_baked_shot_instance_1513'] = repair
            result = extract(source, '_stream_baked_motion_instances_1513', ns)(1, 'vehicle')
            outputs.append((result, calls, counts, ns['g_offh_destr_instances']))
        self.assertEqual(outputs[0], outputs[1])
        self.assertEqual([key for name, key in outputs[1][1] if name == 'admit'], keys[::-1])

    def test_failed_publications_retry_before_each_live_catalog_query(self):
        for source in (self.original, self.patched):
            ns, calls, counts = environment([(1, 1)], [(1, 1)])
            ns.update({
                'g_offh_destr_runtime_space': 1,
                'g_offh_destr_catalog_publish_pending': {
                    'event': ('fragile', 1, 1, 'pos', 0.0, 1.0, None)},
                '_publish_catalog_once_1513': lambda *args:
                    (calls.append(('publication', args)) or False),
                '_diagnostic_flush_1513': lambda now: None,
                '_vehicle_body_bbox': lambda td: 'bbox',
                '_get_destr_authority': lambda: 'authority',
                '_refresh_destroyed_falling_instances_1513': lambda *args: None,
                '_motion_travel_reach': lambda *args: 1.0,
                '_vehicle_swept_box': lambda *args, **kwargs: 'vehicle',
                '_catalog_contact_candidates': lambda *args, **kwargs:
                    (calls.append(('live_catalog', args)) or ([], {})),
                '_catalog_motion_result': lambda status, **kwargs: status,
            })
            extract(source, '_stream_baked_motion_instances_1513', ns)
            extract(source, '_retry_catalog_publications_1513', ns)
            resolve = extract(source, '_catalog_motion_blocked', ns)
            with patch.dict(sys.modules, {'Math': types.ModuleType('Math')}):
                self.assertEqual(resolve(1, 'pos', 0, 1, {}, 2), 'clear')
                self.assertEqual(resolve(1, 'pos', 0, 1, {}, 2), 'clear')
            self.assertEqual([row[0] for row in calls
                              if row[0] in ('publication', 'live_catalog')],
                             ['publication', 'live_catalog'] * 2)
            self.assertIn('event', ns['g_offh_destr_catalog_publish_pending'])

    def test_real_world_bsp_still_precedes_catalog(self):
        calls = []
        sensor = types.SimpleNamespace(_catalog_motion_blocked=lambda *a, **k:
                                      calls.append('catalog'))
        owner = types.SimpleNamespace(
            _combat_diagnostics=None, _vector=lambda p: p,
            _bots=types.SimpleNamespace(states={1: {}}, _turn_speeds={}),
            _turret_motion_is_clear=lambda *args: True,
            _tree_motion_proposal=lambda *args: {'status': 'clear'},
            _destructibles=sensor, _bot_motion_kinds={},
            _runtime=types.SimpleNamespace(bigworld=None, math=None),
            _avatar=types.SimpleNamespace(spaceID=1))
        ns = {'BotRuntime': types.SimpleNamespace(_turret_state_pose=lambda *a: None),
              '_number': lambda value: float(value or 0),
              'math': __import__('math'),
              'world_collision': types.SimpleNamespace(check_horizontal_collision=lambda *a, **k:
                                                       (calls.append('world') or 'hard'))}
        resolve = extract(self.battle, '_resolve_bot_motion', ns, indent='    ')
        self.assertEqual(resolve(owner, 1, (0, 0, 0), 0, 1, {}, .1, 2), 'hard')
        self.assertEqual(calls, ['world'])


if __name__ == '__main__':
    unittest.main()
