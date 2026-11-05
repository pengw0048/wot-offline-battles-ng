"""Native radio admission, fanout and terminal parity with pinned BattleState."""
import copy
import json
import os
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest
from test_stock_vehicle_physics_owner import block
ROOT=Path(__file__).resolve().parents[1]
FORK=os.environ.get('WOT_FORK_SOURCE')
ORACLE=r'''
import ast,collections,json,sys
sys.path[:0]=[sys.argv[1],sys.argv[2]]
import lan_battle_server as s
request=json.load(sys.stdin);state=s.BattleState(clock=lambda:100.)
state.round_id=1;state.phase='battle';state.tick=300
players={}
for value in request['players']:
    p=s.Player(value['id'],None,('127.0.0.1',0))
    for k,v in value.items():
        if k!='id':setattr(p,k,v)
    players[p.player_id]=p
state.players=players;state.bot_manifest=request['manifest'];state.bot_states={v['id']:v for v in request['bots']}
state.bot_orders={'orders':request.get('orders',[])};context=[request.get('context',{})]
state._bot_defense_context=lambda:context[0];state.capture_bases=context[0].get('capture_bases',{})
state.player_spotted={int(k):frozenset(tuple(v) for v in values) for k,values in request.get('player_spotted',{}).items()}
state.bot_spotted={int(k):frozenset(tuple(v) for v in values) for k,values in request.get('bot_spotted',{}).items()}
# Compile the actual cancellation statements out of tick_once so this focused
# test need not execute combat, networking or an unrelated planner iteration.
tree=ast.parse(open(s.__file__).read());cancel_body=None
for node in ast.walk(tree):
    body=getattr(node,'body',None)
    if not isinstance(body,list):continue
    for index,statement in enumerate(body):
        if isinstance(statement,ast.Assign) and any(isinstance(t,ast.Name) and t.id=='cancelled' for t in statement.targets):
            cancel_body=body[index:index+2]
assert cancel_body
function=ast.FunctionDef(name='cancel_actual',args=ast.arguments(posonlyargs=[],args=[ast.arg(arg='self')],vararg=None,kwonlyargs=[],kw_defaults=[],kwarg=None,defaults=[]),body=cancel_body,decorator_list=[])
module=ast.fix_missing_locations(ast.Module(body=[function],type_ignores=[]));exec(compile(module,'<actual cancellation>','exec'))
output=[]
for command in request['commands']:
    deliveries=[]
    for id,p in players.items():p.offer_reliable=lambda message,id=id:deliveries.append([id,message]) or True
    for id,patch in command.get('players',{}).items():
        for k,v in patch.items():setattr(players[int(id)],k,v)
    for id,patch in command.get('bots',{}).items():state.bot_states[int(id)].update(patch)
    state.tick=command.get('tick',state.tick);state.phase=command.get('phase',state.phase)
    if 'context' in command:context[0]=command['context'];state.capture_bases=context[0].get('capture_bases',{})
    ack=None
    if command.get('advance'):state._active_team_commands_locked()
    elif 'cancel' in command:
        state.bot_orders={'orders':[dict(id=id,player_command_cancelled=True) for id in command['cancel']]};cancel_actual(state)
    elif 'clear' in command:state._clear_team_commands_locked(command['clear'])
    else:
        ack=state.submit_team_command(command.get('sender',1),command['message'])
        if ack is not None:state.publish_team_command(ack)
    pstate=[dict(id=id,team_command_seq=p.team_command_seq,team_command_ready_tick=p.team_command_ready_tick,
        team_command_refusal_sent=p.team_command_refusal_sent) for id,p in sorted(players.items())]
    output.append(json.loads(json.dumps(dict(ack=ack,commands=list(state.team_commands.values()),deliveries=deliveries,players=pstate))))
print(json.dumps(output))
'''
@unittest.skipUnless(FORK,'Pinned gameplay source is required')
class RoomRadio(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.folder=tempfile.TemporaryDirectory(prefix='room-radio-');cls.addClassCleanup(cls.folder.cleanup)
        cls.executable=Path(cls.folder.name)/'radio'
        capture=(ROOT/'room/room_strategy_capture.cpp').read_text();defense=(ROOT/'room/room_strategy_defense.cpp').read_text()
        shared=Path(cls.folder.name)/'shared.cpp'
        shared.write_text('#include "room_strategy_data.hpp"\nnamespace wot::room {using namespace strategy;\n'+
            block(capture,'std::vector<Value> Planner::defensePoints(')+'\n'+block(capture,'bool Planner::insideBase(')+'\n'+
            'namespace {\n'+block(defense,'std::vector<Value> contributorRows(')+'\n}\n'+
            block(defense,'std::set<Identity> Planner::contributors(')+'\n}\n')
        prebuilt=os.environ.get('WOT_ROOM_TEST_BIN')
        if prebuilt:
            cls.executable=Path(prebuilt)/'radio'
        else:
            subprocess.run(['c++','-std=c++17','-O1','-Wall','-Wextra','-Werror','-I'+str(ROOT/'room'),
                str(ROOT/'tests/room_strategy_radio_probe.cpp'),str(ROOT/'room/room_strategy_radio.cpp'),
                str(ROOT/'room/room_strategy_data.cpp'),str(ROOT/'room/room_json.cpp'),str(shared),'-o',str(cls.executable)],
                check=True,capture_output=True,text=True)
        sys.path.insert(0,str(ROOT/'wire'))
        try:
            import stage_server
            stage_server.stage(str(Path(FORK)/'server'),str(Path(cls.folder.name)/'server'))
        finally:sys.path.remove(str(ROOT/'wire'))
    def scenario(self,commands,**options):
        players=[dict(id=1,team=1,connected=True,participating=True,alive=True,client_position=True,x=0.,z=0.),
            dict(id=2,team=1,connected=True,participating=True,alive=True,client_position=True,x=1.,z=1.),
            dict(id=3,team=2,connected=True,participating=True,alive=True,client_position=True,x=100.,z=0.)]
        bots=[dict(id=i,team=1 if i<5 else 2,alive=True,world_pose=True,x=float(i*10),z=0.) for i in range(1,6)]
        context=dict(capture_bases={'1':[dict(x=0.,z=0.,radius=50.)],'2':[dict(x=1000.,z=0.,radius=50.)]},
            arena_bounds=[-1000.,-1000.,1000.,1000.],states={'1':dict(invaders=0),'2':dict(invaders=0)},contributors={'1':[],'2':[]})
        request=dict(players=players,bots=bots,manifest=[dict(id=v['id'],team=v['team']) for v in bots],context=context,
            commands=commands,orders=[],player_spotted={'1':[['bot',5],['player',3]]},bot_spotted={'3':[['bot',5]]})
        request.update(options);return request
    def message(self,seq=1,command='ATTACK',**values):
        result=dict(type='team_command',round_id=1,command_seq=seq,command=command);result.update(values);return result
    def compare(self,commands,**options):
        request=self.scenario(commands,**options)
        reference=subprocess.run([sys.executable,'-I','-B','-c',ORACLE,str(Path(self.folder.name)/'server'),
            str(Path(FORK)/'src/res/scripts/client')],input=json.dumps(request),capture_output=True,text=True,check=True)
        expected=json.loads(reference.stdout.splitlines()[-1])
        result=subprocess.run([str(self.executable)],input=json.dumps(request)+'\n',capture_output=True,text=True,check=True)
        actual=json.loads(result.stdout);self.assertEqual(len(expected),len(actual))
        for index,(left,right) in enumerate(zip(expected,actual)):
            with self.subTest(index=index,command=commands[index]):self.assertEqual(left,right)
    def test_idempotence_conflict_cooldown_expiry_and_supersession(self):
        first=self.message()
        self.compare([dict(message=first),dict(message=first),dict(message=self.message(command='HELPME')),
            dict(message=self.message(2)),dict(message=self.message(3)),dict(message=self.message(4),tick=600),
            dict(advance=True,tick=4200)])
    def test_all_stock_shapes_target_classes_and_pure_broadcasts(self):
        requests=[];seq=0
        for name in ['ATTACK','BACKTOBASE','HELPME','POSITIVE','NEGATIVE','RELOADING_READY','RELOADING_UNAVAILABLE',
                'FOLLOWME','TURNBACK','HELPMEEX','STOP','ATTACKENEMY','SUPPORTMEWITHFIRE','ATTENTIONTOCELL',
                'SPG_AIM_AREA','RELOADINGGUN','RELOADING_CASSETE','RELOADING_READY_CASSETE']:
            seq+=1;values={}
            if name in ('FOLLOWME','TURNBACK','HELPMEEX','STOP'):values.update(target_kind='bot',target_id=2)
            if name in ('ATTACKENEMY','SUPPORTMEWITHFIRE'):values.update(target_kind='bot',target_id=5)
            if name=='ATTENTIONTOCELL':values['cell_index']=0
            if name=='SPG_AIM_AREA':values.update(aim_point=[12.,3.,4.],cell_index=99,reload_time=0.)
            if name in ('RELOADINGGUN','RELOADING_CASSETE'):values['reload_time']=5.
            if name in ('RELOADING_CASSETE','RELOADING_READY_CASSETE'):values['quantity']=3
            requests.append(dict(message=self.message(seq,name,**values),tick=300*seq))
        self.compare(requests)
    def test_bad_shapes_sequences_and_numeric_payloads(self):
        messages=[self.message(command='unknown'),self.message(unknown=True),self.message(True),self.message(0),
            self.message(1,'RELOADINGGUN',reload_time=0.),self.message(1,'RELOADINGGUN',reload_time=True),
            self.message(1,'RELOADING_CASSETE',reload_time=1.,quantity=0),
            self.message(1,'SPG_AIM_AREA',reload_time=0.,cell_index=99,aim_point=[1.,2.]),
            self.message(1,'ATTENTIONTOCELL',cell_index=100),self.message(1,'ATTACKENEMY',target_kind='human',target_id=2),
            self.message(1,'STOP',target_kind='human',target_id=3),self.message(1)]
        self.compare([dict(message=v) for v in messages])
    def test_guard_arrival_safe_period_pressure_and_issuer_terminal_states(self):
        self.compare([dict(message=self.message(1,'BACKTOBASE')),dict(advance=True,tick=300),dict(advance=True,tick=1199),
            dict(advance=True,tick=1200)])
        for changes in ({'connected':False},{'participating':False},{'alive':False},{'team':2}):
            self.compare([dict(message=self.message()),dict(advance=True,players={'1':changes})])
        self.compare([dict(message=self.message()),dict(cancel=[1]),dict(cancel=[2,3])])
        self.compare([dict(message=self.message()),dict(clear='round_complete')])
    def test_visibility_distance_and_busy_or_disabled_bots(self):
        for order in (dict(id=1,combat_mode='engage'),dict(id=1,route_attempt=dict(phase='probe')),
                dict(id=1,empty_ammo=True)):
            self.compare([dict(message=self.message())],orders=[order])
        for patch in (dict(ammo_remaining=[0,0]),dict(critical={'destroyed':['engineHealth']}),dict(world_pose=False)):
            base=self.scenario([])['bots'];base[0].update(patch)
            self.compare([dict(message=self.message())],bots=base)
        distant=self.scenario([])['bots']
        for bot in distant:bot['x']+=500.
        self.compare([dict(message=self.message(1,'ATTACKENEMY',target_kind='bot',target_id=5))],bots=distant)
        self.compare([dict(message=self.message(1,'ATTACKENEMY',target_kind='bot',target_id=5))],player_spotted={})
if __name__=='__main__':unittest.main()
