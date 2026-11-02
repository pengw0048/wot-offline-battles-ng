"""The applied Human body receipt survives native freeze, wire and server turnabout."""
import os
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest

from test_stock_vehicle_physics_owner import block
from test_stock_wire_codec import python27_audit_runtime

ROOT = Path(__file__).resolve().parents[1]
FORK = os.environ.get('WOT_FORK_0101_SOURCE')
sys.path.insert(0, str(ROOT / 'wire'))
from codec import WireError
from schema import SCHEMA
import replica_split
import stage_server

BODY = [11., 12.25, 13.5, .7, -.3, .2, -.4, .15]

NATIVE = r'''
import sys,types,threading,copy
import hydraulic_receipt_test as native
source=sys.argv[1]+'/src/res/scripts/client';sys.path.insert(0,source)
for name in ('gui','gui.mods'):
 m=types.ModuleType(name);m.__path__=[source+'/'+name.replace('.','/')];sys.modules[name]=m
from gui.mods.offline_lan_0922 import lan_client as lan
class O(object):
 def __init__(self,**values):self.__dict__.update(values)
# Providers are deliberately unrelated to baseline pose and raw aiming intent.
body=O(translation=(11.,12.25,13.5),yaw=.7,pitch=-.3,roll=.2)
turret=O(yaw=-.4);gun=O(pitch=.15);entity=O(appearance=O(turretMatrix=turret,gunMatrix=gun))
math=O(Matrix=lambda provider:provider)
expected=(11.,12.25,13.5,.7,-.3,.2,-.4,.15)
assert native.sample(math,body,entity,1)==dict(driver=expected,actor=expected)
assert native.sample(math,body,entity,0)==dict(driver=None,actor=None)
entity.appearance.gunMatrix=None
assert native.sample(math,body,entity,1)==dict(driver=None,actor=None)
assert native.frozen()==[(1.,2.1,3.2,.2,.3,.4,.5,.6),None,(4.,5.1,6.2,.7,.8,.9,1.,1.1)]
row=dict(id=1,x=1.,y=2.,z=3.,yaw=.1,pitch=.02,roll=.03,health=10,alive=True,hydraulic_body=list(expected))
def message(row):return dict(round_id=7,players=[row],bots=[])
a=native.accept(message(row),1);assert a['current']==a['target']==expected,a
row['hydraulic_body'][4]=-.5
b=native.accept(message(row),0);assert b['events']==1 and b['target'][4]==-.5,b
row.pop('hydraulic_body');c=native.accept(message(row),0);assert c['current'] is None and c['target'] is None,c
row['hydraulic_body']=list(expected);native.accept(message(row),0)
row['alive']=False;d=native.accept(message(row),0);assert d['current']==d['target']==expected,d
row.pop('hydraulic_body');d=native.accept(message(row),0);assert d['current'] is None and d['target'] is None and d['events']==1,d
native.configure_input(lan)
def client():
 c=lan.LANClient('localhost',28782,'P','ussr:R11_MS-1');c.phase='battle';c.ready=True;c.round_id=7;c.connected=True;c.running=True;c.sock=object();c._stopping=False;c._outbound_accepting=True;c.capabilities=[];c.server_capabilities=[];c._flush_native_outbound=lambda generation:None
 return c
c=client();receipt=list(expected)
assert native.send_input(c,forward=0.,turn=0.,position=(1.,2.,3.),yaw=.1,pitch=.02,hydraulic_body=receipt)
wire=c._outbound_queue[-1][1];assert tuple(wire['hydraulic_body'])==expected and wire['pitch']==.02
receipt[0]=999.;assert tuple(wire['hydraulic_body'])==expected
for bad in ([],[0.]*7,[0.]*9,[True]+[0.]*7,['1']+[0.]*7,[float('nan')]+[0.]*7,[2001.]+[0.]*7):
 before=c._input_seq;assert not native.send_input(c,forward=0.,turn=0.,position=(1.,2.,3.),hydraulic_body=bad);assert c._input_seq==before
for position in (None,(),(1.,2.)):
 before=c._input_seq;assert not native.send_input(c,forward=0.,turn=0.,position=position,hydraulic_body=expected);assert c._input_seq==before
assert native.send_input(c,forward=0.,turn=0.,position=(1.,2.,3.))
assert 'hydraulic_body' not in c._outbound_queue[-1][1]
print('Native applied receipt, body-only update, absence/death clearing, frozen barriers and immutable input passed')
'''

