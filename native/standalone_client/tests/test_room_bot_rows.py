"""Differential compact rows against the exact staged Python codec."""
import copy
import json
import os
from pathlib import Path
import random
import subprocess
import sys
import tempfile
import unittest
ROOT = Path(__file__).resolve().parents[1]
FORK = os.environ.get('WOT_FORK_SOURCE')
ORACLE = r'''
import json,sys
sys.path[:0]=[sys.argv[1],sys.argv[2]]
import battle_wire_bot_state_codec as codec
for request in json.load(sys.stdin):
    response={}
    try:
        if request.get('operation')=='snapshot':
            response['rows'],response['contracts']=codec.encode_snapshot_equipment(request['bots'])
        else:
            row=request.get('row')
            if row is None:row=codec.encode_row(request['state'])
            response['row']=row
            response['decoded']=codec.decode_row(row,request.get('static',{}))
        response['ok']=True
    except (ValueError,TypeError,IndexError,KeyError,AttributeError,OverflowError):response={'ok':False}
    print(json.dumps(response,allow_nan=False))
'''
@unittest.skipUnless(FORK, 'Pinned gameplay source is required')
class RoomBotRows(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.temporary=tempfile.TemporaryDirectory(prefix='room-bot-rows-')
        cls.addClassCleanup(cls.temporary.cleanup)
        cls.folder=Path(cls.temporary.name)
        cls.executable=cls.folder/'codec'
        prebuilt=os.environ.get('WOT_ROOM_TEST_BIN')
        if prebuilt:
            cls.executable=Path(prebuilt)/'botrows'
        else:
            subprocess.run(['c++','-std=c++17','-O1','-Wall','-Wextra','-Werror',
                '-I'+str(ROOT/'room'),str(ROOT/'tests/room_bot_rows_cli.cpp'),
                str(ROOT/'room/room_bot_rows.cpp'),str(ROOT/'room/room_json.cpp'),
                '-o',str(cls.executable)],check=True,capture_output=True,text=True)
        sys.path.insert(0,str(ROOT/'wire'))
        try:
            import stage_server
            stage_server.stage(str(Path(FORK)/'server'),str(cls.folder/'server'))
        finally:sys.path.remove(str(ROOT/'wire'))
    def oracle(self,requests):
        result=subprocess.run([sys.executable,'-I','-B','-c',ORACLE,str(self.folder/'server'),
            str(Path(FORK)/'src/res/scripts/client')],input=json.dumps(requests),
            capture_output=True,text=True,check=True)
        return [json.loads(line) for line in result.stdout.splitlines()]
    def compare(self,requests):
        expected=self.oracle(requests)
        inputs=[]
        for request,result in zip(requests,expected):
            value=dict(request)
            if 'row' in result:value['row']=result['row']
            inputs.append(value)
        result=subprocess.run([str(self.executable)],input=''.join(json.dumps(v)+'\n' for v in inputs),
            capture_output=True,text=True,check=True)
        actual=[json.loads(line) for line in result.stdout.splitlines()]
        self.assertEqual(len(expected),len(actual))
        for index,(reference,result) in enumerate(zip(expected,actual)):
            with self.subTest(index=index,request=requests[index]):
                reference.pop('row',None)
                self.assertEqual(reference,result)
    def test_complete_and_optional_groups(self):
        contract=dict(id=7,name='repair',tags=['repair','crew'])
        state=dict(id=3,x=12.34567,y=-8.125,z=19.25,yaw=1.2,pitch=-.2,roll=.3,
            aim_yaw=1.45,gun_pitch=-.125,speed=-4.5,alive=False,world_pose=True,
            airborne=True,service_brake=True,route_wreck_blocked=True,local_combat_manoeuvre=True,
            movement_dir=-1,rotation_dir=1,ammo_magazine_started=True,fire_seq=12,shell_index=1,
            reload_time=2.375,reload_duration=7.5,ammo_remaining=[12,0,30],next_shell_index=2,
            ammo_reload_pending=True,clip=2,clip_size=3,burst_active=True,burst_group_seq=4,
            burst_count=3,burst_next_index=1,burst_shell_index=2,burst_interval=.25,burst_time_left=.123456,
            siege_state=2,siege_time_left_ms=200,siege_transition_total_ms=500,health=0,display_health=1,
            combat_base_revision=13,combat_seq=21,combat_fire_elapsed=.18,combat_fire_timer=.25,
            death_reason=4,stun_end_server_time_ms=2500,shot_yaw=8.,shot_pitch=-.16,turret_yaw=.25,
            critical=dict(devices=[dict(name='gunHealth',hp=2.75,max_hp=25.5,state='critical'),
                dict(name='engineHealth',hp=0.,max_hp=50.,state='destroyed')],crew_ko=['driver','loader2'],
                crew_roster=['commander','driver','loader2'],fire=True,ammo_rack_death=True),
            equipment_states=[dict(equipment=contract,usesLeft=2,cooldownTimeLeft=.25,active=True,
                autoPendingElapsed=None,aiPendingElapsed=.125)],push_x=1.25,push_z=-2.,push_yaw=.05,
            contact_push_acks=[[9,8,1.25,-2.,3.5,-4.,.25]],
            ram_motion=[[9,12,1000000]+[.25,-.5,.75,10.,2.,-5.,.1,.2,.3,20.,3.,-6.,.2,.3,.4,.5,-.25,.1]+
                [dict(hydraulic_body=[20.,3.,-6.,.2,.3,.4,.5,.6])],
                [10,13,1000001]+[0.]*18+[dict(hydraulic_body=None)]],
            aim_feedback_kind='human',aim_feedback_id=9,aim_feedback_error=.12345,aim_feedback_aligned=True)
        requests=[dict(state=dict(id=1)),dict(state=state,static={'equipment_contracts':[contract]})]
        groups=[('ammo_remaining','next_shell_index','ammo_reload_pending'),
            ('burst_active','burst_group_seq','burst_count','burst_next_index','burst_shell_index','burst_interval','burst_time_left'),
            ('clip','clip_size'),('siege_state','siege_time_left_ms','siege_transition_total_ms'),
            ('shot_yaw','shot_pitch'),('critical',),('equipment_states',),
            ('push_x','push_z','push_yaw','contact_push_acks'),('ram_motion',),
            ('aim_feedback_kind','aim_feedback_id','aim_feedback_error','aim_feedback_aligned')]
        for group in groups:
            variant=copy.deepcopy(state)
            for key in group:variant.pop(key,None)
            requests.append(dict(state=variant,static={'equipment_contracts':[contract]}))
        variant=copy.deepcopy(state)
        variant['ram_motion']=[motion[:21] for motion in variant['ram_motion']]
        variant['critical'].pop('crew_roster');variant['equipment_states']=[]
        requests.append(dict(state=variant))
        self.compare(requests)
    def test_varied_random_checkpoints(self):
        randomizer=random.Random(1513);requests=[]
        for actor in range(1,151):
            state=dict(id=actor,x=randomizer.uniform(-1900.,1900.),y=randomizer.uniform(-900.,900.),
                z=randomizer.uniform(-1900.,1900.),pitch=randomizer.uniform(-9.,9.),roll=randomizer.uniform(-9.,9.),
                yaw=randomizer.uniform(-3.,3.),turret_yaw=randomizer.uniform(-9.,9.),gun_pitch=randomizer.uniform(-1.,1.),
                speed=randomizer.uniform(-70.,70.),movement_dir=randomizer.choice([-1,0,1]),
                rotation_dir=randomizer.choice([-1,0,1]),alive=bool(actor%2),health=actor*7,combat_seq=actor,
                ammo_magazine_started=bool(actor%3))
            if actor%2:state.update(ammo_remaining=[actor,0],next_shell_index=1,ammo_reload_pending=False)
            if actor%3:state.update(critical=dict(crew_ko=['driver'] if actor%2 else [],devices=[]))
            if actor%5:state.update(aim_feedback_kind='',aim_feedback_id=0,aim_feedback_error=.25,aim_feedback_aligned=False)
            requests.append(dict(state=state))
        self.compare(requests)
    def test_malformed_width_type_flags_and_nested_identity(self):
        base=self.oracle([dict(state=dict(id=1))])[0]['row']
        requests=[dict(row=base[:n]) for n in range(len(base))]+[dict(row=base+[1]),dict(row={})]
        for value in (True,1.0,'1',None):
            row=list(base);row[2]=value;requests.append(dict(row=row))
        for flags,tail in [(1<<24,[]),(1<<9,[6]),(1<<7,[10]),(1<<7,[1,9,0,1000,0,0,-1]),
                (1<<7,[1,0,0,1000,3,0,-1]),(1<<7,[0,256,-1]),(1<<7,[0,0,256]),
                (1<<8,[1,1,0,0,-1,-1]),(1<<8,[4]),(1<<17,[0,0,0,1,0,1,0,0,0,0,0]),
                (1<<17,[0,0,0,2]+[1,1,0,0,0,0,0]*2),(1<<21,[31]),(1<<21,[1,0,1,0]+[0]*18),
                ((1<<21)|(1<<26),[1,1,1,0]+[0]*18+[2]),(1<<23,[3,1,0]),
                (1<<23,[0,1,0]),(1<<23,[1,1,314161])]:
            row=list(base);row[1]=flags;requests.append(dict(row=row+tail))
        # The pinned reader ignores unknown bits and prioritizes forward.
        for flags in (1<<30,1<<26,(1<<10)|(1<<11)):
            row=list(base);row[1]=flags;requests.append(dict(row=row))
        self.compare(requests)
    def test_snapshot_contract_deduplication_and_unavailable_rows(self):
        first=dict(id=3,tags=['crew','repair'],cost=5000)
        equal=dict(cost=5000,tags=['crew','repair'],id=3)
        second=dict(id=5,tags=[],cost=1000)
        bots=[dict(id=1),dict(id=2,equipment_states=[]),
            dict(id=3,equipment_states=[dict(equipment=first,usesLeft=2)]),
            dict(id=4,equipment_states=[dict(equipment=equal,usesLeft=0),dict(equipment=second,active=True)]),
            dict(id=5,equipment_states=[dict(equipment=0),dict(unavailable=True)])]
        self.compare([dict(operation='snapshot',bots=bots),
            dict(operation='snapshot',bots=[dict(equipment_states=[5])])])
if __name__=='__main__':unittest.main()
