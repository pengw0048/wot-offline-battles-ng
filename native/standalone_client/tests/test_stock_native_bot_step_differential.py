"""Compare the typed suspension kernel with the pinned gameplay Python law.

Set WOT_GAMEPLAY_SOURCE to the extracted fixed gameplay checkout.
"""
import importlib.util
import contextlib
import io
import os
from pathlib import Path
import subprocess
import sys
import tempfile
import types
import unittest

ROOT = Path(__file__).resolve().parents[1]
GAMEPLAY = os.environ.get('WOT_GAMEPLAY_SOURCE')

PROBE = r'''
#include "stock_native_bot_step_state.hpp"
#include <iomanip>
#include <iostream>
using namespace wot::offline_bot_step;
struct World : VerticalWorld {
  std::optional<double> base=0.;
  std::optional<double> initial_ground(double,double,double) override { return 0.; }
  std::optional<double> basic_ground(double,double,double,std::optional<double> ceiling) override {
    return base && (!ceiling || *base<=*ceiling) ? base : std::nullopt;
  }
  std::optional<double> ground(double,double,double low,double high,std::optional<double>) override {
    return low<=0. && high>=0. ? std::optional<double>(0.) : std::nullopt;
  }
  bool turret_clear(const VerticalState&,const VerticalState&) override { return true; }
  std::array<double,2> passive_move(VerticalState& s,double x,double z,double) override {
    s.position[0]+=x; s.position[2]+=z; return {x,z};
  }
};
int main() {
  const std::array<double,5> p{10000.,10000.,10000.,.02,20.};
  const std::array<double,9> t{12.2625,.1,4.,.6,.4,.15,.35,.06,12.};
  for(int k=0;k<3;++k) {
    SuspensionContact c; c.x=1.; c.y=-.5; c.penetration=.01; c.rigid=true;
    if(k!=2) c.ground=0.;
    auto r=suspension_solve(p,{.5,-1.,0.,0.,.2,0.},{c},k==1?0.:.1,0.,t,12);
    std::cout << std::setprecision(17);
    for(double value:r.state) std::cout << value << ' ';
    std::cout << r.contacts << ' ' << r.rigid << ' ' << r.touched << ' '
      << r.left << ' ' << r.right << ' ' << r.impact_speed.has_value() << ' '
      << r.impact_speed.value_or(0.) << '\n';
  }
  SuspensionDescriptor d; d.parameters=p; d.tuning=t; d.maximum_steps=12;
  d.width=2.; d.clearance=.5; d.rest_length=.5;
  for (int x : {-1,1}) for (int z : {-1,1}) {
    SuspensionContact c; c.spring=true; c.x=x; c.z=z; c.side=x;
    c.compression=.1; c.maximum=.3; c.rest_length=.5;
    c.stiffness=40000.; c.damping=5000.; c.force=100000.; d.springs.push_back(c);
  }
  VerticalState s; s.position={0.,.1,0.}; s.grounded_once=true;
  VerticalMemory memory; World world;
  auto result=advance_vertical(s,memory,d,world,0.,0.,s.position,s.position,.1);
  std::cout << std::setprecision(17) << s.position[1] << ' ' << s.vertical_speed << ' '
      << s.pitch << ' ' << s.roll << ' ' << s.airborne << ' ' << result.blocked << '\n';
  for (int i=0;i<3;++i) {
    world.base=i==1 ? std::nullopt : std::optional<double>(i==2 ? 2. : 0.);
    VerticalState body; body.position={0.,i==1 ? 1. : 0.,0.}; body.grounded_once=true;
    int budget=1;
    auto outcome=advance_basic_vertical(body,d,world,body.position,.1,budget);
    std::cout << body.position[1] << ' ' << body.vertical_speed << ' ' << body.airborne << ' ' << outcome.blocked << '\n';
  }
}
'''

