"""Extract production Visual matrix assembly for actual Win32 public Math/D3DX.

The probe also captures its exact D3DX math dispatch table for the independent
original-instruction oracle. The captured DLL is used only by that audit.
"""
from pathlib import Path
ROOT=Path(__file__).resolve().parents[1]
s=(ROOT/'bigworld/stock_tps_scene.cpp').read_text();body=s[s.index('Matrix buildVisualBasis('):s.index('bool spawnFx(')]
pre=r'''
#include <windows.h>
#pragma warning(push)
#pragma warning(disable:4458)
#include "math/matrix.hpp"
#pragma warning(pop)
#include <cstdint>
#include <cstring>
#include <fstream>
#include <iostream>
using BW::Matrix;
struct TPSVector {float x,y,z;};
'''
post=r'''
int main(int argc,char **argv){if(argc!=3)return 2;std::ifstream in(argv[1],std::ios::binary);float values[20];
 while(in.read((char*)values,sizeof(values))){TPSVector angles{values[0],values[1],values[2]};Matrix attached;std::memcpy(&attached,values+4,64);Matrix result=buildVisualBasis(angles,values[3],attached);std::uint32_t bits[16];std::memcpy(bits,&result,64);for(auto v:bits)std::cout<<v<<' ';std::cout<<'\n';}
 const auto dll=reinterpret_cast<std::uintptr_t>(GetModuleHandleW(L"D3DX9_43.dll"));if(!dll)return 3;
 std::ofstream dispatch(argv[2]);dispatch<<"{";
 const std::uintptr_t slots[]={0x1ca074,0x1ca10c,0x1ca0f0};
 const char * names[]={"D3DXMatrixMultiply","D3DXMatrixRotationYawPitchRoll","D3DXMatrixScaling"};
 for(unsigned i=0;i<3;i++){if(i)dispatch<<',';dispatch<<'\"'<<names[i]<<"\":"<<(*reinterpret_cast<const std::uintptr_t*>(dll+slots[i])-dll);}dispatch<<"}";
 std::ofstream table(std::string(argv[2])+".bin",std::ios::binary);
 const auto dos=reinterpret_cast<const IMAGE_DOS_HEADER*>(dll);
 const auto nt=reinterpret_cast<const IMAGE_NT_HEADERS*>(dll+dos->e_lfanew);
 for(std::uintptr_t o=0x1ca000;o<0x1ca240;o+=4){auto value=*reinterpret_cast<const std::uintptr_t*>(dll+o);if(value>=dll&&value<dll+nt->OptionalHeader.SizeOfImage)value=value-dll+0x10000000;table.write(reinterpret_cast<const char*>(&value),4);}

}
'''
def make_probe():
    return pre+body+post
def write_fixture(destination):
    import random,struct
    r=random.Random(1513);data=bytearray()
    for i in range(4000):
     vals=[r.uniform(-20,20) for _ in range(3)]+[1. if i%5==0 else r.uniform(-5,5)]+[r.uniform(-30,30) for _ in range(16)]
     data+=struct.pack('<20f',*vals)
    Path(destination).write_bytes(data)

if __name__ == '__main__':
    import argparse
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('output',type=Path)
    parser.add_argument('--inputs',type=Path)
    args=parser.parse_args()
    args.output.write_text(make_probe())
    if args.inputs:write_fixture(args.inputs)
