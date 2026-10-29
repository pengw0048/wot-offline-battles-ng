"""Installation DTO retains exact resources and keeps invalid identities local."""
import importlib.util
from pathlib import Path
import sys
import types
import unittest
from unittest.mock import patch

ROOT = Path(__file__).resolve().parents[1]


class SnapshotTest(unittest.TestCase):
    def test_resources_local_boxes_health_dependencies_and_tree_variants(self):
        spec = importlib.util.spec_from_file_location('native_world_install',
            ROOT / 'bigworld/stock_native_world_snapshot.py')
        module = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(module)
        signature = (10000, 0, 20000, 1000, 0, 0, 0, 1000, 0, 0, 0, 1000)
        fracture, touchdown = [object()], [object()]
        descriptors = {
            'Prop.model': dict(type=2, health=10., kineticDamageCorrection=.2),
            'Wall.model': dict(type=3, modules={73: dict(health=20.), 74: dict(health=20.)},
                destroyDepends={73: (74,)}, inversedDestroyDepends={74: (73,)}),
            'Tree.spt': dict(type=0, health=10., height=5., mass=100., springAngle=.2,
                springStiffnes=300., springResist=40., airResist=1., buryDepth=.3,
                fractureEffect=fracture, touchdownEffect=touchdown),
            'Invalid.model': dict(type=1),
        }
        seen = []
        def descriptor(name):
            seen.append(name)
            return descriptors.get(name)
        cache = types.SimpleNamespace(getDescByFilename=descriptor, unitVehicleMass=30000.)
        world = ((10., 1., 20.), ((1., 0., 0.), (0., 1., 0.), (0., 0., 1.)), None)
        def row(filename, kind, index=0):
            return dict(filename=filename.lower(), descriptor_filename=filename,
                kind=kind, boxes=(world,), item_scale=2., box_index=index,
                signature=signature, broad_phase_margin=.002)
        rows = {(1, 1): row('Prop.model', 'fragile'), (1, 2): row('Wall.model', 'structure', None),
                (1, 4): row('Invalid.model', 'fragile'), (1, 5): row('Missing.model', 'fragile')}
        resources = {
            'prop.model': dict(boxes=((-1., 0., -2., 1., 2., 2., None),), retained_collision_boxes=(0,)),
            'wall.model': dict(boxes=((-1., 0., -2., 1., 2., 2., 73),
                                       (-1., 2., -2., 1., 4., 2., 74))),
            'invalid.model': dict(boxes=((-1., 0., -2., 1., 2., 2., None),)),
            'missing.model': dict(boxes=((-1., 0., -2., 1., 2., 2., None),)),
        }
        sensor = types.SimpleNamespace(_destructible_catalog=dict(quantization=1000,
            ambiguous_instances={signature},
            baked_instances=rows, resources=resources, tree_instances={(1, 3): dict(
                filename='tree.spt', descriptor_filename='Tree.spt', kind='tree', signature=signature)}),
            g_offh_destr_instances={}, g_offh_destr_isolated_slots={(1, 4)})
        authority = types.SimpleNamespace(_state=dict(chunks={1: dict(keys={(1, None), (2, 73)})}))
        packages = {}
        for name in ('gui', 'gui.mods', 'gui.mods.offline_lan_0922'):
            packages[name] = types.ModuleType(name)
            packages[name].__path__ = []
        packages['gui.mods.offline_lan_0922'].destructibles_authority = authority
        packages.update(AreaDestructibles=types.SimpleNamespace(g_cache=cache),
            DestructiblesCache=types.SimpleNamespace(scaledDestructibleHealth=lambda scale, health: scale ** 3 * health))
        with patch.dict(sys.modules, packages):
            dto = module.build(sensor)
        entries = {row['key']: row for row in dto['instances']}
        self.assertEqual(entries[(1, 1)]['scaled_health'], ((-1, 80.),))
        self.assertTrue(entries[(1, 1)]['retained_collision'])
        self.assertEqual(entries[(1, 2)]['dependencies'], ((73, (74,)),))
        self.assertEqual(entries[(1, 2)]['inverse_dependencies'], ((74, (73,)),))
        self.assertEqual(entries[(1, 2)]['local_boxes'][0],
            ((0., 1., 0.), ((1., 0., 0.), (0., 1., 0.), (0., 0., 2.)), 73))
        self.assertIs(entries[(1, 3)]['fracture_variants'], fracture)
        self.assertIs(entries[(1, 3)]['touchdown_variants'], touchdown)
        self.assertFalse(entries[(1, 3)]['ambiguous'])
        self.assertTrue(entries[(1, 1)]['ambiguous'])
        self.assertFalse(entries[(1, 4)]['descriptor_valid'])
        self.assertFalse(entries[(1, 5)]['descriptor_valid'])
        self.assertEqual(dto['broken'], ((1, 1, None), (1, 2, 73)))
        self.assertEqual(dto['isolated_slots'], ((1, 4),))
        self.assertEqual(len(seen), 5)


if __name__ == '__main__':
    unittest.main()
