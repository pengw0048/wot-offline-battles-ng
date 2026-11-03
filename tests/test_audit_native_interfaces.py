"""A reproducible negative-control check for the read-only audit script."""
import importlib.util
import json
import os
from pathlib import Path
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1] / 'tools'
PY27 = os.environ.get('WOT_PYTHON27')
SPEC = importlib.util.spec_from_file_location('native_audit', ROOT / 'audit_native_interfaces.py')
AUDIT = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(AUDIT)


class NativeAuditTest(unittest.TestCase):
    @unittest.skipUnless(PY27, 'WOT_PYTHON27 is required for CPython 2.7 extraction')
    def test_aliases_optional_guards_and_fixed_native_methods(self):
        with tempfile.TemporaryDirectory(prefix='ng-native-audit-fixture-') as directory:
            root = Path(directory)
            source = root / 'repo/native/standalone_client/bigworld/exports.cpp'
            source.parent.mkdir(parents=True)
            source.write_text('''
PY_FACTORY_NAMED(PyMatrix,"Matrix",Math)
Py_Header(PyMatrix,PyObjectPlus)
PY_BEGIN_METHODS(PyMatrix)
PY_METHOD_WITH_DOC(invert,"Invert this matrix")
PY_END_METHODS()
PY_MODULE_FUNCTION(syncMousePosition,GUI)
PY_MODULE_FUNCTION(dummy,View)
PY_FACTORY(RequiredFactory,BigWorld)
PY_FACTORY(OptionalFactory,BigWorld)
''')
            registry = AUDIT.registrations(str(root / 'repo'), None, {'BigWorld', 'GUI', 'Math', 'View'})
            registry_path = root / 'registry.json'
            AUDIT.write_json(str(registry_path), registry)
            maker = r'''
import marshal,struct,zipfile
source="""import BigWorld as BW
from Math import Matrix as M
import GUI
from gui.fake import View
class Owner(object):
    def __init__(self):
        self.matrix=M()
    def use(self):
        if self.helper:
            BW.RequiredFactory()
        if hasattr(BW, 'OptionalFactory'):
            BW.OptionalFactory()
        self.matrix.invert()
        GUI.syncMousePosition(0,0)
        View._populate(self)
"""
z=zipfile.ZipFile(__import__('sys').argv[1],'w')
for name in ['scripts/client/probe.pyc','scripts/client/bwobsolete_tests/probe.pyc']:
    z.writestr(name,'\x03\xf3\x0d\x0a'+struct.pack('<I',0)+marshal.dumps(compile(source,name,'exec')))
z.close()
'''
            package = root / 'client/res/packages/scripts.pkg'
            package.parent.mkdir(parents=True)
            subprocess.run([PY27, '-B', '-c', maker, str(package)], check=True)
            demand_path = root / 'demand.json'
            subprocess.run([PY27, '-B', str(ROOT / 'audit_native_interfaces.py'), '--extract-child',
                '--client', str(root / 'client'), '--registry', str(registry_path), '--demand', str(demand_path)], check=True)
            demand = json.loads(demand_path.read_text())
            paths = {ref['native_path'] for ref in demand['references']}
            self.assertTrue({'BigWorld.RequiredFactory', 'BigWorld.OptionalFactory', 'Math.Matrix',
                'Math.Matrix.invert', 'GUI.syncMousePosition'} <= paths)
            self.assertNotIn('View._populate', paths, 'Python class alias was mistaken for a native module')
            required = [ref for ref in demand['references'] if ref['native_path'] == 'BigWorld.RequiredFactory']
            optional = [ref for ref in demand['references'] if ref['native_path'] == 'BigWorld.OptionalFactory']
            self.assertTrue(required and optional)
            self.assertTrue(all(not any(g.get('explicit_native_existence_guard') for g in ref['guards']) for ref in required))
            self.assertTrue(all(any(g.get('explicit_native_existence_guard') for g in ref['guards']) for ref in optional))
            snapshot = root / 'snapshot.json'
            AUDIT.write_json(str(snapshot), {'modules': {'BigWorld': {'names': []},
                'Math': {'names': ['Matrix'], 'types': {'Matrix': {'methods': ['invert']}}},
                'GUI': {'names': ['syncMousePosition']}}})
            result = AUDIT.compare(demand, registry, str(snapshot))
            groups = {group['native_path']: group for group in result['groups']}
            self.assertEqual(groups['BigWorld.RequiredFactory']['priority'], 'P2-live-path-needs-reachability')
            self.assertEqual(groups['BigWorld.OptionalFactory']['priority'], 'P3-guarded-editor-or-nonclient-candidate')
            self.assertEqual(groups['Math.Matrix.invert']['status'], 'runtime-member-present')
            # The runtime snapshot, not a matching source declaration, decides
            # availability. Removing one actual method must expose that gap.
            state = json.loads(snapshot.read_text())
            state['modules']['Math']['types']['Matrix']['methods'] = []
            AUDIT.write_json(str(snapshot), state)
            result = AUDIT.compare(demand, registry, str(snapshot))
            self.assertEqual(next(g for g in result['groups'] if g['native_path'] == 'Math.Matrix.invert')['status'],
                'runtime-member-absent')


if __name__ == '__main__':
    unittest.main()
