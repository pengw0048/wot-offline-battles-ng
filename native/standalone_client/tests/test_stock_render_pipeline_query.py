"""BigWorld.isForwardPipeline and the exact #1513 DecalMap consumer.

The original helpers/DecalMap module runs under Python 2.7 against a BigWorld
extension built from the patched public pyscript conversions and PY_AUTO macros,
the patched DynamicDecalManager owner and the production decal bindings.
"""
from pathlib import Path
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


def method(source, signature):
    start = source.index(signature)
    end = source.index('{', start)
    depth = 1
    while depth:
        end += 1
        depth += (source[end] == '{') - (source[end] == '}')
    return source[start:end + 1]


def patched_engine(directory):
    """Pristine archive plus every build-script patcher, as Root prepares the engine."""
    script = (REPOSITORY / 'tools/build_bigworld_client.ps1').read_text()
    names = re.findall(r'Join-Path \$StandaloneRoot "(patch_[a-z_0-9]+\.py)"\),\s*\$EngineRoot\)', script)
    with tarfile.open(ARCHIVE) as source:
        prefix = source.next().name.split('/')[0] + '/programming/bigworld/'
        wanted = [prefix + part for part in ('client', 'lib', 'third_party/python')]
        source.extractall(directory, members=[member for member in source.getmembers()
                                              if any(member.name.startswith(item) for item in wanted)])
    engine = Path(directory) / prefix
    for name in names:
        subprocess.run([sys.executable, str(ROOT / 'bigworld' / name), str(engine)], check=True,
                       capture_output=True, env=dict(os.environ, PYTHONDONTWRITEBYTECODE='1'))
    return engine


def host_source(engine):
    script = (engine / 'lib/pyscript/script.cpp').read_text()
    header = (engine / 'lib/pyscript/script.hpp').read_text()
    conversions = '\n'.join(method(script, signature) for signature in (
        'int Script::setData( PyObject * pObject, int & rInt,', 'int Script::setData( PyObject * pObject, double & rDouble,',
        'int Script::setData( PyObject * pObject, float & rFloat,', 'int Script::setData( PyObject * pObject, BW::string & rString,',
        'PyObject * Script::getData( const bool data )', 'PyObject * Script::getData( const int data )',
        'PyObject * Script::argCountError( const char * fn, int optas, int allas, ... )'))
    names = script[script.index('template <> const char * zeroValueName<int>()'):]
    names = 'namespace Script\n{\n' + names[:names.index('} // namespace Script')] + '}\n'
    links = header[header.index('#define PY_MODULE_FUNCTION( FUNC_NAME, MODULE_NAME )'):]
    links = links[:links.index('/// This macro defines an ordinary function and adds it to a module\n#define PY_MODULE_FUNCTION_WITH_KEYWORDS')]
    automatic = header[header.index('#define PY_AUTO_MODULE_FUNCTION( RET, FUNC_NAME, ARGS, MODULE_NAME )'):]
    automatic = automatic[:automatic.index('/// This macro defines an alias')]
    macros = header[header.index('// Section: Macros: Recursion utilities'):header.index('// Section: PyModuleLink')]
    decals = (engine / 'lib/moo/dynamic_decal.cpp').read_text()
    manager = '\n'.join(method(decals, signature) for signature in (
        'int DynamicDecalManager::addDecalGroup(', 'int DynamicDecalManager::textureIndex('))
    production = (ROOT / 'bigworld/stock_decal_resources.cpp').read_text()
    production = production[production.index('void setDamageStickerCriticalAngle(float angle)'):]
    production = production[:production.index('\n', production.index('PY_AUTO_MODULE_FUNCTION(RETDATA, wg_decalTextureIndex')) + 1]
    return ((ROOT / 'tests/stock_render_pipeline_query_host.cpp').read_text()
            .replace('@SCRIPT@', conversions + '\n' + names)
            .replace('@MACROS@', links + '\n' + automatic + '\n' + macros)
            .replace('@MANAGER@', manager).replace('@PRODUCTION@', production))


