"""BigWorld.WGFlockManager: #1513 trigger contract, camera space fence, world tick seam and PYC consumers.

The production stock_flock_field.hpp runs in a host for exact state-machine
values. The patcher is applied to the pinned public engine at its build-chain
position, and the patched camera space setter is compiled and driven. Real
CPython 2.7 then runs the exact #1513 FlockManager/FlockExotic PYC, plus the
authored FlockExotic records of two exact maps, over the same core.
"""
import importlib.util
import io
import json
import os
from pathlib import Path
import re
import shlex
import shutil
import struct
import subprocess
import sys
import tempfile
import unittest
import zipfile

ROOT = Path(__file__).resolve().parents[1]
REPOSITORY = ROOT.parents[1]
TESTS = Path(__file__).resolve().parent
ARCHIVE = Path(os.environ.get('WOT_PUBLIC_ENGINE_ARCHIVE', '/tmp/wot-bigworld-engine-14.4.1.tar.gz'))
CLIENT = os.environ.get('WOT_0922_CLIENT')
PY27 = Path(os.environ.get('WOT_PYTHON27', str(Path.home() / '.pyenv/versions/2.7.18/bin/python2.7')))
PATCHER = 'patch_stock_flock_manager.py'
ENGINE_DIRECTORIES = ('client', 'lib', 'third_party/python/Include', 'third_party/python/Modules',
                      'third_party/python/Objects', 'third_party/python/Python')
SEAM_FILES = ('client/world_app.cpp', 'lib/space/deprecated_space_helpers.hpp',
              'lib/space/deprecated_space_helpers.cpp')

spec = importlib.util.spec_from_file_location('patch_stock_flock_manager', ROOT / 'bigworld' / PATCHER)
patcher = importlib.util.module_from_spec(spec)
spec.loader.exec_module(patcher)

# Hand-checked against the #1513 008a2440..008a5860 contract.
EXPECTED_CORE = {
    'default.interval': '1', 'default.running': '0',
    'record.trigger2': '400', 'record.min2': '64', 'record.max2': '1225', 'record.respawn': '400',
    'record.id': '77', 'record.last': '-1', 'record.inside': '0',
    'nospace.running': '0', 'nospace.records': '1',
    'start.running': '1', 'start.width': '5', 'start.height': '10', 'start.interval': '1',
    'start.due-same-frame': '0', 'start.due-early': '0', 'start.due': '1',
    'restart-ignored.interval': '1', 'restart.interval': '2',
    'reschedule.due-before': '0', 'reschedule.due': '1',
    'negative.running': '1', 'negative.scheduled': '0', 'nan.interval': '1', 'nan.due': '1',
    'insideout.width': '0', 'insideout.height': '0', 'rounding.width': '2', 'rounding.height': '1',
    'bin.x0': '0', 'bin.z0': '0', 'bin.center': '1', 'bin.clamped': '1', 'bin.clamped-record': '3',
    'late.indexed': '149', 'late.records': '150',
    'window.corner': '202', 'window.center': '4613', 'window.high': '8934', 'window.nan-empty': '1',
    'disk.outside': '0', 'disk.edge': '1', 'disk.inside-no-repeat': '1',
    'cooldown.outside-skipped': '1', 'cooldown.inside-after': '1',
    'respawn.still-inside': '1', 'respawn.left': '1', 'respawn.reentered': '2',
    'cooldown.both-sources-skipped': '2', 'ordered.left-after-cooldown': '2', 'ordered.left-inside': '0',
    'ordered.first-of-two': '3', 'elapsed': '11',
    'ring.inner': '0', 'ring.min-edge': '1', 'ring.max-edge': '2', 'ring.outer': '2', 'ring.consumed': '0',
    'ordered.vehicle-then-shot': '1', 'ordered.shot-while-inside': '1', 'window.outside-source': '0',
    'reenter.stop.callbacks': '1', 'reenter.stop.running': '0', 'reenter.stop.records': '0',
    'reenter.stop.scheduled': '0',
    'reenter.restart.callbacks': '1', 'reenter.restart.running': '1',
    'reenter.restart.new-schedule-kept': '1', 'reenter.restart.interval': '1',
    'reenter.restart.new-record-fresh': '0',
    'reenter.append.callbacks': '1', 'reenter.append.next-sweep': '65',
    'stop.idle.points': '0', 'stop.idle.running': '0', 'empty.callbacks': '0', 'empty.rescheduled': '1',
    'space.idle-stale': '0', 'space.same.swept': '1', 'space.same.callbacks': '1',
    'space.replaced.swept': '0', 'space.replaced.callbacks': '0', 'space.replaced.running': '0',
    'space.replaced.records': '0', 'space.next.first': '82', 'space.next.running': '1',
    'space.callback.callbacks': '1', 'space.callback.running': '0', 'space.callback.scheduled': '0',
}

