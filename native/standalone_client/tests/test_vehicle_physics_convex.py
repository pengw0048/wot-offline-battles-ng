"""Test actual convex-construction ownership and topology under ASan/UBSan.

The axis-only normalization service is a declared unit-test boundary. Actual
D3DX numerical evidence comes from vehicle_physics_convex_oracle.py and the
separate Win32 DLL probe, rather than this portable service.
"""
from pathlib import Path
import os
import shutil
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]


SOURCE = r'''
#include "wot/vehicle_physics.hpp"
#include <cassert>
#include <cmath>
#include <stdexcept>
#include <vector>
namespace {
std::vector<wot::Vector3> requests;
wot::Vector3 axis_normalize(wot::Vector3 value){
 requests.push_back(value);
 const unsigned nonzero=(value.x!=0)+(value.y!=0)+(value.z!=0);
 assert(nonzero==1);
 const float length=std::abs(value.x)+std::abs(value.y)+std::abs(value.z);
 return {value.x/length,value.y/length,value.z/length};
}
wot::VehicleTankConvexSource cube(){
 return {{{-1,-1,-1},{-1,-1,1},{1,-1,1},{1,-1,-1},
          {-1,1,-1},{-1,1,1},{1,1,1},{1,1,-1}},
         {4,0,1,2,3,4,4,7,6,5,4,0,4,5,1,4,3,2,6,7,
          4,0,3,7,4,4,1,5,6,2}};
}
}
int main(){
 const auto source=cube();
 const auto result=wot::build_vehicle_convex_topology(source,&axis_normalize);
 assert(source.vertices.size()==8&&source.faces.size()==30);
 assert(result.vertices.size()==8&&result.faces.size()==6);
 assert(result.normal_directions.size()==3&&result.edge_directions.size()==3);
 assert(result.faces[0].normal_index==0&&result.faces[1].normal_index==0);
 assert(result.faces[0].normal_scale==1&&result.faces[1].normal_scale==-1);
 assert((result.vertex_faces[0]==std::vector<std::uint32_t>{0,2,4}));
 assert(result.local_bounds.minimum.x==-1&&result.local_bounds.maximum.z==1);
 assert(requests.size()==9);
 assert(requests[0].x==0&&requests[0].y==-4&&requests[0].z==0);
 // Returned geometry owns its storage; rebuilding another value cannot
 // append stale face IDs to the earlier value's adjacency vectors.
 for(unsigned iteration=0;iteration<500;++iteration){
  auto duplicate=source;duplicate.vertices.push_back(source.vertices[0]);
  duplicate.faces.erase(duplicate.faces.begin(),duplicate.faces.begin()+5);
  duplicate.faces.insert(duplicate.faces.begin(),{6,0,8,1,2,3,0});
  const auto second=wot::build_vehicle_convex_topology(duplicate,&axis_normalize);
  assert(second.vertices.size()==8&&second.faces[0].vertices.size()==5);
  assert((second.faces[0].vertices==std::vector<std::uint32_t>{0,1,2,3,0}));
  assert((second.vertex_faces[0]==std::vector<std::uint32_t>{0,0,2,4}));
  assert((result.vertex_faces[0]==std::vector<std::uint32_t>{0,2,4}));
 }
 auto collinear=source;collinear.vertices.push_back({-1,-1,0});
 collinear.faces.erase(collinear.faces.begin(),collinear.faces.begin()+5);
 collinear.faces.insert(collinear.faces.begin(),{5,0,8,1,2,3});
 const auto third=wot::build_vehicle_convex_topology(collinear,&axis_normalize);
 assert(third.faces.size()==6&&third.faces[0].vertices.size()==5);
 assert(third.faces[0].normal_scale==1);
 auto line=source;line.faces={2,0,1};
 const auto degenerate=wot::build_vehicle_convex_topology(line,&axis_normalize);
 assert(degenerate.vertices.size()==8&&degenerate.faces.empty());
 assert(degenerate.normal_directions.empty()&&degenerate.edge_directions.empty());
 for(const auto&adj:degenerate.vertex_faces)assert(adj.empty());
 for(unsigned bad=0;bad!=3;++bad){
  auto invalid=source;
  if(bad==0)invalid.faces={0};
  else if(bad==1)invalid.faces={4,0};
  else invalid.faces={1,999};
  bool rejected=false;
  try{wot::build_vehicle_convex_topology(invalid,&axis_normalize);}
  catch(const std::invalid_argument&){rejected=true;}
  assert(rejected);
 }
 bool rejected=false;
 try{wot::build_vehicle_convex_topology(source,nullptr);}
 catch(const std::invalid_argument&){rejected=true;}
 assert(rejected);
}
'''


class VehiclePhysicsConvexTests(unittest.TestCase):
    def test_actual_core_topology_ownership_and_degenerate_faces(self):
        if not shutil.which('c++'):
            raise unittest.SkipTest('C++ compiler unavailable: c++')
        with tempfile.TemporaryDirectory(prefix='vehicle-convex-contract-') as temporary:
            root = Path(temporary)
            (root / 'probe.cpp').write_text(SOURCE)
            command = ['c++', '-std=c++17', '-Wall', '-Wextra', '-Werror',
                       '-ffp-contract=off', '-fno-fast-math', '-fsanitize=address,undefined',
                       '-I', str(ROOT / 'include'), str(root / 'probe.cpp')]
            command += [str(ROOT / 'src' / name) for name in
                        ['vehicle_physics.cpp', 'vehicle_dynamics.cpp', 'point_velocity.cpp']]
            subprocess.run(command + ['-o', str(root / 'probe')], check=True)
            subprocess.run([str(root / 'probe')], check=True, env=dict(os.environ,
                           ASAN_OPTIONS='detect_leaks=0'))


if __name__ == '__main__':
    unittest.main()