def packed_tree(element):
    sys.path.insert(0, str(REPOSITORY / 'tools'))
    import packed_xml
    kinds = {packed_xml.TYPE_INTEGER: 'int', packed_xml.TYPE_ELEMENT: 'element', packed_xml.TYPE_STRING: 'string'}

    def convert(value, children):
        kind = kinds.get(value.value_type, 'float' if isinstance(value.value, float) else 'other')
        raw = value.value.decode() if isinstance(value.value, bytes) else value.value
        result = {'value': [kind, raw if kind != 'element' else None], 'children': []}
        for name, child in children:
            name = name.decode() if isinstance(name, bytes) else name
            nested = child.value if child.value_type == packed_xml.TYPE_ELEMENT else None
            result['children'].append([name, convert(nested.value, nested.children) if nested else convert(child, ())])
        return result
    return convert(element.value, element.children)


def client_payload():
    sys.path.insert(0, str(REPOSITORY / 'tools'))
    import packed_xml
    config = packed_xml.read_packed_xml((Path(CLIENT) / 'res/scripts_config.xml').read_bytes())
    decal = next(child.value for name, child in config.children if (name.decode() if isinstance(name, bytes) else name) == 'decal')
    package = zipfile.ZipFile(str(Path(CLIENT) / 'res/packages/scripts.pkg'))
    chassis = packed_xml.read_packed_xml(package.read('scripts/item_defs/vehicles/common/chassis_effects.xml'))
    return {'decal': packed_tree(decal), 'chassis': packed_tree(chassis)}


def build_and_run(source, payload):
    flags = subprocess.run([PY27 + '-config', '--includes'], capture_output=True, text=True, check=True).stdout.split()
    with tempfile.TemporaryDirectory(prefix='render-pipeline-host-') as directory:
        directory = Path(directory)
        (directory / 'host.cpp').write_text(source)
        subprocess.run(['c++', '-std=c++14', '-Wall', '-Wextra', '-Werror', '-Wno-unused-parameter',
                        '-Wno-unused-function', '-Wno-missing-field-initializers', '-Wno-deprecated-declarations',
                        '-Wno-register', '-UNDEBUG', '-I' + str(ROOT / 'bigworld')] + flags +
                       ['-bundle', '-undefined', 'dynamic_lookup', str(directory / 'host.cpp'),
                        '-o', str(directory / 'BigWorld.so')], check=True)
        return subprocess.run([PY27, str(ROOT / 'tests/stock_render_pipeline_query_consumer.py'), CLIENT,
                               str(directory), json.dumps(payload)], capture_output=True, text=True)


def requirements(test):
    if not CLIENT or not PY27:
        test.skipTest('Pinned client and PY27 are required')
    if not ARCHIVE.exists():
        test.skipTest('Pristine public-engine archive is required')


