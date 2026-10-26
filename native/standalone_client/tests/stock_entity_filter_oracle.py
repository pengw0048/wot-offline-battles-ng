"""Run the original #1513 entity broad-phase instructions, without copied math."""
import argparse
import random
import struct
import subprocess
from pathlib import Path


def bits(value):
    return struct.unpack('<I', struct.pack('<f', value))[0]


class EntityGeometryOracle:
    def __init__(self, executable):
        from stock_tree_lod_oracle import OriginalTreeInstructionOracle
        self.oracle = OriginalTreeInstructionOracle(executable)
        self.uc = self.oracle.emulator
        self.owner = self.oracle.allocate(0x500)
        self.start = self.oracle.allocate(12)
        self.end = self.oracle.allocate(12)

    def run(self, mode, words):
        from unicorn.x86_const import UC_X86_REG_EAX
        assert len(words) == 13
        self.uc.mem_write(self.owner, bytes(0x500))
        self.uc.mem_write(self.owner + 0x330, struct.pack('<4I', *words[:4]))
        self.uc.mem_write(self.owner + 0x330 + 0x94, struct.pack('<3I', *words[4:7]))
        self.uc.mem_write(self.start, struct.pack('<3I', *words[7:10]))
        self.uc.mem_write(self.end, struct.pack('<3I', *words[10:13]))
        self.oracle.call(0x6f16c0, (self.start, self.end, mode), ecx=self.owner)
        return self.uc.reg_read(UC_X86_REG_EAX) & 255


