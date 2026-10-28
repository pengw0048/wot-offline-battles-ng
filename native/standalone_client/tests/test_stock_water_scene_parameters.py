"""BigWorld.wg_setWaterTexScale/FreqX/FreqZ from the arena reader to the water render state.

The exact #1513 ArenaType reader and Avatar init step run under Python 2.7; their
recorded values drive the production setters and the patched Water::setupRenderState.
"""
from pathlib import Path
import io
import json
import os
import re
import subprocess
import sys
import tarfile
import tempfile
import unittest
import zipfile

ROOT = Path(__file__).resolve().parents[1]
REPOSITORY = ROOT.parents[1]
CLIENT = os.environ.get('WOT_0922_CLIENT')
PY27 = os.environ.get('PY27')
ARCHIVE = Path(os.environ.get('WOT_PUBLIC_ENGINE_ARCHIVE', '/tmp/wot-bigworld-engine-14.4.1.tar.gz'))
SETTERS = ('wg_setWaterTexScale', 'wg_setWaterFreqX', 'wg_setWaterFreqZ')


def method(source, signature):
    start = source.index(signature)
    end = source.index('{', start)
    depth = 1
    while depth:
        end += 1
        depth += (source[end] == '{') - (source[end] == '}')
    return source[start:end + 1]


def arena_tree(element):
    sys.path.insert(0, str(REPOSITORY / 'tools'))
    import packed_xml
    kinds = {packed_xml.TYPE_INTEGER: 'int', packed_xml.TYPE_ELEMENT: 'element'}

    def convert(value, children):
        kind = kinds.get(value.value_type, 'float' if isinstance(value.value, float) else 'other')
        result = {'value': [kind, value.value if isinstance(value.value, (int, float)) else None], 'children': {}}
        for name, child in children:
            name = name.decode() if isinstance(name, bytes) else name
            nested = child.value if child.value_type == packed_xml.TYPE_ELEMENT else None
            result['children'][name] = convert(nested.value, nested.children) if nested else convert(child, ())
        return result
    return convert(element.value, element.children)


def original_entries():
    """Run ArenaType.__readGeometryCfg and PlayerAvatar.__onInitStepCompleted from scripts.pkg."""
    sys.path.insert(0, str(REPOSITORY / 'tools'))
    import packed_xml
    package = zipfile.ZipFile(str(Path(CLIENT) / 'res/packages/scripts.pkg'))
    cases = [{'name': name, 'tree': arena_tree(packed_xml.read_packed_xml(
        package.read('scripts/arena_defs/%s.xml' % name)))} for name in ('01_karelia', '04_himmelsdorf')]
    cases.append({'name': 'authored-signs', 'tree': {'value': ['element', None], 'children': {'water': {
        'value': ['element', None], 'children': {'texScale': {'value': ['float', -2.5], 'children': {}},
                                                 'freqX': {'value': ['int', 3], 'children': {}},
                                                 'freqZ': {'value': ['float', 0.125], 'children': {}}}}}}})
    result = subprocess.run([PY27, str(ROOT / 'tests/stock_water_scene_parameters_consumer.py'), CLIENT,
                             json.dumps(cases)], capture_output=True, text=True)
    if result.returncode:
        raise AssertionError(result.stderr)
    return json.loads(result.stdout)


def patched_water():
    """Patched romp/water.cpp from the pristine archive and the build-script patcher chain."""
    script = (REPOSITORY / 'tools/build_bigworld_client.ps1').read_text()
    names = re.findall(r'Join-Path \$StandaloneRoot "(patch_[a-z_0-9]+\.py)"\),\s*\$EngineRoot\)', script)
    with tempfile.TemporaryDirectory(prefix='water-scene-engine-') as directory:
        with tarfile.open(ARCHIVE) as source:
            prefix = source.next().name.split('/')[0] + '/programming/bigworld/'
            wanted = [prefix + part for part in ('client', 'lib', 'third_party/python')]
            source.extractall(directory, members=[member for member in source.getmembers()
                                                  if any(member.name.startswith(item) for item in wanted)])
        engine = Path(directory) / prefix
        for name in names:
            subprocess.run([sys.executable, str(ROOT / 'bigworld' / name), str(engine)], check=True,
                           capture_output=True, env=dict(os.environ, PYTHONDONTWRITEBYTECODE='1'))
        return (engine / 'lib/romp/water.cpp').read_text()