EXPECTED_CONSUMER = {
    'singleton': 'True', 'callback-bound': 'True',
    'start-arity-0': "'TypeError'", 'start-arity-2': "'TypeError'", 'addFlock-arity-4': "'TypeError'",
    'factory-ignores-args': "'WGFlockManager'",
    'events-subscribed': '(1, 1)', 'python-list': "['near', 'ring']",
    'no-space-sweeps': '0', 'same-frame-sweeps': '0',
    'far-vehicle': '[]', 'dynamic-provider': "['near']", 'inside-no-repeat': "['near']",
    'projectile-ring': "['near', 'ring']", 'projectile-inner-and-cooldown': "['near', 'ring']",
    'vehicle-left': "['near', 'ring']", 'other-vehicle': "['near', 'ring', 'after-leave']",
    'stopped-handlers': '(0, 0)', 'stopped-list': '[]', 'callback-kept': 'True', 'stopped-sweeps': '0',
    'reenter-stop': "['first']", 'reenter-stop-sweeps-after': '0',
    'leave-in-callback': "['d1']",
    'space-same-sweeps': '1', 'space-replaced-sweeps': '0', 'space-replaced': '[]',
    'space-reassigned-sweeps': '1', 'space-next': "['b1']",
    'space-reused-sweeps': '0', 'space-reused': "['b1']",
    'space-callback': "['c1']", 'space-callback-sweeps-after': '0',
    'empty-sweeps': '(1, 1)',
}

# Provider samples and triggers per scenario from the production binding.
EXPECTED_BINDING = {
    'keep': 'a=1 b=1 c=0 triggered=1,2',
    'leave-before': 'a=1 b=0 c=0 triggered=1',
    'leave-during': 'a=1 b=0 c=0 triggered=1',
    'reenter-during': 'a=1 b=0 c=0 triggered=1',
    'reenter-next': 'a=2 b=1 c=0 triggered=1,2',
    'duplicate-keep': 'a=1 b=2 c=0 triggered=1,2',
    'duplicate-leave-during': 'a=1 b=1 c=0 triggered=1,2',
    'leave-self': 'a=1 b=1 c=0 triggered=1,2',
    'enter-during': 'a=1 b=0 c=0 triggered=1',
    'enter-next': 'a=2 b=0 c=1 triggered=1,3',
    'stop-during': 'a=1 b=0 c=0 triggered=1',
    'stop-next': 'a=1 b=0 c=0 triggered=1',
    'space-during': 'a=1 b=0 c=0 triggered=1',
}
BINDING_INCLUDES = ('pch.hpp', 'app.hpp', 'camera_app.hpp', 'client_camera.hpp', 'camera/base_camera.hpp',
                    'moo/render_context.hpp', 'pyscript/pyobject_plus.hpp', 'pyscript/script.hpp',
                    'pyscript/script_math.hpp', 'space/client_space.hpp', 'space/deprecated_space_helpers.hpp',
                    'cstdmf/bw_namespace.hpp')

