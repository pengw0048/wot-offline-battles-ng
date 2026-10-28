"""WTau reader and actual static-voice lifecycle; no audible acceptance claim."""
from pathlib import Path
import os
import struct
import shutil
import subprocess
import sys
import tempfile
import unittest
import zipfile

ROOT = Path(__file__).resolve().parents[1]


class SpaceSoundTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        if not shutil.which('c++'):
            raise unittest.SkipTest('C++ compiler unavailable: c++')
        cls.temporary = tempfile.TemporaryDirectory(prefix='stock-space-sounds-')
        cls.directory = Path(cls.temporary.name)
        cls.probe = cls.directory/'reader'
        subprocess.run(['c++','-std=c++14','-O2','-Wall','-Wextra','-Werror','-UNDEBUG',
                        '-I'+str(ROOT/'bigworld'),'-I'+str(ROOT/'include'),str(ROOT/'tests/test_stock_space_sounds_data.cpp'),
                        '-o',str(cls.probe)],check=True)

    @classmethod
    def tearDownClass(cls):
        cls.temporary.cleanup()

    def test_transactional_reader_and_ignored_tail(self):
        subprocess.run([str(self.probe)],check=True)

    def test_production_group_owner_lifetimes(self):
        probe=self.directory/'owners'
        subprocess.run(['c++','-std=c++14','-O1','-g','-Wall','-Wextra','-Werror','-UNDEBUG',
                        '-fsanitize=address,undefined',str(ROOT/'tests/test_stock_space_sound_owners.cpp'),
                        '-o',str(probe)],check=True)
        subprocess.run([str(probe)],check=True)

    def test_original_group_creation_and_start_order_fixture(self):
        client=os.environ.get('WOT_0922_CLIENT')
        if not client:self.skipTest('Pinned original client is required')
        sys.path.insert(0,str(ROOT/'tests'));sys.path.insert(0,str(ROOT.parents[1]/'tools'))
        from stock_space_sounds_oracle import OriginalSpaceSoundGroups
        from packed_xml import read_packed_xml
        oracle=OriginalSpaceSoundGroups(Path(client)/'WorldOfTanks.exe')
        self.assertEqual(oracle.start_order([b'z',b'a',b'c',b'b']),[b'b',b'z',b'a',b'c'])
        with zipfile.ZipFile(Path(client)/'res/packages/scripts.pkg')as archive:
            root=read_packed_xml(archive.read('scripts/environment_effects.xml'))
        authored=[dict(v.value.children)[b'modelName'].value for unused,v in dict(root.children)[b'lightSounds'].value.children]
        cases=[[('group_%04d'%i).encode()for i in range(count)]for count in (0,1,8,9,64,65,512,513)]
        cases += [[b'z',b'a',b'c',b'b',b'z',b'A',b'long_name_over_15_chars',u'树_材质'.encode('utf-8')],authored]
        encoded=bytearray(struct.pack('<I',len(cases)));expected=bytearray()
        for names in cases:
            encoded.extend(struct.pack('<I',len(names)))
            for name in names:encoded.extend(struct.pack('<I',len(name))+name)
            ordered=oracle.start_order(names)
            self.assertEqual(set(ordered),set(names))
            expected.extend(struct.pack('<I',len(ordered)))
            for name in ordered:expected.extend(struct.pack('<I',len(name))+name)
        destination=os.environ.get('WOT_SPACE_SOUNDS_FIXTURE_OUT')
        if destination:
            destination=Path(destination);destination.mkdir(parents=True,exist_ok=True)
            (destination/'group-order-input.bin').write_bytes(encoded)
            (destination/'group-order-expected.bin').write_bytes(expected)
        print('Actual 1249d60/124a450/124ac80: %d native group/start-order cases; Win32 production comparison fixture prepared'%len(cases))

    def test_real_model_hardpoint_selection_matches_original(self):
        client=os.environ.get('WOT_0922_CLIENT')
        if not client:self.skipTest('Pinned original client is required')
        sys.path.insert(0,str(ROOT/'tests'));sys.path.insert(0,str(ROOT.parents[1]/'tools'))
        from stock_space_sounds_oracle import OriginalModelSoundMatrices
        from space_bin_0922 import CompiledSpace
        oracle=OriginalModelSoundMatrices(Path(client)/'WorldOfTanks.exe')
        def arrays(data):
            result=[];offset=0
            while offset<len(data):
                stride,count=struct.unpack_from('<II',data,offset);offset+=8
                result.append(data[offset:offset+stride*count]);offset+=stride*count
            self.assertEqual(offset,len(data));return result
        total=0;hardpoints=0;seen=0
        for package in sorted((Path(client)/'res/packages').glob('*.pkg')):
            with zipfile.ZipFile(package)as archive:
                for name in archive.namelist():
                    if not name.endswith('/space.bin'):continue
                    binary=CompiledSpace(archive.read(name))
                    sections=[binary.section_data(tag)for tag in ('BSMI','BSMO','BSMA')]
                    instances,models=arrays(sections[0]),arrays(sections[1])
                    ids=[v[0]for v in struct.iter_unpack('<I',instances[3])]
                    sounds=[v[0]for v in struct.iter_unpack('<I',models[5])]
                    indices=[i for i,model in enumerate(ids)if sounds[model]!=0xffffffff]
                    source,output=self.directory/'models',self.directory/'model-operands'
                    source.write_bytes(b''.join(struct.pack('<I',len(v))+v for v in sections))
                    subprocess.run([str(self.probe),'--models',str(source),str(output)],check=True)
                    self.assertEqual(output.read_bytes(),oracle.arguments(instances,models,indices))
                    total+=len(indices);seen+=1
                    for index in indices:
                        typ,unused=struct.unpack_from('<II',models[4],ids[index]*8)
                        hardpoints+=typ==2
        self.assertEqual(seen,52);self.assertGreater(hardpoints,0)
        print('Actual 6c24f0: %d model sound transforms / %d fragile hardpoints exact D3DX operands; hardPoint string unused'%(total,hardpoints))

    def test_original_instructions_and_all_52_sections(self):
        client = os.environ.get('WOT_0922_CLIENT')
        if not client:
            self.skipTest('Pinned original client is required')
        sys.path.insert(0,str(ROOT/'tests'))
        sys.path.insert(0,str(ROOT.parents[1]/'tools'))
        from stock_space_sounds_oracle import OriginalSpaceSoundReader
        from space_bin_0922 import CompiledSpace
        oracle = OriginalSpaceSoundReader(Path(client)/'WorldOfTanks.exe')
        sections=[]
        for package in sorted((Path(client)/'res/packages').glob('*.pkg')):
            with zipfile.ZipFile(package) as archive:
                for name in archive.namelist():
                    if name.endswith('/space.bin'):
                        binary=CompiledSpace(archive.read(name))
                        payload=binary.section_data('WTau')
                        if payload is not None:
                            sections.append(payload)
        self.assertEqual(len(sections),52)
        total=0
        for i,payload in enumerate(sections):
            stride,count=struct.unpack_from('<II',payload)
            end=8+24*count
            self.assertEqual(stride,24)
            self.assertEqual(payload[end:],struct.pack('<II',24,0))
            expected,cursor=oracle.read(payload)
            self.assertEqual(cursor,end)
            self.assertEqual(len(expected),16*count)
            source,output=self.directory/'wtau',self.directory/'output'
            source.write_bytes(payload)
            subprocess.run([str(self.probe),str(source),str(output)],check=True,stdout=subprocess.DEVNULL)
            self.assertEqual(output.read_bytes(),expected,'WTau section %d'%i)
            # Only entry+4 and entry+c are consumed; arbitrary words at +0/+8
            # and an opaque/absent tail leave the exact native output unchanged.
            changed=bytearray(payload[:end])
            for n in range(count):
                struct.pack_into('<I',changed,8+n*24,0xfedcba98)
                struct.pack_into('<I',changed,16+n*24,0x7fc00000)
            for tail in (b'',b'not-an-array',struct.pack('<II',24,1)+bytes(24)):
                native,consumed=oracle.read(bytes(changed)+tail)
                self.assertEqual(native,expected)
                self.assertEqual(consumed,end)
            total+=count
        self.assertEqual(total,998)
        for bad in (b'',struct.pack('<II',23,0),struct.pack('<II',24,1)+bytes(23)):
            result,unused=oracle.read(bad)
            self.assertIsNone(result)
        identityCount=0
        for package in sorted((Path(client)/'res/packages').glob('*.pkg')):
            with zipfile.ZipFile(package) as archive:
                for name in archive.namelist():
                    if not name.endswith('/space.bin'):continue
                    payload=CompiledSpace(archive.read(name)).section_data('WGDE')
                    stride,count=struct.unpack_from('<II',payload)
                    self.assertEqual(stride,12)
                    chunks=[struct.unpack_from('<III',payload,8+i*12) for i in range(count)]
                    queries=[(key,index) for key,unused,size in chunks for index in
                             sorted(set((0,max(0,size-1),size,0xffffffff,0x80000000)))]
                    queries.append((0xfefefefe,0))
                    native,cursor=oracle.identities(payload,queries)
                    self.assertEqual(cursor,len(payload))
                    source,query,output=self.directory/'wgde',self.directory/'queries',self.directory/'identities'
                    source.write_bytes(payload)
                    query.write_bytes(struct.pack('<I',len(queries))+b''.join(struct.pack('<II',*v)for v in queries))
                    subprocess.run([str(self.probe),'--identities',str(source),str(query),str(output)],check=True)
                    self.assertEqual(output.read_bytes(),b''.join(struct.pack('<I',v)for v in native))
                    identityCount+=len(queries)
        print('Actual 6ba4f0/6ba8f0: WGDE three arrays / %d chunk-item identity queries exact'%identityCount)
        print('Actual 6ccfc0/6cc350/6cc590/6cce90: 52 sections / 998 registrations exact; +0/+8/tail unused')


if __name__=='__main__':
    unittest.main()
