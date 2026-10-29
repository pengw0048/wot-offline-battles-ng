"""Compare physical impact with the pinned BotRuntime and module laws."""
import contextlib
import io
import os
from pathlib import Path
import subprocess
import sys
import tempfile
import types
import unittest

SOURCE=os.environ.get('WOT_GAMEPLAY_SOURCE')
ROOT=Path(__file__).resolve().parents[1]
PROBE=r'''
#include "stock_native_bot_environment.hpp"
#include <iostream>
using namespace wot::offline_bot_step;
struct World:ImpactWorld {
 std::vector<Point> ground_witnesses() override{return {{0.,0.,0.}};}
 std::vector<ImpactHit> material_segment(const Point&,const Point&) override{return {{1.,"engineHealth"},{2.,"fuelTankHealth"}};}
 std::vector<ImpactHit> interior_segment(const Point&,const Point&) override{return {{0.,"driverHealth"}};}
};
int main(){CombatState s;s.alive=true;s.health=s.maximum_health=1000;
 s.descriptor_crew_roster={"driver","commander"};
 s.critical.devices["engineHealth"]={100.,100.,50.,false,false,100.};
 s.critical.devices["fuelTankHealth"]={50.,100.,50.,false,true,100.};
 VerticalState v;EnvironmentTotals totals;World w;
 auto r=apply_environment_fall_damage(1,s,v,20.,true,{},Point{0.,1.,0.},w,totals);
 std::cout<<r.damage<<' '<<s.health<<' '<<*s.critical.devices["engineHealth"].hp<<' '
 <<*s.critical.devices["fuelTankHealth"].hp<<' '<<s.critical.fire<<' '
 <<s.critical.crew_ko.count("driver")<<' '<<totals.fall_damage[1]<<' '
 <<totals.fire_provenance.count(1)<<' '<<totals.critical.size()<<'\n';}
'''

@unittest.skipUnless(SOURCE,'set WOT_GAMEPLAY_SOURCE to the fixed gameplay source')
class EnvironmentDifferential(unittest.TestCase):
    def test_impact(self):
        scripts=str(Path(SOURCE)/'src/res/scripts/client');sys.path.insert(0,scripts)
        try:
            from gui.mods.offline_lan_0922.bot_runtime import BotRuntime
            td=types.SimpleNamespace(_offline_critical_device_pools={'engineHealth':(100,50),'fuelTankHealth':(100,50)},
                miscAttrs={},type=types.SimpleNamespace(crewRoles=(('driver',),('commander',))))
            runtime=BotRuntime.__new__(BotRuntime);runtime._descriptors={1:td};runtime._turn_speeds={};runtime._friendly_repositions={}
            runtime._environment_fall_totals={};runtime._environment_critical_totals={}
            runtime.impact_contacts_resolver=lambda *args:[{'name':'engineHealth','weight':.8},
                {'name':'fuelTankHealth','weight':2./3.},{'name':'driverHealth','weight':1.}]
            state={'id':1,'alive':True,'health':1000,'max_health':1000,'_landing_ground_observed':True,
                'critical':{'devices':[{'name':'engineHealth','hp':100.,'max_hp':100.,'state':'normal'},
                                       {'name':'fuelTankHealth','hp':50.,'max_hp':100.,'state':'critical'}],
                            'destroyed':[],'crew_ko':[],'fire':False,'ammo_rack_death':False}}
            with contextlib.redirect_stdout(io.StringIO()): damage=runtime._apply_bot_fall_damage(state,20.)
            critical=state['critical'];devices={row['name']:row['hp'] for row in critical['devices']}
            expected=[damage,state['health'],devices['engineHealth'],devices['fuelTankHealth'],critical['fire'],
                      'driver' in critical['crew_ko'],runtime._environment_fall_totals[1],
                      state.get('_environment_fire',False),len(runtime._environment_critical_totals)]
        finally:sys.path.remove(scripts)
        with tempfile.TemporaryDirectory() as folder:
            folder=Path(folder);(folder/'probe.cpp').write_text(PROBE)
            subprocess.run(['c++','-std=c++17','-I',str(ROOT/'bigworld'),
                str(ROOT/'bigworld/stock_native_bot_step_state.cpp'),str(ROOT/'bigworld/stock_native_bot_combat.cpp'),
                str(ROOT/'bigworld/stock_native_bot_environment.cpp'),str(folder/'probe.cpp'),'-o',str(folder/'probe')],check=True)
            actual=[float(x) for x in subprocess.check_output([str(folder/'probe')],text=True).split()]
        self.assertEqual(actual,expected)

if __name__=='__main__':unittest.main()