class RenderPipelineQueryTests(unittest.TestCase):
    def test_original_decal_map_against_the_production_binding(self):
        requirements(self)
        with tempfile.TemporaryDirectory(prefix='render-pipeline-engine-') as directory:
            source = host_source(patched_engine(directory))
        payload = client_payload()
        run = build_and_run(source, payload)
        self.assertEqual(run.returncode, 0, run.stdout + run.stderr)
        result = json.loads(run.stdout)
        groups = [name for name, unused in payload['decal']['children'][1][1]['children']]
        buffers = [name for name, unused in
                   next(child for name, child in next(child for name, child in payload['chassis']['children']
                        if name == 'decals')['children'] if name == 'bufferPrefs')['children']]
        textures = [child['children'][0][1]['value'][1] for name, child in
                    next(child for name, child in payload['decal']['children'] if name == 'textures')['children']]

        deferred = result['deferred']
        self.assertEqual((deferred['forward'], deferred['isFalse'], deferred['type']), (False, True, 'bool'))
        self.assertEqual(deferred['errors'], [])
        # Each Avatar.__init__ re-registers every authored group; textures keep their first index.
        registered = [name for name, unused in deferred['afterGroups']['groups']]
        self.assertEqual(sorted(set(registered)), sorted(set(groups + buffers)))
        self.assertEqual(len(registered), 2 * len(set(groups + buffers)))
        lifetimes = dict(deferred['afterGroups']['groups'])
        self.assertEqual((lifetimes['slow'], lifetimes['medium'], lifetimes['fast']), (100.0, 50.0, 25.0))
        # Python 2 dict order decides registration order, as in the original consumer.
        self.assertEqual(sorted(deferred['afterGroups']['textures']), sorted(textures))
        self.assertEqual(sorted(deferred['indices'].values()), list(range(len(textures))))
        self.assertTrue(deferred['afterInit']['traces'])
        self.assertAlmostEqual(deferred['afterInit']['stickerRadians'], 30.0 * 3.1415927 / 180.0, places=6)

        forward = result['forward']
        self.assertEqual((forward['forward'], forward['isFalse'], forward['type']), (True, False, 'bool'))
        # The early return registers nothing and leaves every DecalMap index unset.
        self.assertEqual(forward['afterGroups']['groups'], deferred['afterGroups']['groups'])
        self.assertEqual(forward['afterGroups']['textures'], deferred['afterGroups']['textures'])
        self.assertEqual(set(forward['indices'].values()), {-1})
        self.assertEqual(len(forward['errors']), len(textures))
        self.assertTrue(all(entry[0] == 'LOG_ERROR' and 'Invalid texture name' in entry[1] for entry in forward['errors']))

        message = 'TypeError: isForwardPipeline() takes no arguments.'
        self.assertEqual(result['arguments'], {'(1,)': message, '(None,)': message, '(1, 2)': message})

    def test_production_binding_matches_the_original_entry(self):
        source = (ROOT / 'bigworld/stock_decal_resources.cpp').read_text()
        self.assertEqual(source.count('PY_AUTO_MODULE_FUNCTION(RETDATA, isForwardPipeline, END, BigWorld)'), 1)
        self.assertIn('Renderer::instance().pipelineType() == IRendererPipeline::TYPE_FORWARD_SHADING',
                      method(source, 'bool isForwardPipeline()'))
        self.assertNotIn('isDeferredPipeline', source)
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

        self.assertEqual(code(0x59b110, 0x59b121), ['push 0', 'push 0x650970', 'push 0x14b30e4', 'push 0x14994e4'])
        self.assertEqual(bytes(memory[0x14b30e4 - base:0x14b30f6 - base]), b'isForwardPipeline\0')
        self.assertEqual(memory.count(b'isDeferredPipeline\0'), 0)
        # PyTuple_Size == 0, Renderer::s_pInstance->m_type == 1, PyBool; otherwise argCountError(name, 0, 0).
        self.assertEqual(code(0x650970, 0x6509b1), [
            'push ebp', 'mov ebp, esp', 'push dword ptr [ebp + 0xc]', 'call 0xfb3610', 'add esp, 4',
            'test eax, eax', 'jne 0x65099e', 'mov ecx, dword ptr [0x1d20620]', 'call 0xa49a80', 'cmp eax, 1',
            'sete al', 'push eax', 'call 0x68ee00', 'add esp, 4', 'pop ebp', 'ret', 'push 0', 'push 0',
            'push 0x14b30e4', 'call 0x68e100', 'add esp, 0xc', 'pop ebp', 'ret'])
        self.assertEqual(code(0xa49a80, 0xa49a84), ['mov eax, dword ptr [ecx + 8]', 'ret'])
        self.assertEqual(code(0x68ee00, 0x68ee10), ['push ebp', 'mov ebp, esp', 'movzx eax, byte ptr [ebp + 8]',
                                                    'mov dword ptr [ebp + 8], eax', 'pop ebp', 'jmp 0xfc35f0'])
        self.assertEqual(code(0xfc35f0, 0xfc3608), ['push ebp', 'mov ebp, esp', 'cmp dword ptr [ebp + 8], 0',
                                                    'mov eax, 0x1a60974', 'mov ecx, 0x1a60980', 'cmovne eax, ecx',
                                                    'inc dword ptr [eax]', 'pop ebp', 'ret'])


if __name__ == '__main__':
    unittest.main()
