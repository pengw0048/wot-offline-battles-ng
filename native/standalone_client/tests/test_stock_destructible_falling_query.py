"""Exact original spatial fall-constraint probe/search and collision filtering."""
import random
import os
from test_stock_space_models import definition
import struct
import subprocess
import tempfile
from pathlib import Path
import unittest
from test_stock_ground_decals import ROOT,compiler_command,exact_client


class FallingQueryTests(unittest.TestCase):
    def test_original_query_segments_and_pitch_return(self):
        client=exact_client(self)
        from stock_destructible_falling_query_oracle import OriginalFallingQuery
        oracle=OriginalFallingQuery(Path(client)/'WorldOfTanks.exe')
        rng=random.Random(9431513);encoded=bytearray();expected=bytearray();queries=set()
        for case in range(1024):
            origin=tuple(rng.uniform(-2000,2000)for n in range(3));height=rng.uniform(.2,100);yaw=rng.uniform(-3.14,3.14)
            origin=struct.unpack('<3f',struct.pack('<3f',*origin));height,yaw=struct.unpack('<2f',struct.pack('<2f',height,yaw))
            mode=case%4
            if mode==0:distances=[height*.1]*32
            elif mode==1:distances=[-1.]*32
            else:distances=[-1.,height*.95*rng.uniform(.75,1.3),rng.choice((-1.,height*.25))]+[rng.choice((-1.,0.,height*.1,height*.5,height*.94,height))for n in range(29)]
            distances=struct.unpack('<32f',struct.pack('<32f',*distances));encoded.extend(struct.pack('<37f',*(origin+(height,yaw)+distances)))
            result,segments=oracle.query(origin,height,yaw,distances);queries.add(len(segments))
            expected.extend(result+struct.pack('<I',len(segments))+b''.join(segments))
        self.assertIn(1,queries);self.assertIn(2,queries);self.assertGreater(max(queries),4)
        with tempfile.TemporaryDirectory(prefix='stock-falling-query-')as name:
            binary=Path(name)/'probe'
            subprocess.run(compiler_command(self)+['-I'+str(ROOT/'bigworld'),str(ROOT/'tests/stock_destructible_falling_query_probe.cpp'),'-o',str(binary)],check=True)
            actual=subprocess.check_output([str(binary)],input=encoded)
        self.assertEqual(actual,expected)
        print('Original6b4ad0:1024 cases match complete pitch/kind and every probe segment; query counts%s'%sorted(queries))

    def test_original_wrapper_uint_schema_and_failed_argument_prefix(self):
        client=exact_client(self)
        from stock_destructible_falling_query_oracle import OriginalFallingQuery
        import stock_runtime_services_oracle as base
        original=OriginalFallingQuery(Path(client)/'WorldOfTanks.exe')
        for argc in (0,1,2,3,5):
            returned,calls,captured=original.wrapper(argc)
            self.assertEqual(returned,0);self.assertEqual(calls[0][:4],['argCountError','wg_getDestructibleFallPitchConstr',4,4]);self.assertFalse(captured)
        kinds=['uint','uint','uint','float']
        for failed in range(4):
            returned,calls,captured=original.wrapper(fail_at=failed)
            self.assertEqual(returned,0);self.assertEqual([row[1]for row in calls],kinds[:failed+1]);self.assertFalse(captured)
        returned,calls,captured=original.wrapper()
        self.assertEqual(returned,base.MARKS+0xfe00);self.assertEqual([row[1]for row in calls],kinds)
        self.assertEqual(captured,[[41,0x8081,3,.25]])

    def test_production_owner_sdk_callback_dispatch_and_original_filter(self):
        client=exact_client(self)
        from stock_destructible_falling_query_oracle import OriginalFallingQuery
        oracle=OriginalFallingQuery(Path(client)/'WorldOfTanks.exe')
        sdk=Path(os.environ.get('WOT_BIGWORLD_ROOT','/tmp/wot-bigworld-engine-14.4.1/programming/bigworld'))
        callback=sdk/'lib/physics2/collision_callback.hpp'
        if not callback.is_file():self.skipTest('Pinned SDK collision callback required')
        header=callback.read_text();constants=header[header.index('const int COLLIDE_STOP'):header.index('/**')]
        source=(ROOT/'tests/stock_destructible_falling_query_host.cpp').read_text()
        source=source.replace('@COLLISION_CONSTANTS@',constants).replace('@COLLISION_CLASS@',definition(header,'class CollisionCallback\n')+';')
        production=(ROOT/'bigworld/stock_destructible_falling_query.cpp').read_text()
        production='\n'.join(line for line in production.splitlines()if not line.startswith('#include'))
        source=source.replace('@PRODUCTION@',production)
        encoded=[];expected=bytearray()
        for material in (0,46,47,64,70,71,86,87,100,101,255):
            for low in range(256):
                flags=(material<<8)|low;distance=(-1.,.5,100.,200.)[low%4]
                response,state=oracle.filter(flags,distance)
                expected.extend(struct.pack('<I',response)+state);encoded.append('%u %f\n'%(flags,distance))
        with tempfile.TemporaryDirectory(prefix='stock-falling-query-owner-')as name:
            path=Path(name)/'owner.cpp';binary=Path(name)/'owner';path.write_text(source)
            # The unchanged SDK base methods have unused named parameters.
            subprocess.run(compiler_command(self)+['-Wno-unused-parameter','-I'+str(ROOT/'bigworld'),str(path),'-o',str(binary)],check=True)
            actual=subprocess.check_output([str(binary)],input=''.join(encoded).encode())
        self.assertEqual(actual,expected)
        print('Original6b47f0:2816 filter/closest-record cases; production query uses real SDK callback visit dispatch')


if __name__=='__main__':unittest.main()
