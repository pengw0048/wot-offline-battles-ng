"""Same-step rigid body composition and the complete historical RAM receipt tail."""
import json
import os
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest

from test_stock_vehicle_physics_owner import block
from native_test_compile_flags import native_test_compile_flags
from test_stock_hydraulic_receipt import SERVER

ROOT = Path(__file__).resolve().parents[1]
FORK = os.environ.get('WOT_FORK_0101_SOURCE')
sys.path.insert(0, str(ROOT / 'wire'))
import stage_server

HOST = r'''
#include "stock_hydraulic_matrix_fixture.hpp"
#include "stock_native_bot_publication.hpp"
#include <cassert>
#include <iomanip>
#include <iostream>
namespace geometry {
using Point=std::array<double,3>;
BW::Vector3 native(Point p){return {float(p[0]),float(p[1]),float(p[2])};}
BW::Matrix product(BW::Matrix a,const BW::Matrix&b){a.postMultiply(b);return a;}
@REBASE@
}
BW::Matrix matrix(const std::array<double,6>&p){BW::Matrix m;m.setRotate(float(p[3]),float(p[4]),float(p[5]));m.translation({float(p[0]),float(p[1]),float(p[2])});return m;}
std::array<double,8> receipt(const BW::Matrix&m){auto p=m.applyToOrigin();return {p.x,p.y,p.z,m.yaw(),m.pitch(),m.roll(),.5,-.1};}
void close(const BW::Matrix&a,const BW::Matrix&b){for(unsigned i=0;i<4;++i)for(unsigned j=0;j<3;++j)assert(std::abs(a.rows[i][j]-b.rows[i][j])<.00005f);}
int main(){
 for(unsigned i=0;i<100;++i){const double n=double(i)/100.;const std::array<double,6>baseline{10.+n,20.-n,30.,.7,.2+n*.05,-.3},proposal{14.,17.+n,35.,-.4,-.25,.18};
  auto old=matrix(baseline),next=matrix(proposal);BW::Matrix aim,relative;aim.setRotate(0.f,float(.1+n*.1),0.f);aim.translation({0.f,float(1.2*std::sin(.1+n*.1)),float(1.2*(1.-std::cos(.1+n*.1)))});relative.setRotate(.04f,-.03f,.02f);relative.translation({.1f,-.2f,.3f});
  // This is the local native composition: relative * (held aim * ground).
  const auto applied=geometry::product(relative,geometry::product(aim,old));const auto source=receipt(applied);const auto result=geometry::rebaseHydraulicBody(source,baseline,proposal);assert(result);assert((*result)[6]==source[6]&&(*result)[7]==source[7]);close(matrix({(*result)[0],(*result)[1],(*result)[2],(*result)[3],(*result)[4],(*result)[5]}),geometry::product(relative,geometry::product(aim,next)));
  assert(geometry::rebaseHydraulicBody(source,baseline,baseline)==source);
 }
 namespace c=stock_native_bot_contacts;namespace pub=stock_native_bot_publication;
 c::Descriptor descriptor;descriptor.drive.mass=25000.;descriptor.collision_shape={1.5,3.5,-.8,2.};descriptor.half_width=1.5;descriptor.half_length=3.5;descriptor.side_full_y=descriptor.side_full=descriptor.side_min=1.;descriptor.drive.t[0]=9.81;for(auto&v:descriptor.drive.terrain)v=1.;
 c::Motion motion;motion.id=1;motion.position={0.,0.,10.};motion.speed=12.;motion.alive=true;std::map<c::ID,c::Actor>actors;actors[1].motion=&motion;actors[1].descriptor=&descriptor;actors[1].team=1;
 c::Human human;human.network_id=2;human.body.team=2;human.body.flags=offline_contact::Alive|offline_contact::HasY;human.body.mass=25000.;human.body.shape=descriptor.collision_shape;human.body.z=8.;const std::array<double,8>body{2.,3.25,4.5,.2,.3,.4,.5,.6};human.hydraulic_body=body;
 c::Contacts contacts(1000000);contacts.translations(actors,{human},{{1,{0.,0.,0.}}},1000000,{1});assert(actors[1].ram_motion.size()==1);auto frozen=actors[1].ram_motion.at(2);assert(frozen.player_hydraulic_body==body);assert(frozen.player_position==c::Point({0.,0.,8.}));human.hydraulic_body=std::array<double,8>{999.,0.,0.,0.,0.,0.,0.,0.};assert(frozen.player_hydraulic_body==body);
 pub::Input input;input.flags=pub::Alive|pub::WorldPose|pub::RamMotion;input.scalars[0]=1;input.scalars[17]=5.;input.scalars[27]=input.scalars[28]=1000.;input.motions={frozen};const auto row=pub::encode(input,0.);assert(row[1]&pub::RamMotionBodies);std::cout<<'[';for(unsigned i=0;i<row.size();++i){if(i)std::cout<<',';std::cout<<row[i];}std::cout<<']';
}
'''

