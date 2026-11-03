"""Compare resource-family rules with the complete pinned Python policy."""
import ast
import json
import os
from pathlib import Path
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]


class SceneryPolicyTests(unittest.TestCase):
    def test_soft_plants_and_solid_railway_families(self):
        source = os.environ.get('WOT_FORK_SOURCE')
        if not source:
            self.skipTest('Pinned gameplay source is required')
        package = Path(source) / 'src/res/scripts/client/gui/mods/offline_lan_0922'
        tree = ast.parse((package / 'destructibles_sensor.py').read_text())
        names = {'_SOFT_VEGETATION_TOKENS_1513', '_SOFT_PLANT_STEMS_1513'}
        nodes = [node for node in tree.body if
                 (isinstance(node, ast.Assign) and any(isinstance(n, ast.Name) and n.id in names for n in node.targets)) or
                 (isinstance(node, ast.FunctionDef) and node.name in ('_normalized_filename', '_skip_soft_vegetation_felling_1513'))]
        env = {'_INTEGER_TYPES': (int,), '_STRING_TYPES': (str,)}
        exec(compile(ast.Module(body=nodes, type_ignores=[]), 'pinned-scenery', 'exec'), env)
        exec((package / 'scenery_policy.py').read_text(), env)
        files = ['trees/BananaTree_03.spt', 'tree/Oak_01.spt', 'plants/not_fern_extra.spt',
                 'content/rw004_carriage/normal/model.model', 'content/rw005_locomotive/model.model',
                 'content/rw004_carriage_extra/model.model', 'railway/tracks/model.model', '']
        files += ['vegetation/' + stem + '.spt' for stem in env['_SOFT_PLANT_STEMS_1513']]
        files += ['vegetation/' + token + '_03.spt' for token in env['_SOFT_VEGETATION_TOKENS_1513']]
        files += [name.upper().replace('/', '\\') for name in files]
        cases = [(name, hp) for name in files for hp in (0., 3., 5., 6., 1000.)]
        expected = [(env['railway_vehicle_resource'](name),
                     env['_skip_soft_vegetation_felling_1513'](name, {'type': 7, 'health': hp}, 7))
                    for name, hp in cases]
        host = r'''
#include "stock_native_scenery_policy.hpp"
#include <iomanip>
#include <iostream>
int main(){std::string name;double hp;while(std::cin>>std::quoted(name)>>hp)
    std::cout<<stock_native_scenery_policy::railwayVehicle(name)<<' '
             <<stock_native_scenery_policy::softVegetation(name,hp)<<'\n';}
'''
        with tempfile.TemporaryDirectory(prefix='scenery-policy-') as directory:
            path = Path(directory)
            (path / 'host.cpp').write_text(host)
            built = subprocess.run(['c++', '-std=c++17', '-Wall', '-Wextra', '-Werror',
                                    '-I' + str(ROOT / 'bigworld'), str(path / 'host.cpp'),
                                    '-o', str(path / 'host')], text=True, capture_output=True)
            self.assertEqual(0, built.returncode, built.stderr)
            result = subprocess.run([str(path / 'host')], input='\n'.join(
                '%s %s' % (json.dumps(name), hp) for name, hp in cases), text=True, capture_output=True)
            self.assertEqual(0, result.returncode, result.stderr)
            self.assertEqual(expected, [tuple(bool(int(v)) for v in line.split()) for line in result.stdout.splitlines()])


if __name__ == '__main__':
    unittest.main()