def cases(count=4000):
    randomizer = random.Random(1513006)
    result = []
    for index in range(count):
        radius = randomizer.uniform(0.0, 150.0)
        values = [radius, radius * radius, radius * radius * randomizer.uniform(0.1, 1.1),
                  randomizer.uniform(0, 80)]
        values += [randomizer.uniform(-100, 100) for unused in range(9)]
        words = [bits(value) for value in values]
        if index % 11 == 0:
            words[10:13] = words[7:10]
        if index % 17 == 0:
            words[0] = bits(-1.0)
        if index % 19 == 0:
            special = [0, 0x80000000, 0x7f800000, 0xff800000, 0x7fc00000]
            words[(index // 19) % 13] = special[(index // (19 * 13)) % len(special)]
        result.append(([0, 1, 2, 0xffffffff][index % 4], words))
    return result


def production_probe_source(source):
    source = Path(source).read_text()
    signature = 'bool StockEntityFilterGeometry::segmentMayHit('
    start = source.index(signature)
    end = source.index('\n}\n', start) + 2
    return r'''
#include <cmath>
#include <cstdint>
#include <cstring>
#include <iostream>
struct Vector3 {float x,y,z;};
struct StockEntityFilterGeometry {
 float radius,planarRadiusSquared,lowerPlanarRadiusSquared,verticalRadius;
 Vector3 points[7];
 bool segmentMayHit(const Vector3&,const Vector3&,unsigned)const;
};
''' + source[start:end] + r'''
int main(){
 unsigned mode=0;std::uint32_t words[13]{};unsigned count=0;
 while(std::cin>>mode){
  for(auto&word:words)if(!(std::cin>>word))return 3;
  float values[13];std::memcpy(values,words,sizeof(values));
  StockEntityFilterGeometry geometry{};
  geometry.radius=values[0];geometry.planarRadiusSquared=values[1];
  geometry.lowerPlanarRadiusSquared=values[2];geometry.verticalRadius=values[3];
  geometry.points[6]={values[4],values[5],values[6]};
  Vector3 start{values[7],values[8],values[9]},end{values[10],values[11],values[12]};
  std::cout<<(geometry.segmentMayHit(start,end,mode)?1:0)<<'\n';++count;
 }
 return count?0:4;
}
'''


def verify(executable, probe, count=4000):
    fixture = cases(count)
    oracle = EntityGeometryOracle(executable)
    expected = [oracle.run(mode, words) for mode, words in fixture]
    inputs = ''.join('%u %s\n' % (mode, ' '.join(str(word) for word in words))
                     for mode, words in fixture)
    output = subprocess.check_output([str(probe)], input=inputs, universal_newlines=True)
    actual = [int(value) for value in output.splitlines()]
    assert len(actual) == len(expected), (len(actual), len(expected))
    for index, (first, second) in enumerate(zip(actual, expected)):
        assert first == second, (index, fixture[index], first, second)
    print('Original x86 entity broad-phase: %u production cases exactly matched' % count)



class ChassisHistoryOracle:
    """Execute the complete native vector/history writer and angle reducer."""
    def __init__(self, executable):
        from stock_tree_lod_oracle import OriginalTreeInstructionOracle
        from unicorn import UC_HOOK_CODE
        from unicorn.x86_const import UC_X86_REG_EAX, UC_X86_REG_ESP, UC_X86_REG_EIP
        self.oracle = OriginalTreeInstructionOracle(executable)
        self.uc = self.oracle.emulator
        self.owner = self.oracle.allocate(0x700)
        self.storage = self.oracle.allocate(48 * 24)
        connection = self.oracle.allocate(4)
        table = self.oracle.allocate(0x40)
        self.clock = self.oracle.allocate(8)
        stub = self.oracle.allocate(7)
        self.uc.mem_write(stub, b'\xdd\x05' + struct.pack('<I', self.clock) + b'\xc3')
        self.uc.mem_write(connection, struct.pack('<I', table))
        self.uc.mem_write(table + 0x24, struct.pack('<I', stub))
        self.uc.mem_write(self.owner + 0x680, struct.pack('<3I', self.storage,
                          self.storage, self.storage + 48 * 24))

        def clock_connection(uc, address, size, data):
            stack = uc.reg_read(UC_X86_REG_ESP)
            uc.reg_write(UC_X86_REG_EAX, connection)
            uc.reg_write(UC_X86_REG_EIP, self.oracle.word(stack))
            uc.reg_write(UC_X86_REG_ESP, stack + 4)
        self.uc.hook_add(UC_HOOK_CODE, clock_connection, begin=0x6410a0, end=0x6410a0)

    def run(self, time, angles):
        self.uc.mem_write(self.clock, struct.pack('<d', time))
        self.oracle.call(0x6ef520, angles, ecx=self.owner)
        end = self.oracle.word(self.owner + 0x684)
        count = (end - self.storage) // 24
        assert 0 < count <= 24
        result = []
        for index in range(count):
            address = self.storage + index * 24
            result += list(struct.unpack('<3I', self.uc.mem_read(address, 12)))
            result += list(struct.unpack('<2I', self.uc.mem_read(address + 16, 8)))
        return [count] + result


def chassis_cases(count=4000):
    randomizer = random.Random(1513520)
    result = []
    time = 0.0
    for index in range(count):
        time += [0.02, 0.01, 0.009, -0.05][index % 4] if index % 73 == 0 else 0.02
        angles = [bits(randomizer.uniform(-1000000, 1000000)) for unused in range(3)]
        if index % 37 == 0:
            angles[index % 3] = [0, 0x80000000, bits(3.1415927410125732),
                                bits(-3.1415927410125732)][(index // 37) % 4]
        result.append((float('nan') if index % 281 == 280 else time, angles))
    return result


def chassis_probe_source(source):
    from test_stock_entity_filter import block
    source = Path(source).read_text()
    normalise = block(source, 'float normaliseFilterAngle(')
    sync = block(source, '    void syncChassisOrientation(')
    return r'''
#include <cmath>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <vector>
struct Vector3{float x,y,z;};
struct ChassisOrientationSample{Vector3 angles;double time;};
''' + normalise + '\nstruct State {std::vector<ChassisOrientationSample> chassisHistory;\n' + sync + r'''
};
int main(){
 State state;std::uint32_t words[5];
 while(std::cin>>words[0]){
  for(unsigned i=1;i<5;++i)if(!(std::cin>>words[i]))return 3;
  double time;Vector3 angles;
  std::memcpy(&time,words,8);std::memcpy(&angles,words+2,12);
  state.syncChassisOrientation(angles,time);std::cout<<state.chassisHistory.size();
  for(const auto&sample:state.chassisHistory){
   std::uint32_t values[5];std::memcpy(values,&sample.angles,12);std::memcpy(values+3,&sample.time,8);
   for(auto value:values)std::cout<<' '<<value;
  }
  std::cout<<'\n';
 }
}
'''


def verify_chassis(executable, probe, count=4000):
    fixture = chassis_cases(count)
    oracle = ChassisHistoryOracle(executable)
    expected = [oracle.run(time, angles) for time, angles in fixture]
    inputs = ''.join('%s %s\n' % (' '.join(str(value) for value in struct.unpack('<2I', struct.pack('<d', time))),
                                 ' '.join(str(value) for value in angles)) for time, angles in fixture)
    actual = [[int(value) for value in line.split()] for line in
              subprocess.check_output([str(probe)], input=inputs, universal_newlines=True).splitlines()]
    assert len(actual) == len(expected)
    for index, (first, second) in enumerate(zip(actual, expected)):
        assert first == second, (index, fixture[index], first, second)
    print('Original x86 chassis history: %u production operations exactly matched' % count)


class GroundDeformationOracle:
    """Run original scalar instructions with the real Win32 D3DX result replayed."""
    def __init__(self, executable):
        from stock_tree_lod_oracle import OriginalTreeInstructionOracle
        from unicorn import UC_HOOK_CODE
        from unicorn.x86_const import UC_X86_REG_EAX, UC_X86_REG_EIP, UC_X86_REG_ESP
        self.oracle = OriginalTreeInstructionOracle(executable)
        self.uc = self.oracle.emulator
        self.owner = self.oracle.allocate(0x700)
        self.physics = self.oracle.allocate(0x40)
        self.result = self.oracle.allocate(4)
        self.store = self.oracle.allocate(7)
        self.uc.mem_write(self.store, b'\xd9\x1d' + struct.pack('<I', self.result) + b'\xc3')
        self.normalized = None
        self.angular = None
        self.sdk_calls = 0

        def normalize_boundary(uc, address, size, data):
            stack = uc.reg_read(UC_X86_REG_ESP)
            output, source = self.oracle.word(stack + 4), self.oracle.word(stack + 8)
            assert bytes(uc.mem_read(source, 12)) == self.angular
            uc.mem_write(output, self.normalized)
            uc.reg_write(UC_X86_REG_EAX, output)
            uc.reg_write(UC_X86_REG_EIP, self.oracle.word(stack))
            uc.reg_write(UC_X86_REG_ESP, stack + 12)
            self.sdk_calls += 1
        self.uc.hook_add(UC_HOOK_CODE, normalize_boundary, begin=0x11c3bbe, end=0x11c3bbe)

    def run(self, mode, words, normalized):
        self.uc.mem_write(self.owner, bytes(0x700))
        self.angular = struct.pack('<3I', *words[:3])
        self.normalized = struct.pack('<3I', *normalized)
        for offset, values in [(0x30c, words[:3]), (0x560, words[3:6]),
                               (0x570, words[6:9]), (0x5a8, words[9:12])]:
            self.uc.mem_write(self.owner + offset, struct.pack('<3I', *values))
        self.uc.mem_write(self.owner + 0x51c, struct.pack('<I', self.physics if mode else 0))
        self.uc.mem_write(self.physics + 0x20, struct.pack('<6I', *words[12:18]))
        self.oracle.call(0x6eaa40, ecx=self.owner)
        self.oracle.call(self.store)
        return self.oracle.word(self.result)


def ground_cases(count=4000):
    randomizer = random.Random(1513 + 0x6eaa40)
    result = []
    for index in range(count):
        mode = index % 3
        values = [randomizer.uniform(-20, 20) for unused in range(12)]
        values += [-randomizer.uniform(0, 5) for unused in range(3)]
        values += [randomizer.uniform(0, 5) for unused in range(3)]
        if index % 13 == 0: values[:3] = [0, 0, 0]
        if mode == 2: values[12], values[15] = values[15], values[12]
        result.append((mode, [bits(value) for value in values]))
    return result


def ground_probe_source(source):
    from test_stock_entity_filter import block
    method = block(Path(source).read_text(), '    float groundDeformationRate() const')
    return r'''
#include <d3dx9.h>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <iostream>
using Vector3=D3DXVECTOR3;
namespace wot {
struct Bounds{Vector3 minimum,maximum;};
struct VehiclePhysics{Bounds bounds;const Bounds&collision_bounds()const{return bounds;}};
}
struct Matrix{Vector3 rows[4];const Vector3&operator[](unsigned i)const{return rows[i];}};
struct Ref{wot::VehiclePhysics*value=nullptr;explicit operator bool()const{return value!=nullptr;}wot::VehiclePhysics*get()const{return value;}};
wot::VehiclePhysics*unwrapStockVehiclePhysics(wot::VehiclePhysics*value){return value;}
struct State{Vector3 predictedAngularVelocity,velocity;Matrix body;Ref physics;
''' + method + r'''
};
int main(){
 unsigned mode;std::uint32_t words[18];
 while(std::cin>>mode){
  for(auto&word:words)if(!(std::cin>>word))return 3;
  float values[18];std::memcpy(values,words,sizeof(values));State state{};wot::VehiclePhysics core;
  std::memcpy(&state.predictedAngularVelocity,values,12);std::memcpy(&state.body.rows[0],values+3,12);
  std::memcpy(&state.body.rows[1],values+6,12);std::memcpy(&state.velocity,values+9,12);
  std::memcpy(&core.bounds.minimum,values+12,12);std::memcpy(&core.bounds.maximum,values+15,12);
  if(mode)state.physics.value=&core;
  const float result=state.groundDeformationRate();Vector3 normalized;
  D3DXVec3Normalize(&normalized,&state.predictedAngularVelocity);
  std::uint32_t output[4];std::memcpy(output,&result,4);std::memcpy(output+1,&normalized,12);
  std::cout<<output[0]<<' '<<output[1]<<' '<<output[2]<<' '<<output[3]<<'\n';
 }
}
'''


def verify_ground_output(executable, fixture, output):
    oracle = GroundDeformationOracle(executable)
    actual = [[int(value) for value in line.split()] for line in output.splitlines()]
    assert len(actual) == len(fixture)
    for index, ((mode, words), result) in enumerate(zip(fixture, actual)):
        expected = oracle.run(mode, words, result[1:])
        assert result[0] == expected, (index, mode, words, result, expected)
    assert oracle.sdk_calls == len(fixture)
    print('Original x86 ground deformation with actual Win32 D3DX: %u cases exactly matched' % len(fixture))


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('executable', type=Path)
    parser.add_argument('--probe', type=Path, required=True)
    parser.add_argument('--kind', choices=['geometry', 'chassis'], default='geometry')
    parser.add_argument('--count', type=int, default=4000)
    arguments = parser.parse_args()
    (verify if arguments.kind == 'geometry' else verify_chassis)(
        arguments.executable, arguments.probe, arguments.count)
