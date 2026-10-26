"""Pinned audio type contracts; real SDK execution uses the Win32 probe."""
import os
from pathlib import Path
import struct
import unittest

from stock_audio_objects_oracle import OriginalAudioObjects


class AudioObjectTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        client = os.environ.get('WOT_0922_CLIENT')
        if not client:
            raise unittest.SkipTest('Pinned x86 executable is required')
        cls.original = OriginalAudioObjects(Path(client)/'WorldOfTanks.exe')

    def test_original_named_methods_are_conversion_only(self):
        for address, name in [(0x124b390,b'stop'), (0x124ae90,b'isPlaying')]:
            result, trace = self.original.wrapper(address)
            self.assertEqual(result, 0x1a5c798)
            self.assertEqual(trace, [('arity',1),('item',0),('string-conversion',0)])
            result, trace = self.original.wrapper(address, conversion=-1)
            self.assertEqual(result, 0)
            self.assertEqual(trace[-1], ('string-conversion',-1))
            for count in [0,2,3]:
                result, trace = self.original.wrapper(address,count)
                self.assertEqual(result,0)
                self.assertEqual(trace,[('arity',count),('arity-error',name,1,1)])

    def test_original_constant_getters_and_readonly(self):
        result, trace = self.original.wrapper(0x1252760)
        self.assertEqual(trace,[('float',0x3f800000)])
        first, trace = self.original.wrapper(0x69ae90)
        self.assertEqual(trace,[('list',0,first)])
        second, trace = self.original.wrapper(0x69ae90)
        self.assertNotEqual(first,second)
        self.assertEqual(trace,[('list',0,second)])
        result, trace = self.original.null_matrix_getter()
        self.assertEqual(result,0x1a5c798)
        self.assertEqual(trace,[('matrix-result',0)])
        for address,attribute in [(0x6283a0,b'__members__'),(0xa03f30,b'__methods__'),
                                  (0xab3270,b'name'),(0x12527c0,b'isPlaying')]:
            # The readonly writer reads only the owner's native type name.
            p=self.original.oracle
            kind=p.allocate(32);self.original.put(self.original.owner+4,kind)
            text=p.allocate(16);self.original.machine.mem_write(text,b'PySound\0')
            self.original.put(kind+12,text)
            result, trace = self.original.wrapper(address)
            self.assertEqual(result,0xffffffff)
            self.assertEqual(len(trace),1)
            self.assertEqual(trace[0][2],b'Sorry, the attribute '+attribute+b' in %s is read-only')

    def test_original_volume_forwards_bits_and_checks_identity(self):
        for bits in [0,0x80000000,0x3f000000,0x3f800000,0xbf800000,0x40000000,0x7f800000,0x7fc12345]:
            self.assertEqual(self.original.volume(bits),[
                ('volume',0x87654321,0x12345678,0x12345678,0x87654321,bits)])
        self.assertEqual(self.original.volume(0,index=3),[])
        self.assertEqual(self.original.volume(0,generation=8),[])

    def test_original_provider_transform_and_fixture(self):
        cases=[]
        matrices=[(1,0,0,0, 0,1,0,0, 0,0,1,0, 0,0,0,1),
                  (0,0,-1,0, 0,1,0,0, 1,0,0,0, 10,-20,30,1),
                  (0,1,0,0, 0,0,1,0, 1,0,0,0, -0.125,50,1.25,1)]
        for matrix in matrices:
            for offset in [(0,0,0),(1.5,-2.25,3.125)]:
                trace=self.original.provider_position(matrix,offset)
                self.assertEqual(trace[0],('provider',))
                self.assertEqual(trace[1][:3],('position',0x87654321,0x12345678))
                # Original native copies row 2 as front and row 0 as top.
                expected=struct.pack('<9f',*(matrix[8:11]+matrix[:3]+tuple(matrix[12+i]+offset[i] for i in range(3))))
                self.assertEqual(trace[1][3],expected)
                cases.append(struct.pack('<19f',*(matrix+offset))+expected)
        self.assertEqual(self.original.provider_position(matrices[0],(1,2,3),False),[])
        destination=os.environ.get('WOT_AUDIO_OBJECTS_FIXTURE_OUT')
        if destination:
            path=Path(destination);path.mkdir(parents=True,exist_ok=True)
            (path/'provider-transform.bin').write_bytes(struct.pack('<I',len(cases))+b''.join(cases))

    def test_original_provider_setter_samples_before_detach(self):
        matrix=(1,0,0,0, 0,1,0,0, 0,0,1,0, 10,20,30,1)
        trace, sampled, stored, new = self.original.provider_setter(matrix)
        self.assertEqual(sampled,[new])
        self.assertEqual(stored,new)
        self.assertEqual([row[0] for row in trace],['provider','position'])
        trace, sampled, stored, unused = self.original.provider_setter(matrix,detach=True)
        self.assertEqual(sampled,[self.original.provider])
        self.assertEqual(stored,0)
        self.assertEqual([row[0] for row in trace],['provider','position'])
        trace, sampled, stored, unused = self.original.provider_setter(matrix,detach=True,old_present=False)
        self.assertEqual((trace,sampled,stored),([],[],0))


if __name__=='__main__':
    unittest.main()
