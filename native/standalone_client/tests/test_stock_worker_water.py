"""CPU water completion and live compiled-space ownership without worker drawing."""
from pathlib import Path
import os
import subprocess
import sys
import tarfile
import tempfile
import unittest

from native_test_compile_flags import native_test_compile_flags
from test_stock_ground_effects import body

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'bigworld'))
import patch_stock_space_water

HEADERS = ('pch.hpp', 'cstdmf/bw_namespace.hpp', 'math/matrix.hpp', 'math/boundbox.hpp',
           'compiled_space/loader.hpp', 'compiled_space/binary_format.hpp',
           'compiled_space/compiled_space.hpp', 'compiled_space/string_table.hpp',
           'space/dynamic_scene_provider.hpp', 'space/deprecated_space_helpers.hpp',
           'scene/scene.hpp', 'scene/scene_object.hpp', 'scene/draw_operation.hpp',
           'scene/spatial_query_operation.hpp', 'romp/water.hpp', 'romp/romp_collider.hpp')


def engine_water(test, path):
    archive = Path(os.environ.get('WOT_PUBLIC_BIGWORLD_ARCHIVE', '/tmp/wot-bigworld-engine-14.4.1.tar.gz'))
    if not archive.is_file():
        test.skipTest('Pinned public BigWorld archive required')
    names = {'lib/romp/water.hpp', 'lib/romp/water.cpp', 'lib/cstdmf/bgtask_manager.cpp'}
    with tarfile.open(archive) as source:
        for member in source:
            relative = member.name.partition('/programming/bigworld/')[2]
            if relative in names:
                destination = path / relative
                destination.parent.mkdir(parents=True, exist_ok=True)
                destination.write_bytes(source.extractfile(member).read())
                names.remove(relative)
                if not names:
                    break
    test.assertFalse(names)
    patch_stock_space_water.patch(path)
    first = {name: (path / 'lib/romp' / name).read_bytes() for name in ('water.hpp', 'water.cpp')}
    patch_stock_space_water.patch(path)
    test.assertEqual(first, {name: (path / 'lib/romp' / name).read_bytes() for name in first})
    result = {name: data.decode() for name, data in first.items()}
    result['tasks.cpp'] = (path / 'lib/cstdmf/bgtask_manager.cpp').read_text()
    return result


def build(test, path, original_completion=False):
    engine = engine_water(test, path / 'engine')
    source, header = engine['water.cpp'], engine['water.hpp']
    test.assertEqual(source.count('stockQueryDataReady_ = true;'), 1)
    test.assertIn('initialised_( false ),\n    stockQueryDataReady_( false ),', source)
    rebuild = body(source, 'void Water::rebuild(')
    test.assertLess(rebuild.index('stockQueryDataReady_ = false;'), rebuild.index('state_ = state;'))
    setup = body(source, 'void Water::setup2ndPhase(')
    test.assertLess(setup.index('bbActual_.setBounds'), setup.index('stockQueryDataReady_ = true;'))
    test.assertLess(setup.index('stockQueryDataReady_ = true;'), setup.index('createUnmanagedObjects();'))
    include = path / 'include'
    for name in HEADERS:
        dest = include / name
        dest.parent.mkdir(parents=True, exist_ok=True)
        dest.write_text('#pragma once\n')
    begin = header.index('\ttypedef struct _WaterState')
    end = header.index('} WaterState;', begin) + len('} WaterState;')
    (include / 'stock_worker_water_state.inc').write_text(header[begin:end])
    tasks = engine['tasks.cpp']
    task_code = body(tasks, 'CStyleBackgroundTask::CStyleBackgroundTask( const char* name,')
    task_code += '\n' + body(tasks, 'void CStyleBackgroundTask::doBackgroundTask(')
    task_code += '\n' + body(tasks, 'void CStyleBackgroundTask::doMainThreadTask(')
    task_code += '\nnamespace {\nuint64 s_stockWaterQueryGeneration = 0;\n'
    task_code += body(source, 'struct StockWaterTablesContext') + ';\n'
    task_code += body(source, 'class StockWaterTablesTask') + ';\n}\n'
    (include / 'stock_worker_water_tasks.inc').write_text(task_code)
    constructor = body(source, 'Water::Water( const WaterState&')
    begin = constructor.index('    if (Water::backgroundLoad())')
    (include / 'stock_worker_water_schedule.inc').write_text(constructor[begin:constructor.index('    static bool first', begin)])
    producers = '\n'.join(body(source, 'void Water::' + name + '(') for name in (
        'doCreateTables', 'onCreateTablesComplete', 'setup2ndPhase', 'rebuild'))
    producers += '\n' + body(source, 'bool Water::stillValid(') + '\n' + body(source, 'void Water::deleteWater(')
    if original_completion:
        producers = producers.replace('    stockQueryDataReady_ = true;', '')
    (include / 'stock_worker_water_producers.inc').write_text('namespace BW {\n' + producers + '\n}\n')
    ground = (ROOT / 'bigworld/stock_ground_effects.cpp').read_text()
    begin = ground.index('float Water::stockGroundRayDistance(')
    (include / 'stock_ground_water_query.inc').write_text('namespace BW {\n' +
        ground[begin:ground.index('\nnamespace\n{', begin)] + '\n}\n')
    query = (ROOT / 'bigworld/stock_battle_scene_queries.cpp').read_text()
    (include / 'stock_worker_water_alpha.inc').write_text('namespace BW {\n' +
        body(query, 'float Water::stockWaterRayDistance(') + '\n' +
        body(query, 'float collideStockWater(') + '\n}\n')
    executable = path / 'worker_water'
    result = subprocess.run(['c++', *native_test_compile_flags('c++'), '-std=c++17',
        '-Wall', '-Wextra', '-Werror', '-Wno-unused-variable', '-Wno-unused-parameter', '-Wno-mismatched-tags',
        '-ffp-contract=off', '-DWOT_STOCK_SPACE_WATER_PORTABLE', '-I', str(include),
        '-I', str(ROOT / 'bigworld'), str(ROOT / 'tests/stock_worker_water_host.cpp'),
        str(ROOT / 'bigworld/stock_space_water.cpp'), '-o', str(executable)], capture_output=True, text=True)
    test.assertEqual(result.returncode, 0, result.stdout + result.stderr)
    return executable


class WorkerWaterTests(unittest.TestCase):
    def test_cpu_completion_live_owner_and_all_callsite_dispatch(self):
        with tempfile.TemporaryDirectory(prefix='worker-water-') as directory:
            binary = build(self, Path(directory))
            result = subprocess.run([str(binary)], capture_output=True, text=True)
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
            self.assertIn('unload and rebind passed', result.stdout)
            print(result.stdout.strip())

    def test_original_completion_is_a_negative_control(self):
        with tempfile.TemporaryDirectory(prefix='worker-water-original-') as directory:
            binary = build(self, Path(directory), original_completion=True)
            result = subprocess.run([str(binary), 'original'], capture_output=True, text=True)
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
            self.assertIn('worker misses water', result.stdout)
            print(result.stdout.strip())


if __name__ == '__main__':
    unittest.main()