SERVER = r'''
import sys
sys.path[:0]=[sys.argv[1],sys.argv[2]+'/tests',sys.argv[2]+'/src/res/scripts/client']
import lan_battle_server
assert 'hydraulic_body' in lan_battle_server.Player.__dataclass_fields__,lan_battle_server.__file__
from test_port_0922_server_projectiles import _state
from test_port_0922_input_ledger import _frame
from lan_battle_server import BattleState
import battle_wire_schema as schema
import battle_wire_replica as split
state=_state();player=state.players[1]
body=[11.,12.25,13.5,.7,-.3,.2,-.4,.15]
frame=_frame(state,hydraulic_body=body,pitch=.02)
frame=schema.SCHEMA.decode(schema.SCHEMA.encode(frame));state.update_input(1,frame)
assert player.hydraulic_body==tuple(body) and player.pitch==.02
assert player.pose_history[-1]['hydraulic_body']==tuple(body)
row=BattleState._public_player(player,False);assert row['hydraulic_body']==tuple(body)
# Whole-frame rejection leaves prior gameplay/receipt intact and consumes just
# this ordered decision; the next healthy frame still clears the old body.
for bad in ([],[0.]*7,[0.]*9,[True]+[0.]*7,['1']+[0.]*7,[float('nan')]+[0.]*7,[2001.]+[0.]*7):
 before=(player.input_seq,player.x,player.hydraulic_body)
 seq=player.input_processed_seq+1
 state.update_input(1,_frame(state,hydraulic_body=bad,x=99.))
 assert (player.input_seq,player.x,player.hydraulic_body)==before
 assert player.input_processed_seq==seq
state.update_input(1,_frame(state))
assert player.hydraulic_body is None and 'hydraulic_body' not in player.pose_history[-1]
# Complete worker snapshot and visible split both encode the same body sample.
player.hydraulic_body=tuple(body)
snapshots=[]
def capture(message,*args,**kwargs):
 if message.get('type')=='snapshot':snapshots.append(message)
 return True
for endpoint in state.players.values():
 endpoint.offer_snapshot=endpoint.offer_reliable=endpoint.offer_snapshot_repair=capture
state.tick_once(0.)
assert snapshots,'No production tick snapshot offered'
snapshot=dict(snapshots[-1])
decoded=schema.SCHEMA.decode(schema.SCHEMA.encode(snapshot))
assert decoded['players'][0]['hydraulic_body']==body
out,baseline=split.encode(snapshot,None,999,False)
wire=schema.SCHEMA.decode(schema.SCHEMA.encode(out))
assert wire['player_motion'][0]['hydraulic_body']==body
player.hydraulic_body=None
for endpoint in state.players.values():endpoint.snapshot_tick_sent=-1
state.tick_once(0.);snapshot=dict(snapshots[-1])
out,baseline=split.encode(snapshot,baseline,999,False)
wire=schema.SCHEMA.decode(schema.SCHEMA.encode(out))
assert wire['player_motion'][0]['hydraulic_body'] is None
state._reset_round();assert player.hydraulic_body is None
player.hydraulic_body=tuple(body)
change=BattleState._public_player(player,True);change['max_health']+=1
# The retained unit player bypasses hello and has no mounted descriptor yet.
change['vehicle_compact_descr']='AQID'
assert state.select_vehicle(1,change) and player.hydraulic_body is None
print('Staged production server atomic validation, history, worker/replica forward and absence/round/vehicle clearing passed')
'''

