"""Original UDOS/PYC, complete x86 classifier and the real zone/audio consumer."""
import importlib.util
import json
import math
import os
from pathlib import Path
import random
import re
import shlex
import struct
import subprocess
import sys
import tarfile
import tempfile
import unittest
import xml.etree.ElementTree as ET
import zipfile
from native_test_compile_flags import native_test_compile_flags

ROOT = Path(__file__).resolve().parents[1]


def module(path):
    spec = importlib.util.spec_from_file_location(path.stem, path)
    result = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(result)
    return result


def sdk_files(names):
    archive = Path(os.environ.get('WOT_PUBLIC_ENGINE_ARCHIVE', '/tmp/wot-bigworld-engine-14.4.1.tar.gz'))
    if not archive.exists(): raise unittest.SkipTest('Pinned public engine archive required')
    output = {}
    with tarfile.open(archive) as tar:
        for member in tar:
            for name in names:
                if member.name.endswith('/programming/bigworld/' + name):
                    output[name] = tar.extractfile(member).read().decode('utf-8-sig')
            if len(output) == len(names): break
    assert len(output) == len(names)
    return output


class SoundZoneTest(unittest.TestCase):
    def test_original_classifier_production_owner_and_pyc_consumers(self):
        client = os.environ.get('WOT_0922_CLIENT')
        if not client: self.skipTest('WOT_0922_CLIENT required')
        if not all(importlib.util.find_spec(n) for n in ('pefile', 'unicorn')):
            self.skipTest('Original x86 oracle dependencies required')
        python = Path(os.environ.get('WOT_PYTHON27', '/Users/peng/.pyenv/versions/2.7.18/bin/python2.7'))
        config = python.with_name('python2.7-config')
        if not config.exists(): self.skipTest('CPython 2.7 development files required')
        from test_stock_vector4_basic import block
        from test_stock_hangar_presentation import between
        from stock_sound_zones_oracle import OriginalSoundZones
        values = sdk_files(('lib/pyscript/script.hpp', 'lib/pyscript/script.cpp', 'lib/cstdmf/event.hpp'))
        header, source = values['lib/pyscript/script.hpp'], values['lib/pyscript/script.cpp']
        macros = between(header, '// Section: Macros: Module Links', '// Section: Macros: Enums', False)
        macros += between(header, '// Section: Macros: Recursion utilities', '// Section: PyModuleLink', False)
        converters = ''.join(block(source, 'int Script::setData( PyObject * pObject, ' + name)
            for name in ('double & rDouble,', 'float & rFloat,', 'BW::string & rString,', 'Vector2 & rVector,'))
        converters += block(source, 'PyObject * Script::argCountError(')
        event = values['lib/cstdmf/event.hpp'].split('BW_BEGIN_NAMESPACE', 1)[1].split('BW_END_NAMESPACE', 1)[0]
        audio = (ROOT / 'bigworld/stock_audio.cpp').read_text()
        bridges = '\n'.join(block(audio, name) for name in (
            'unsigned stockSoundZoneNameID(', 'void postStockSoundZoneEvent(', 'bool setStockAudioAuxiliarySend('))
        host = (ROOT / 'tests/stock_sound_zones_host.cpp').read_text()
        host = host.replace('@SDK_EVENT@', event).replace('@AUDIO_BRIDGES@', bridges)
        host = host.replace('@SCRIPT_BODIES@', converters).replace('@PYAUTO_MACROS@', macros)
        with tempfile.TemporaryDirectory(prefix='sound-zone-production-') as folder:
            root = Path(folder)
            oracle = OriginalSoundZones(Path(client) / 'WorldOfTanks.exe')
            random_source = random.Random(1513)
            cases = []
            def f(value): return struct.unpack('<f', struct.pack('<f', value))[0]
            for i in range(2600):
                x, z = [f(random_source.uniform(-500, 500)) for unused in range(2)]
                hx, hz = [f(random_source.uniform(1, 100)) for unused in range(2)]
                angle = random_source.choice((0, .25, -.25, 1.5707963267948966, -1.5707963267948966, 3.14))
                sine, cosine = f(math.sin(angle)), f(math.cos(angle))
                camera = (f(x + random_source.uniform(-hx*1.2, hx*1.2)), f(i),
                          f(z + random_source.uniform(-hz*1.2, hz*1.2)))
                if i % 23 == 0: camera = (x, 0, z)
                elif i % 23 == 1: camera = (f(x+hx), 0, z)
                elif i % 23 == 2: camera = (x, 0, f(z+hz))
                elif i % 23 == 3: camera = (float('nan'), 0, z)
                elif i % 23 == 4: hx = float('nan')
                elif i % 23 == 5: hx = -hx
                radius = f(max(hx, hz) ** 2)
                record = struct.pack('<4f3I4f', x,z,hx,hz,11,12,13,sine,cosine,radius,.75)
                oracle.load([record])
                selected, trace = oracle.tick(camera)
                self.assertEqual(trace, [('event', 11), ('aux', 13, 0x3f400000)] if selected == 0 else [])
                cases.append(record + struct.pack('<3fi', *camera, int(selected == 0)))
            # First match owns overlaps; normal exit precedes entry and aux.
            a = struct.pack('<4f3I4f',0,0,10,10,1,2,3,0,1,100,1)
            b = struct.pack('<4f3I4f',0,0,20,20,4,5,6,0,1,400,.5)
            oracle.load([a,b])
            self.assertEqual(oracle.tick((0,0,0)),(0,[('event',1),('aux',3,0x3f800000)]))
            self.assertEqual(oracle.tick((15,0,0)),(1,[('event',2),('event',4),('aux',6,0x3f000000)]))
            self.assertEqual(oracle.tick((100,0,0)),(-1,[('event',5),('default',)]))
            (root / 'geometry.bin').write_bytes(struct.pack('<I',len(cases))+b''.join(cases))
            rows = self.original_udo_rows(Path(client))
            self.assertEqual(len(rows), 14)
            from prepare_native_client import OriginalResources
            from prepare_native_vehicle import xml_root
            original = OriginalResources(Path(client))
            defaults = {}
            for name in original.index:
                if name.startswith('scripts/arena_defs/') and name.endswith('.xml'):
                    section = xml_root(original.read(name)).find('wwDefaultReverb')
                    if section is not None:
                        defaults[name] = (section.findtext('auxBus'),float(section.findtext('controlValue')))
            self.assertEqual(defaults, {'scripts/arena_defs/07_lakeville.xml': ('AUX_env_Mountain',1.),
                                        'scripts/arena_defs/18_cliff.xml': ('AUX_env_Mountain',1.)})
            (root / 'records.json').write_text(json.dumps(rows))
            (root / 'host.cpp').write_text(host)
            flags = shlex.split(subprocess.check_output([str(config),'--cflags'],text=True))
            libs = shlex.split(subprocess.check_output([str(config),'--ldflags'],text=True))
            command = ['c++','-std=c++14','-Wall','-Wextra','-Werror','-ffp-contract=off',
                       *flags,*native_test_compile_flags(),'-UNDEBUG','-Wno-write-strings',
                       '-Wno-missing-field-initializers','-Wno-deprecated-declarations',
                       '-I',str(ROOT/'bigworld'),str(root/'host.cpp'),'-o',str(root/'probe'),*libs]
            result = subprocess.run(command,capture_output=True,text=True)
            self.assertEqual(result.returncode,0,result.stderr)
            env = dict(os.environ,WOT_SOUND_ZONE_RECORDS=str(root/'records.json'),
                       ASAN_OPTIONS='detect_leaks=0')
            result = subprocess.run([str(root/'probe'),str(ROOT/'tests/stock_sound_zones_consumer.py'),
                str(root/'geometry.bin')],capture_output=True,text=True,env=env)
            self.assertEqual(result.returncode,0,result.stdout+result.stderr)
            self.assertIn('constructors: 14',result.stdout)

    def test_original_wrapper_conversion_return_and_resource_hashes(self):
        client = os.environ.get('WOT_0922_CLIENT')
        if not client: self.skipTest('WOT_0922_CLIENT required')
        if not all(importlib.util.find_spec(n) for n in ('pefile', 'unicorn')):
            self.skipTest('Original x86 oracle dependencies required')
        from stock_sound_zones_oracle import OriginalSoundZones
        oracle = OriginalSoundZones(Path(client) / 'WorldOfTanks.exe')
        arguments = ((10.,-20.),(100.,50.),-.25,'enter','exit','AUX_env_Mountain',.75)
        for count in (0,1,6,8):
            result,trace,created = oracle.wrapper(arguments,count=count)
            self.assertEqual(result,0)
            self.assertEqual(trace,[('arity',count),('arity-error',)])
            self.assertIsNone(created)
        expected_types=('Vector2','Vector2','float','string','string','string','float')
        for failure in range(7):
            result,trace,created = oracle.wrapper(arguments,fail=failure)
            self.assertEqual(result,0)
            self.assertEqual(trace,[('arity',7)]+[('convert',i,expected_types[i]) for i in range(failure+1)])
            self.assertIsNone(created)
        result,trace,created=oracle.wrapper(arguments)
        self.assertEqual(result,0x1a5c798)
        self.assertEqual(trace,[('arity',7)]+[('convert',i,expected_types[i]) for i in range(7)])
        actual=struct.unpack('<4f3I4f',created)
        self.assertEqual(actual[:4],(10.,-20.,50.,25.))
        self.assertEqual(actual[9],2500.)
        self.assertAlmostEqual(actual[7],math.sin(-.25),places=7)
        self.assertAlmostEqual(actual[8],math.cos(-.25),places=7)
        self.assertEqual(actual[-1],.75)
        def expected_hash(text):
            if not text:return 0
            value=2166136261
            for byte in text.lower():value=((value*16777619)^byte)&0xffffffff
            return value
        for name in (b'',b'enter',b'EXIT',b'AUX_env_Mountain',b'aUx_env_cItY'):
            self.assertEqual(oracle.name_id(name),expected_hash(name))

    @staticmethod
    def original_udo_rows(client):
        sys.path.insert(0,str(ROOT.parents[1]/'tools'))
        from space_bin_0922 import CompiledSpace
        from prepare_native_vehicle import xml_root
        count = 0
        rows = []
        for package in sorted((client/'res/packages').glob('*.pkg')):
            with zipfile.ZipFile(package) as archive:
                for name in archive.namelist():
                    if not name.endswith('/space.bin'): continue
                    count += 1
                    space = CompiledSpace(archive.read(name))
                    for entry in xml_root(space.section_data('UDOS')):
                        if entry.findtext('type') != 'SoundZoneTrigger': continue
                        transform = [[float(v) for v in entry.findtext('transform/row'+str(i)).split()] for i in range(4)]
                        x,y = transform[:2]
                        normal_y = lambda v:v[1]/math.sqrt(sum(p*p for p in v))
                        angle = math.atan2(normal_y(x),normal_y(y))
                        properties = entry.find('properties')
                        rows.append(dict(archive=package.name,member=name,position=transform[3],angle=angle,
                            size=[float(v) for v in properties.findtext('Size').split()],
                            enter=properties.findtext('ZoneEnter') or '',exit=properties.findtext('ZoneExit') or '',
                            reverb=properties.findtext('Reverb'),gain=float(properties.findtext('ReverbLevel'))))
        assert count == 52
        return rows

    def test_lifecycle_patch_composes_and_executes_guarded_publication(self):
        from test_stock_vector4_basic import block
        names = ('lib/compiled_space/compiled_space_mapping.cpp', 'lib/compiled_space/compiled_space_mapping.hpp',
                 'lib/compiled_space/compiled_space.cpp','lib/space/client_space.cpp','lib/space/client_space.hpp',
                 'client/script_app.cpp','client/device_app.cpp','client/script_bigworld.cpp','client/connection_control.hpp',
                 'client/connection_control.cpp','client/entity_manager.cpp','client/entity_manager.hpp',
                 'lib/compiled_space/particle_system_loader.hpp','lib/compiled_space/particle_system_loader.cpp',
                 'lib/space/space_manager.hpp','lib/space/space_manager.cpp')
        # Use the real declared runner ordering, on pristine SDK files. The
        # scoped lifecycle patch also edits connection/entity retirement seams.
        runner = (ROOT.parents[1]/'tools/build_bigworld_client.ps1').read_text()
        ordered = re.findall(r'"(patch_stock_[a-z_]+\.py)"', runner)
        selected = {'patch_stock_audio.py', 'patch_stock_space_data.py', 'patch_stock_space_lifecycle.py',
                    'patch_stock_space_sounds.py', 'patch_stock_geometry_retirement.py'}
        with tempfile.TemporaryDirectory(prefix='sound-zones-seams-') as folder:
            root = Path(folder)
            values = sdk_files(names)
            for name,value in values.items():
                path=root/name;path.parent.mkdir(parents=True,exist_ok=True);path.write_text(value)
            for name in ordered:
                if name in selected:
                    item = module(ROOT/'bigworld'/name)
                    (getattr(item, 'patch', None) or getattr(item, 'apply', None) or item.patch_engine)(root)
            patcher = module(ROOT/'bigworld/patch_stock_sound_zones.py')
            patcher.patch(root)
            self.assertEqual((root/'lib/stock_sound_zones.hpp').read_bytes(),
                             (ROOT/'bigworld/stock_sound_zones.hpp').read_bytes())
            first={name:(root/name).read_bytes() for name in names}
            # The new patch is idempotent on the fully composed result.
            # The production runner itself always replays from pristine SDK.
            patcher.patch(root)
            self.assertEqual(first,{name:(root/name).read_bytes() for name in names})
            body=block((root/names[0]).read_text(),'void CompiledSpaceMapping::loadBGFinished()')
            # Execute the patched real mapping completion body, with actual
            # branch/publication ordering and re-entry revocation guards.
            fini=block((root/'client/script_app.cpp').read_text(),'void ScriptApp::fini()')
            host=(ROOT/'tests/stock_sound_zones_mapping_host.cpp').read_text().replace('@BODY@',body).replace('@FINI@',fini)
            (root/'host.cpp').write_text(host)
            result=subprocess.run(['c++','-std=c++14','-Wall','-Wextra','-Werror',
                *native_test_compile_flags(),str(root/'host.cpp'),'-o',str(root/'probe')],capture_output=True,text=True)
            self.assertEqual(result.returncode,0,result.stderr)
            result=subprocess.run([str(root/'probe')],capture_output=True,text=True)
            self.assertEqual(result.returncode,0,result.stdout+result.stderr)


if __name__ == '__main__': unittest.main()
