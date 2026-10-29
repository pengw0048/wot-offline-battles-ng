"""Differential checks against the fixed gameplay's critical damage rules."""
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
SOURCE = os.environ.get('WOT_GAMEPLAY_SOURCE')
PROBE = r'''
#include "stock_native_bot_combat.hpp"
#include <iostream>
#include <iomanip>
using namespace wot::offline_bot_step;
int main(){
 CombatState s;s.health=s.maximum_health=100;
 s.critical.devices["engineHealth"]={0.,100.,50.,true,false,100.};
 advance_critical(s,1.,1.,.57,1.);
 std::cout<<std::setprecision(17)<<*s.critical.devices["engineHealth"].hp<<' '<<s.critical.devices["engineHealth"].destroyed<<'\n';
 CombatState b;b.health=b.maximum_health=100;b.critical.fire=true;
 b.critical.devices["ammoBayHealth"]={100.,100.,50.,false,false,100.};
 b.critical.devices["engineHealth"]={50.,100.,50.,false,true,100.};
 b.critical.devices["fuelTankHealth"]={10.,100.,50.,false,true,100.};
 auto result=advance_critical(b,1.,1.,.57,1.);
 std::cout<<result.fire_damage<<' '<<*b.critical.devices["ammoBayHealth"].hp<<' '
 <<*b.critical.devices["engineHealth"].hp<<' '<<*b.critical.devices["fuelTankHealth"].hp<<' '
 <<b.critical.devices["fuelTankHealth"].destroyed<<'\n';
 CombatState state;state.health=state.maximum_health=100;state.critical.devices["engineHealth"]={0.,100.,50.,true,false,100.};
 CombatLedger ledger;ledger.initialize(state,0,0,0);CombatReplayStep step;step.elapsed=1.;step.now=1.;
 advance_critical(state,1.,1.,.57,1.,false);ledger.unpublished_steps.push_back(step);ledger.mark_publication(state);
 ServerCombat hit;hit.record=combat_record(state);hit.record.health=90;hit.record.critical.devices["engineHealth"].hp=0.;
 hit.revision=1;hit.base_revision=1;ledger.reconcile(state,hit,.57,1.);
 std::cout<<state.health<<' '<<*state.critical.devices["engineHealth"].hp<<' '<<ledger.next_sequence<<' '<<ledger.unpublished_steps.size()<<'\n';
 CombatState takeover;takeover.health=takeover.maximum_health=100;
 takeover.critical.devices["engineHealth"]={0.,100.,50.,true,false,100.};
 CombatLedger handoff;handoff.initialize(takeover,0,0,0);handoff.authority_handoff_pending=true;
 ServerCombat incoming;incoming.record=combat_record(takeover);incoming.record.health=85;
 incoming.revision=1;incoming.base_revision=0;incoming.acknowledged_sequence=4;
 handoff.reconcile(takeover,incoming,.57,1.);
 std::cout<<takeover.health<<' '<<handoff.next_sequence<<' '<<handoff.acknowledged_sequence<<' '
 <<handoff.authority_handoff_pending<<' '<<handoff.pending.size()<<'\n';
}
'''