@unittest.skipUnless(GAMEPLAY, 'set WOT_GAMEPLAY_SOURCE to the fixed gameplay source')
class SuspensionDifferential(unittest.TestCase):
    def test_rigid_edge_airborne_and_zero_dt(self):
        base = Path(GAMEPLAY) / 'src/res/scripts/client/gui/mods/offline_lan_0922'
        # vehicle_physics has only one module-level dependency, its timer decorator.
        name = 'gui.mods.offline_lan_0922.worker_diagnostics'
        previous = sys.modules.get(name)
        stub = types.ModuleType(name)
        stub.observed = lambda unused: (lambda function: function)
        sys.modules[name] = stub
        try:
            spec = importlib.util.spec_from_file_location('fixed_vehicle_physics', base / 'vehicle_physics.py')
            physics = importlib.util.module_from_spec(spec)
            spec.loader.exec_module(physics)
        finally:
            if previous is None:
                del sys.modules[name]
            else:
                sys.modules[name] = previous
        with tempfile.TemporaryDirectory() as folder:
            path = Path(folder)
            (path / 'probe.cpp').write_text(PROBE)
            subprocess.run(['c++', '-std=c++17', '-I', str(ROOT / 'bigworld'),
                str(ROOT / 'bigworld/stock_native_bot_step_state.cpp'),
                str(path / 'probe.cpp'), '-o', str(path / 'probe')], check=True)
            output = subprocess.check_output([str(path / 'probe')], text=True).splitlines()
        params = dict(mass=10000., pitch_inertia=10000., roll_inertia=10000.,
            fixed_step=.02, constraint_iterations=20, springs=[], pseudo_contacts=[
                dict(x=1., y=-.5, z=0., penetration=.01, kind='rigid', side=None)])
        state = dict(height=.5, vertical_velocity=-1., pitch=0., pitch_velocity=0.,
                     roll=.2, roll_velocity=0.)
        for index, line in enumerate(output[:3]):
            values = [float(value) for value in line.split()]
            expected = physics._reference_damper_suspension_step(params, state, [],
                0. if index == 1 else .1, [None if index == 2 else 0.], 0.)
            keys = ('height', 'vertical_velocity', 'pitch', 'pitch_velocity', 'roll', 'roll_velocity',
                    'contact_count', 'rigid_contact_count', 'touched_contact_count')
            for actual, key in zip(values, keys):
                self.assertAlmostEqual(actual, expected[key], places=11, msg=key)
            self.assertEqual(bool(values[9]), not expected['left_flying'])
            self.assertEqual(bool(values[10]), not expected['right_flying'])
            self.assertEqual(bool(values[11]), expected['impact_speed'] is not None)
            if expected['impact_speed'] is not None:
                self.assertAlmostEqual(values[12], expected['impact_speed'], places=11)
        springs = [dict(x=x, y=0., z=z, static_compression=.1, max_compression=.3,
            stiffness=40000., damping=5000., max_force=100000.,
            side='left' if x < 0 else 'right') for x in (-1, 1) for z in (-1, 1)]
        params['springs'] = springs
        params['pseudo_contacts'] = []
        state.update(height=.1, vertical_velocity=0., roll=0.)
        expected = physics._reference_damper_suspension_step(params, state, [0.] * 4, .1, [], 0.)
        values = [float(value) for value in output[3].split()]
        for actual, key in zip(values, ('height', 'vertical_velocity', 'pitch', 'roll', 'airborne')):
            self.assertAlmostEqual(actual, expected[key], places=11, msg=key)
        self.assertEqual(values[5], 0.)
        scripts = str(Path(GAMEPLAY) / 'src/res/scripts/client')
        sys.path.insert(0, scripts)
        try:
            from gui.mods.offline_lan_0922.bot_runtime import BotRuntime
            for i, raw in enumerate((0., None, 2.)):
                runtime = BotRuntime.__new__(BotRuntime)
                runtime.round_id = 1
                runtime._suspension_params_for = lambda unused: None
                runtime._suspension_params = {}
                runtime._turn_speeds = {}
                runtime._physics_ground_probe = lambda x,z,h: raw
                runtime._ground_probe_at = lambda x,z,h,maximum_y=None: (
                    raw if raw is not None and (maximum_y is None or raw <= maximum_y) else None)
                runtime._invalidate_realised_motion = lambda *unused: None
                y = 1. if raw is None else 0.
                bot = dict(id=1, x=0., y=y, z=0., yaw=0., pitch=0., terrain_pitch=0.,
                    roll=0., speed=0., vertical_speed=0., grounded_once=True, airborne=False)
                with contextlib.redirect_stdout(io.StringIO()):
                    blocked = runtime._integrate_vertical_motion(bot, .1, (0.,y,0.))
                actual = [float(value) for value in output[4+i].split()]
                for got, want in zip(actual, (bot['y'], bot['vertical_speed'], bot['airborne'], blocked)):
                    self.assertAlmostEqual(got, want, places=11)
        finally:
            sys.path.remove(scripts)

if __name__ == '__main__':
    unittest.main()
