"""Build the portable room probes once, then run every pinned room check.

Usage: WOT_FORK_SOURCE=/path/to/pinned/source python3.11 -B room/run_room_tests.py
No proprietary client files, engine, Python 2 runtime or network listener is used.
"""
import argparse
import concurrent.futures
import contextlib
import os
from pathlib import Path
import shlex
import subprocess
import sys
import tempfile

ROOM = Path(__file__).resolve().parent
ROOT = ROOM.parent
TESTS = ROOT / 'tests'


def run(command, **options):
    result = subprocess.run([str(v) for v in command], capture_output=True,
                            text=True, **options)
    if result.returncode:
        raise RuntimeError('Command failed: %s\n%s%s' %
                           (shlex.join([str(v) for v in command]),
                            result.stdout, result.stderr))
    return result


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--oracle', type=Path,
                        default=os.environ.get('WOT_FORK_SOURCE'))
    parser.add_argument('--cxx', default=os.environ.get('CXX', 'c++'))
    parser.add_argument('--jobs', type=int, default=min(4, os.cpu_count() or 1))
    parser.add_argument('--no-sanitizers', action='store_true',
                        help='Build probes without AddressSanitizer/UBSan')
    parser.add_argument('--build-dir', type=Path,
                        help='Retain compiled probes for a focused test rerun')
    args = parser.parse_args()
    if sys.version_info[:2] != (3, 11):
        parser.error('The gameplay oracle uses CPython 3.11, matching native-core.yml; '
                     'newer base64/Unicode laws can change reference admission')
    if not args.oracle:
        parser.error('--oracle or WOT_FORK_SOURCE must name the pinned gameplay source')
    pin = args.oracle.resolve()
    for relative in ('server/lan_battle_server.py', 'native/offline_contact_roster.cpp',
                     'src/res/scripts/client/gui/mods/offline_lan_0922/ammo_policy.py',
                     'navgraphs/04_himmelsdorf.json', 'navgraphs/05_prohorovka.json'):
        if not (pin / relative).is_file():
            parser.error('Pinned source is missing %s' % relative)
    if args.jobs < 1:
        parser.error('--jobs must be positive')
    compiler = shlex.split(args.cxx)
    flags = ['-std=c++17', '-O0', '-ffp-contract=off', '-pthread', '-Wall', '-Wextra',
             '-Werror', '-I' + str(ROOM), '-I' + str(pin / 'native')]
    sanitizer = [] if args.no_sanitizers else ['-fsanitize=address,undefined']
    flags += sanitizer
    sources = []
    for pattern in ('room_core*.cpp', 'room_rules*.cpp', 'room_strategy*.cpp'):
        sources.extend(p for p in ROOM.glob(pattern)
                       if not p.name.endswith(('_probe.cpp', '_test_host.cpp')))
    sources += [ROOM / (name + '.cpp') for name in
                ('room_inputs', 'room_json', 'room_lobby_policy', 'room_bot_rows',
                 'room_publication_clock', 'room_host', 'room_channel',
                 'room_network', 'room_replica')]
    sources += [ROOT / 'bigworld/stock_native_bot_combat.cpp',
                ROOT / 'bigworld/stock_native_room_resources.cpp',
                pin / 'native/offline_contact_roster.cpp']
    probes = dict(rules=ROOM / 'room_rules_test_host.cpp',
                  rules_wire=ROOM / 'room_rules_wire_probe.cpp',
                  worker=ROOM / 'room_rules_worker_test_host.cpp',
                  clock=ROOM / 'room_publication_clock_probe.cpp',
                  strategy=ROOM / 'room_strategy_probe.cpp',
                  tick=ROOM / 'room_core_tick_probe.cpp',
                  host=ROOM / 'room_host_backpressure_probe.cpp',
                  lobby=TESTS / 'room_lobby_probe.cpp',
                  policy=TESTS / 'room_lobby_policy_probe.cpp',
                  inputs=TESTS / 'room_inputs_test_cli.cpp',
                  inputs_archive=ROOM / 'room_inputs_archive_probe.cpp',
                  botrows=TESTS / 'room_bot_rows_cli.cpp',
                  radio=TESTS / 'room_strategy_radio_probe.cpp',
                  rewards=TESTS / 'room_rules_achievements_test_cli.cpp',
                  resources=TESTS / 'room_resources_probe.cpp')
    storage = (contextlib.nullcontext(str(args.build_dir.resolve())) if args.build_dir
               else tempfile.TemporaryDirectory(prefix='native-room-tests-'))
    with storage as temporary:
        build = Path(temporary)
        build.mkdir(parents=True, exist_ok=True)
        resources = build / 'resources.json'
        run([sys.executable, '-B', ROOM / 'bake_resources.py', pin, resources])
        print('Compiling %d room objects once for %d probes' %
              (len(sources), len(probes)), flush=True)

        def compile_one(source):
            target = build / (source.stem + '.o')
            run(compiler + flags + ['-c', str(source), '-o', str(target)])
            return target

        with concurrent.futures.ThreadPoolExecutor(max_workers=args.jobs) as pool:
            objects = list(pool.map(compile_one, sources))
        archive = build / 'room.a'
        run(['ar', 'rcs', archive, *objects])

        def link_probe(item):
            name, source = item
            run(compiler + flags + [str(source), str(archive), '-o', str(build / name)])

        with concurrent.futures.ThreadPoolExecutor(max_workers=args.jobs) as pool:
            list(pool.map(link_probe, probes.items()))
        environment = dict(os.environ, WOT_FORK_SOURCE=str(pin),
                           WOT_ROOM_TEST_BIN=str(build),
                           WOT_ROOM_TEST_RESOURCES=str(resources),
                           WOT_ROOM_RESOURCES=str(resources),
                           WOT_ROOM_NAVGRAPHS=str(pin / 'navgraphs'),
                           PYTHONDONTWRITEBYTECODE='1')
        suites = [ROOM / 'room_rules_test.py', ROOM / 'room_rules_worker_test.py',
                  ROOM / 'test_room_publication_clock.py',
                  TESTS / 'room_rules_achievements_test.py',
                  TESTS / 'test_room_inputs.py', TESTS / 'test_room_bot_rows.py',
                  TESTS / 'test_room_strategy_radio_oracle.py',
                  TESTS / 'room_lobby_policy_test.py', TESTS / 'room_lobby_test.py',
                  TESTS / 'test_stock_native_room_resources.py']
        checks = [[sys.executable, '-B', suite] for suite in suites]
        for name in ('test_room_strategy_oracle.py',
                     'test_room_strategy_observation_oracle.py'):
            checks.append([sys.executable, '-B', ROOM / name, '--oracle', pin,
                           '--probe', build / 'strategy'])
        checks.append([sys.executable, '-B', ROOM / 'test_room_core_tick.py',
                       '--probe', build / 'tick', '--resources', resources])
        checks.append([sys.executable, '-B', ROOM / 'test_room_core_command.py',
                       '--oracle', pin, '--probe', build / 'tick',
                       '--resources', resources])
        checks.append([sys.executable, '-B', ROOM / 'test_room_rules_wire.py',
                       '--probe', build / 'rules_wire', '--resources', resources])
        checks.append([build / 'inputs_archive', resources])
        checks.append([build / 'host', resources, build / 'host-state'])
        for check in checks:
            print('Running %s' % Path(str(check[2] if check[0] == sys.executable else check[0])).name, flush=True)
            result = run(check, env=environment)
            print(result.stdout, end='')
            print(result.stderr, end='')
        # Cold factory results may retain borrowed subtrees inside their resource
        # object. Exercise those boundaries separately from the exact Host view.
        nested = dict(environment, WOT_ROOM_TEST_RESOURCE_VIEW='nested')
        nested_suites = {'room_rules_test.py', 'room_rules_worker_test.py',
                         'test_room_inputs.py', 'room_lobby_policy_test.py',
                         'room_lobby_test.py', 'test_stock_native_room_resources.py',
                         'test_room_strategy_oracle.py', 'test_room_core_tick.py',
                         'test_room_rules_wire.py'}
        for check in checks:
            if check[0] != sys.executable or Path(str(check[2])).name not in nested_suites:
                continue
            print('Running nested Shared resources: %s' % Path(str(check[2])).name, flush=True)
            result = run(check, env=nested)
            print(result.stdout, end='')
            print(result.stderr, end='')
        print('All portable native room checks passed', flush=True)


if __name__ == '__main__':
    main()