class WaterSceneParameterTests(unittest.TestCase):
    def test_production_registers_one_float_per_setter(self):
        source = (ROOT / 'bigworld/stock_water_parameters.cpp').read_text()
        for name, static in zip(SETTERS, ('s_fTexScale', 's_fFreqX', 's_fFreqZ')):
            self.assertEqual(source.count('PY_AUTO_MODULE_FUNCTION(RETVOID, %s, ARG(float, END), BigWorld)' % name), 1)
            self.assertIn('Water::%s = value;' % static, method(source, 'void %s(float value)' % name))

    def test_original_entry_values_reach_the_patched_water_render_state(self):
        if not CLIENT or not PY27:
            self.skipTest('Pinned client and PY27 are required')
        if not ARCHIVE.exists():
            self.skipTest('Pristine public-engine archive is required')
        entries = original_entries()
        values = {entry['name']: entry['values'] for entry in entries}
        self.assertEqual(values['01_karelia'], {'waterTexScale': 0.0, 'waterFreqX': 0.0, 'waterFreqZ': 0.0})
        self.assertEqual(values['04_himmelsdorf'], {'waterTexScale': 0.5, 'waterFreqX': 1.0, 'waterFreqZ': 1.0})
        self.assertEqual(values['authored-signs'], {'waterTexScale': -2.5, 'waterFreqX': 3.0, 'waterFreqZ': 0.125})
        arguments = []
        for entry in entries:
            # One positional float each, after worldDrawEnabled and before ProjectileMover.setSpaceID.
            self.assertEqual([call[0] for call in entry['water']], list(SETTERS))
            self.assertTrue(all(len(call) == 2 for call in entry['water']))
            self.assertTrue(entry['after_world_draw'] and entry['before_projectiles'])
            arguments += [call[1] for call in entry['water']]
        water = patched_water()
        self.assertEqual(water.count('paramCache_->setFloat("texScale",s_fTexScale);'), 1)
        self.assertEqual(len(re.findall(r'\bs_fFreq[XZ]\b', water)), 2)
        statics = '\n'.join(line for line in water.splitlines()
                            if re.match(r'float\s+Water::s_f(TexScale|FreqX|FreqZ) =', line))
        production = (ROOT / 'bigworld/stock_water_parameters.cpp').read_text()
        host = ((ROOT / 'tests/stock_water_scene_parameters_host.cpp').read_text()
                .replace('@STATICS@', statics)
                .replace('@SETUP@', method(water, 'void Water::setupRenderState( float dTime )'))
                .replace('@PRODUCTION@', '\n'.join(method(production, 'void %s(float value)' % name) for name in SETTERS)))
        with tempfile.TemporaryDirectory(prefix='water-scene-host-') as directory:
            directory = Path(directory)
            (directory / 'host.cpp').write_text(host)
            subprocess.run(['c++', '-std=c++14', '-Wall', '-Wextra', '-Werror', '-Wno-unused-parameter', '-UNDEBUG',
                            '-ffp-contract=off', str(directory / 'host.cpp'), '-o', str(directory / 'host')], check=True)
            result = subprocess.run([str(directory / 'host')] + arguments, capture_output=True, text=True)
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual(result.stdout.count(' published'), len(entries))
        self.assertIn('water scene parameters passed', result.stdout)

    def test_original_setters_store_public_water_statics(self):
        try:
            import capstone
            import pefile
        except ImportError:
            self.skipTest('pefile and capstone are required')
        if not CLIENT:
            self.skipTest('Pinned client is required')
        image = pefile.PE(str(Path(CLIENT) / 'WorldOfTanks.exe'), fast_load=True)
        base, memory = image.OPTIONAL_HEADER.ImageBase, image.get_memory_mapped_image()
        engine = capstone.Cs(capstone.CS_ARCH_X86, capstone.CS_MODE_32)

        def code(address, end):
            return [(item.mnemonic + ' ' + item.op_str).strip()
                    for item in engine.disasm(memory[address - base:end - base], address)]

        def references(value):
            pattern, found, index = value.to_bytes(4, 'little'), [], memory.find(value.to_bytes(4, 'little'))
            while index != -1:
                found.append(index + base)
                index = memory.find(pattern, index + 1)
            return found

        statics = {}
        for name, register, entry, static, default in (
                ('wg_setWaterTexScale', 0x59c070, 0x654a60, 0x1a1e3f0, 0.5),
                ('wg_setWaterFreqX', 0x59c010, 0x654960, 0x1a1e3f4, 1.0),
                ('wg_setWaterFreqZ', 0x59c040, 0x6549e0, 0x1a1e3f8, 1.0)):
            listing = code(register, register + 0x16)
            self.assertEqual(listing[:2], ['push 0', 'push 0x%x' % entry])
            self.assertEqual(listing[3], 'push 0x14994e4')  # "BigWorld"
            label = int(listing[2].split()[-1], 16)
            self.assertEqual(bytes(memory[label - base:label - base + len(name) + 1]), name.encode() + b'\0')
            body = code(entry, entry + 0x76)
            # Exactly one argument, the shared PY_AUTO float converter, one static store, None.
            for expected in ('call 0xfb3610', 'add eax, -1', 'call 0x68fba0', 'xor eax, eax',
                             'movss dword ptr [0x%x], xmm0' % static, 'inc dword ptr [0x1a5c798]',
                             'mov eax, 0x1a5c798', 'call 0x68e100'):
                self.assertIn(expected, body)
            self.assertEqual(int.from_bytes(memory[static - base:static - base + 4], 'little'),
                             int.from_bytes(__import__('struct').pack('<f', default), 'little'))
            statics[name] = references(static)
        # texScale has one reader, published by name; neither frequency static is read.
        self.assertEqual(len(statics['wg_setWaterTexScale']), 2)
        reader = [address for address in statics['wg_setWaterTexScale'] if not 0x654a60 <= address < 0x654ad6][0]
        publish = code(reader - 4, reader + 0x20)
        self.assertIn('movss xmm0, dword ptr [0x1a1e3f0]', publish)
        self.assertIn('push 0x1504f5c', publish)
        self.assertEqual(bytes(memory[0x1504f5c - base:0x1504f65 - base]), b'texScale\0')
        self.assertEqual(len(statics['wg_setWaterFreqX']), 1)
        self.assertEqual(len(statics['wg_setWaterFreqZ']), 1)
        # The shader frequencies come from per-surface state +0xf8/+0xfc.
        for address in (0xa298e3, 0xa29907):
            self.assertRegex(code(address, address + 8)[0], r'dword ptr \[edi \+ 0xf[8c]\]')
        shaders = zipfile.ZipFile(str(Path(CLIENT) / 'res/packages/shaders.pkg'))
        for member in ('shaders/water/water.10.dx9.fxo', 'shaders/water/water.11.dx9.fxo'):
            effect = zipfile.ZipFile(io.BytesIO(shaders.read(member))).read('effect')
            for parameter in (b'texScale', b'freqX', b'freqZ'):
                self.assertIn(parameter, effect)


if __name__ == '__main__':
    unittest.main()
