"""Exact #1513 scene-bank selection, original events and native callback order."""
from pathlib import Path
import importlib.util
import json
import os
import shutil
import subprocess
import struct
import sys
import tarfile
import tempfile
import unittest
import zipfile

from native_test_compile_flags import native_test_compile_flags
from test_vehicle_compound_nodes import block
from audit_wwise_bank import sound_id

ROOT = Path(__file__).resolve().parents[1]
spec = importlib.util.spec_from_file_location('audio_space_patch', ROOT / 'bigworld/patch_stock_audio_space.py')
patcher = importlib.util.module_from_spec(spec)
spec.loader.exec_module(patcher)


def stage_sdk(test, root):
    name = 'WOT_PUBLIC_ENGINE_ARCHIVE'
    archive = Path(os.environ.get(name, '/tmp/wot-bigworld-engine-14.4.1.tar.gz'))
    if not archive.is_file():
        if name in os.environ:
            test.fail('Configured public SDK archive is missing')
        test.skipTest('Pinned public SDK archive required')
    names = ['client/facade_app.cpp', 'client/script_bigworld.cpp',
             'client/connection_control.hpp', 'client/connection_control.cpp',
             'client/script_app.cpp', 'lib/space/space_manager.hpp',
             'lib/space/space_manager.cpp', 'lib/space/client_space.hpp',
             'lib/space/client_space.cpp']
    names += ['lib/compiled_space/' + name + ext for name in
              ('compiled_space', 'compiled_space_mapping', 'particle_system_loader', 'loader')
              for ext in ('.cpp', '.hpp')]
    with tarfile.open(archive) as tar:
        members = {m.name.split('/programming/bigworld/', 1)[1]: m for m in tar
                   if '/programming/bigworld/' in m.name}
        for name in names:
            target = root / name
            target.parent.mkdir(parents=True, exist_ok=True)
            target.write_bytes(tar.extractfile(members[name]).read())
    # This is the declared central build ordering. In particular audio must
    # follow the mapping and retirement owners, not rewrite their older bodies.
    script = (ROOT.parents[1] / 'tools/build_bigworld_client.ps1').read_text()
    patches = ('space_lifecycle', 'space_sounds', 'sound_zones', 'compiled_space_mapping', 'audio_space')
    positions = [script.index('patch_stock_' + name + '.py') for name in patches]
    test.assertEqual(positions, sorted(positions), 'Audio producer must run after mapping lifecycle patches')
    for name in patches[:-1]:
        spec = importlib.util.spec_from_file_location(name, ROOT / ('bigworld/patch_stock_' + name + '.py'))
        module = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(module)
        module.patch(root)


def resource_fixture(test, destination):
    path = client(test)
    sys.path.insert(0, str(ROOT.parents[1] / 'tools'))
    from packed_xml import read_packed_xml
    resources = []
    for name in ('33_fjord', '01_karelia', 'hangar_v2'):
        member = 'spaces/' + name + '/space.settings'
        with zipfile.ZipFile(path / ('res/packages/' + name + '.pkg')) as archive:
            section = read_packed_xml(archive.read(member))
        children = [key.decode() for key, value in section.children]
        test.assertNotIn('gameplayTypes', children)
        resources.append((member, children))
    with zipfile.ZipFile(path / 'res/packages/scripts.pkg') as archive:
        for name in ('33_fjord', '01_karelia'):
            member = 'scripts/arena_defs/' + name + '.xml'
            section = read_packed_xml(archive.read(member))
            children = [key.decode() for key, value in section.children]
            test.assertIn('gameplayTypes', children)
            test.assertIn('wwmusicSetup', children)
            resources.append((member, children))
        test.assertNotIn('scripts/arena_defs/hangar_v2.xml', archive.namelist())
    destination.write_text(''.join(name + ' ' + ' '.join(children) + '\n' for name, children in resources))


def client(test):
    value = os.environ.get('WOT_0922_CLIENT')
    if not value:
        test.skipTest('Exact #1513 client required')
    path = Path(value)
    test.assertTrue((path / 'WorldOfTanks.exe').is_file(), 'Invalid explicit WOT_0922_CLIENT')
    return path


