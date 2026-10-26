"""Compile the real state converter against the patched public WaterState."""
import importlib.util
import os
from pathlib import Path
import random
import struct
import subprocess
import sys
import tarfile
import tempfile
import unittest
import zipfile

ROOT = Path(__file__).resolve().parents[1]
spec = importlib.util.spec_from_file_location('water_patch',ROOT/'bigworld/patch_stock_space_water.py')
patch = importlib.util.module_from_spec(spec)
spec.loader.exec_module(patch)

# Field identities come from 00a2eb80 shader setters, 00a25ff0 transform/grid,
# 00a2a6c0 tables, and 00a2e870 simulation/volume consumers, not public ABI offsets.
FIELDS = (
    ('position_',0,12),('size_',12,8),('orientation_',20,4),('tessellation_',24,4),
    ('textureTessellation_',28,4),('consistency_',32,4),('reflectionScale_',36,4),
    ('refractionScale_',40,4),('refractionPower_',44,4),('animationSpeed_',48,4),
    ('scrollSpeed1_',52,8),('scrollSpeed2_',60,8),('waveScale_',68,8),
    ('sunPower_',76,4),('sunScale_',80,4),('sunScaleDeferred_',84,4),
    ('windVelocity_',88,4),('depth_',92,4),('reflectionTint_',96,16),('refractionTint_',112,16),
    ('simCellSize_',128,4),('smoothness_',132,4),('useEdgeAlpha_',136,1),
    ('useSimulation_',137,1),('visibility_',140,4),('reflectBottom_',144,1),
    ('forwardWaterParameters_',148,16),('shallowWaterBiasForward_',164,4),
    ('deepColour_',168,16),('deepColourScale_',184,4),('fadeDepth_',188,4),
    ('foamIntersection_',192,4),('foamMultiplier_',196,4),('foamTiling_',200,4),
    ('foamFrequency_',204,4),('foamAmplitude_',208,4),('foamWidth_',212,4),
    ('bypassDepth_',216,1),('freqX',220,4),('freqZ',224,4),('waveHeight',228,4),
    ('causticsPower',232,4),('waveTextureNumber_',236,4),('useShadows_',240,1))


def write_probe(root):
    header = (root/'lib/romp/water.hpp').read_text()
    begin = header.index('typedef struct _WaterState')
    end = header.index('} WaterState;',begin)+len('} WaterState;')
    structure = header[begin:end]
    owner = (ROOT/'bigworld/stock_space_water_scene.cpp').read_text()
    begin = owner.index('using Binary =')
    end = owner.index('BoundingBox bounds(',begin)
    converter = owner[begin:end]
    # Only value storage is supplied here. Conversion executes the production
    # function and the actual public declaration, with no emulated Water owner.
    source = '''#include "stock_space_water.hpp"
#include <cassert>
#include <cstdint>
#include <fstream>
struct Vector2 {float x,y;Vector2(float a,float b):x(a),y(b){}Vector2()=default;};
struct Vector3 {float x,y,z;Vector3(float a,float b,float c):x(a),y(b),z(c){}Vector3()=default;};
struct Vector4 {float x,y,z,w;Vector4(float a,float b,float c,float d):x(a),y(b),z(c),w(d){}Vector4()=default;};
using uint32=std::uint32_t;using DataSectionPtr=int;struct Matrix {};
struct Water { STRUCTURE };
CONVERTER
template<class T> void write(std::ostream& out,const T& v){out.write(reinterpret_cast<const char*>(&v),sizeof(v));}
int main(int argc,char** argv){assert(argc==3);std::ifstream in(argv[1],std::ios::binary);std::ofstream out(argv[2],std::ios::binary);
Binary::Definition row;while(in.read(reinterpret_cast<char*>(row.state.data()),244)){
const auto converted=state(row);
FIELDS
}}
'''.replace('STRUCTURE',structure).replace('CONVERTER',converter)
    source = source.replace('FIELDS','\n'.join('write(out,converted.%s);'%field[0] for field in FIELDS))
    (root/'parameters.cpp').write_text(source)


class WaterParameterTests(unittest.TestCase):
    def test_native_fields_do_not_follow_public_abi(self):
        archive = Path(os.environ.get('WOT_PUBLIC_ENGINE_ARCHIVE','/tmp/wot-bigworld-engine-14.4.1.tar.gz'))
        if not archive.is_file():
            self.skipTest('Pinned public source archive required')
        with tempfile.TemporaryDirectory(prefix='wot-water-parameters-') as name:
            root = Path(name)
            with tarfile.open(archive) as original:
                for relative in ('lib/romp/water.hpp','lib/romp/water.cpp'):
                    member = next(n for n in original.getnames() if n.endswith('/programming/bigworld/'+relative))
                    path = root/relative;path.parent.mkdir(parents=True,exist_ok=True)
                    path.write_bytes(original.extractfile(member).read())
            patch.patch(root)
            first = [(root/p).read_bytes() for p in ('lib/romp/water.hpp','lib/romp/water.cpp')]
            patch.patch(root)
            self.assertEqual(first,[(root/p).read_bytes() for p in ('lib/romp/water.hpp','lib/romp/water.cpp')])
            write_probe(root)
            subprocess.run(['c++','-std=c++17','-DWOT_STOCK_SPACE_WATER_PORTABLE','-Wall','-Wextra','-Werror',
                            '-fsanitize=address,undefined','-I',str(ROOT/'bigworld'),str(root/'parameters.cpp'),
                            str(ROOT/'bigworld/stock_space_water.cpp'),'-o',str(root/'parameters')],check=True)
            rows=[]
            client = os.environ.get('WOT_0922_CLIENT')
            if client:
                sys.path.insert(0,str(ROOT.parents[1]/'tools'))
                from space_bin_0922 import CompiledSpace
                for pkg in (Path(client)/'res/packages').glob('*.pkg'):
                    with zipfile.ZipFile(pkg) as packed:
                        for member in packed.namelist():
                            if member.endswith('/space.bin'):
                                water = CompiledSpace(packed.read(member)).section_data('BWWa')
                                count=struct.unpack_from('<I',water,4)[0]
                                rows.extend(water[8+268*i:8+268*i+244] for i in range(count))
                self.assertEqual(len(rows),36)
            rng=random.Random(1513)
            for unused in range(4000):
                rows.append(struct.pack('<61f',*(rng.uniform(-100,100) for n in range(61))))
            (root/'input.bin').write_bytes(b''.join(rows))
            expected=b''.join((bytes([bool(row[offset])]) if size==1 else row[offset:offset+size])
                              for row in rows for field,offset,size in FIELDS)
            subprocess.run([str(root/'parameters'),str(root/'input.bin'),str(root/'actual.bin')],check=True)
            self.assertEqual((root/'actual.bin').read_bytes(),expected)
            destination=os.environ.get('WOT_WATER_PARAMETER_FREEZE')
            if destination:
                import shutil
                target=Path(destination);target.mkdir(parents=True,exist_ok=True)
                for n in ('parameters.cpp','input.bin','actual.bin'):
                    shutil.copy(root/n,target/n)
                (target/'expected.bin').write_bytes(expected)
            print('Production WaterState converter: %d native states / %d semantic fields PASS'%(len(rows),len(FIELDS)))

    def test_changed_source_does_not_partially_patch(self):
        self.assertEqual(patch.replace('old','old','new'),'new')
        self.assertEqual(patch.replace('new','old','new'),'new')
        for text in ('missing','old old'):
            with self.assertRaises(RuntimeError):patch.replace(text,'old','new')


if __name__=='__main__':unittest.main()