@unittest.skipUnless(SOURCE, 'set WOT_GAMEPLAY_SOURCE to the fixed gameplay source')
class CombatDifferential(unittest.TestCase):
    def test_repair_and_fire(self):
        scripts = str(Path(SOURCE) / 'src/res/scripts/client')
        sys.path.insert(0, scripts)
        try:
            from gui.mods.offline_lan_0922 import critical_damage
            td = types.SimpleNamespace(_offline_critical_device_pools={
                name: (100,50) for name in ('engineHealth','ammoBayHealth','fuelTankHealth')},
                miscAttrs={}, type=types.SimpleNamespace(crewRoles=()))
            def vehicle(devices, destroyed=(), fire=False):
                return types.SimpleNamespace(id=1, health=100, maxHealth=100, typeDescriptor=td,
                    devices_hp=devices, _destroyed_devices=set(destroyed), _critical_devices=set(),
                    _crew_ko=set(), is_on_fire=fire, _fire_started=0., _fire_timer=0.,
                    _ammo_rack_death=False)
            repaired=vehicle({'engineHealth':0.}, ['engineHealth'])
            critical_damage.tick_repair(repaired,1.,repair_factor=.57)
            burning=vehicle({'engineHealth':50.,'ammoBayHealth':100.,'fuelTankHealth':10.},fire=True)
            with contextlib.redirect_stdout(io.StringIO()):
                damage,_=critical_damage.tick_fire(burning,1.,now=1.)
            from gui.mods.offline_lan_0922.bot_runtime import BotRuntime
            runtime=BotRuntime.__new__(BotRuntime);runtime._combat_sync={};runtime._descriptors={1:td};runtime._equipment_passives={};runtime._friendly_repositions={}
            runtime._bot_stunned=lambda unused:False;runtime._bot_repair_factor=lambda unused,descriptor:.57
            payload={'devices':[{'name':'engineHealth','hp':0.,'max_hp':100.,'state':'destroyed'}],
                     'destroyed':['engineHealth'],'crew_ko':[],'fire':False,'ammo_rack_death':False,'events':[]}
            state=dict(id=1,health=100,max_health=100,alive=True,critical=payload,combat_revision=0,
                       combat_base_revision=0,combat_ack_seq=0,combat_fire_elapsed=0.,combat_fire_timer=0.,stun_end_server_time_ms=0)
            runtime._combat_sync_state(state)
            runtime._advance_bot_critical(state,1.,1.,record_step=False,advance_fire=False,equipment_effects=[])
            runtime._combat_sync[1]['unpublished_steps']=[(1.,1.,False,())];runtime._mark_combat_publication(state)
            raw=dict(health=90,alive=True,critical=payload,combat_revision=1,combat_base_revision=1,
                     combat_ack_seq=0,combat_fire_elapsed=0.,combat_fire_timer=0.,stun_end_server_time_ms=0)
            runtime._apply_server_combat_state(state,raw,1)
            lineage=(state['health'],state['critical']['devices'][0]['hp'],runtime._combat_sync[1]['next_seq'],
                     len(runtime._combat_sync[1]['unpublished_steps']))
            takeover=dict(id=2,health=100,max_health=100,alive=True,critical=payload,combat_revision=0,
                          combat_base_revision=0,combat_ack_seq=0,combat_fire_elapsed=0.,combat_fire_timer=0.,stun_end_server_time_ms=0)
            sync=runtime._combat_sync_state(takeover);sync['authority_handoff_pending']=True
            incoming=dict(raw,health=85,combat_revision=1,combat_base_revision=0,combat_ack_seq=4)
            runtime._apply_server_combat_state(takeover,incoming,2)
            handoff=(takeover['health'],sync['next_seq'],sync['acked_seq'],sync['authority_handoff_pending'],len(sync['pending']))
        finally:
            sys.path.remove(scripts)
        with tempfile.TemporaryDirectory() as folder:
            path=Path(folder); (path/'probe.cpp').write_text(PROBE)
            subprocess.run(['c++','-std=c++17','-I',str(ROOT/'bigworld'),
                str(ROOT/'bigworld/stock_native_bot_combat.cpp'),str(path/'probe.cpp'),
                '-o',str(path/'probe')],check=True)
            lines=subprocess.check_output([str(path/'probe')],text=True).splitlines()
        first=[float(x) for x in lines[0].split()]
        self.assertAlmostEqual(first[0],round(repaired.devices_hp['engineHealth'],3),places=11)
        self.assertEqual(bool(first[1]),'engineHealth' in repaired._destroyed_devices)
        second=[float(x) for x in lines[1].split()]
        expected=(damage,burning.devices_hp['ammoBayHealth'],burning.devices_hp['engineHealth'],
                  burning.devices_hp['fuelTankHealth'],'fuelTankHealth' in burning._destroyed_devices)
        self.assertEqual(second,list(expected))
        third=[float(x) for x in lines[2].split()]
        self.assertEqual(third,list(lineage))
        self.assertEqual([float(x) for x in lines[3].split()],list(handoff))

if __name__ == '__main__':
    unittest.main()