def events(data):
    found = set()
    offset = 0
    while offset < len(data):
        tag, size = struct.unpack_from('<4sI', data, offset)
        offset += 8
        assert offset + size <= len(data)
        if tag == b'HIRC':
            count = struct.unpack_from('<I', data, offset)[0]
            at = offset + 4
            for unused in range(count):
                kind, length = struct.unpack_from('<BI', data, at)
                at += 5
                assert length >= 4 and at + length <= offset + size
                if kind == 4:
                    found.add(struct.unpack_from('<I', data, at)[0])
                at += length
            assert at == offset + size
        offset += size
    assert offset == len(data)
    return found


class AudioSpaceTests(unittest.TestCase):
    def test_patch_scope_idempotence_and_failure(self):
        with tempfile.TemporaryDirectory(prefix='audio-space-patch-') as temporary:
            root = Path(temporary)
            stage_sdk(self, root)
            before = {p: p.read_bytes() for p in root.rglob('*') if p.is_file()}
            patcher.patch(root)
            after = {p: p.read_bytes() for p in before}
            changed = {str(p.relative_to(root)) for p in before if before[p] != after[p]}
            self.assertEqual(changed, {'client/facade_app.cpp',
                'lib/compiled_space/compiled_space_mapping.hpp',
                'lib/compiled_space/compiled_space.hpp', 'lib/compiled_space/compiled_space.cpp'})
            patcher.patch(root)
            self.assertEqual(after, {p: p.read_bytes() for p in before})
            # Validation failure is atomic across all touched SDK files.
            for p, value in before.items():
                p.write_bytes(value)
            path = root / 'lib/compiled_space/compiled_space.cpp'
            path.write_text(path.read_text().replace('retired.swap( mappings_ );', 'changed.swap( mappings_ );'))
            snapshot = {p: p.read_bytes() for p in before}
            with self.assertRaises(ValueError):
                patcher.patch(root)
            self.assertEqual(snapshot, {p: p.read_bytes() for p in before})

    def test_actual_bank_diff_and_mapping_camera_notification_order(self):
        compiler = os.environ.get('CXX') or shutil.which('c++')
        if not compiler:
            self.skipTest('C++ compiler required')
        audio = (ROOT / 'bigworld/stock_audio.cpp').read_text()
        host = (ROOT / 'tests/stock_audio_space_host.cpp').read_text()
        for token, signature in (('BANK_DIFF', 'bool selectStockAudioBankSet('),
                                 ('SPACE_SELECTOR', 'void setStockAudioSpaceBankSet('),
                                 ('NOTIFY', 'void notifyStockAudioCameraSpace(')):
            host = host.replace('@' + token + '@', block(audio, signature))
        with tempfile.TemporaryDirectory(prefix='audio-space-native-') as temporary:
            root = Path(temporary)
            stage_sdk(self, root)
            fixture = root / 'resources.txt'
            resource_fixture(self, fixture)
            patcher.patch(root)
            cpp_source = (root / 'lib/compiled_space/compiled_space.cpp').read_text()
            for token, signature in (('SPACE_QUERY', 'BW::string CompiledSpace::stockAudioGeometryPath('),
                                     ('ADD_MAPPING', 'bool CompiledSpace::addMappingWithVisibilityMask('),
                                     ('DEL_MAPPING', 'void CompiledSpace::doDelMapping('),
                                     ('CLEAR_MAPPING', 'void CompiledSpace::doClear(')):
                host = host.replace('@' + token + '@', block(cpp_source, signature))
            host = host.replace('@MAPPING_ACCESS@', patcher.MAPPING_ACCESS)
            host = host.replace('@FACADE_TICK@', block((root / patcher.PATH).read_text(), 'void FacadeApp::tick('))
            command = [compiler, '-std=c++17', '-UNDEBUG', '-Wall', '-Wextra', '-Werror',
                       *native_test_compile_flags(compiler), '-I', str(ROOT / 'bigworld')]
            cpp, binary = root / 'host.cpp', root / 'host'
            cpp.write_text(host)
            subprocess.run(command + [str(cpp), '-o', str(binary)], check=True)
            subprocess.run([str(binary), str(fixture)], check=True)
            # Old resource producer: actual complete space.settings has no
            # gameplayTypes, so initial arena music/ambient still sees hangar.
            negative = host.replace('resource.empty() ? NULL :\n        BWResource::openSection(BW::string(resource.c_str(), resource.size()))',
                'BWResource::openSection(geometryPath + "/space.settings")')
            self.assertNotEqual(negative, host)
            cpp.write_text(negative)
            subprocess.run(command + [str(cpp), '-o', str(binary)], check=True)
            result = subprocess.run([str(binary), str(fixture)], capture_output=True, text=True)
            self.assertNotEqual(result.returncode, 0)
            # Correct resource alone cannot fix a camera selected before mapping.
            cpp.write_text(host.replace('\n\t\tnotifyStockAudioCameraSpace( this );', ''))
            subprocess.run(command + [str(cpp), '-o', str(binary)], check=True)
            result = subprocess.run([str(binary), str(fixture)], capture_output=True, text=True)
            self.assertNotEqual(result.returncode, 0)

    def test_exact_pyc_initial_music_definition_and_guards(self):
        path = client(self)
        interpreter = Path(os.environ.get('WOT_PYTHON27', '/Users/peng/.pyenv/versions/2.7.18/bin/python2.7'))
        if not interpreter.is_file():
            if 'WOT_PYTHON27' in os.environ:
                self.fail('Invalid explicit WOT_PYTHON27')
            self.skipTest('Pinned CPython 2.7 interpreter required')
        sys.path.insert(0, str(ROOT.parents[1] / 'tools'))
        from packed_xml import read_packed_xml
        with zipfile.ZipFile(path / 'res/packages/scripts.pkg') as archive:
            section = read_packed_xml(archive.read('scripts/arena_defs/33_fjord.xml'))
        children = dict(section.children)
        music = {key.decode(): value.value.decode() for key, value in children[b'wwmusicSetup'].value.children}
        ambient = children[b'wwambientSound'].value.decode()
        self.assertEqual(music['wwmusicLoading'], 'music_euro_vilage')
        script = r'''import json,sys,zipfile,marshal,types
package,definition,ambient=sys.argv[1:]
music=json.loads(definition)
z=zipfile.ZipFile(package)
module=marshal.loads(z.read('scripts/client/MusicControllerWWISE.pyc')[8:])
owner=next(k for k in module.co_consts if isinstance(k,types.CodeType) and k.co_name=='MusicController')
code=next(k for k in owner.co_consts if isinstance(k,types.CodeType) and k.co_name=='__getArenaSoundEvent')
assert code.co_firstlineno==404
class Object(object):pass
arenaType=Object();arenaType.wwmusicSetup=music;arenaType.ambientSound=ambient
arena=Object();arena.arenaType=arenaType
player=Object();player.arena=arena
world=Object();world.player=lambda:player
calls=[]
sounds=Object();sounds.getSound2D=lambda name:calls.append(name) or name
groups=Object();groups.g_instance=sounds
scope={'BigWorld':world,'isPlayerAvatar':lambda:True,'SoundGroups':groups,
 '_BATTLE_RESULT_MUSIC_EVENTS':(), 'MUSIC_EVENT_COMBAT_LOADING':41,'AMBIENT_EVENT_COMBAT':42}
function=types.FunctionType(code,scope)
assert function(None,41)==music['wwmusicLoading'] and calls==[music['wwmusicLoading']]
assert function(None,42)==ambient and calls[-1]==ambient
scope['isPlayerAvatar']=lambda:False
assert function(None,41) is None and len(calls)==2
scope['isPlayerAvatar']=lambda:True;player.arena=None
assert function(None,41) is None and len(calls)==2
print('Exact initial music/ambient producer and Avatar/arena guards PASS')
'''
        subprocess.run([str(interpreter), '-c', script, str(path / 'res/packages/scripts.pkg'),
                        json.dumps(music), ambient], check=True)

    def test_exact_bank_event_owners(self):
        path = client(self)
        with zipfile.ZipFile(path / 'res/packages/audioww.pkg') as archive:
            names = {Path(n).name.lower(): n for n in archive.namelist()}
            checks = (('wpn.bnk', 'wpn_small_PC'),
                      ('gun_reload.bnk', 'gun_rld_20_45mm'),
                      ('eng_gaz_aa.bnk', 'eng_gaz_aa_pc'))
            login_events = events(archive.read(names['hangar_v2.bnk']))
            for bank, event in checks:
                event_id = sound_id(event)
                self.assertIn(event_id, events(archive.read(names[bank])))
                self.assertNotIn(event_id, login_events)

    def test_exact_default_voice_owner_and_python_language_boundary(self):
        path = client(self)
        sys.path.insert(0, str(ROOT.parents[1] / 'tools'))
        from packed_xml import read_packed_xml
        root = read_packed_xml((path / 'res/engine_config.xml').read_bytes())
        sound = dict(root.children)[b'soundMgr'].value
        active = dict(sound.children)[b'WWISE_active_profile'].value
        profile = dict(sound.children)[active].value
        voice = dict(profile.children)[b'voice_soundbanks'].value
        banks = [dict(project.value.children)[b'name'].value.decode()
                 for name, project in voice.children if name == b'project']
        self.assertEqual(banks, ['voiceover.bnk'])
        voice_events = events((path / 'res/audioww' / banks[0]).read_bytes())
        with zipfile.ZipFile(path / 'res/packages/gui.pkg') as archive:
            notifications = read_packed_xml(archive.read('gui/sound_notifications.xml'))
        for name in (b'start_battle', b'enemy_killed_by_player',
                     b'enemy_hp_damaged_by_projectile_and_chassis_damaged_by_player'):
            voice = dict(dict(notifications.children)[name].value.children)[b'voice'].value
            event = dict(voice.children)[b'wwsound'].value.decode()
            self.assertIn(sound_id(event), voice_events, event)
        interpreter = Path(os.environ.get('WOT_PYTHON27', '/Users/peng/.pyenv/versions/2.7.18/bin/python2.7'))
        if not interpreter.is_file():
            self.skipTest('Pinned CPython 2.7 interpreter required')
        script = r'''import marshal,sys,types,zipfile
z=zipfile.ZipFile(sys.argv[1])
module=marshal.loads(z.read('scripts/client/SoundGroups.pyc')[8:])
owner=next(c for c in module.co_consts if isinstance(c,types.CodeType) and c.co_name=='SoundModes')
codes={c.co_name:c for c in owner.co_consts if isinstance(c,types.CodeType)}
class Object(object):pass
calls=[]
wwise=Object();wwise.setLanguage=lambda language:calls.append(language) or True
default=Object();default.name='default';default.voiceLanguage=''
german=Object();german.name='DE';german.voiceLanguage='germany'
folder=Object();folder.values=lambda:[]
resources=Object();resources.openSection=lambda name:folder
def unexpected(*args):raise AssertionError(args)
scope={'__builtins__':__builtins__,'WWISE':wwise,'ResMgr':resources,'LOG_CURRENT_EXCEPTION':unexpected,
       'LOG_ERROR':unexpected,'LOG_WARNING':unexpected,'LOG_DEBUG':lambda *args:None}
class Modes(object):
 DEFAULT_MODE_NAME='default';MEDIA_PATH='audioww'
 _SoundModes__MODES_FOLDER='gui/soundModes/'
 _SoundModes__MODES_FILENAME='main_sound_modes.xml'
 # Resource descriptors are the boundary; original initialization and mode
 # transition bytecode, including its default-language early return, run intact.
 def _SoundModes__readSoundModesConfig(self,*args):return [default,german],[]
for name in ('__init__','setMode','__setMode'):
 setattr(Modes,'_SoundModes'+name if name=='__setMode' else name,types.FunctionType(codes[name],scope))
scope['SoundModes']=Modes
mode=Modes('default')
assert mode._SoundModes__currentMode=='default' and calls==[]
assert mode.setMode('DE') is True and calls==['germany']
assert mode.setMode('DE') is True and calls==['germany']
assert mode.setMode('default') is True and calls==['germany','']
print('Exact SoundModes default startup and explicit language transitions PASS')
'''
        subprocess.run([str(interpreter), '-c', script, str(path / 'res/packages/scripts.pkg')], check=True)

    def test_exact_native_startup_loads_voice_after_loadonce(self):
        path = client(self)
        from stock_battle_gui_support import Executable
        executable = Executable(path / 'WorldOfTanks.exe')
        self.assertEqual(executable.cstr(executable.word(0x12562d4)), 'voice_soundbanks')
        self.assertEqual(executable.code(0x12562e0, 6).hex(), '8d8768010000')
        self.assertEqual(executable.cstr(executable.word(0x12561cc)), 'SFX_soundbanks_loadonce')
        self.assertEqual(executable.code(0x12561b1, 6).hex(), '8db738010000')
        for dependency in ('pefile', 'unicorn'):
            if importlib.util.find_spec(dependency) is None:
                self.skipTest('Optional x86 oracle dependency unavailable: ' + dependency)
        import unicorn
        from unicorn.x86_const import UC_X86_REG_ECX
        from stock_flash_input_mode_oracle import OriginalFlashInputMode, MARKS
        oracle = OriginalFlashInputMode(path / 'WorldOfTanks.exe')
        calls = []
        owner = MARKS + 0x3000
        def load(address):
            self.assertEqual(oracle.u.reg_read(UC_X86_REG_ECX), owner)
            calls.append((address, oracle._arg(1)-owner, oracle._arg(2)))
            oracle._return(0, 8)
        for address in (0x1254e10, 0x1255480):
            oracle.hooks[address] = lambda address=address: load(address)
            oracle.u.hook_add(unicorn.UC_HOOK_CODE, oracle._hook, begin=address, end=address)
        # 01247668 calls this application startup owner. 012562d3 maps
        # voice_soundbanks to +0x168; 012561cb maps loadonce to +0x138.
        oracle._call(0x12559c0, owner)
        self.assertEqual(calls, [(0x1254e10, 0x138, 1), (0x1255480, 0x168, 1),
                                (0x1255480, 0x80, 1), (0x1254e10, 0x118, 1)])

    def test_exact_native_arena_resource_formatter(self):
        path = client(self)
        for dependency in ('pefile', 'unicorn'):
            if importlib.util.find_spec(dependency) is None:
                self.skipTest('Optional x86 oracle dependency unavailable: ' + dependency)
        compiler = os.environ.get('CXX') or shutil.which('c++')
        if not compiler:
            self.skipTest('C++ compiler required')
        import unicorn
        from unicorn.x86_const import UC_X86_REG_EAX, UC_X86_REG_EBP, UC_X86_REG_ECX, UC_X86_REG_EDI, UC_X86_REG_EIP
        from stock_flash_input_mode_oracle import OriginalFlashInputMode, MARKS, SENTINEL
        oracle = OriginalFlashInputMode(path / 'WorldOfTanks.exe')
        frame, vector, output = MARKS + 0x3000, MARKS + 0x4000, MARKS + 0x5000
        cursor = [MARKS + 0x6000]
        def write_string(address, value):
            raw = value.encode()
            if len(raw) < 16:
                value = raw + b'\0' * (16-len(raw)) + struct.pack('<II', len(raw), 15)
            else:
                memory = cursor[0]
                cursor[0] += 0x100
                oracle.u.mem_write(memory, raw + b'\0')
                value = struct.pack('<IIIIII', memory, 0, 0, 0, len(raw), len(raw))
            oracle.u.mem_write(address, value)
        def read_string(address):
            data = bytes(oracle.u.mem_read(address, 24))
            length, capacity = struct.unpack_from('<II', data, 16)
            if capacity >= 16:
                address = struct.unpack_from('<I', data)[0]
            return bytes(oracle.u.mem_read(address, length)).decode()
        def reserve():
            oracle._return(oracle.u.reg_read(UC_X86_REG_ECX), 4)
        def append():
            address = oracle.u.reg_read(UC_X86_REG_ECX)
            value = bytes(oracle.u.mem_read(oracle._arg(1), oracle._arg(2))).decode()
            write_string(address, read_string(address) + value)
            oracle._return(address, 8)
        def finish():
            oracle.u.reg_write(UC_X86_REG_EAX, 0)
            oracle.u.reg_write(UC_X86_REG_EIP, SENTINEL)
        for address, callback in ((0x623910, reserve), (0x607ea0, append),
                                  (0x11e7c30, lambda: oracle._return(0)), (0x1246cdb, finish)):
            oracle.hooks[address] = callback
            oracle.u.hook_add(unicorn.UC_HOOK_CODE, oracle._hook, begin=address, end=address)
        # Slash-token construction is the explicit regex primitive boundary;
        # original vector count/second component/SSO/concat/move run unchanged.
        cases = ('', '33_fjord', 'spaces/', '/', 'spaces//', '/33_fjord',
                 'spaces/33_fjord', 'spaces/33_fjord/',
                 'spaces/01_karelia', 'spaces/hangar_v2', 'spaces/33_fjord/suffix',
                 'spaces/Case_Sensitive_Map_With_Long_Name', 'other/33_fjord')
        with tempfile.TemporaryDirectory(prefix='audio-path-original-') as temporary:
            root = Path(temporary)
            cpp, binary = root / 'path.cpp', root / 'path'
            cpp.write_text('#include <iostream>\n#include "stock_audio_space_path.hpp"\n'
                'int main(int n,char**v){if(n!=2)return 2;std::cout<<wot::stockAudioArenaResource(v[1]);}\n')
            subprocess.run([compiler, '-std=c++17', '-Wall', '-Wextra', '-Werror',
                '-I', str(ROOT / 'bigworld'), str(cpp), '-o', str(binary)], check=True)
            for geometry in cases:
                tokens = geometry.split('/')
                if tokens[-1] == '':
                    tokens.pop()
                write_string(output, '')
                for index, token in enumerate(tokens):
                    write_string(vector + index * 24, token)
                oracle.u.mem_write(frame - 0x4c, struct.pack('<II', vector, vector + len(tokens)*24))
                oracle.u.reg_write(UC_X86_REG_EBP, frame)
                oracle.u.reg_write(UC_X86_REG_EDI, output)
                oracle._call(0x1246bdc, 0)
                result = subprocess.check_output([str(binary), geometry], text=True)
                self.assertEqual(result, read_string(output), geometry)

    def test_exact_native_bank_group_selection(self):
        path = client(self)
        for dependency in ('pefile', 'unicorn'):
            if importlib.util.find_spec(dependency) is None:
                self.skipTest('Optional x86 oracle dependency unavailable: ' + dependency)
        import unicorn
        from unicorn.x86_const import UC_X86_REG_EAX, UC_X86_REG_EBP, UC_X86_REG_EBX
        from stock_flash_input_mode_oracle import OriginalFlashInputMode, MARKS
        oracle = OriginalFlashInputMode(path / 'WorldOfTanks.exe')
        selected = []
        def boundary():
            value = oracle.u.reg_read(UC_X86_REG_EBX)
            selected.append(value)
            oracle._return(value)
        oracle.hooks[0x1255deb] = boundary
        oracle.u.hook_add(unicorn.UC_HOOK_CODE, oracle._hook, begin=0x1255deb, end=0x1255deb)
        for login in (0, 1):
            for gameplay_present in (0, 1):
                oracle.u.reg_write(UC_X86_REG_EBP, MARKS + 0x2000)
                oracle.u.mem_write(MARKS + 0x200c, struct.pack('<I', login))
                oracle.u.reg_write(UC_X86_REG_EAX, gameplay_present)
                result = oracle._call(0x1255dcf, 0)
                self.assertEqual(result, 0x144 if login else 0x15c if gameplay_present else 0x150)
        self.assertEqual(selected, [0x150, 0x15c, 0x144, 0x144])


if __name__ == '__main__':
    unittest.main()