SEAM_HOST = r'''
#include "deprecated_space_helpers.hpp"
#include <cstdio>
using namespace BW;
static void report(const char * name)
{
    std::printf("%s %u\n", name, unsigned(DeprecatedSpaceHelpers::cameraSpaceChanges()));
}
int main()
{
    ClientSpace a, b;
    report("initial");
    DeprecatedSpaceHelpers::cameraSpace(ClientSpacePtr(&a));
    report("first");
    DeprecatedSpaceHelpers::cameraSpace(ClientSpacePtr(&a));
    report("same-every-frame");
    DeprecatedSpaceHelpers::cameraSpace(ClientSpacePtr());
    DeprecatedSpaceHelpers::cameraSpace(ClientSpacePtr(&a));
    report("cleared-then-reused");
    DeprecatedSpaceHelpers::cameraSpace(ClientSpacePtr(&b));
    report("replaced");
    std::printf("current-is-b %d\n", DeprecatedSpaceHelpers::cameraSpace().get() == &b);
    return 0;
}
'''

# Only what deprecated_space_helpers.{hpp,cpp} use from the engine.
SEAM_DECLARATIONS = r'''
#pragma once
#include <cstddef>
#include <cstdint>
namespace BW
{
typedef uint32_t uint32;
class ClientSpace {};
class ClientSpacePtr
{
public:
    ClientSpacePtr(ClientSpace * object = 0) : object_(object) {}
    ClientSpace * get() const { return object_; }
private:
    ClientSpace * object_;
};
}
'''


def run_host(test, source, *flags):
    with tempfile.TemporaryDirectory() as directory:
        binary = Path(directory) / 'host'
        result = subprocess.run(['c++', '-std=c++17', '-O1', '-ffp-contract=off', '-Wall', '-Wextra',
                                 '-Werror', '-I', str(ROOT / 'bigworld'), *flags, str(source),
                                 '-o', str(binary)], capture_output=True, text=True)
        test.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        result = subprocess.run([str(binary)], capture_output=True, text=True)
        test.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        return dict(line.split(' ', 1) for line in result.stdout.splitlines())


def build_chain():
    """Engine-only patchers in tools/build_bigworld_client.ps1 order, this one after the traces."""
    text = (REPOSITORY / 'tools/build_bigworld_client.ps1').read_text()
    names = re.findall(r'Join-Path \$StandaloneRoot "(patch_[a-z_0-9]+\.py)"\),\s*\$EngineRoot\)', text)
    if PATCHER in names:
        assert names.index(PATCHER) > names.index('patch_stock_vehicle_traces.py')
        names.remove(PATCHER)
    return names, names.index('patch_stock_vehicle_traces.py') + 1


def run_patchers(names, engine):
    for name in names:
        subprocess.run([sys.executable, str(ROOT / 'bigworld' / name), str(engine)], check=True,
                       capture_output=True, env=dict(os.environ, PYTHONDONTWRITEBYTECODE='1'))


def tree_bytes(root):
    return {path.relative_to(root).as_posix(): path.read_bytes()
            for path in root.rglob('*') if path.is_file()}


class Engine(object):
    """Pinned public engine through the build chain, with and without the flock seam."""
    directory = None

    @classmethod
    def build(cls, test):
        if not ARCHIVE.is_file():
            test.skipTest('Pinned public BigWorld engine archive required')
        if cls.directory is not None:
            return cls
        cls.temporary = tempfile.TemporaryDirectory(prefix='flock-manager-engine-')
        work = Path(cls.temporary.name)
        prefix = subprocess.run(['tar', '-tzf', str(ARCHIVE)], check=True, capture_output=True,
                                text=True).stdout.split('/', 1)[0]
        members = [prefix + '/programming/bigworld/' + name for name in ENGINE_DIRECTORIES]
        subprocess.run(['tar', '-xzf', str(ARCHIVE), '-C', str(work)] + members, check=True)
        names, position = build_chain()
        cls.after_traces = work / 'after-traces'
        (work / prefix / 'programming/bigworld').rename(cls.after_traces)
        run_patchers(names[:position], cls.after_traces)
        cls.integrated = work / 'integrated'
        shutil.copytree(cls.after_traces, cls.integrated)
        run_patchers([PATCHER], cls.integrated)
        run_patchers(names[position:], cls.integrated)
        cls.reference = work / 'reference'
        shutil.copytree(cls.after_traces, cls.reference)
        run_patchers(names[position:], cls.reference)
        cls.directory = work
        return cls


