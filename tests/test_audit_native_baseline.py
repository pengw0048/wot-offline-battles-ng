"""Exercise census regressions and negative evidence without a game or SDK."""
import json
import io
from pathlib import Path
import sys
import tempfile
import unittest

TOOLS = Path(__file__).resolve().parents[1] / 'tools'
sys.path.insert(0, str(TOOLS))
import audit_native_baseline as baseline
import audit_native_interfaces as native
import audit_renderer_abi as renderer


def reference(path):
    return dict(native_path=path, receiver='module', member='scripts/client/probe.pyc',
        code_path='<module>.use', offset=0, scope='client-common-runtime-candidate',
        guards=[], module_or_class_initialization=False)


class NativeEvidenceTests(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory()
        self.addCleanup(self.temporary.cleanup)
        self.snapshot = Path(self.temporary.name) / 'snapshot.json'
        self.registry = dict(exports=[], classes={}, limitations=[], original_positive_catalog={})

    def compare(self, path, modules):
        self.snapshot.write_text(json.dumps(dict(modules=modules)))
        return native.compare(dict(references=[reference(path)], limitations=[]),
            self.registry, str(self.snapshot))['groups'][0]

    def test_unverified_original_registration_is_not_confirmed(self):
        catalog = self.registry['original_positive_catalog']
        catalog['exports'] = {'BigWorld.missing': [dict(current_PE_registration_bytes_verified=False)]}
        row = self.compare('BigWorld.missing', {'BigWorld': []})
        self.assertEqual(row['difference_status'], 'original-registration-instruction-unverified')
        catalog['exports']['BigWorld.missing'][0]['current_PE_registration_bytes_verified'] = True
        row = self.compare('BigWorld.missing', {'BigWorld': []})
        self.assertEqual(row['difference_status'], 'confirmed-export-gap-versus-original')
        self.assertFalse(row['native_behavior_proved'])
        self.assertFalse(row['live_reachability_proved'])

    def test_missing_original_catalog_is_unresolved_not_original_absence(self):
        row = self.compare('BigWorld.missing', {'BigWorld': []})
        self.assertEqual(row['difference_status'], 'stock-reference-without-reviewed-original-export')

    def test_live_snapshot_overrides_source_and_inherited_schema(self):
        self.registry['exports'] = [dict(module='Math', name='Matrix', native_class='Child')]
        self.registry['classes'] = {'Child': dict(methods=[], bases=['Base']),
            'Base': dict(methods=['invert'], bases=[])}
        row = self.compare('Math.Matrix.invert', {'Math': dict(names=['Matrix'], types={'Matrix': []})})
        self.assertEqual(row['status'], 'runtime-member-absent')
        row = self.compare('Math.Matrix.invert', {})
        self.assertEqual(row['status'], 'source-member-present')

    def test_dynamic_attributes_need_instance_evidence(self):
        self.registry['exports'] = [dict(module='GUI', name='Window', native_class='Dynamic')]
        self.registry['classes'] = {'Dynamic': dict(dynamic_lookup_override=True)}
        row = self.compare('GUI.Window.child', {'GUI': dict(names=['Window'], types={'Window': []})})
        self.assertEqual(row['status'], 'dynamic-member-needs-instance-evidence')
        self.assertFalse(row['native_behavior_proved'])

    def test_cpp_comments_do_not_create_exports_or_providers(self):
        source = '''// PY_MODULE_FUNCTION(fake,BigWorld)
        /* PY_FACTORY(Fake,GUI) */
        PY_FACTORY_NAMED(Real, "Window", GUI)
        PY_MODULE_FUNCTION_WITH_DOC(real,BigWorld,"commas, parentheses()")
        '''
        calls = list(native.macro_calls(native.stripped(source)))
        self.assertEqual([row[0] for row in calls], ['PY_FACTORY_NAMED', 'PY_MODULE_FUNCTION_WITH_DOC'])
        self.assertEqual(calls[1][1][-1], '"commas, parentheses()"')
        report = renderer.source_inventory({'probe.cpp': '''// registerAutoConstant(x,"False")
            getMapping("DirectProvider"); unregisterAutoConstant(x,"Removed");
            registerAutoConstant(x,"Live");'''} )
        self.assertEqual({row['semantic'] for row in report['semantic_source_registrations']}, {'DirectProvider', 'Live'})


class BaselineRegressionTests(unittest.TestCase):
    def setUp(self):
        self.native = dict(runtime_snapshot='explicit.json', groups=[
            dict(native_path='BigWorld.stable', status='runtime-export-present', difference_status='not-a-confirmed-missing-runtime-name'),
            dict(native_path='BigWorld.optional', status='runtime-export-absent', difference_status='confirmed-export-gap-versus-original')])
        self.renderer = dict(effects={'one.fxo': {}}, decode_errors=[],
            unmatched_used_semantics=[dict(semantic='Known')], public_scaleform_surface={})
        self.reviewed = dict(native_statuses={'runtime-export-present': ['BigWorld.stable'],
            'runtime-export-absent': ['BigWorld.optional']}, renderer_unmatched_semantics={'Known': {}},
            scaleform_source_schema={}, renderer_reviews=[], gui_reviews=[], native_scene_reviews=[], limitations=[])

    def review(self):
        return baseline.review_reports(self.native, self.renderer, self.reviewed)

    def test_reviewed_gap_remains_visible(self):
        report = self.review()
        self.assertEqual(report['outcome'], 'reviewed-census-only')
        self.assertEqual(report['retained_native_gaps_or_unresolved'][0]['native_path'], 'BigWorld.optional')

    def test_name_removal_and_source_only_evidence_are_regressions(self):
        for status in ('runtime-export-absent', 'source-registration-present', 'member-schema-unresolved'):
            with self.subTest(status=status):
                self.native['groups'][0]['status'] = status
                self.assertTrue(self.review()['failures'])

    def test_improved_export_name_does_not_claim_behavior(self):
        self.native['groups'][1]['status'] = 'runtime-export-present'
        report = self.review()
        self.assertFalse(report['failures'])
        self.assertFalse(report['native_name_improvements'][0]['behavior_proved'])

    def test_new_unresolved_consumer_requires_review(self):
        self.native['groups'].append(dict(native_path='GUI.New.child', status='dynamic-member-needs-instance-evidence', difference_status='unresolved'))
        self.assertEqual(self.review()['failures'][0]['native_path'], 'GUI.New.child')

    def test_conflicting_native_groups_are_not_silently_overwritten(self):
        self.native['groups'].append(dict(self.native['groups'][0]))
        self.assertTrue(self.review()['failures'])

    def test_truncated_demand_is_not_silent_improvement(self):
        self.native['groups'].pop()
        self.assertTrue(self.review()['failures'])

    def test_missing_snapshot_and_empty_effects_are_not_passes(self):
        self.native['runtime_snapshot'] = None
        self.renderer['effects'] = {}
        self.assertEqual(len(self.review()['failures']), 2)

    def test_shader_decode_failure_and_new_provider_candidate_require_review(self):
        self.renderer['decode_errors'] = [dict(effect='bad.fxo', error='bad binding')]
        self.renderer['unmatched_used_semantics'].append(dict(semantic='New'))
        self.assertEqual(len(self.review()['failures']), 2)

    def test_public_scaleform_argument_change_requires_review(self):
        self.renderer['public_scaleform_surface'] = {'public-sdk/lib/scaleform/movie.cpp': [
            dict(macro='PY_AUTO_METHOD_DECLARE', arguments=['bool', 'create', 'ARG(bool,true)'])]}
        self.assertTrue(self.review()['failures'])
        schema = baseline.scaleform_schema(self.renderer['public_scaleform_surface'])
        self.reviewed['scaleform_source_schema'] = json.loads(json.dumps(schema))
        self.assertFalse(self.review()['failures'])
        self.renderer['public_scaleform_surface']['public-sdk/lib/scaleform/movie.cpp'][0]['arguments'][-1] = 'ARG(bool,false)'
        self.assertTrue(self.review()['failures'])

    def test_unused_declaration_is_not_missing_used_semantic(self):
        effects = {'one.fxo': dict(uniforms=[
            dict(semantic='Used', used_by_shaders=1), dict(semantic='Unused', used_by_shaders=0),
            dict(semantic='Provided', used_by_shaders=2)])}
        rows = renderer.unmatched_semantics(effects, [dict(semantic='Provided')])
        self.assertEqual([row['semantic'] for row in rows], ['Used'])

    def test_contract_summary_preserves_skipped_evidence(self):
        class Probe(unittest.TestCase):
            def test_pass(self):
                pass
            @unittest.skip('exact data unavailable')
            def test_skip(self):
                pass
        result = unittest.TextTestRunner(stream=io.StringIO()).run(
            unittest.defaultTestLoader.loadTestsFromTestCase(Probe))
        statistics = baseline.contract_statistics(result)
        self.assertEqual(statistics['tests'], 2)
        self.assertEqual(statistics['skipped'], 1)
        self.assertEqual(statistics['skip_details'][0]['reason'], 'exact data unavailable')

    def test_repository_manifest_contract_modules_exist(self):
        reviewed = baseline.load(baseline.REVIEW)
        self.assertTrue(reviewed['native_scene_reviews'])
        for name in reviewed['contract_tests']:
            self.assertTrue((baseline.ROOT / 'native/standalone_client/tests' / (name + '.py')).is_file(), name)
        names = [name for group in reviewed['native_statuses'].values() for name in group]
        self.assertEqual(len(names), len(set(names)), 'A path must have one reviewed evidence classification')


if __name__ == '__main__':
    unittest.main()