class HydraulicReceiptTests(unittest.TestCase):
    def test_typed_shape_and_motion_only_clear(self):
        message = dict(type='input', round_id=7, hydraulic_body=BODY)
        self.assertEqual(SCHEMA.decode(SCHEMA.encode(message)), message)
        for bad in (None, [], BODY[:-1], BODY + [0.], [True] + BODY[1:], ['1'] + BODY[1:]):
            with self.subTest(bad=bad), self.assertRaises(WireError):
                SCHEMA.encode(dict(message, hydraulic_body=bad))
        # A body-only change belongs to motion; absence emits null rather than
        # silently retaining a previous receipt in a delta receiver.
        from schema import PLAYER_MOTION_NAMES
        row = dict((name, 0.) for name in PLAYER_MOTION_NAMES if name != 'hydraulic_body')
        row.update(id=1, hydraulic_body=BODY)
        motion, states, removed, baseline = replica_split._actors([row], PLAYER_MOTION_NAMES,
            PLAYER_MOTION_NAMES, ({}, {}), True)
        row['hydraulic_body'] = list(BODY); row['hydraulic_body'][4] += .1
        motion, states, removed, baseline = replica_split._actors([row], PLAYER_MOTION_NAMES,
            PLAYER_MOTION_NAMES, baseline, False)
        self.assertEqual(len(motion), 1); self.assertEqual(states, [])
        row.pop('hydraulic_body')
        motion, states, removed, baseline = replica_split._actors([row], PLAYER_MOTION_NAMES,
            PLAYER_MOTION_NAMES, baseline, False)
        self.assertIsNone(motion[0]['hydraulic_body'])

    @unittest.skipUnless(FORK, 'Pinned gameplay source is required')
    def test_native_applied_sample_freeze_and_sender(self):
        python = python27_audit_runtime()
        prefix = Path(subprocess.check_output([python, '-c', 'import sys;print(sys.prefix)'], text=True).strip())
        with tempfile.TemporaryDirectory(prefix='hydraulic-receipt-native-') as temporary:
            folder = Path(temporary)
            law = (Path(FORK) / 'native/offline_release_drive_python.inc').read_text().split('PyObject *WOT_CDECL release_drive(')[0]
            (folder / 'offline_release_drive_law.hpp').write_text('#pragma once\n#include <algorithm>\n#include <cmath>\n' + law)
            syntax = ['c++', '-std=c++17', '-fsyntax-only', '-Wno-register',
                      '-DWOT_NATIVE_BATTLE_HOST_BUILD', '-DWOT_CLIENT_SELF_HOST_BUILD',
                      '-DWOT_CLIENT_INPUT_RUNTIME_HOST_BUILD', '-isystem', str(prefix / 'include/python2.7'),
                      '-I' + str(folder), '-I' + str(ROOT / 'bigworld'), '-I' + str(ROOT.parent),
                      '-I' + str(Path(FORK) / 'native')]
            for name in ('stock_native_client_self_presentation.cpp', 'stock_native_client_input_runtime.cpp',
                         'stock_native_client_drive_runtime.cpp'):
                result = subprocess.run(syntax + [str(ROOT / 'bigworld' / name)], capture_output=True, text=True)
                self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
            sampler = block((ROOT / 'bigworld/stock_native_client_self_presentation.cpp').read_text(), 'void sampleHydraulicBody(')
            (folder / 'hydraulic_receipt_sampler.inc').write_text(sampler)
            sources = ['tests/stock_hydraulic_receipt_host.cpp', 'bigworld/stock_native_client_state.cpp',
                       'bigworld/stock_native_client_drive_publication.cpp', 'bigworld/stock_native_client_input.cpp',
                       'bigworld/stock_native_client_landing.cpp']
            command = ['c++', '-std=c++17', '-O1', '-Wall', '-Wextra', '-Werror', '-Wno-unused-parameter', '-Wno-register',
                       '-DWOT_CLIENT_INPUT_HOST_BUILD', '-DWOT_CLIENT_REPLICA_HOST_BUILD', '-bundle', '-undefined', 'dynamic_lookup',
                       '-isystem', str(prefix / 'include/python2.7'), '-I' + str(folder), '-I' + str(ROOT / 'bigworld'),
                       '-I' + str(Path(FORK) / 'native')]
            result = subprocess.run(command + [str(ROOT / name) for name in sources] + ['-o', str(folder / 'hydraulic_receipt_test.so')], capture_output=True, text=True)
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
            result = subprocess.run([python, '-B', '-c', NATIVE, FORK], cwd=folder, capture_output=True, text=True)
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
            print(result.stdout.strip())

    @unittest.skipUnless(FORK, 'Pinned gameplay source is required')
    def test_actual_staged_server_turnabout(self):
        with tempfile.TemporaryDirectory(prefix='hydraulic-receipt-server-') as temporary:
            folder = Path(temporary) / 'server'
            stage_server.stage(str(Path(FORK) / 'server'), str(folder))
            result = subprocess.run([sys.executable, '-B', '-c', SERVER, str(folder), FORK], cwd=folder,
                                    capture_output=True, text=True)
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
            print(result.stdout.strip())

if __name__ == '__main__':
    unittest.main()
