"""Material-near-point query: original consumer and x86 callback evidence."""
import json
import os
from pathlib import Path
import subprocess
import tempfile
import unittest

from test_stock_battle_scene_queries import ROOT, build_host


class StockMaterialQueryTest(unittest.TestCase):
    def test_production_query_and_original_consumer(self):
        with tempfile.TemporaryDirectory(prefix='material-query-') as directory:
            binary = build_host(self, Path(directory))
            env = dict(os.environ)
            if env.get('WOT_0922_CLIENT'):
                env['STOCK_SCRIPTS_PKG'] = str(Path(env['WOT_0922_CLIENT']) / 'res/packages/scripts.pkg')
            result = subprocess.run([str(binary), str(ROOT / 'tests/stock_material_query_consumer.py')],
                                    env=env, capture_output=True, text=True)
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
            self.assertIn('callback and retirement passed', result.stdout)
            if env.get('WOT_0922_CLIENT'):
                self.assertIn('Exact #1513 EffectMaterialCalculation', result.stdout)

    def test_original_x86_callback_matches_production(self):
        client = os.environ.get('WOT_0922_CLIENT')
        if not client:
            self.skipTest('WOT_0922_CLIENT is required for the instruction oracle')
        try:
            from stock_material_query_oracle import OriginalMaterialQuery, material_cases
        except ImportError:
            self.skipTest('pefile and Unicorn are required for the instruction oracle')
        oracle = OriginalMaterialQuery(Path(client) / 'WorldOfTanks.exe')
        cases = material_cases()
        with tempfile.TemporaryDirectory(prefix='material-query-oracle-') as directory:
            path = Path(directory)
            binary = build_host(self, path)
            (path / 'cases.json').write_text(json.dumps(cases))
            env = dict(os.environ, MATERIAL_QUERY_CASES=str(path / 'cases.json'),
                       MATERIAL_QUERY_OUTPUT=str(path / 'output.json'))
            run = subprocess.run([str(binary), str(ROOT / 'tests/stock_material_query_consumer.py')],
                                 env=env, capture_output=True, text=True)
            self.assertEqual(run.returncode, 0, run.stdout + run.stderr)
            results = json.loads((path / 'output.json').read_text())
        for case, actual in zip(cases, results):
            expected = oracle.run_query(case)
            self.assertEqual(actual['result'], expected['result'], case)
            self.assertEqual(actual['calls'], expected['calls'], case)
            self.assertTrue(all(v == 3 for v in expected['verdicts']), case)
        print('Original f610d0 complete material-query callback: %d cases passed' % len(cases))


if __name__ == '__main__':
    unittest.main()
