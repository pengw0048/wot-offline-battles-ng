"""Complete CPU measurement owner against the exact instruction flow."""
import os
from pathlib import Path
import random
import shutil
import struct
import subprocess
import tempfile
import unittest
from test_stock_ground_decals import ROOT,compiler_command,exact_client


def vendor_words(name):
    assert len(name)==12
    ebx,edx,ecx=struct.unpack('<3I',name)
    return (0x10,ebx,ecx,edx)


class CpuDetectionTests(unittest.TestCase):
    def test_win32_codegen_uses_raw_cpu_instructions_and_real_api_symbols(self):
        compiler=shutil.which('clang++')
        if not compiler:self.skipTest('Optional Clang Win32 code-generation probe unavailable')
        prelude=r"""
#define BW_BEGIN_NAMESPACE namespace BW {
#define BW_END_NAMESPACE }
namespace BW {using uint64=unsigned long long;using int64=long long;}
extern "C" unsigned long long __rdtsc();
extern "C" void __cpuid(int*,int);
#pragma intrinsic(__rdtsc)
#pragma intrinsic(__cpuid)
struct SYSTEM_INFO {char before[20];unsigned long dwNumberOfProcessors;char after[12];};
extern "C" __declspec(dllimport) void __stdcall Sleep(unsigned long);
extern "C" __declspec(dllimport) void __stdcall GetSystemInfo(SYSTEM_INFO*);
namespace wot {long long stockCpuFrequencyScore(unsigned long long,unsigned,unsigned);}
"""
        production=(ROOT/'bigworld/stock_cpu_detection.cpp').read_text()
        production='\n'.join(line for line in production.splitlines()if not line.startswith('#include'))
        with tempfile.TemporaryDirectory(prefix='stock-cpu-detection-win32-codegen-')as name:
            directory=Path(name);source=directory/'probe.cpp';assembly=directory/'probe.s'
            source.write_text(prelude+production)
            subprocess.run([compiler,'--target=i686-pc-windows-msvc','-fms-extensions','-std=c++14','-O2','-Wall','-Wextra','-Werror',
                '-S',str(source),'-o',str(assembly)],check=True)
            text=assembly.read_text()
        instructions=[line.strip().split()[0]for line in text.splitlines()if line.startswith('\t')and line.strip()]
        self.assertEqual(instructions.count('rdtsc'),2)
        self.assertEqual(instructions.count('cpuid'),1)
        self.assertFalse(set(instructions)&{'rdtscp','lfence','mfence'})
        self.assertIn('__imp__Sleep@4',text);self.assertIn('__imp__GetSystemInfo@4',text)
        self.assertIn('$500',text)

    def test_production_measurement_sequence_vendor_cache_and_unsigned_wrap(self):
        client=exact_client(self)
        from stock_cpu_detection_oracle import OriginalCpuDetection
        vendors=[b'GenuineIntel',b'AuthenticAMD',b'OtherVendor!',b'GenuineInteX',b'GenuenticAMD',b'AuthineIntel']
        rng=random.Random(935570)
        vendors += [bytes(rng.randrange(256)for unused in range(12))for index in range(20)]
        with tempfile.TemporaryDirectory(prefix='stock-cpu-detection-')as name:
            directory=Path(name)
            stubs={'pch.hpp':'#pragma once\n',
                'cstdmf/stdmf.hpp':'#pragma once\n#include <cstdint>\n#define BW_BEGIN_NAMESPACE namespace BW {\n#define BW_END_NAMESPACE }\nnamespace BW {using int64=std::int64_t;using uint64=std::uint64_t;}\n',
                'intrin.h':'#pragma once\n#define __rdtsc hostReadTicks\n#define __cpuid hostCpuid\n',
                'windows.h':'#pragma once\n'}
            for path,text in stubs.items():
                output=directory/path;output.parent.mkdir(parents=True,exist_ok=True);output.write_text(text)
            binary=directory/'probe'
            subprocess.run(compiler_command(self)+['-I'+str(directory),'-I'+str(ROOT/'bigworld'),'-I'+str(ROOT/'include'),
                str(ROOT/'tests/stock_cpu_detection_host.cpp'),'-o',str(binary)],check=True)
            total=0
            for index,name in enumerate(vendors):
                oracle=OriginalCpuDetection(Path(client)/'WorldOfTanks.exe');words=vendor_words(name)
                # Even count0/1 samples/sleeps; only the first multi-CPU call executes CPUID.
                cases=[(0,1250000000,0,words),(99,1250000099,1,words),
                    (1000,1250001000,2,words),(0,2250000000,4,vendor_words(b'AuthenticAMD')),
                    ((1<<64)-100,50,64,vendor_words(b'GenuineIntel')),
                    (1000,999,3,words),(0,(1<<63)+17,16,words),
                    ((1<<64)-1,0,1,words),(0,(1<<64)-1,0xffffffff,words)]
                expected=[oracle.measure(*case)for case in cases]
                self.assertEqual([row[1]for row in expected],['RSRG','RSRG','RSRGC']+['RSRG']*6)
                encoded=''.join('%d %d %d %d %d %d %d\n'%((start,end,count)+tuple(value))for start,end,count,value in cases)
                result=subprocess.run([str(binary)],input=encoded,capture_output=True,text=True,check=True)
                actual=[(int(line.split()[0]),line.split()[1])for line in result.stdout.splitlines()]
                self.assertEqual(actual,expected,(index,name,result.stderr));total+=len(cases)
        print('Original CPU producer: %d measurements,26 CPUID identities,actual sequence/vendor cache/uint64 wrap exact'%total)


if __name__=='__main__':unittest.main()
