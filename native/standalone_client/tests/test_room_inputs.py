"""Ordered player input parity with the actual NG-staged room source."""
import copy
import json
import os
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest
ROOT=Path(__file__).resolve().parents[1]
FORK=os.environ.get('WOT_FORK_SOURCE')
FIELDS=('x y z yaw pitch roll up_cosine hydraulic_body aim_yaw gun_pitch forward turn speed siege_state siege_transition_ticks '
    'fire_seq shell_index next_shell_index shell_change_pending input_seq input_processed_seq gun_checkpoint_seq gun_checkpoint '
    'gun_checkpoints pose_time_us pose_history connected participating alive client_position ram_contact_seq '
    'ram_contact_resolved_seq ram_contact ram_contacts destructible_contact_seq destructible_contact_resolved_seq '
    'destructible_contacts last_input_reject input_reject_counts tank_pushes turret_pushes').split()
ORACLE=r'''
import collections,json,sys
sys.path[:0]=[sys.argv[1],sys.argv[2]]
import lan_battle_server as s
request=json.load(sys.stdin)
state=s.BattleState(clock=lambda:100.)
state.round_id=1;state.phase='battle';state.tick=300
p=s.Player(1,None,('127.0.0.1',0))
for k,v in request['actor'].items():
    if k=='id':continue
    if k=='pose_history':v=collections.deque(v)
    if k=='gun_checkpoints':v=collections.OrderedDict((int(a),b) for a,b in v.items())
    if k in ('ram_contacts','destructible_contacts'):v=collections.OrderedDict((a['seq'],a) for a in v)
    if k=='tank_pushes':v={a[0]:a for a in v}
    if k=='turret_pushes':v={a[0]:a for a in v}
    setattr(p,k,v)
state.players[1]=p
state.bot_states={a['id']:a for a in request.get('bots',[])}
state.human_collision_profiles={int(k):v for k,v in request.get('resources',{}).get('human_collision_profiles',{}).items()}
state.detached_turrets=request.get('resources',{}).get('detached_turrets',{})
state.simulation_worker=s.SimulationWorker(None,('127.0.0.1',0),capabilities=s.SIMULATION_WORKER_REQUIRED_CAPABILITIES)
state.bot_authority_id=s.SIMULATION_WORKER_AUTHORITY_ID
state.simulation_worker.offer_reliable=lambda m:True
receipt=[1000000]
state._logical_motion_time_us=lambda:receipt[0]
state.bot_state_time_us=1000000
state._record_vehicle_travel=lambda *a:None
state._remember_environment_push=lambda *a:None
for v in request.get('archive',[]):state.ram_pose_archive.remember(v['revision'],v['stamp'],{a['id']:a for a in v['bots']})
fields=sys.argv[3].split(',')
output=[]
for command in request.get('commands',[]):
    for k,v in command.get('actor_patch',{}).items():setattr(p,k,v)
    state.phase=command.get('phase',state.phase);state.tick=command.get('tick',state.tick)
    state.bot_state_revision=command.get('bot_state_revision',state.bot_state_revision)
    state.bot_state_time_us=command.get('bot_state_time_us',state.bot_state_time_us)
    receipt[0]=command.get('motion_time_us',receipt[0])
    replies=[];p.offer_reliable=lambda m:replies.append(m) or True
    for unused in range(command.get('advance_ticks',0)):state._advance_siege_states()
    accepted=state.update_input(1,command['message'])
    actor={}
    for k in fields:
        v=getattr(p,k)
        if k=='pose_history':v=list(v)
        if k in ('ram_contacts','destructible_contacts','tank_pushes','turret_pushes'):v=list(v.values())
        actor[k]=v
    output.append(json.loads(json.dumps(dict(accepted=bool(accepted),actor=actor,replies=replies))))
print(json.dumps(output))
'''
@unittest.skipUnless(FORK,'Pinned gameplay source is required')
class RoomInputs(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.folder=tempfile.TemporaryDirectory(prefix='room-inputs-');cls.addClassCleanup(cls.folder.cleanup)
        cls.executable=Path(cls.folder.name)/'inputs'
        prebuilt=os.environ.get('WOT_ROOM_TEST_BIN')
        if prebuilt:
            cls.executable=Path(prebuilt)/'inputs'
        else:
            subprocess.run(['c++','-std=c++17','-O1','-Wall','-Wextra','-Werror','-I'+str(ROOT/'room'),
                str(ROOT/'tests/room_inputs_test_cli.cpp'),str(ROOT/'room/room_inputs.cpp'),str(ROOT/'room/room_json.cpp'),
                '-o',str(cls.executable)],check=True,capture_output=True,text=True)
        sys.path.insert(0,str(ROOT/'wire'))
        try:
            import stage_server
            stage_server.stage(str(Path(FORK)/'server'),str(Path(cls.folder.name)/'server'))
        finally:sys.path.remove(str(ROOT/'wire'))
    def base(self):
        return dict(id=1,vehicle='ussr:R11_MS-1',vehicle_compact_descr='AA==',capabilities=['human_ram_timeline_v1','player_projectile_owner_v1','ram_contact_ledger_v2'],
            x=0.,y=0.,z=0.,yaw=0.,pitch=0.,roll=0.,up_cosine=1.,hydraulic_body=None,aim_yaw=0.,gun_pitch=0.,forward=0.,turn=0.,speed=0.,
            siege_state=0,siege_transition_ticks=0,fire_seq=0,shell_index=0,next_shell_index=0,shell_change_pending=False,
            input_seq=0,input_processed_seq=0,gun_checkpoint_seq=0,gun_checkpoint={},gun_checkpoints={},pose_time_us=None,pose_history=[],
            connected=True,participating=True,alive=True,client_position=False,ram_contact_seq=0,ram_contact_resolved_seq=0,
            ram_contact={},ram_contacts=[],destructible_contact_seq=0,destructible_contact_resolved_seq=0,
            destructible_contacts=[],last_input_reject={},input_reject_counts={},tank_pushes=[],turret_pushes=[],effective_params={})
    def message(self,seq=1,**changes):
        value=dict(type='input',round_id=1,input_seq=seq,shell_index=0,gun_checkpoint=dict(reload_time=0.,reload_duration=5.,clip=1,clip_size=1,dispersion=.02))
        value.update(changes);return value
    def compare(self,commands,**options):
        request=dict(actor=self.base(),commands=commands);request.update(options)
        reference=subprocess.run([sys.executable,'-I','-B','-c',ORACLE,str(Path(self.folder.name)/'server'),
            str(Path(FORK)/'src/res/scripts/client'),','.join(FIELDS)],input=json.dumps(request),capture_output=True,text=True,check=True)
        expected=json.loads(reference.stdout.splitlines()[-1])
        result=subprocess.run([str(self.executable)],input=json.dumps(request)+'\n',capture_output=True,text=True,check=True)
        actual=json.loads(result.stdout)
        mapped=dict(request,map_archive=True)
        direct=subprocess.run([str(self.executable)],input=json.dumps(mapped)+'\n',capture_output=True,text=True,check=True)
        self.assertEqual(actual,json.loads(direct.stdout),'Canonical-map archive changed input admission')
        self.assertEqual(len(expected),len(actual))
        for index,(left,right) in enumerate(zip(expected,actual)):
            right['actor']={k:right['actor'].get(k) for k in FIELDS}
            for key in left:
                if key=='actor':
                    for name in FIELDS:
                        with self.subTest(index=index,field=name,command=commands[index]):self.assertEqual(left[key][name],right[key][name])
                else:
                    with self.subTest(index=index,field=key,command=commands[index]):self.assertEqual(left[key],right[key])
    def test_ordered_duplicate_conflict_gap_rejection_and_recovery(self):
        first=self.message(1,forward=1.,x=2.,z=3.,pose_time_us=1000000)
        invalid=self.message(2,forward=2.)
        self.compare([dict(message=first),dict(message=first),dict(message=self.message(1,forward=.5)),
            dict(message=self.message(3)),dict(message=invalid),dict(message=invalid),dict(message=self.message(3,forward=-1.))])
    def test_inactive_envelopes_wreck_pose_and_countdown(self):
        self.compare([dict(message=self.message(),phase='waiting'),dict(message=self.message(2,forward=2.)),
            dict(message=self.message(3),phase='battle',tick=1),dict(message=self.message(4),tick=300),
            dict(message=self.message(5,x=5.,z=7.,yaw=.1,pose_time_us=1000000,wreck_pose=True),actor_patch={'alive':False}),
            dict(message=self.message(6,x=99.,z=99.))])
    def test_shell_checkpoint_hydraulic_and_whitelist_failure_classes(self):
        messages=[self.message(1,unknown=1),self.message(2,next_shell_index=1),
            self.message(3,shell_change_pending=False),self.message(4,next_shell_index=1,shell_change_pending=False),
            self.message(5,up_cosine=True),self.message(6,hydraulic_body=[0]*8),
            self.message(7,x=0.,y=0.,z=0.,yaw=0.,hydraulic_body=[0]*7),self.message(8,siege_enabled=1),
            self.message(9,x=0.,y=0.,z=0.,yaw=.5,hydraulic_body=[1.,2.,3.,.1,.2,.3,.4,.5],up_cosine=.9876543)]
        messages[0]['gun_checkpoint'].pop('dispersion')
        messages.append(self.message(10,next_shell_index=1,shell_change_pending=True,fire_seq=9))
        self.compare([dict(message=m) for m in messages])
    def test_history_eviction_clock_rebase_and_frozen_checkpoints(self):
        commands=[dict(message=self.message(seq,x=seq*.1,z=0.,pose_time_us=seq*100000),motion_time_us=seq*100000) for seq in range(1,141)]
        commands+=[dict(message=self.message(1)),dict(message=self.message(141,x=20.,z=1.,pose_time_us=99999999)),
            dict(message=self.message(142,x=21.,z=1.,pose_time_us=14000000))]
        self.compare(commands)
    def test_contact_heads_are_terminal_and_later_sweeps_keep_advancing(self):
        sweep=dict(seq=1,x=0.,y=0.,z=0.,yaw=0.,speed=10.,dt=.1,
            end_x=0.,end_y=0.,end_z=1.,end_yaw=0.,token=[[2,3,None]])
        invalid=copy.deepcopy(sweep);invalid['dt']=0.
        second=copy.deepcopy(sweep);second['seq']=2
        third=copy.deepcopy(sweep);third['seq']=3
        self.compare([dict(message=self.message(1,x=0.,z=0.,pose_time_us=1000000,
            destructible_contacts=[invalid,second])),dict(message=self.message(2,
            destructible_contacts=[second,third])),dict(message=self.message(3,
            ram_contacts=[dict(seq=1),dict(seq=2)]))])
    def test_invalid_optional_contacts_only_reject_an_inactive_frame(self):
        self.compare([dict(message=self.message(1,tank_pushes=True,turret_pushes=True,
            ram_contacts=True,destructible_contacts=True)),dict(message=self.message(2,
            tank_pushes=True),phase='waiting'),dict(message=self.message(3,
            ram_contacts=[dict(seq=1)])),dict(message=self.message(4,
            destructible_contacts=[dict(seq=1)]))])
    def test_ram_receipts_pin_exact_brackets_and_reject_stale_evidence(self):
        resources=dict(human_collision_profiles={'1':dict(shape=[1.5,3.5,-1.,2.],
            ram_profile=dict(spall_coefficient=1.))})
        bot=dict(id=1,x=-2.,y=0.,z=0.,yaw=0.,pitch=0.,roll=0.,aim_yaw=0.,gun_pitch=0.,alive=True,
            team=2,vehicle='ussr:R11_MS-1')
        contact=dict(seq=1,bot_id=1,bot_state_revision=1,presentation_time_us=1000000,
            native_contact_time_us=1000000,contact_x=-1.,contact_y=0.,contact_z=0.,
            contact_normal_x=1.,contact_normal_z=0.,contact_armor_player=20.,contact_armor_bot=30.,
            contact_screened_player=False,contact_screened_bot=False,contact_spall_player=1.,
            contact_bonus_player=0.,x=0.,y=0.,z=0.,yaw=0.,pitch=0.,roll=0.,
            vx=0.,vy=0.,vz=0.,bot_vx=.1,bot_vy=0.,bot_vz=0.,bot_history_bracket=[1,1000000,1,1000000])
        invalid=copy.deepcopy(contact);invalid['seq']=2;invalid['bot_history_bracket']=[1,900000,1,900000]
        newer=copy.deepcopy(contact);newer['seq']=3
        self.compare([dict(message=self.message(1,ram_contacts=[contact]),bot_state_revision=1),
            dict(message=self.message(2,ram_contacts=[invalid,newer])),
            dict(message=self.message(3,ram_contacts=[dict(contact,seq=4,contact_screened_player=True)]))],
            resources=resources,bots=[bot],archive=[dict(revision=1,stamp=1000000,bots=[bot])])
    def test_siege_transitions_keep_pose_and_freeze_the_drivetrain(self):
        actor=self.base();actor['vehicle']='sweden:S11_Strv_103B'
        self.compare([dict(message=self.message(1,speed=0.,siege_enabled=True,x=1.,z=2.)),
            dict(message=self.message(2,speed=3.,forward=1.,x=2.,z=3.)),
            dict(message=self.message(3,speed=99.),actor_patch={'siege_state':2}),
            dict(message=self.message(4,speed=0.,siege_enabled=False))],actor=actor)
    def test_round_and_sequence_identity_failures_do_not_consume_other_slots(self):
        for value in (None,True,False,0,2,1.5,'1.0',' 1 ','+01',1.0):
            first=self.message(round_id=value)
            self.compare([dict(message=first),dict(message=self.message(1)),dict(message=self.message(2))])
        for value in (None,True,False,0,-1,1.0,'1',2147483648):
            self.compare([dict(message=self.message(value)),dict(message=self.message(1))])
    def test_each_bounded_input_class_is_terminal_then_recovers(self):
        commands=[];seq=0
        bounds={'forward':1.,'turn':1.,'speed':200.,'aim_yaw':6.283185307179586,'gun_pitch':1.2,
            'x':2000.,'y':1000.,'z':2000.,'yaw':6.283185307179586,'pitch':3.141592653589793,'roll':3.141592653589793}
        for name,limit in bounds.items():
            for value in (True,None,str(limit),limit+.001,-limit-.001,limit,-limit):
                seq+=1;commands.append(dict(message=self.message(seq,**{name:value})))
        for value in ({},None,dict(reload_time=1.,reload_duration=0.,clip=0,clip_size=1,dispersion=.02),
                dict(reload_time=6.,reload_duration=5.,clip=0,clip_size=1,dispersion=.02),
                dict(reload_time=0.,reload_duration=5.,clip=2,clip_size=1,dispersion=.02)):
            seq+=1;commands.append(dict(message=self.message(seq,gun_checkpoint=value)))
        seq+=1;commands.append(dict(message=self.message(seq,forward=.5)))
        self.compare(commands)
    def test_siege_ticks_complete_and_ceil_countdown(self):
        actor=self.base();actor['vehicle']='sweden:S11_Strv_103B'
        self.compare([dict(message=self.message(1,speed=0.,siege_enabled=True)),
            dict(message=self.message(2,speed=5.),advance_ticks=59),
            dict(message=self.message(3,speed=5.),advance_ticks=1),
            dict(message=self.message(4,speed=0.,siege_enabled=False)),
            dict(message=self.message(5,speed=2.),advance_ticks=39)],actor=actor)
    def test_ram_motion_witness_binds_both_poses_and_velocities(self):
        resources=dict(human_collision_profiles={'1':dict(shape=[1.5,3.5,-1.,2.],ram_profile=dict(spall_coefficient=1.))})
        motion=[1,5,1000000]+[.1,0.,0.,-2.,0.,0.,0.,0.,0.,0.,0.,0.,0.,0.,0.,0.,0.,0.]
        bot=dict(id=1,x=-2.,y=0.,z=0.,yaw=0.,pitch=0.,roll=0.,aim_yaw=0.,gun_pitch=0.,alive=True,
            team=2,vehicle='ussr:R11_MS-1',ram_motion=[motion])
        contact=dict(seq=1,bot_id=1,bot_state_revision=1,presentation_time_us=1000000,native_contact_time_us=1000000,
            contact_x=-1.,contact_y=0.,contact_z=0.,contact_normal_x=1.,contact_normal_z=0.,
            contact_armor_player=20.,contact_armor_bot=30.,contact_screened_player=False,contact_screened_bot=False,
            contact_spall_player=1.,contact_bonus_player=0.,x=0.,y=0.,z=0.,yaw=0.,pitch=0.,roll=0.,
            vx=0.,vy=0.,vz=0.,bot_vx=.1,bot_vy=0.,bot_vz=0.,bot_history_bracket=[1,1000000,1,1000000],
            bot_ram_motion_seq=5,track_contacts_player=['rightTrackHealth','leftTrackHealth'])
        commands=[dict(message=self.message(1,ram_contacts=[contact]),bot_state_revision=1)]
        for seq,(name,value) in enumerate([('bot_ram_motion_seq',6),('bot_vx',.5),('x',.5),('vx',1.)],2):
            row=dict(contact,seq=seq);row[name]=value
            commands.append(dict(message=self.message(seq,ram_contacts=[row])))
        self.compare(commands,resources=resources,bots=[bot],archive=[dict(revision=1,stamp=1000000,bots=[bot])])
    def test_ram_archive_interpolates_the_named_shortest_angle_pair(self):
        resources=dict(human_collision_profiles={'1':dict(shape=[1.5,3.5,-1.,2.],ram_profile=dict(spall_coefficient=1.))})
        left=dict(id=1,x=-2.,y=0.,z=0.,yaw=3.,pitch=.1,roll=.2,aim_yaw=3.,gun_pitch=.1,alive=True,team=2,vehicle='x')
        right=dict(left,x=-1.,yaw=-3.,aim_yaw=-3.,alive=False)
        contact=dict(seq=1,bot_id=1,bot_state_revision=2,presentation_time_us=950000,native_contact_time_us=950000,
            contact_x=-1.,contact_y=0.,contact_z=0.,contact_normal_x=1.,contact_normal_z=0.,
            contact_armor_player=20.,contact_armor_bot=30.,contact_screened_player=False,contact_screened_bot=False,
            contact_spall_player=1.,contact_bonus_player=0.,x=0.,y=0.,z=0.,yaw=0.,pitch=0.,roll=0.,
            vx=0.,vy=0.,vz=0.,bot_vx=0.,bot_vy=0.,bot_vz=0.,bot_history_bracket=[1,900000,2,1000000])
        self.compare([dict(message=self.message(1,ram_contacts=[contact]),bot_state_revision=2)],resources=resources,
            bots=[right],archive=[dict(revision=1,stamp=900000,bots=[left]),dict(revision=2,stamp=1000000,bots=[right])])
    def test_push_checkpoints_keep_only_new_known_actors(self):
        bot=dict(id=1,x=0.,y=0.,z=0.,yaw=0.,team=2,alive=True)
        resources=dict(detached_turrets={'bot:1':{}})
        self.compare([dict(message=self.message(1,forward=1.,tank_pushes=[[1,1,0.,1.,0.,0.,0.],[2,1,0.,9.,0.,0.,0.]],
            turret_pushes=[['bot:1',1,1.,2.,3.,4.,5.,6.],['player:2',1,1.,2.,3.,4.,5.,6.]])),
            dict(message=self.message(2,tank_pushes=[[1,1,0.,99.,0.,0.,0.]],turret_pushes=[['bot:1',1,99.,2.,3.,4.,5.,6.]])),
            dict(message=self.message(3,tank_pushes=[[1,2,0.,2.,0.,0.,0.]],turret_pushes=[['bot:1',2,2.,3.,4.,5.,6.,7.]]))],
            bots=[bot],resources=resources)
if __name__=='__main__':unittest.main()
