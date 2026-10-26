"""Execute the original Tank scalar mass/inertia producer before D3DX.

The actual f84370 instructions supply the expected arithmetic. Chassis reset is
an explicit external-owner boundary; the oracle stops at D3DXMatrixScaling and
records its arguments. It does not replace or claim to execute matrix inversion.
"""
import argparse
import random
import struct
import subprocess
from pathlib import Path


def bits(value):
    return struct.unpack('<I', struct.pack('<f', value))[0]


class HullMassOracle:
    def __init__(self, executable):
        from stock_tree_lod_oracle import OriginalTreeInstructionOracle
        from unicorn import UC_HOOK_CODE
        from unicorn.x86_const import UC_X86_REG_ESP, UC_X86_REG_EIP
        self.oracle = OriginalTreeInstructionOracle(executable)
        self.uc = self.oracle.emulator
        self.owner = self.oracle.allocate(0x800)
        self.body = self.oracle.allocate(0xe0)
        self.chassis = self.oracle.allocate(0x1d4)
        chassis_owner = self.oracle.allocate(0x150)
        body_vector = self.oracle.allocate(8)
        chassis_vector = self.oracle.allocate(8)
        table = self.oracle.allocate(0x40)
        reset = self.oracle.allocate(1)
        self.uc.mem_write(reset, b'\xc3')
        self.uc.mem_write(self.owner, struct.pack('<I', table))
        self.uc.mem_write(table + 0x1c, struct.pack('<I', reset))
        self.uc.mem_write(self.owner + 0x1b8, struct.pack('<I', chassis_owner))
        self.uc.mem_write(self.owner + 0x638, struct.pack('<II', 0, body_vector))
        self.uc.mem_write(body_vector, struct.pack('<II', self.body, 0))
        self.uc.mem_write(chassis_owner + 0xc0, struct.pack('<II', 0, chassis_vector))
        self.uc.mem_write(chassis_vector, struct.pack('<II', self.chassis, 0))
        self.diagonal = None

        def scaling(uc, address, size, data):
            stack = uc.reg_read(UC_X86_REG_ESP)
            assert self.oracle.word(stack + 4) == self.owner + 0x1d0
            self.diagonal = list(struct.unpack('<3I', uc.mem_read(stack + 8, 12)))
            uc.reg_write(UC_X86_REG_EIP, 0x30000000)
            uc.emu_stop()
        self.uc.hook_add(UC_HOOK_CODE, scaling, begin=0x11c3bca, end=0x11c3bca)

    def run(self, words):
        assert len(words) == 8
        self.uc.mem_write(self.body + 4, struct.pack('<I', words[0]))
        self.uc.mem_write(self.chassis + 0x68, struct.pack('<I', words[1]))
        self.uc.mem_write(self.body + 0x14, struct.pack('<3I', *words[2:5]))
        self.uc.mem_write(self.body + 8, struct.pack('<3I', *words[5:8]))
        self.diagonal = None
        self.oracle.call(0xf84370, ecx=self.owner)
        assert self.diagonal is not None
        return list(struct.unpack('<2I', self.uc.mem_read(self.owner + 0x1c8, 8))) + self.diagonal