TAIL = r'''
import json
encoded=json.load(open(sys.argv[3]));decoded=lan_battle_server.bot_state_codec.decode_row(encoded,{})
body=[2.,3.25,4.5,.2,.3,.4,.5,.6]
assert decoded['ram_motion'][0][21]=={'hydraulic_body':body},decoded['ram_motion']
assert decoded['ram_motion'][0][12:15]==[0.,0.,8.]
assert lan_battle_server.bot_state_codec.decode_row(lan_battle_server.bot_state_codec.encode_row(decoded),{})['ram_motion']==decoded['ram_motion']
identity=dict(id=1,team=1,slot=0,name='Bot',vehicle='ussr:R11_MS-1',max_health=1000,profile={},route={})
canonical=BattleState._sanitize_bot_state(decoded,identity,None,trusted=True)
assert canonical['ram_motion'][0][21]['hydraulic_body']==body
state.phase='battle';state.bot_states[1]=canonical
for endpoint in state.players.values():endpoint.snapshot_tick_sent=-1
state.tick_once(0.);snapshot=dict(snapshots[-1])
full=schema.SCHEMA.decode(schema.SCHEMA.encode(snapshot));assert full['bots'][0]['ram_motion'][0][21]['hydraulic_body']==body
out,base=split.encode(snapshot,None,999,False);visible=schema.SCHEMA.decode(schema.SCHEMA.encode(out));assert visible['bot_states'][0]['ram_motion'][0][21]['hydraulic_body']==body
# An absent receipt is a typed null tail, with the original 21 values intact.
unknown=list(decoded['ram_motion'][0]);unknown[21]={'hydraulic_body':None}
normalized=lan_battle_server.ram_motion.normalize([unknown])[0];assert normalized[:21]==unknown[:21] and normalized[21]['hydraulic_body'] is None
for bad in ({'hydraulic_body':[True]*8},{'hydraulic_body':[0.]*7},{'hydraulic_body':[float('nan')]*8},{'hydraulic_body':None,'extra':1}):
 try:lan_battle_server.ram_motion.normalize([unknown[:21]+[bad]])
 except ValueError:pass
 else:raise AssertionError('malformed RAM receipt accepted')
print('Historical RAM compact publication, staged server validation/full/replica and typed nullable body tail passed')
'''

@unittest.skipUnless(FORK, 'Pinned gameplay source is required')
class HydraulicQuery(unittest.TestCase):
    def test_staging_rejects_missing_duplicate_and_disconnected_receipt_seams(self):
        package = Path(FORK) / 'src/res/scripts/client/gui/mods/offline_lan_0922'
        for name, adapt, marker in (
                ('ram_motion', stage_server.ram_motion_source, 'ROW_LENGTH = 21'),
                ('bot_state_codec', stage_server.bot_state_codec_source, 'F_HAS_RAM_MOTION = 1 << 21'),
                ('bot_state_codec', stage_server.bot_state_codec_source,
                 '[_real(cursor.take(), scale) for scale in ram_motion.SCALES])')):
            source = (package / (name + '.py')).read_text()
            for mutation in (source.replace(marker, '', 1), source + '\n' + marker):
                with self.subTest(name=name, marker=marker), self.assertRaisesRegex(ValueError, 'receipt seam'):
                    adapt(mutation)

    def test_noncommuting_same_step_rebase_and_historical_ram_chain(self):
        with tempfile.TemporaryDirectory(prefix='hydraulic-query-') as temporary:
            folder = Path(temporary);fork = Path(FORK)
            law = (fork / 'native/offline_release_drive_python.inc').read_text().split('PyObject *WOT_CDECL release_drive(')[0]
            (folder / 'offline_release_drive_law.hpp').write_text('#pragma once\n#include <algorithm>\n#include <cmath>\n' + law)
            rebase = block((ROOT / 'bigworld/stock_native_bot_vehicle_geometry.cpp').read_text(), 'std::optional<std::array<double,8>> rebaseHydraulicBody(')
            (folder / 'probe.cpp').write_text(HOST.replace('@REBASE@', rebase))
            sources = ['stock_native_bot_publication', 'stock_native_bot_contacts', 'stock_native_bot_motion', 'stock_native_bot_combat']
            command = ['c++', '-std=c++17', '-O0', '-Wall', '-Wextra', '-Werror', '-ffp-contract=off',
                       *native_test_compile_flags(), '-I' + str(folder), '-I' + str(ROOT / 'tests'),
                       '-I' + str(ROOT / 'bigworld'), '-I' + str(fork / 'native'), str(folder / 'probe.cpp')]
            command += [str(ROOT / 'bigworld' / (name + '.cpp')) for name in sources]
            command += [str(fork / 'native' / name) for name in ('offline_contact_roster.cpp', 'offline_math_batch_geometry.cpp')]
            subprocess.run(command + ['-o', str(folder / 'probe')], check=True)
            row = subprocess.check_output([str(folder / 'probe')], text=True)
            json.loads(row);(folder / 'row.json').write_text(row)
            stage_server.stage(str(fork / 'server'), str(folder / 'server'))
            run = subprocess.run([sys.executable, '-B', '-c', SERVER + TAIL, str(folder / 'server'), FORK,
                                  str(folder / 'row.json')], capture_output=True, text=True)
            self.assertEqual(run.returncode, 0, run.stdout + run.stderr)
            print(run.stdout.strip())

if __name__ == '__main__':
    unittest.main()
