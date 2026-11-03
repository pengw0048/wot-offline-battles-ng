"""Verify the staged 1.1.1 radio core against the direct-sharing contract."""
import importlib.util
import os
from pathlib import Path
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]


class NativeRadioV111(unittest.TestCase):
    def test_leases_death_directive_and_relay(self):
        source = os.environ.get('WOT_FORK_SOURCE')
        if not source:
            self.skipTest('WOT_FORK_SOURCE is required')
        native = Path(source) / 'native'
        spec = importlib.util.spec_from_file_location('patch_offline_radio', ROOT / 'bigworld/patch_offline_radio.py')
        patch = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(patch)
        with tempfile.TemporaryDirectory() as folder:
            out = Path(folder)
            for name in ('offline_simulation_types.h',):
                (out / name).write_text((native / name).read_text())
            (out / 'offline_simulation_control.h').write_text(patch.patch_header((native / 'offline_simulation_control.h').read_text()))
            (out / 'offline_simulation_control.cpp').write_text(patch.patch((native / 'offline_simulation_control.cpp').read_text()))
            host = out / 'host.cpp'
            host.write_text(r'''
#include "offline_simulation_control.h"
#include <cassert>
#include <cmath>
using namespace offline_simulation;
using namespace offline_simulation::control;
int main() {
  Store store;
  const ActorKey observer{ActorKind::Human,1}, ally{ActorKind::Bot,2}, newcomer{ActorKind::Bot,3}, enemy{ActorKind::Bot,4}, new_enemy{ActorKind::Bot,5};
  std::vector<ActorKey> keys{observer,ally,newcomer,enemy,new_enemy};
  return 0;
}
''')
            # The host exercises real sight, the death edge, frozen recipients,
            # Last Effort, directive duration and non-cascading relay.
            body = host.read_text().replace('  return 0;\n}', r'''
  std::vector<ActorConfig> configs;
  for (auto k:keys) { ActorConfig c; c.key=k; c.team=k.id>=4?2:1; c.radio=100.; c.designated_duration=14.; c.last_effort_duration=5.; configs.push_back(c); }
  std::vector<ActorSample> rows;
  for (auto k:keys) { ActorSample s; s.key=k; s.pose.mask=1|2|4|8|(1u<<6); s.pose.position={0.,0.,0.}; s.pose.values[6]=0.; rows.push_back(s); }
  rows[0].last_effort=true; rows[0].designated=true; rows[1].pose.position={100.,0.,0.}; rows[2].pose.position={1000.,0.,0.}; rows[3].pose.position={0.,0.,40.}; rows[4].pose.position={0.,0.,500.};
  store.configure(configs);
  auto frame=[&](int n,double now) {store.update(rows);store.begin({1,1,n,0},keys,100,false,true,now);store.observe_humans([](const SightRequest& r){SightReply x;x.status=QueryStatus::DetectionComplete;x.has_detection=true;x.detected=true;x.sampled_at=r.now;return x;});store.finish();};
  frame(1,0.); assert(std::abs(store.recipient_contact(ally,enemy,0.).remaining-14.)<1e-8);
  rows[0].alive=false;rows[0].last_effort=false;rows[2].pose.position={0.,0.,0.};rows[4].pose.position={0.,0.,40.};
  frame(2,1.);assert(store.recipient_contact(ally,enemy,1.).remaining>0.);assert(store.recipient_contact(newcomer,enemy,1.).remaining==0.);assert(store.recipient_contact(ally,new_enemy,1.).remaining==0.);
  frame(3,2.);assert(store.recipient_contact(newcomer,enemy,2.).remaining==0.);assert(!store.recipient_sources(ally,enemy,2.).empty());
  frame(4,7.);assert(store.recipient_contact(ally,enemy,7.).remaining>0.);
  frame(5,15.);assert(store.recipient_contact(ally,enemy,15.).remaining==0.);
  Store narrow; configs[0].designated_sector=.02; rows[0].alive=true;rows[0].designated=true;rows[3].pose.position={2.,0.,40.};
  narrow.configure(configs);narrow.update(rows);narrow.begin({1,1,1,0},keys,100,false,true,0.);
  narrow.observe_humans([](const SightRequest&r){SightReply x;x.status=QueryStatus::DetectionComplete;x.has_detection=true;x.detected=true;x.sampled_at=r.now;return x;});
  assert(std::abs(narrow.recipient_contact(ally,enemy,0.).remaining-10.)<1e-8);narrow.finish();
  Store relay;std::vector<ActorConfig> rc(3);std::vector<ActorSample> rs(3);std::vector<ActorKey> rk;
  for(int i=0;i<3;++i){rk.push_back({ActorKind::Bot,10+i});rc[i].key=rk[i];rc[i].team=1;rc[i].radio=100.;rs[i].key=rk[i];rs[i].pose.position={double(i*210),0.,0.};}
  rc[0].relay=.1;rc[1].relay=.2;rs[1].pose.position={190.,0.,0.};rs[2].pose.position={395.,0.,0.};relay.configure(rc);relay.update(rs);relay.begin({1,1,1,0},rk,0,false,false,0.);
  auto ranges=relay.radio_actors();assert(std::abs(ranges[0].range-120.)<1e-8);assert(std::abs(ranges[1].range-110.)<1e-8);assert(std::abs(ranges[2].range-100.)<1e-8);assert(relay.radio_connected(rk[1],rk[2]));relay.finish();
  return 0;
}''')
            host.write_text(body)
            binary = out / 'host'
            built = subprocess.run(['c++', '-std=c++17', '-O1', '-I'+folder,
                str(out/'offline_simulation_control.cpp'), str(host), '-o', str(binary)],
                text=True, capture_output=True)
            self.assertEqual(0, built.returncode, built.stderr)
            run = subprocess.run([str(binary)], capture_output=True, text=True)
            self.assertEqual(0, run.returncode, run.stderr)

    def test_python_radio_oracle_retains_old_receivers_only(self):
        source = os.environ.get('WOT_FORK_SOURCE')
        if not source:
            self.skipTest('WOT_FORK_SOURCE is required')
        import sys
        path = str(Path(source) / 'src/res/scripts/client')
        sys.path.insert(0, path)
        try:
            from gui.mods.offline_lan_0922.radio import RadioNetwork
            from gui.mods.offline_lan_0922 import spotting
            network = RadioNetwork()
            observer, ally, newcomer, target = ('human', 1), ('bot', 2), ('bot', 3), ('bot', 4)
            initial = {observer:(1,(0.,0.,0.),100.), ally:(1,(100.,0.,0.),100.),
                       newcomer:(1,(1000.,0.,0.),100.)}
            network.configure(initial, 0.)
            network.observe(observer, target, 0., 14., {'position': (0.,0.,40.)})
            remaining = {ally:(1,(100.,0.,0.),100.), newcomer:(1,(0.,0.,0.),100.)}
            network.configure(remaining, 1., retiring=(observer,))
            self.assertEqual(13., network.contact(ally,target,1.)[0])
            self.assertEqual(0., network.contact(newcomer,target,1.)[0])
            self.assertEqual(14., spotting.MAX_SPOT_MEMORY_SECONDS)
        finally:
            sys.path.remove(path)

    def test_patch_refuses_drift(self):
        spec = importlib.util.spec_from_file_location('patch_offline_radio', ROOT / 'bigworld/patch_offline_radio.py')
        patch = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(patch)
        with self.assertRaises(ValueError):
            patch.patch('unrelated source')


if __name__ == '__main__':
    unittest.main()
