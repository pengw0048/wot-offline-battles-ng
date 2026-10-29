"""Exercise the actual CPython 2.7 projectile wire boundary."""
import os
from pathlib import Path
import subprocess
import tempfile
import unittest
ROOT=Path(__file__).resolve().parents[1]
SCRIPT=r'''
import copy,math
import projectile_wire_test as native
row=dict(projectile_id='shell',shooter_kind='bot',shooter_id=4,source_vehicle='ussr:T-34',shot_seq=8,burst_group_seq=8,burst_index=0,burst_count=1,shell_index=0,origin=[0.,0.,0.],velocity=[0.,0.,100.],range_origin=[0.,0.,0.],segment_origin=[0.,0.,0.],segment_velocity=[0.,0.,100.],gravity=10.,max_distance=500.,max_time_ms=20000,is_he=False,splash_radius=0.,penetration_factor=1.,launch_server_time_ms=0,segment_start_time_ms=0,ricochet_count=0,base_penetration_multiplier=1.)
row['source_shot']=dict(speed=100.,gravity=10.,maxDistance=500.,piercingPower=[100.,80.],deadeye=False,shell=dict(kind='ARMOR_PIERCING',caliber=100.,damage=[200.,80.],explosionRadius=0.))
value=native.parse(row);assert value['shooter']==4 and value['damage']==200 and value['device_damage']==80 and value['segment']==0
same=copy.deepcopy(row);same['source_shot']['speed']=100;assert native.parse(same)['signature']==value['signature']
for kind,he in [('ARMOR_PIERCING',False),('ARMOR_PIERCING_HE',False),('ARMOR_PIERCING_CR',False),('HOLLOW_CHARGE',False),('HIGH_EXPLOSIVE',True)]:
    test=copy.deepcopy(row);test['source_shot']['shell']['kind']=kind;test['is_he']=he
    assert native.parse(test)
for field,bad in [('burst_index',1),('burst_count',65),('shot_seq',9),('gravity',float('nan')),('penetration_factor',-1.),('max_time_ms',20001),('origin',[0.,0.]),('fire_intent_seq',5),('bot_presentation_time_us',-1),('is_he',1)]:
    test=copy.deepcopy(row);test[field]=bad
    try:native.parse(test)
    except (ValueError,TypeError):pass
    else:raise AssertionError((field,bad))
class Number(object):
    def __float__(self):raise AssertionError('Python conversion callback ran')
test=copy.deepcopy(row);test['gravity']=Number()
try:native.parse(test)
except ValueError:pass
else:raise AssertionError('nonplain numeric admitted')
player=copy.deepcopy(row);player['shooter_kind']='player';assert native.parse(player) is None
bounce=copy.deepcopy(row);bounce.update(ricochet_count=1,segment_start_time_ms=200,checked_through_ms=200,segment_velocity=[0.,0.,-100.],base_penetration_multiplier=.75);assert native.parse(bounce)['segment']==1
rows=native.packed();assert len(rows)==1 and rows[0][:2]==(13,'resolve')
w=rows[0][2];assert w['authority_epoch']==7 and w['impact']==[1.,2.,3.] and w['hit_vehicle']
e=w['direct'];assert e['critical_target_base_revision']==15 and e['critical_target_ack_seq']==9 and e['hull_damage']==11
assert e['critical_delta']==dict(devices=[dict(name='engineHealth',hp_loss=12.346)],crew_ko=['loader1'],ignite=True)
assert e['critical']['crew_roster']==['commander','driver','loader1','gunner1']
print('CPython 2.7 projectile ingress and publication passed')
'''
@unittest.skipUnless(os.environ.get('WOT_FORK_0101_SOURCE'), 'fixed gameplay source is required')
class NativeProjectileWire(unittest.TestCase):
    def test_wire(self):
        fork=Path(os.environ['WOT_FORK_0101_SOURCE'])
        py27=subprocess.check_output(['pyenv','which','python2.7'],env=dict(os.environ,PYENV_VERSION='2.7.18'),text=True).strip()
        prefix=subprocess.check_output([py27,'-c','import sys;print(sys.prefix)'],text=True).strip()
        names=['stock_native_projectile_wire','stock_native_projectile_owner','stock_native_projectile_cursor','stock_native_projectile_history','stock_native_projectile_chord','stock_native_projectile_damage','stock_native_bot_armor_law','stock_native_bot_combat']
        with tempfile.TemporaryDirectory(prefix='native-projectile-wire-') as folder:
            folder=Path(folder);binary=folder/'projectile_wire_test.so';script=folder/'fixture.py';script.write_text(SCRIPT)
            command=['c++','-std=c++17','-O0','-ffp-contract=off','-Wall','-Wextra','-Werror','-bundle','-undefined','dynamic_lookup','-isystem',str(Path(prefix)/'include/python2.7'),'-I'+str(ROOT/'bigworld'),'-I'+str(fork/'native'),str(ROOT/'tests/stock_native_projectile_wire_host.cpp')]+[str(ROOT/'bigworld'/(name+'.cpp')) for name in names]+['-o',str(binary)]
            subprocess.run(command,check=True);subprocess.run([py27,'-B',str(script)],check=True)
if __name__=='__main__':unittest.main()
