"""Exercise player water observations and callback-free entity ingress."""
import os
from pathlib import Path
import shlex
import shutil
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]


def method(source, signature):
    start = source.index(signature)
    brace = source.index('{', start)
    depth = 1
    end = brace + 1
    while depth:
        depth += (source[end] == '{') - (source[end] == '}')
        end += 1
    return source[start:end]


class EnvironmentTests(unittest.TestCase):
    def test_cadence_and_hull_geometry(self):
        with tempfile.TemporaryDirectory(prefix='native-player-environment-') as folder:
            binary = Path(folder) / 'environment'
            command = ['c++', '-std=c++17', '-Wall', '-Wextra', '-Werror',
                       str(ROOT / 'tests/test_stock_native_player_environment.cpp'),
                       str(ROOT / 'bigworld/stock_native_player_environment.cpp'),
                       str(ROOT / 'bigworld/stock_native_bot_combat.cpp'), '-o', str(binary)]
            built = subprocess.run(command, capture_output=True, text=True)
            self.assertEqual(built.returncode, 0, built.stderr)
            run = subprocess.run([str(binary)], capture_output=True, text=True)
            self.assertEqual(run.returncode, 0, run.stderr)

    def test_python_boundaries_read_native_damage_owner(self):
        source = os.environ.get('WOT_FORK_0101_SOURCE')
        if not source or not shutil.which('pyenv'):
            self.skipTest('Fixed gameplay source and CPython 2.7.18 are required')
        python = subprocess.check_output(['pyenv', 'which', 'python2.7'], text=True,
            env=dict(os.environ, PYENV_VERSION='2.7.18')).strip()
        includes = shlex.split(subprocess.check_output([python + '-config', '--includes'], text=True))
        linking = shlex.split(subprocess.check_output([python + '-config', '--ldflags'], text=True))
        units = ['stock_native_player_environment', 'stock_native_bot_descriptor',
                 'stock_native_bot_resource_lifecycle', 'stock_native_bot_combat', 'stock_native_client_state']
        critical = (ROOT / 'bigworld/stock_native_battle_critical.cpp').read_text()
        helpers = critical[critical.index('namespace descriptors ='):critical.index('double crewStatFactor(')]
        projectile = method((ROOT / 'bigworld/stock_native_projectile_damage_python.cpp').read_text(),
                            'bool readProjectileTargetState(')
        environment = (ROOT / 'bigworld/stock_native_player_environment_python.cpp').read_text()
        host = (ROOT / 'tests/test_stock_native_player_environment_python.cpp').read_text()
        host = host.replace('@PRODUCER_HELPERS@', 'namespace {\n' + helpers + '\n}')
        host = host.replace('@PRODUCER@', method(critical, 'Value applyPayload('))
        host = host.replace('@PROJECTILE_READER@', projectile)
        local = (ROOT / 'bigworld/stock_native_battle_critical_local.cpp').read_text()
        host = host.replace('@RECONCILE@', method(local, 'bool reconcileAuthority('))
        with tempfile.TemporaryDirectory(prefix='native-player-ingress-') as folder:
            binary = Path(folder) / 'environment_python'
            generated = Path(folder) / 'host.cpp'
            generated.write_text(host)
            ingress = Path(folder) / 'environment.cpp'
            ingress.write_text(environment)
            command = ['c++', '-std=c++17', '-Wno-register', '-DWOT_RESOURCE_LIFECYCLE_HOST_BUILD',
                       '-DWOT_CLIENT_REPLICA_HOST_BUILD', *includes, '-I' + str(ROOT / 'bigworld'),
                       '-I' + str(ROOT / 'include'), '-I' + str(Path(source) / 'native'),
                       str(generated), str(ingress)]
            command += [str(ROOT / ('bigworld/' + unit + '.cpp')) for unit in units]
            command += linking + ['-o', str(binary)]
            built = subprocess.run(command, capture_output=True, text=True)
            self.assertEqual(built.returncode, 0, built.stderr)
            run = subprocess.run([str(binary)], capture_output=True, text=True,
                                 env=dict(os.environ, PYTHONDONTWRITEBYTECODE='1'))
            self.assertEqual(run.returncode, 0, run.stderr)
            # Restore each previous consumer's real stale fire read independently.
            for consumer in ('projectile', 'environment', 'initial'):
                generated.write_text(host.replace(
                    'out.critical=native_battle::critical::simulationState(*actor,entity,descriptor.critical.maxima);',
                    'out.critical=native_battle::critical::simulationState(*actor,entity,descriptor.critical.maxima);'
                    'out.critical.fire=p::flag(p::Ref(stock_native_bot::readNativeEntityField(battle,entity,"is_on_fire")).get());')
                    if consumer == 'projectile' else host.replace(
                        'b.critical->vehicles.emplace(vehicle.engine_id, VehicleDamage{});',
                        '(void)vehicle;') if consumer == 'initial' else host)
                ingress.write_text(environment.replace(
                    'row.critical=native_battle::critical::simulationState(record,entity,metadata.maxima);',
                    'row.critical=native_battle::critical::simulationState(record,entity,metadata.maxima);'
                    'row.critical.fire=p::flag(entityValue("is_on_fire").get());')
                    if consumer == 'environment' else environment)
                built = subprocess.run(command, capture_output=True, text=True)
                self.assertEqual(built.returncode, 0, built.stderr)
                run = subprocess.run([str(binary)], capture_output=True, text=True,
                                     env=dict(os.environ, PYTHONDONTWRITEBYTECODE='1'))
                self.assertNotEqual(run.returncode, 0, consumer + ' stale-field negative control passed')

if __name__ == '__main__':
    unittest.main()