class StockFlockFieldTest(unittest.TestCase):
    def test_core_contract_values(self):
        reports = run_host(self, TESTS / 'stock_flock_field_host.cpp')
        self.assertEqual(reports, EXPECTED_CORE)


class StockFlockPatchTest(unittest.TestCase):
    def setUp(self):
        self.engine = Engine.build(self)

    def text(self, tree, relative):
        return (tree / relative).read_text(encoding='utf-8')

    def test_seam_is_the_only_difference_at_the_end_of_the_chain(self):
        reference = tree_bytes(self.engine.reference)
        integrated = tree_bytes(self.engine.integrated)
        self.assertEqual(sorted(reference), sorted(integrated))
        changed = sorted(name for name in reference if reference[name] != integrated[name])
        self.assertEqual(changed, sorted(SEAM_FILES))
        for name, token, edits in patcher.FILES:
            before = self.text(self.engine.reference, name)
            expected = before
            for old, new in edits:
                self.assertEqual(expected.count(old), 1, (name, old))
                expected = expected.replace(old, new, 1)
            self.assertEqual(self.text(self.engine.integrated, name), expected, name)

    def test_world_tick_runs_flock_sweeps_once_in_the_scene_tick_scope(self):
        world = self.text(self.engine.integrated, 'client/world_app.cpp')
        tick = world[world.index('void WorldApp::tick('):world.index('void WorldApp::inactiveTick(')]
        self.assertEqual(world.count('tickStockFlockManagers();'), 1)
        self.assertLess(tick.index('ScopedDogWatch sdw( sceneTick );'), tick.index('tickStockFlockManagers();'))
        self.assertLess(tick.index('tickStockVehicleTraces();'), tick.index('tickStockFlockManagers();'))
        # inactiveTick reuses tick, so the hidden worker services the queue too.
        inactive = world[world.index('void WorldApp::inactiveTick('):]
        self.assertIn('this->tick( dGameTime, dRenderTime );', inactive[:inactive.index('\n}\n')])

    def test_patch_is_idempotent_and_fails_closed(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            for name in SEAM_FILES:
                (root / name).parent.mkdir(parents=True, exist_ok=True)
                shutil.copy(self.engine.reference / name, root / name)
            patcher.patch(root)
            once = {name: (root / name).read_bytes() for name in SEAM_FILES}
            for name in SEAM_FILES:
                self.assertEqual(once[name], (self.engine.integrated / name).read_bytes(), name)
            patcher.patch(root)
            self.assertEqual({name: (root / name).read_bytes() for name in SEAM_FILES}, once)
            # A partial seam writes nothing, including to the files that would apply.
            original = (self.engine.reference / SEAM_FILES[0]).read_bytes()
            for name in SEAM_FILES:
                shutil.copy(self.engine.reference / name, root / name)
            helpers = root / 'lib/space/deprecated_space_helpers.cpp'
            helpers.write_text(helpers.read_text() + '\n// cameraSpaceChanges\n')
            with self.assertRaises(ValueError):
                patcher.patch(root)
            self.assertEqual((root / SEAM_FILES[0]).read_bytes(), original)
            # A duplicated seam is not mistaken for an applied one, and nothing is written.
            for name in SEAM_FILES:
                shutil.copy(self.engine.integrated / name, root / name)
            world = root / 'client/world_app.cpp'
            world.write_text(world.read_text().replace('        tickStockFlockManagers();\n',
                                                       '        tickStockFlockManagers();\n' * 2))
            self.assertEqual(world.read_text().count('        tickStockFlockManagers();\n'), 2)
            duplicated = {name: (root / name).read_bytes() for name in SEAM_FILES}
            with self.assertRaises(ValueError):
                patcher.patch(root)
            self.assertEqual({name: (root / name).read_bytes() for name in SEAM_FILES}, duplicated)
            # A moved anchor fails instead of guessing a position.
            for name in SEAM_FILES:
                shutil.copy(self.engine.reference / name, root / name)
            world = root / 'client/world_app.cpp'
            world.write_text(world.read_text().replace('        tickStockVehicleTraces();\n', ''))
            with self.assertRaises(ValueError):
                patcher.patch(root)

    def test_patch_preserves_crlf_and_bom(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            for name in SEAM_FILES:
                (root / name).parent.mkdir(parents=True, exist_ok=True)
                text = (self.engine.reference / name).read_bytes().replace(b'\r\n', b'\n')
                (root / name).write_bytes(b'\xef\xbb\xbf' + text.replace(b'\n', b'\r\n'))
            patcher.patch(root)
            for name in SEAM_FILES:
                data = (root / name).read_bytes()
                expected = (self.engine.integrated / name).read_bytes().replace(b'\r\n', b'\n')
                self.assertEqual(data, b'\xef\xbb\xbf' + expected.replace(b'\n', b'\r\n'), name)
            mixed = root / SEAM_FILES[0]
            mixed.write_bytes(mixed.read_bytes() + b'\n')
            with self.assertRaises(ValueError):
                patcher.patch(root)

    def test_camera_space_setter_counts_only_replacements(self):
        with tempfile.TemporaryDirectory() as directory:
            work = Path(directory)
            for name in ('deprecated_space_helpers.hpp', 'deprecated_space_helpers.cpp'):
                shutil.copy(self.engine.integrated / 'lib/space' / name, work / name)
            (work / 'forward_declarations.hpp').write_text(SEAM_DECLARATIONS)
            shim = work / 'shim'
            for name in ('pch.hpp', 'chunk/chunk_manager.hpp', 'chunk/chunk_space.hpp',
                         'space/space_manager.hpp', 'space/client_space.hpp'):
                (shim / name).parent.mkdir(parents=True, exist_ok=True)
                (shim / name).write_text('#pragma once\n')
            host = work / 'host.cpp'
            host.write_text(SEAM_HOST)
            with tempfile.TemporaryDirectory() as output:
                binary = Path(output) / 'host'
                result = subprocess.run(['c++', '-std=c++17', '-Wall', '-Wextra', '-Werror', '-I', str(shim),
                                         '-I', str(work), str(host), str(work / 'deprecated_space_helpers.cpp'),
                                         '-o', str(binary)], capture_output=True, text=True)
                self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
                result = subprocess.run([str(binary)], capture_output=True, text=True, check=True)
        reports = dict(line.split(' ', 1) for line in result.stdout.splitlines())
        self.assertEqual(reports, {'initial': '0', 'first': '1', 'same-every-frame': '1',
                                   'cleared-then-reused': '3', 'replaced': '4', 'current-is-b': '1'})


class StockFlockBindingTest(unittest.TestCase):
    def setUp(self):
        self.source = (ROOT / 'bigworld/stock_flock_manager.cpp').read_text()

    def test_production_binding_samples_only_registered_providers(self):
        # The fakes in the host supply every engine include of the binding.
        with tempfile.TemporaryDirectory() as directory:
            for name in BINDING_INCLUDES:
                (Path(directory) / name).parent.mkdir(parents=True, exist_ok=True)
                (Path(directory) / name).write_text('#pragma once\n')
            reports = run_host(self, TESTS / 'stock_flock_binding_host.cpp', '-I', directory)
        self.assertEqual(reports, EXPECTED_BINDING)

    def test_method_and_attribute_tables_follow_1513(self):
        methods = self.source[self.source.index('PY_BEGIN_METHODS'):self.source.index('PY_END_METHODS')]
        self.assertEqual(re.findall(r'PY_METHOD\( (\w+) \)', methods),
                         ['vehicleEnter', 'vehicleLeave', 'start', 'stop', 'addFlock', 'addActivationPoint'])
        attributes = self.source[self.source.index('PY_BEGIN_ATTRIBUTES'):self.source.index('PY_END_ATTRIBUTES')]
        self.assertEqual(re.findall(r'PY_ATTRIBUTE\( (\w+) \)', attributes), ['onTriggerCallback'])
        self.assertIn('PY_FACTORY_NAMED( WGFlockManager, "WGFlockManager", BigWorld )', self.source)

    def test_every_entry_point_fences_the_started_space_first(self):
        for signature in ('void vehicleEnter( MatrixProviderPtr provider )\n',
                          'const Vector2 & explosionRadius, float respawnTime )\n',
                          'void addActivationPoint( const Vector2 & position )\n',
                          'void WGFlockManager::vehicleLeave( MatrixProviderPtr provider )\n',
                          'void WGFlockManager::start( float interval )\n'):
            lines = self.source[self.source.index(signature) + len(signature):].split('\n')
            self.assertEqual([lines[0].strip(), lines[1].strip()], ['{', 'fence();'], signature)
        tick = self.source[self.source.index('void WGFlockManager::tick('):]
        self.assertIn('if (fence() || !field_.due( now, frame )) return;', tick[:tick.index('\n}\n')])
        call = self.source.index('Script::call( callback.getObject(), args, "" );')
        self.assertEqual(self.source[call:].split('\n')[1].strip(), 'owner.fence();')

    def test_exact_1513_type_and_method_table(self):
        if not CLIENT:
            self.skipTest('WOT_0922_CLIENT is required for the exact #1513 executable')
        image = (Path(CLIENT) / 'WorldOfTanks.exe').read_bytes()
        header = struct.unpack_from('<I', image, 0x3c)[0]
        count = struct.unpack_from('<H', image, header + 6)[0]
        optional = struct.unpack_from('<H', image, header + 20)[0]
        base = struct.unpack_from('<I', image, header + 24 + 28)[0]
        sections = [struct.unpack_from('<IIII', image, header + 24 + optional + 40 * index + 8)
                    for index in range(count)]

        def offset(address):
            for unused, rva, size, raw in sections:
                if base + rva <= address < base + rva + size:
                    return raw + address - base - rva
            raise ValueError(hex(address))

        def word(address):
            return struct.unpack_from('<I', image, offset(address))[0]

        def string(address):
            start = offset(address)
            return image[start:image.index(b'\0', start)].decode('ascii')

        self.assertEqual(string(word(0x1a16d80 + 12)), 'WGFlockManager')
        self.assertEqual(word(0x1a16d80 + 4 * 6), 0x8a2b60)  # tp_dealloc stops first
        self.assertEqual(word(0x1a16d80 + 4 * 29), 0x1a16e78)  # tp_methods
        table, entries = 0x1a16e78, []
        while word(table):
            entries.append((string(word(table)), word(table + 4), word(table + 8)))
            table += 16
        self.assertEqual(entries, [('vehicleEnter', 0x8a2880, 3), ('vehicleLeave', 0x8a29a0, 3),
                                   ('start', 0x8a27a0, 3), ('stop', 0x8a2830, 3),
                                   ('addFlock', 0x8a25d0, 3), ('addActivationPoint', 0x8a2540, 3)])


def authored_space(client, name):
    """FlockExotic records from space.bin and chunk bounds from space.settings."""
    sys.path.insert(0, str(REPOSITORY / 'tools'))
    try:
        import packed_xml
    finally:
        sys.path.pop(0)
    with zipfile.ZipFile(Path(client) / 'res/packages' / (name + '.pkg')) as archive:
        settings = packed_xml.read_packed_xml(archive.read('spaces/%s/space.settings' % name))
        data = archive.read('spaces/%s/space.bin' % name)
    chunks = dict(next(child.value for key, child in settings.children if key == b'bounds').children)
    chunk = {key.decode(): item.value for key, item in chunks.items()}

    def value(item):
        if item.value_type == packed_xml.TYPE_ELEMENT:
            return {key.decode(): value(child) for key, child in item.value.children}
        if item.value_type == packed_xml.TYPE_VECTOR:
            return struct.unpack('<' + 'f' * (len(item.value) // 4), item.value)
        return item.value.decode(errors='replace') if isinstance(item.value, bytes) else item.value

    def walk(node, records):
        record = {key.decode(): value(child) for key, child in node.children}
        if record.get('type') == 'FlockExotic':
            records.append(record)
        for unused, child in node.children:
            if child.value_type == packed_xml.TYPE_ELEMENT:
                walk(child.value, records)

    records, at = [], data.find(packed_xml.PACKED_XML_MAGIC)
    while at >= 0:
        stream = io.BytesIO(data[at:])
        stream.seek(5)
        dictionary = []
        while True:
            key = packed_xml._read_cstring(stream)
            if not key:
                break
            dictionary.append(key)
        try:
            element, unused = packed_xml._read_element(stream, dictionary)
        except Exception:
            at = data.find(packed_xml.PACKED_XML_MAGIC, at + 4)
            continue
        walk(element, records)
        at = data.find(packed_xml.PACKED_XML_MAGIC, at + stream.tell())
    # 100 m chunks; y is not read by the manager beyond its min <= max check.
    bounds = [chunk['minX'] * 100.0, -1000.0, chunk['minY'] * 100.0,
              (chunk['maxX'] + 1) * 100.0, 1000.0, (chunk['maxY'] + 1) * 100.0]
    return {'name': name, 'bounds': bounds, 'records': records}


class StockFlockConsumerTest(unittest.TestCase):
    def test_exact_flock_manager_pyc_and_authored_maps(self):
        if not CLIENT:
            self.skipTest('WOT_0922_CLIENT is required for the exact #1513 FlockManager PYC')
        config = PY27.with_name('python2.7-config')
        package = Path(CLIENT) / 'res/packages/scripts.pkg'
        if not config.exists() or not package.is_file():
            self.skipTest('CPython 2.7 development files and the exact #1513 scripts.pkg are required')
        flags = [flag for flag in shlex.split(subprocess.check_output([str(config), '--cflags'], text=True))
                 if flag not in ('-DNDEBUG', '-Wstrict-prototypes')]
        libraries = shlex.split(subprocess.check_output([str(config), '--ldflags'], text=True))
        with tempfile.TemporaryDirectory() as directory:
            spaces = []
            for name in ('13_erlenberg', '17_munchen'):
                path = Path(directory) / (name + '.json')
                path.write_text(json.dumps(authored_space(CLIENT, name)))
                spaces.append(str(path))
            binary = Path(directory) / 'host'
            result = subprocess.run(['c++', '-std=c++17', '-O1', '-ffp-contract=off', *flags, '-Wall', '-Werror',
                                     '-Wno-register', '-Wno-deprecated-register', '-Wno-writable-strings',
                                     '-Wno-missing-field-initializers', '-I', str(ROOT / 'bigworld'),
                                     str(TESTS / 'stock_flock_python_host.cpp'), *libraries,
                                     '-L' + str(PY27.parents[1] / 'lib'), '-o', str(binary)],
                                    capture_output=True, text=True)
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
            result = subprocess.run([str(binary), str(TESTS / 'stock_flock_python_driver.py')],
                                    capture_output=True, text=True,
                                    env=dict(os.environ, WOT_SCRIPTS_PACKAGE=str(package),
                                             WOT_FLOCK_AUTHORED=os.pathsep.join(spaces),
                                             PYTHONHOME=str(PY27.parents[1])))
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        self.assertNotIn('Traceback', result.stderr)
        reports = dict(line.split(' ', 1) for line in result.stdout.splitlines())
        authored = {name: reports.pop(name) for name in list(reports)
                    if name.startswith(('13_erlenberg', '17_munchen'))}
        self.assertEqual(reports, EXPECTED_CONSUMER)
        self.assertEqual(authored, {
            '13_erlenberg-count': '19', '13_erlenberg-triggered': repr(list(range(19))),
            '13_erlenberg-respawn': '[400.0, 1000.0]', '13_erlenberg-cooldown-repeat': '1',
            '13_erlenberg-sweeps': '1',
            '17_munchen-count': '0', '17_munchen-triggered': '[]', '17_munchen-respawn': '[]',
            '17_munchen-sweeps': '1'})


if __name__ == '__main__':
    unittest.main()
