"""Execute original f6d270 topology with recorded actual D3DX normalization.

Discovery runs only collect unknown SDK inputs; their exploratory output never
becomes an expected result. A verification run requires every normalization
result to have been recorded from an actual D3DX9_43.dll process. The original
instructions then produce topology, direction grouping, adjacency and bounds.
The later f55e40 world-transform consumer is a separate construction boundary.
"""
from pathlib import Path
import math
import random
import struct


def words(values):
    return list(struct.unpack('<%uI' % len(values), struct.pack('<%uf' % len(values), *values)))


class OriginalConvexTopologyOracle:
    def __init__(self, executable, normalizations, discover=False):
        from stock_tree_lod_oracle import OriginalTreeInstructionOracle
        from unicorn import UC_HOOK_CODE
        from unicorn.x86_const import UC_X86_REG_ESP, UC_X86_REG_EIP
        self.oracle = OriginalTreeInstructionOracle(executable)
        self.uc = self.oracle.emulator
        self.normalizations = normalizations
        self.discover = discover
        self.missing = set()
        self.owner = self.oracle.allocate(0x150)
        self.vertices = self.oracle.allocate(4096)
        self.indices = self.oracle.allocate(4096)
        self.vertex_header = self.oracle.allocate(12)
        self.index_header = self.oracle.allocate(12)
        self.uc.mem_write(0x2131dcc, struct.pack('<i', -1))
        tls = self.oracle.allocate(0x4000)
        slots = self.oracle.allocate(4096)
        self.uc.mem_write(0x2c, struct.pack('<I', slots))
        self.uc.mem_write(slots, struct.pack('<I', tls))
        self.completed = False

        def hook(uc, address, size, data):
            if address not in (0x11c3bbe, 0x11c3bca, 0xf55e40):
                return
            stack = uc.reg_read(UC_X86_REG_ESP)
            arg = lambda index: self.oracle.word(stack + 4 + index * 4)
            if address == 0xf55e40:
                self.completed = True
                uc.emu_stop()
                return
            if address == 0x11c3bbe:
                value = tuple(struct.unpack('<3I', uc.mem_read(arg(1), 12)))
                result = self.normalizations.get(value)
                if result is None:
                    self.missing.add(value)
                    if not self.discover:
                        raise AssertionError(('No actual D3DX result', value))
                    # This provisional value is only for SDK-input discovery.
                    # A complete strict rerun is required after collecting SDK
                    # results, including newly reachable normalization inputs.
                    xyz = struct.unpack('<3f', struct.pack('<3I', *value))
                    length = math.sqrt(sum(v*v for v in xyz))
                    result = words(tuple(v/length for v in xyz) if length else (0, 0, 0))
                uc.mem_write(arg(0), struct.pack('<3I', *result))
                cleanup = 8
            else:
                # Local-bounds scaling follows the topology and is not read
                # until the separate world-transform boundary where we stop.
                uc.mem_write(arg(0), bytes(64))
                cleanup = 16
            uc.reg_write(UC_X86_REG_EIP, self.oracle.word(stack))
            uc.reg_write(UC_X86_REG_ESP, stack + 4 + cleanup)
        self.uc.hook_add(UC_HOOK_CODE, hook)

    def vector(self, header, stride=3):
        begin, end = struct.unpack('<2I', self.uc.mem_read(header, 8))
        assert end >= begin and (end-begin) % (stride*4) == 0
        return list(struct.unpack('<%uI' % ((end-begin)//4), self.uc.mem_read(begin, end-begin)))

    def run(self, vertices, indices):
        assert 0 < len(vertices) * 12 <= 4096 and len(indices)*4 <= 4096
        # Each case is a fresh constructor. Reusing f6d270 on an existing
        # owner retains inner adjacency vectors; that in-place rebuild path
        # is deliberately not presented as fresh-construction evidence.
        self.owner = self.oracle.allocate(0x150)
        vertex_words = [value for vertex in vertices for value in vertex]
        self.uc.mem_write(self.vertices, struct.pack('<%uI' % len(vertex_words), *vertex_words))
        self.uc.mem_write(self.indices, struct.pack('<%uI' % len(indices), *indices))
        self.uc.mem_write(self.vertex_header, struct.pack('<3I', self.vertices,
            self.vertices+len(vertices)*12, self.vertices+len(vertices)*12))
        self.uc.mem_write(self.index_header, struct.pack('<3I', self.indices,
            self.indices+len(indices)*4, self.indices+len(indices)*4))
        self.completed = False
        self.oracle.call(0xf6d270, (self.vertex_header, self.index_header, 0x1b5b390), ecx=self.owner)
        assert self.completed
        result = []
        for offset in (0x24, 0x0c, 0x18):
            data = self.vector(self.owner+offset)
            result += [len(data)//3] + data
        begin, end = struct.unpack('<2I', self.uc.mem_read(self.owner, 8))
        result += [(end-begin)//20]
        for face in range(begin, end, 20):
            data = self.vector(face, 1)
            result += [len(data)] + data + list(struct.unpack('<2I', self.uc.mem_read(face+12, 8)))
        begin, end = struct.unpack('<2I', self.uc.mem_read(self.owner+0x54, 8))
        result += [(end-begin)//12]
        for adjacency in range(begin, end, 12):
            data = self.vector(adjacency, 1)
            result += [len(data)] + data
        return result + list(struct.unpack('<6I', self.uc.mem_read(self.owner+0x138, 24)))


def cases(count=400):
    randomizer = random.Random(1513+0xf6d270)
    base = [(-1,-1,-1),(-1,-1,1),(1,-1,1),(1,-1,-1),
            (-1,1,-1),(-1,1,1),(1,1,1),(1,1,-1)]
    faces = [4,0,1,2,3, 4,4,7,6,5, 4,0,4,5,1, 4,3,2,6,7,
             4,0,3,7,4, 4,1,5,6,2]
    result = []
    for index in range(count):
        scale = [randomizer.uniform(0.1, 4) for unused in range(3)]
        shift = [randomizer.uniform(-3, 3) for unused in range(3)]
        axes = [[scale[i] if i == j else randomizer.uniform(-0.2, 0.2)
                 for j in range(3)] for i in range(3)]
        vertices = [words(tuple(sum(p[k]*axes[k][j] for k in range(3))+shift[j]
                                for j in range(3))) for p in base]
        indices = list(faces)
        if index % 4 == 0:
            # Coincident vertices and adjacent repeated indices.
            vertices.append(list(vertices[0]))
            indices[:5] = [6,0,8,1,2,3,0]
        elif index % 4 == 1:
            # Collinear initial candidates require another noncollinear point.
            first = struct.unpack('<3f', struct.pack('<3I', *vertices[0]))
            second = struct.unpack('<3f', struct.pack('<3I', *vertices[1]))
            vertices.append(words(tuple((a+b)*0.5 for a,b in zip(first,second))))
            indices[:5] = [5,0,8,1,2,3]
        elif index % 4 == 2:
            # A two-point face cannot produce a normal or edge directions.
            indices = [2,0,1] + indices
        else:
            # Duplicate parallel/opposite faces share one normal direction.
            indices += [4,3,2,1,0]
        result.append((vertices, indices))
    return result


def tank_shape_cases(executable, count=100):
    """Original f8b8d0 pre-D3DX inputs for all four actual convex builders.

    These cases isolate the topology stage. Its input vertices are exactly the
    original SDK caller's inputs; they do not claim the later local translation
    or world-space bounds have been executed.
    """
    from vehicle_physics_configuration_oracle import TankShapeSourceOracle, shape_cases
    oracle = TankShapeSourceOracle(executable)
    result = []
    for source in shape_cases(count):
        oracle.run(source)
        for index, (vertex_count, faces) in enumerate(oracle.meshes):
            start = 0 if index < 2 else 16
            result.append((oracle.vertices[start:start+vertex_count], faces))
    return result


def encode_cases(fixture):
    return ''.join(' '.join(map(str,[len(vertices)] + [v for p in vertices for v in p] +
        [len(indices)] + indices))+'\n' for vertices,indices in fixture)


def probe_source():
    """Call the actual SDK DLL through its published x86 stdcall vector ABI."""
    return r'''
#include "wot/vehicle_physics.hpp"
#include <windows.h>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <string>
static_assert(sizeof(wot::Vector3)==12,"D3DXVECTOR3 published ABI");
using Normalize=wot::Vector3*(WINAPI*)(wot::Vector3*,const wot::Vector3*);
Normalize normalizer=nullptr;
wot::Vector3 normalize(wot::Vector3 input){wot::Vector3 output{};normalizer(&output,&input);return output;}
float decode(std::uint32_t word){float value;std::memcpy(&value,&word,4);return value;}
void put(float value){std::uint32_t word;std::memcpy(&word,&value,4);std::cout<<word<<' ';}
void vector(wot::Vector3 value){put(value.x);put(value.y);put(value.z);}
int main(int argc,char**argv){
 HMODULE module=LoadLibraryW(L"D3DX9_43.dll");if(!module)return 10;
 FARPROC procedure=GetProcAddress(module,"D3DXVec3Normalize");if(!procedure)return 11;
 static_assert(sizeof(procedure)==sizeof(normalizer),"Win32 function pointer ABI");
 std::memcpy(&normalizer,&procedure,sizeof(normalizer));
 if(argc==2&&std::string(argv[1])=="normalize"){
  std::uint32_t x,y,z;while(std::cin>>x>>y>>z){vector(normalize({decode(x),decode(y),decode(z)}));std::cout<<'\n';}
 }else{
  unsigned n;while(std::cin>>n){
   wot::VehicleTankConvexSource source;for(unsigned i=0;i<n;++i){std::uint32_t x,y,z;if(!(std::cin>>x>>y>>z))return 3;source.vertices.push_back({decode(x),decode(y),decode(z)});}
   if(!(std::cin>>n))return 4;for(unsigned i=0;i<n;++i){std::uint32_t word;if(!(std::cin>>word))return 5;source.faces.push_back(word);}
   const auto r=wot::build_vehicle_convex_topology(source,&normalize);
   for(const auto*values:{&r.vertices,&r.edge_directions,&r.normal_directions}){std::cout<<values->size()<<' ';for(auto value:*values)vector(value);}
   std::cout<<r.faces.size()<<' ';for(const auto&face:r.faces){std::cout<<face.vertices.size()<<' ';for(auto v:face.vertices)std::cout<<v<<' ';std::cout<<face.normal_index<<' ';put(face.normal_scale);}
   std::cout<<r.vertex_faces.size()<<' ';for(const auto&adj:r.vertex_faces){std::cout<<adj.size()<<' ';for(auto v:adj)std::cout<<v<<' ';}
   vector(r.local_bounds.minimum);vector(r.local_bounds.maximum);std::cout<<'\n';
  }
 }
 FreeLibrary(module);
}
'''
