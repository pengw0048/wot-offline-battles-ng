"""Run the actual public Water decompressor on original BWWa2 table resources."""
import importlib.util
import os
from pathlib import Path
import struct
import subprocess
import sys
import tarfile
import tempfile
import unittest
import zipfile

ROOT=Path(__file__).resolve().parents[1]
spec=importlib.util.spec_from_file_location('water_patch',ROOT/'bigworld/patch_stock_space_water.py')
patch=importlib.util.module_from_spec(spec);spec.loader.exec_module(patch)


def decode(data,width):
    out=bytearray();offset=0
    while offset<len(data):
        repeated=data[offset]==53
        if repeated:offset+=1
        if offset+width+int(repeated)>len(data):raise ValueError('Truncated original water RLE')
        value=data[offset:offset+width];offset+=width
        count=data[offset] if repeated else 1
        offset+=int(repeated)
        if width==1:value=bytes([bool(value[0])])
        out+=value*count
    return out


class WaterTableTests(unittest.TestCase):
    def test_actual_resource_tables_match_mesh_grid(self):
        client=os.environ.get('WOT_0922_CLIENT')
        if not client:self.skipTest('WOT_0922_CLIENT required')
        sys.path.insert(0,str(ROOT.parents[1]/'tools'))
        from bw_primitives_geometry import _sections
        from space_bin_0922 import CompiledSpace
        tables=[];surfaces=vertices=0
        f32=lambda value:struct.unpack('<f',struct.pack('<f',value))[0]
        import math
        for pkg in (Path(client)/'res/packages').glob('*.pkg'):
            with zipfile.ZipFile(pkg) as archive:
                for member in archive.namelist():
                    if not member.endswith('/space.bin'):continue
                    space=CompiledSpace(archive.read(member));water=space.section_data('BWWa')
                    count=struct.unpack_from('<I',water,4)[0]
                    strings=space.section_data('BWST');n=struct.unpack_from('<I',strings,4)[0];pool=8+n*12+4
                    names={key:strings[pool+start:pool+start+length].decode()
                           for key,start,length in (struct.unpack_from('<III',strings,8+12*i) for i in range(n))}
                    for surface in range(count):
                        offset=8+268*surface
                        x,z,unused,tess=struct.unpack_from('<4f',water,offset+12)
                        grid=math.prod(int(f32(f32(math.ceil(f32(v/tess)))+1.)) for v in (x,z))
                        resource=names[struct.unpack_from('<I',water,offset+256)[0]]
                        sections=_sections(archive.read(resource))
                        self.assertEqual(struct.unpack('<i',sections['version'])[0],2)
                        for key,width in (('alpha',4),('rigid',1)):
                            blob=sections[key];decoded=decode(blob,width)
                            self.assertEqual(len(decoded),grid*width)
                            tables.append((width,blob))
                        surfaces+=1;vertices+=grid
        self.assertEqual((surfaces,vertices),(36,82076))
        # Escaped marker, zero-count run, high signed bytes, normal literals.
        tables.extend([(4,b'\x35\xff\x80\xab\xcd\x02'+b'\xff\x00\xff\xff'),
                       (4,b'\x35\x35\x00\x00\x00\x01'+b'\x35\xff\xff\xff\xff\x00'),
                       (1,b'\x00\x01\x35\x35\x04\x35\x00\x00\x02')])
        with tempfile.TemporaryDirectory(prefix='wot-water-tables-') as directory:
            root=Path(directory)
            with tarfile.open(os.environ.get('WOT_PUBLIC_ENGINE_ARCHIVE','/tmp/wot-bigworld-engine-14.4.1.tar.gz')) as archive:
                for name in ('water.cpp','water.hpp'):
                    member=next(n for n in archive.getnames() if n.endswith('/programming/bigworld/lib/romp/'+name))
                    path=root/'lib/romp'/name;path.parent.mkdir(parents=True,exist_ok=True);path.write_bytes(archive.extractfile(member).read())
            patch.patch(root)
            source=(root/'lib/romp/water.cpp').read_text()
            begin=source.index('void readValue( const char* pCompressedData, uint32& index, uint32& value )')
            end=source.index('/**\n *  Load the previously saved transparency/rigidity data.',begin)
            producer='\n'.join(line for line in source[begin:end].splitlines() if not line.startswith('#pragma'))
            driver='''#include <cassert>
#include <cstdint>
#include <fstream>
#include <vector>
using uint32=std::uint32_t;using uint8=std::uint8_t;using uint=unsigned;
namespace BW {template<class T> using vector=std::vector<T>;}
#define RLE_KEY_VALUE 53
PRODUCER
int main(int argc,char** argv){assert(argc==3);std::ifstream in(argv[1],std::ios::binary);std::ofstream out(argv[2],std::ios::binary);
uint32 count;in.read(reinterpret_cast<char*>(&count),4);for(uint32 i=0;i<count;++i){uint32 width,size;in.read(reinterpret_cast<char*>(&width),4);in.read(reinterpret_cast<char*>(&size),4);std::vector<char> blob(size);in.read(blob.data(),size);assert(in);
if(width==4){BW::vector<uint32> output;decompressVector(blob.data(),size,output);uint32 len=static_cast<uint32>(output.size());out.write(reinterpret_cast<const char*>(&len),4);out.write(reinterpret_cast<const char*>(output.data()),len*4);}
else{BW::vector<bool> output;decompressVector(blob.data(),size,output);uint32 len=static_cast<uint32>(output.size());out.write(reinterpret_cast<const char*>(&len),4);for(bool value:output){const char v=value?1:0;out.write(&v,1);}}}}
'''.replace('PRODUCER',producer)
            (root/'tables.cpp').write_text(driver)
            subprocess.run(['c++','-std=c++17','-Wall','-Wextra','-Werror','-fsanitize=address,undefined',str(root/'tables.cpp'),'-o',str(root/'tables')],check=True)
            data=struct.pack('<I',len(tables))+b''.join(struct.pack('<II',width,len(blob))+blob for width,blob in tables)
            expected=b''.join(struct.pack('<I',len(decode(blob,width))//width)+decode(blob,width) for width,blob in tables)
            (root/'table-inputs.bin').write_bytes(data)
            subprocess.run([str(root/'tables'),str(root/'table-inputs.bin'),str(root/'table-actual.bin')],check=True)
            self.assertEqual((root/'table-actual.bin').read_bytes(),expected)
            destination=os.environ.get('WOT_WATER_TABLE_FREEZE')
            if destination:
                import shutil
                target=Path(destination);target.mkdir(parents=True,exist_ok=True)
                for n in ('tables.cpp','table-inputs.bin','table-actual.bin'):shutil.copy(root/n,target/n)
                (target/'table-expected.bin').write_bytes(expected)
            print('Production Water RLE: 36 original tables / 82076 alpha+rigid mesh samples PASS')


if __name__=='__main__':unittest.main()
