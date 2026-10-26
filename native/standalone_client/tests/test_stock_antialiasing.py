"""Compare original AA algorithms and sample decoding with production policy."""
import itertools
import json
import os
from pathlib import Path
import subprocess
import tempfile
import unittest

ROOT=Path(__file__).resolve().parents[1]


class AntialiasingTests(unittest.TestCase):
    def test_original_temporal_kernel_layout_and_float_recipe(self):
        oracle_python=Path(os.environ.get('WOT_ORACLE_PYTHON','/tmp/wot-native-boundaries-venv/bin/python'))
        client=Path(os.environ.get('WOT_0922_CLIENT','/Users/peng/Downloads/World_of_Tanks_0.09.22.00.01_CH_1513_HD'))
        if not oracle_python.exists() or not(client/'WorldOfTanks.exe').exists():
            self.skipTest('Original x86 executable and Unicorn interpreter unavailable')
        cases=[(w,h,x,y) for w,h in [(1920,1080),(960,540),(1279,719),(1,1),(3840,2160)]
            for x,y in [(0,0),(-.125,-.375),(.375,-.125),(.125,.375),(-.375,.125)]]
        result=subprocess.run([str(oracle_python),str(ROOT/'tests/stock_antialiasing_oracle.py'),str(client/'WorldOfTanks.exe'),'kernel'],input=json.dumps(cases),text=True,capture_output=True)
        self.assertEqual(result.returncode,0,result.stderr);original=json.loads(result.stdout)
        source=r'''
#include "wot/stock_antialiasing.hpp"
#include <cstring>
#include <iostream>
int main(){float width,height,x,y;
while(std::cin>>width>>height>>x>>y){
 auto k=wot::stockTemporalAAKernel(width,height,x,y);
 for(const auto&values:{k.offsets.data(),k.weights.data(),k.lowPassWeights.data()}){
  unsigned count=values==k.offsets.data()?20:12;
  for(unsigned i=0;i<count;++i){unsigned bits;std::memcpy(&bits,values+i,4);std::cout<<bits<<' ';}}
 std::cout<<'\n';
}}
'''
        with tempfile.TemporaryDirectory()as folder:
            cpp=Path(folder)/'probe.cpp';binary=Path(folder)/'probe';cpp.write_text(source)
            result=subprocess.run(['c++','-std=c++14','-ffp-contract=off','-Wall','-Wextra','-Werror','-I'+str(ROOT/'include'),str(cpp),'-o',str(binary)],capture_output=True,text=True)
            self.assertEqual(result.returncode,0,result.stderr)
            result=subprocess.run([str(binary)],input='\n'.join(' '.join(str(v)for v in row)for row in cases),capture_output=True,text=True)
            self.assertEqual(result.returncode,0,result.stderr)
            self.assertEqual([[int(v)for v in row.split()]for row in result.stdout.splitlines()],original)

    def test_original_x86_modes_vendor_queries_and_sample_quality(self):
        oracle_python=Path(os.environ.get('WOT_ORACLE_PYTHON','/tmp/wot-native-boundaries-venv/bin/python'))
        client=Path(os.environ.get('WOT_0922_CLIENT','/Users/peng/Downloads/World_of_Tanks_0.09.22.00.01_CH_1513_HD'))
        if not oracle_python.exists() or not(client/'WorldOfTanks.exe').exists():
            self.skipTest('Original x86 executable and Unicorn interpreter unavailable')
        modes=[0,1,2,3,4,6,8,12,16,32,0x407,0x408,0x808,0x410,0x810,0x811,0x80000000,0xffffffff]
        cases=list(itertools.product(modes,[0,7],[False,True],[False,True],[False,True]))
        result=subprocess.run([str(oracle_python),str(ROOT/'tests/stock_antialiasing_oracle.py'),str(client/'WorldOfTanks.exe')],input=json.dumps(cases),text=True,capture_output=True)
        self.assertEqual(result.returncode,0,result.stderr);original=json.loads(result.stdout)
        source=r'''
#include "wot/stock_antialiasing.hpp"
#include <iostream>
int main(){unsigned desired,quality;bool nvidia,temporal,checked;
while(std::cin>>desired>>quality>>nvidia>>temporal>>checked){
 auto mode=wot::stockCustomAAMode(desired,temporal);auto d3d=wot::stockD3D9Multisampling(desired,quality,nvidia);bool queried=false;
 bool supported=wot::stockD3D9SupportsMultisampling(desired,nvidia,[&](unsigned){queried=true;return checked;});
 std::cout<<(desired<(temporal?4u:2u)?desired:0u)<<' '<<mode.fxaa<<' '<<mode.temporalSamples<<' '<<mode.mipMapLodBias<<' '<<mode.eightBitHistory<<' '<<(mode.temporalSamples>1)<<' '<<d3d.samples<<' '<<d3d.quality<<' '<<supported<<' '<<queried<<'\n';
}}
'''
        with tempfile.TemporaryDirectory()as folder:
            cpp=Path(folder)/'probe.cpp';binary=Path(folder)/'probe';cpp.write_text(source)
            result=subprocess.run(['c++','-std=c++14','-Wall','-Wextra','-Werror','-I'+str(ROOT/'include'),str(cpp),'-o',str(binary)],capture_output=True,text=True)
            self.assertEqual(result.returncode,0,result.stderr)
            result=subprocess.run([str(binary)],input='\n'.join(' '.join(str(int(v))for v in row)for row in cases),capture_output=True,text=True)
            self.assertEqual(result.returncode,0,result.stderr)
            current=[[int(v)for v in row.split()]for row in result.stdout.splitlines()]
            self.assertEqual(current,original)

    def test_original_temporal_frame_history_and_jitter_bits(self):
        oracle_python=Path(os.environ.get('WOT_ORACLE_PYTHON','/tmp/wot-native-boundaries-venv/bin/python'))
        client=Path(os.environ.get('WOT_0922_CLIENT','/Users/peng/Downloads/World_of_Tanks_0.09.22.00.01_CH_1513_HD'))
        if not oracle_python.exists() or not(client/'WorldOfTanks.exe').exists():
            self.skipTest('Original x86 executable and Unicorn interpreter unavailable')
        cases=list(itertools.product(list(range(65))+[0x7ffffffe,0x80000000,0xfffffffe,0xffffffff],
            [0,1,2,3,4,8,16,0xffffffff],[False,True],[False,True],[0,1]))
        result=subprocess.run([str(oracle_python),str(ROOT/'tests/stock_antialiasing_oracle.py'),str(client/'WorldOfTanks.exe'),'temporal'],input=json.dumps(cases),text=True,capture_output=True)
        self.assertEqual(result.returncode,0,result.stderr);original=json.loads(result.stdout)
        source=r'''
#include "wot/stock_antialiasing.hpp"
#include <cstring>
#include <iostream>
int main(){unsigned frame,samples,history;bool supported,accumulation;
while(std::cin>>frame>>samples>>supported>>accumulation>>history){
 wot::StockTemporalAAFrame state;state.frame=frame;state.currentHistory=history;
 state.advance(samples,supported,accumulation);unsigned x,y;
 std::memcpy(&x,&state.jitterX,4);std::memcpy(&y,&state.jitterY,4);
 std::cout<<state.frame<<' '<<state.sampleIndex<<' '<<state.previousHistory<<' '<<state.currentHistory<<' '<<x<<' '<<y<<'\n';
}}
'''
        with tempfile.TemporaryDirectory()as folder:
            cpp=Path(folder)/'probe.cpp';binary=Path(folder)/'probe';cpp.write_text(source)
            result=subprocess.run(['c++','-std=c++14','-ffp-contract=off','-Wall','-Wextra','-Werror','-I'+str(ROOT/'include'),str(cpp),'-o',str(binary)],capture_output=True,text=True)
            self.assertEqual(result.returncode,0,result.stderr)
            result=subprocess.run([str(binary)],input='\n'.join(' '.join(str(int(v))for v in row)for row in cases),capture_output=True,text=True)
            self.assertEqual(result.returncode,0,result.stderr)
            current=[[int(v)for v in row.split()]for row in result.stdout.splitlines()]
            self.assertEqual(current,original)