def cases(count=4000):
    randomizer = random.Random(1513 + 0xf84370)
    result = []
    for index in range(count):
        values = [randomizer.uniform(0.01, 200), randomizer.uniform(0, 0.75)]
        values += [randomizer.uniform(0.01, 20) for unused in range(3)]
        values += [randomizer.uniform(0.1, 4) for unused in range(3)]
        words = [bits(value) for value in values]
        if index % 17 == 0:
            words[(index // 17) % 8] = [0, 0x80000000, bits(1.0), 0x7f800000, 0x7fc00000][(index // 136) % 5]
        result.append(words)
    return result


def probe_source():
    return r'''
#include "wot/vehicle_physics.hpp"
#include <cstdint>
#include <cstring>
#include <iostream>
int main(){
 std::uint32_t words[8];
 while(std::cin>>words[0]){
  for(unsigned i=1;i<8;++i)if(!(std::cin>>words[i]))return 3;
  float values[8];std::memcpy(values,words,sizeof(values));
  wot::VehicleBodyConfiguration body{};wot::VehicleChassisConfiguration chassis{};
  body.full_mass=values[0];chassis.chassis_mass_fraction=values[1];
  body.hull_size={values[2],values[3],values[4]};body.hull_inertia_factors={values[5],values[6],values[7]};
  const auto result=wot::build_vehicle_hull_mass_properties(body,chassis);
  float output[5]={result.mass,result.inverse_mass,result.inertia_diagonal.x,result.inertia_diagonal.y,result.inertia_diagonal.z};
  std::uint32_t encoded[5];std::memcpy(encoded,output,sizeof(output));
  for(unsigned i=0;i<5;++i)std::cout<<(i?" ":"")<<encoded[i];std::cout<<'\n';
 }
}
'''


def verify(executable, probe, count=4000):
    oracle = HullMassOracle(executable)
    fixture = cases(count)
    expected = [oracle.run(words) for words in fixture]
    inputs = ''.join(' '.join(map(str, words)) + '\n' for words in fixture)
    output = subprocess.check_output([str(probe)], input=inputs, universal_newlines=True)
    actual = [[int(value) for value in line.split()] for line in output.splitlines()]
    assert len(actual) == len(expected)
    for index, (first, second) in enumerate(zip(actual, expected)):
        assert first == second, (index, fixture[index], first, second)
    print('Original Tank hull mass/inertia D3DX caller: %u production cases exactly matched' % count)



class TankShapeSourceOracle:
    """Capture f8b8d0 arguments at explicit libm/D3DX/convex boundaries.

    Original instructions compute all geometry and face indices. The libm
    results are supplied binary32 inputs. D3DX transforms are not executed:
    their input vertices are captured and their opaque output is zeroed. Only
    the translation matrix's y entry, needed by subsequent native arithmetic,
    is supplied from that call's recorded y argument. No transformed bounds,
    topology, inertia inverse or completed physical vehicle is claimed here.
    """
    def __init__(self, executable):
        from stock_tree_lod_oracle import OriginalTreeInstructionOracle
        from unicorn import UC_HOOK_CODE
        from unicorn.x86_const import (UC_X86_REG_ESP, UC_X86_REG_ECX,
            UC_X86_REG_EAX, UC_X86_REG_EIP, UC_X86_REG_XMM0)
        self.oracle = OriginalTreeInstructionOracle(executable)
        self.uc = self.oracle.emulator
        self.owner = self.oracle.allocate(0xb00)
        self.body = self.oracle.allocate(0xe0)
        self.chassis = self.oracle.allocate(0x1d4)
        chassis_owner = self.oracle.allocate(0x150)
        body_vector = self.oracle.allocate(8)
        chassis_vector = self.oracle.allocate(8)
        self.poly_vector = self.oracle.allocate(8)
        self.box_vector = self.oracle.allocate(4)
        self.box = self.oracle.allocate(0x11c)
        self.uc.mem_write(self.owner + 0x1b8, struct.pack('<I', chassis_owner))
        self.uc.mem_write(self.owner + 0x638, struct.pack('<II', 0, body_vector))
        self.uc.mem_write(body_vector, struct.pack('<II', self.body, 0))
        self.uc.mem_write(chassis_owner + 0xc0, struct.pack('<II', 0, chassis_vector))
        self.uc.mem_write(chassis_vector, struct.pack('<II', self.chassis, 0))
        self.uc.mem_write(self.chassis + 0x19c, struct.pack('<I', 5))
        self.uc.mem_write(self.owner + 0x184, struct.pack('<I', self.poly_vector))
        self.uc.mem_write(self.owner + 0x178, struct.pack('<I', self.box_vector))
        self.uc.mem_write(self.box_vector, struct.pack('<I', self.box))
        self.buffers = [self.oracle.allocate(4096), self.oracle.allocate(4096)]
        self.uc.mem_write(self.poly_vector, struct.pack('<II',
            self.oracle.allocate(0x150), self.oracle.allocate(0x150)))
        self.known = {0x13dad43, 0x11c3bd0, 0x11c3bdc, 0x72f1a0,
                      0x6b56a0, 0xf6d270, 0xf65a70, 0xf65930}

        def hook(uc, address, size, data):
            if address not in self.known:
                return
            stack = uc.reg_read(UC_X86_REG_ESP)
            arg = lambda index: self.oracle.word(stack + 4 + index * 4)
            cleanup = 0
            if address == 0x13dad43:
                self.angles.append(uc.reg_read(UC_X86_REG_XMM0) & 0xffffffff)
                uc.reg_write(UC_X86_REG_XMM0, self.tangents[len(self.angles)-1])
            elif address == 0x11c3bd0:
                self.translation = list(struct.unpack('<3I', uc.mem_read(stack+8, 12)))
                uc.mem_write(arg(0), bytes(64))
                uc.mem_write(arg(0)+52, struct.pack('<I', self.translation[1]))
                cleanup = 16
            elif address == 0x11c3bdc:
                self.vertices.append(list(struct.unpack('<3I', uc.mem_read(arg(1), 12))))
                uc.mem_write(arg(0), bytes(16))
                cleanup = 12
                if len(self.meshes) == 4 and len(self.vertices) == 34:
                    uc.emu_stop()
                    return
            elif address in (0x72f1a0, 0x6b56a0):
                header = uc.reg_read(UC_X86_REG_ECX)
                width = 12 if address == 0x72f1a0 else 4
                begin, end, capacity = struct.unpack('<3I', uc.mem_read(header, 12))
                if not begin:
                    begin = end = self.buffers[0 if width == 12 else 1]
                    capacity = begin + 4096
                    uc.mem_write(header, struct.pack('<3I', begin, end, capacity))
                assert end + width <= capacity
                uc.mem_write(end, bytes(uc.mem_read(arg(0), width)))
                uc.mem_write(header+4, struct.pack('<I', end+width))
                cleanup = 4
            elif address == 0xf6d270:
                begin, end = struct.unpack('<2I', uc.mem_read(arg(0), 8))
                first, last = struct.unpack('<2I', uc.mem_read(arg(1), 8))
                self.meshes.append(((end-begin)//12,
                    list(struct.unpack('<%uI' % ((last-first)//4), uc.mem_read(first,last-first)))))
                cleanup = 12
                if len(self.meshes) == 4 and not self.complex:
                    uc.emu_stop()
                    return
            elif address == 0xf65a70:
                uc.reg_write(UC_X86_REG_EAX, self.poly_count)
                self.poly_count += 1
            elif address == 0xf65930:
                uc.reg_write(UC_X86_REG_EAX, 0)
            uc.reg_write(UC_X86_REG_EIP, self.oracle.word(stack))
            uc.reg_write(UC_X86_REG_ESP, stack+4+cleanup)
        self.uc.hook_add(UC_HOOK_CODE, hook)

    def run(self, words):
        # sizeXYZ, comY, boxZ, turretXYZ, turretWidth, complex,
        # auxiliary clearance, tank aft/front chamfers, terrain aft/front,
        # wheel penetration (+0x74), clearance, wheel positions[5],
        # terrain/tank tangents.
        assert len(words) == 24
        self.uc.mem_write(self.body + 0x14, struct.pack('<9I', *words[:9]))
        self.complex = bool(words[9])
        self.uc.mem_write(self.body + 0x94, bytes([self.complex]))
        for offset, index in [(0x98,13),(0x9c,14),(0xa4,11),(0xa8,12),(0xb0,10)]:
            self.uc.mem_write(self.body + offset, struct.pack('<I', words[index]))
        self.uc.mem_write(self.body + 0xac, struct.pack('<I', bits(13.0)))
        self.uc.mem_write(self.body + 0xa0, struct.pack('<I', bits(17.0)))
        self.uc.mem_write(self.chassis + 0x74, struct.pack('<I', words[15]))
        self.uc.mem_write(self.chassis + 0x6c, struct.pack('<I', words[16]))
        self.uc.mem_write(self.chassis + 4, struct.pack('<5I', *words[17:22]))
        self.tangents = [words[23], words[22]]
        self.angles, self.vertices, self.meshes = [], [], []
        self.translation = None
        self.poly_count = 0
        self.oracle.call(0xf8b8d0, ecx=self.owner)
        assert len(self.meshes) == 4 and len(self.vertices) == (34 if self.complex else 32)
        output = self.translation + [int(self.complex)]
        output += [v for vertex in self.vertices for v in vertex]
        for count, faces in self.meshes:
            output += [count, len(faces)] + faces
        return output


def shape_cases(count=4000, exceptional=False):
    randomizer = random.Random(1513 + 0xf8b8d0)
    result=[]
    for index in range(count):
        values = [randomizer.uniform(0.01, 20) for unused in range(3)]
        values += [randomizer.uniform(-3, 3) for unused in range(6)]
        values += [0.0, randomizer.uniform(0, 2)]
        values += [randomizer.uniform(0, 0.49) for unused in range(4)]
        values += [randomizer.uniform(0.01, 2), randomizer.uniform(0, 2)]
        values += [randomizer.uniform(-4, 4) for unused in range(5)]
        values += [randomizer.uniform(-0.5, 0.5), randomizer.uniform(-0.5, 0.5)]
        words=[bits(value) for value in values]
        words[9] = index % 2
        if index % 13 == 0:
            selected=(index//13)%23
            if selected >= 9: selected += 1
            words[selected]=([0, 0x80000000, 0x7f800000, 0x7fc00000] if exceptional else
                             [0, 0x80000000, bits(0.00001), bits(-0.00001)])[(index//312)%4]
        result.append(words)
    return result


def shape_probe_source():
    return r'''
#include "wot/vehicle_physics.hpp"
#include <cstdint>
#include <cstring>
#include <iostream>
int main(){
 std::uint32_t words[24];
 while(std::cin>>words[0]){
  for(unsigned i=1;i<24;++i)if(!(std::cin>>words[i]))return 3;
  float v[24];std::memcpy(v,words,sizeof(v));
  wot::VehicleBodyConfiguration b{};wot::VehicleShapeConfiguration s{};wot::VehicleChassisConfiguration c{};
  b.hull_size={v[0],v[1],v[2]};b.hull_com_shift_y=v[3];b.hull_box_offset_z=v[4];
  b.turret_top_position={v[5],v[6],v[7]};b.turret_top_width=v[8];s.use_complex_form=words[9]!=0;
  s.auxiliary_clearance=v[10];s.tank_aft_chamfer_fraction=v[11];s.tank_front_chamfer_fraction=v[12];
  s.terrain_aft_chamfer_fraction=v[13];s.terrain_front_chamfer_fraction=v[14];
  c.wheel_penetration=v[15];c.clearance=v[16];for(unsigned i=0;i<5;++i)c.road_wheel_positions[i]=v[17+i];
  const auto r=wot::build_vehicle_tank_shape_source(b,s,c,v[22],v[23]);
  auto put=[](float value){std::uint32_t word;std::memcpy(&word,&value,4);std::cout<<word<<' ';};
  auto vec=[&](const wot::Vector3& a){put(a.x);put(a.y);put(a.z);};
  vec(r.local_translation);std::cout<<r.has_auxiliary_box<<' ';
  for(unsigned j=0;j<4;++j){const auto&m=r.polyhedra[j];unsigned start=j%2?12:0;
   for(unsigned i=start;i<m.vertices.size();++i)vec(m.vertices[i]);}
  if(r.has_auxiliary_box)for(const auto& a:r.auxiliary_box_endpoints)vec(a);
  for(const auto&m:r.polyhedra){std::cout<<m.vertices.size()<<' '<<m.faces.size()<<' ';for(auto a:m.faces)std::cout<<a<<' ';}
  std::cout<<'\n';
 }
}
'''


def verify_shapes(executable, probe, count=4000):
    oracle=TankShapeSourceOracle(executable)
    fixture=shape_cases(count)
    expected=[oracle.run(words) for words in fixture]
    inputs=''.join(' '.join(map(str,words))+'\n' for words in fixture)
    output=subprocess.check_output([str(probe)], input=inputs, universal_newlines=True)
    actual=[[int(value) for value in line.split()] for line in output.splitlines()]
    assert len(actual)==len(expected), (len(actual),len(expected))
    for index,(first,second) in enumerate(zip(actual,expected)):
        assert first==second,(index,fixture[index],[(j,a,b) for j,(a,b) in enumerate(zip(first,second)) if a!=b][:12])
    print('Original Tank shape D3DX/convex caller: %u production cases exactly matched' % count)



class ChassisWheelSourceOracle:
    """Execute f87e90 through each actual f866f0 wheel-constructor call.

    Wheel allocation/construction is an explicit next-stage boundary. The
    original f82b30/f837f0/f82200 instructions still compute positions, weights,
    equilibrium deflection and all ten actual constructor parameter lists.
    """
    def __init__(self, executable):
        from stock_tree_lod_oracle import OriginalTreeInstructionOracle
        from unicorn import UC_HOOK_CODE
        from unicorn.x86_const import UC_X86_REG_ESP, UC_X86_REG_ECX, UC_X86_REG_EIP
        self.oracle=OriginalTreeInstructionOracle(executable)
        self.uc=self.oracle.emulator
        self.owner=self.oracle.allocate(0x150)
        self.settings=self.oracle.allocate(0x1d4)
        self.size=self.oracle.allocate(12)
        vector=self.oracle.allocate(8)
        self.groups=self.oracle.allocate(0x38)
        self.indices=[self.oracle.allocate(20),self.oracle.allocate(20)]
        self.uc.mem_write(self.owner+0xc0,struct.pack('<II',0,vector))
        self.uc.mem_write(vector,struct.pack('<II',self.settings,0))
        self.uc.mem_write(self.settings+0x19c,struct.pack('<I',5))
        self.known={0xf85f60,0xf866f0,0x83f4d0,0x6b56a0}
        def hook(uc,address,size,data):
            if address not in self.known:return
            stack=uc.reg_read(UC_X86_REG_ESP)
            arg=lambda i:self.oracle.word(stack+4+i*4)
            cleanup=0
            if address==0xf85f60:
                assert arg(0)==2
                uc.mem_write(arg(1),struct.pack('<III',self.groups,self.groups+0x38,self.groups+0x38))
                for i in range(2):uc.mem_write(self.groups+0x10+i*0x1c,struct.pack('<III',self.indices[i],self.indices[i],self.indices[i]+20))
                cleanup=8
            elif address==0xf866f0:
                assert self.oracle.word(arg(0))==self.owner
                row=list(struct.unpack('<3I',uc.mem_read(arg(1),12)))
                row += [self.oracle.word(arg(i)) for i in range(2,12)]
                self.wheels.append(row)
                cleanup=48
            else:
                header=uc.reg_read(UC_X86_REG_ECX)
                begin,end,capacity=struct.unpack('<III',uc.mem_read(header,12))
                assert end+4<=capacity
                uc.mem_write(end,bytes(uc.mem_read(arg(0),4)))
                uc.mem_write(header+4,struct.pack('<I',end+4))
                cleanup=4
            uc.reg_write(UC_X86_REG_EIP,self.oracle.word(stack))
            uc.reg_write(UC_X86_REG_ESP,stack+4+cleanup)
        self.uc.hook_add(UC_HOOK_CODE,hook)

    def run(self,words):
        # sizeXYZ/comY, gauge/mass/radius/inertia/fwdInertia, stiffness0/1,
        # roadWheelPositions[5], stiffnessFactors[5], current vertical force.
        assert len(words)==22
        self.uc.mem_write(self.size,struct.pack('<3I',*words[:3]))
        for offset,index in [(0,4),(0x64,5),(0x60,6),(0xa4,7),(0xa8,8),(0x7c,9),(0x80,10)]:
            self.uc.mem_write(self.settings+offset,struct.pack('<I',words[index]))
        self.uc.mem_write(self.settings+4,struct.pack('<5I',*words[11:16]))
        self.uc.mem_write(self.settings+0x2c,struct.pack('<5I',*words[16:21]))
        self.uc.mem_write(self.settings+0x18,struct.pack('<5f',*([10.0]*5)))
        self.uc.mem_write(self.settings+0x40,struct.pack('<5f',*([1.0]*5)))
        self.uc.mem_write(self.owner+0x64,struct.pack('<I',words[21]))
        self.wheels=[]
        self.oracle.call(0xf87e90,(self.size,words[3]),ecx=self.owner)
        assert len(self.wheels)==10
        return [value for wheel in self.wheels for value in wheel]


def wheel_cases(count=4000):
    randomizer=random.Random(1513+0xf87e90)
    result=[]
    for index in range(count):
        values=[randomizer.uniform(0.1,20) for unused in range(3)]
        values += [randomizer.uniform(-3,3),randomizer.uniform(0.2,2)]
        values += [randomizer.uniform(0.01,300),randomizer.uniform(0.05,2),randomizer.uniform(0.01,4),randomizer.uniform(0.01,4)]
        values += [randomizer.uniform(0.01,1000),randomizer.choice([0.0,0.005,0.01,0.01001,randomizer.uniform(0.1,100)])]
        values += [randomizer.uniform(-10,10) for unused in range(5)]
        values += [randomizer.uniform(0.01,5) for unused in range(5)]
        values += [randomizer.uniform(0,1000) if index%3 else 0.0]
        words=[bits(value) for value in values]
        if index%17==0:words[3]=[0,0x80000000][(index//17)%2]
        result.append(words)
    return result


def wheel_probe_source():
    return r'''
#include "wot/vehicle_physics.hpp"
#include <cstdint>
#include <cstring>
#include <iostream>
int main(){
 std::uint32_t words[22];
 while(std::cin>>words[0]){
  for(unsigned i=1;i<22;++i)if(!(std::cin>>words[i]))return 3;
  float v[22];std::memcpy(v,words,sizeof(v));
  wot::VehicleBodyConfiguration b{};wot::VehicleChassisConfiguration c{};
  b.hull_size={v[0],v[1],v[2]};b.hull_com_shift_y=v[3];c.track_gauge_factor=v[4];
  c.chassis_mass=v[5];c.wheel_radius=v[6];c.wheel_inertia_factor=v[7];c.wheel_forward_inertia_factor=v[8];
  c.stiffness_zero=v[9];c.stiffness_one=v[10];
  for(unsigned i=0;i<5;++i){c.road_wheel_positions[i]=v[11+i];c.stiffness_factors[i]=v[16+i];}
  const auto r=wot::build_vehicle_chassis_wheels(b,c,v[21]);
  auto put=[](float value){std::uint32_t word;std::memcpy(&word,&value,4);std::cout<<word<<' ';};
  for(const auto&a:r){put(a.local_position.x);put(a.local_position.y);put(a.local_position.z);
   std::cout<<a.side<<' '<<a.index<<' ';put(a.carrying_length);put(a.stiffness_factor);put(a.equilibrium_deflection);
   put(a.normalized_load);put(a.radius);put(a.mass);put(a.forward_inertia_factor);put(a.inertia_factor);}
  std::cout<<'\n';
 }
}
'''


def verify_wheels(executable,probe,count=4000):
    oracle=ChassisWheelSourceOracle(executable)
    fixture=wheel_cases(count)
    expected=[oracle.run(words) for words in fixture]
    output=subprocess.check_output([str(probe)],input=''.join(' '.join(map(str,row))+'\n' for row in fixture),universal_newlines=True)
    actual=[[int(value) for value in line.split()] for line in output.splitlines()]
    assert len(actual)==len(expected),(len(actual),len(expected))
    for index,(first,second) in enumerate(zip(actual,expected)):
        assert first==second,(index,fixture[index],[(j,a,b) for j,(a,b) in enumerate(zip(first,second)) if a!=b][:12])
    print('Original TrackChassis ten wheel constructors: %u production cases exactly matched' % count)



class WheelMassOracle:
    """Execute f7f280 scalar mass/inertia before D3DXMatrixScaling.

    The common rigid-body constructor and pose installation are explicitly
    outside this scalar probe. They cannot supply the tested mass/radius or
    inertia arguments, which the original constructor reads from its inputs.
    """
    def __init__(self,executable):
        from stock_tree_lod_oracle import OriginalTreeInstructionOracle
        from unicorn import UC_HOOK_CODE
        from unicorn.x86_const import UC_X86_REG_ESP,UC_X86_REG_EIP
        self.oracle=OriginalTreeInstructionOracle(executable)
        self.uc=self.oracle.emulator
        self.owner=self.oracle.allocate(0x4b4)
        self.chassis=self.oracle.allocate(0x150)
        self.position=self.oracle.allocate(12)
        self.known={0xf6f800,0x11c3bd0,0xf527b0,0xf80870,0x11c3bca}
        def hook(uc,address,size,data):
            if address not in self.known:return
            stack=uc.reg_read(UC_X86_REG_ESP)
            cleanup={0xf6f800:0,0x11c3bd0:16,0xf527b0:0,0xf80870:4}.get(address,0)
            if address==0x11c3bca:
                assert self.oracle.word(stack+4)==self.owner+0x24
                self.diagonal=list(struct.unpack('<3I',uc.mem_read(stack+8,12)))
                uc.emu_stop();return
            uc.reg_write(UC_X86_REG_EIP,self.oracle.word(stack))
            uc.reg_write(UC_X86_REG_ESP,stack+4+cleanup)
        self.uc.hook_add(UC_HOOK_CODE,hook)
    def run(self,words):
        # mass/radius/wheel inertia/wheel forward inertia
        assert len(words)==4
        self.diagonal=None
        self.oracle.call(0xf7f280,(self.chassis,self.position,0,0,0,0,0,0,
            words[1],words[0],words[3],words[2],1),ecx=self.owner)
        assert self.diagonal is not None
        return list(struct.unpack('<2I',self.uc.mem_read(self.owner+0x1c,8)))+self.diagonal


def wheel_mass_cases(count=4000):
    randomizer=random.Random(1513+0xf7f280)
    result=[]
    for index in range(count):
        values=[randomizer.uniform(0.01,100),randomizer.uniform(0.01,2),
                randomizer.uniform(0.01,5),randomizer.uniform(0.01,5)]
        words=[bits(value) for value in values]
        if index%17==0:words[(index//17)%4]=[0,0x80000000,bits(1.0),0x7fc00000][(index//68)%4]
        result.append(words)
    return result


def wheel_mass_probe_source():
    return r'''
#include "wot/vehicle_physics.hpp"
#include <cstdint>
#include <cstring>
#include <iostream>
int main(){
 std::uint32_t words[4];
 while(std::cin>>words[0]){
  for(unsigned i=1;i<4;++i)if(!(std::cin>>words[i]))return 3;
  float v[4];std::memcpy(v,words,sizeof(v));
  wot::VehicleWheelConstruction w{};w.mass=v[0];w.radius=v[1];w.inertia_factor=v[2];w.forward_inertia_factor=v[3];
  const auto r=wot::build_vehicle_wheel_mass_properties(w);
  float output[5]={r.mass,r.inverse_mass,r.inertia_diagonal.x,r.inertia_diagonal.y,r.inertia_diagonal.z};
  std::uint32_t encoded[5];std::memcpy(encoded,output,sizeof(output));
  for(unsigned i=0;i<5;++i)std::cout<<(i?" ":"")<<encoded[i];std::cout<<'\n';
 }
}
'''


def verify_wheel_mass(executable,probe,count=4000):
    oracle=WheelMassOracle(executable);fixture=wheel_mass_cases(count)
    expected=[oracle.run(words) for words in fixture]
    output=subprocess.check_output([str(probe)],input=''.join(' '.join(map(str,row))+'\n' for row in fixture),universal_newlines=True)
    actual=[[int(value) for value in line.split()] for line in output.splitlines()]
    assert len(actual)==len(expected),(len(actual),len(expected))
    for index,(first,second) in enumerate(zip(actual,expected)):
        assert first==second,(index,fixture[index],first,second)
    print('Original wheel mass/inertia D3DX caller: %u production cases exactly matched' % count)


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('executable', type=Path)
    parser.add_argument('--probe', type=Path, required=True)
    parser.add_argument('--shape', action='store_true')
    parser.add_argument('--wheels', action='store_true')
    parser.add_argument('--wheel-mass', action='store_true')
    parser.add_argument('--count', type=int, default=4000)
    arguments = parser.parse_args()
    (verify_wheel_mass if arguments.wheel_mass else verify_wheels if arguments.wheels else verify_shapes if arguments.shape else verify)(arguments.executable, arguments.probe, arguments.count)
