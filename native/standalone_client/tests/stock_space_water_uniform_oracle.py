"""Compare original a2eb80 forward-water instructions with production shader setup."""
import argparse
import importlib.util
import json
from pathlib import Path
import random
import struct
import subprocess
import tarfile
import tempfile


def audit(client,destination=None):
    import pefile
    from unicorn import Uc,UC_ARCH_X86,UC_MODE_32
    from unicorn.x86_const import UC_X86_REG_EDI,UC_X86_REG_EBP,UC_X86_REG_ESP
    root=Path(__file__).resolve().parents[1]
    spec=importlib.util.spec_from_file_location('water_patch',root/'bigworld/patch_stock_space_water.py')
    patch=importlib.util.module_from_spec(spec);spec.loader.exec_module(patch)
    pe=pefile.PE(str(Path(client)/'WorldOfTanks.exe'));base=pe.OPTIONAL_HEADER.ImageBase;image=pe.get_memory_mapped_image()
    uc=Uc(UC_ARCH_X86,UC_MODE_32);uc.mem_map(base,(len(image)+4095)//4096*4096);uc.mem_write(base,image)
    uc.mem_map(0x30000000,0x10000);owner,frame,stack=0x30000000,0x30003000,0x30008000
    rng=random.Random(1513);inputs=[];expected=[]
    edges=[0x7fc01234,0xffc01234,0x7f800000,0xff800000,0x42c80000,0x42c7ffff,0x42c80001]
    for case in range(4000):
        words=[struct.unpack('<I',struct.pack('<f',rng.uniform(-150,150)))[0] for unused in range(4)]
        if case<len(edges):words[1]=edges[case]
        uc.mem_write(owner+0xb0,struct.pack('<4I',*words));uc.reg_write(UC_X86_REG_EDI,owner);uc.reg_write(UC_X86_REG_EBP,frame);uc.reg_write(UC_X86_REG_ESP,stack)
        uc.emu_start(0xa2f357,0xa2f3cd,count=100)
        expected.append(' '.join(str(x) for x in struct.unpack('<4I',uc.mem_read(frame-0xcc,16))))
        inputs.append(' '.join(str(x) for x in words))
    with tempfile.TemporaryDirectory(prefix='wot-water-uniform-') as directory:
        work=Path(directory)
        with tarfile.open('/tmp/wot-bigworld-engine-14.4.1.tar.gz') as original:
            for name in ('water.cpp','water.hpp'):
                member=next(n for n in original.getnames() if n.endswith('/programming/bigworld/lib/romp/'+name))
                target=work/'lib/romp'/name;target.parent.mkdir(parents=True,exist_ok=True);target.write_bytes(original.extractfile(member).read())
        patch.patch(work);source=(work/'lib/romp/water.cpp').read_text();begin=source.index('    Vector4 forward = state_.forwardWaterParameters_;');end=source.index('    paramCache_->setVector( "forwardWaterParameters", &forward );',begin)
        producer=source[begin:end]
        driver='''#include <cstdint>
#include <cstring>
#include <iostream>
struct Vector4 {float x,y,z,w;};
int main(){std::uint32_t in[4],out[4];while(std::cin>>in[0]>>in[1]>>in[2]>>in[3]){
struct State{Vector4 forwardWaterParameters_;}state_;std::memcpy(&state_.forwardWaterParameters_,in,16);
PRODUCER
std::memcpy(out,&forward,16);std::cout<<out[0]<<' '<<out[1]<<' '<<out[2]<<' '<<out[3]<<'\\n';}}
'''.replace('PRODUCER',producer)
        (work/'uniform.cpp').write_text(driver)
        subprocess.run(['c++','-std=c++17','-Wall','-Wextra','-Werror','-fno-fast-math','-ffp-contract=off',str(work/'uniform.cpp'),'-o',str(work/'uniform')],check=True)
        data='\n'.join(inputs)+'\n';actual=subprocess.run([str(work/'uniform')],input=data,capture_output=True,text=True,check=True).stdout.splitlines()
        if actual!=expected:raise AssertionError([(i,a,b) for i,(a,b) in enumerate(zip(actual,expected)) if a!=b][:10])
        if destination:
            import shutil
            target=Path(destination);target.mkdir(parents=True,exist_ok=True);shutil.copy(work/'uniform.cpp',target/'uniform.cpp');(target/'uniform-inputs.txt').write_text(data);(target/'uniform-expected.txt').write_text('\n'.join(expected)+'\n')
    return {'original':'00a2eb80','instructions':'00a2f357-00a2f3cd','cases':4000,'bitMismatches':0,'status':'PASS'}


if __name__=='__main__':
    parser=argparse.ArgumentParser(description=__doc__);parser.add_argument('client',type=Path);parser.add_argument('--output',type=Path);args=parser.parse_args();print(json.dumps(audit(args.client,args.output)))
