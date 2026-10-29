"""Check the native publication boundary against the pinned launch consumer."""
import os
from pathlib import Path
import subprocess
import tempfile
import unittest
ROOT=Path(__file__).resolve().parents[1]
FIXTURE=r'''
import sys,types
import publication_test as native
source=sys.argv[1]+'/src/res/scripts/client/gui/mods/offline_lan_0922/'
namespace={}
code=open(source+'native_weapons.py').read()
start=code.index('def _proof_value(')
end=code.index('\n\nclass NativeWeapons',start)
namespace.update(sys=sys,TARGET_KINDS=('', 'bot', 'human'))
exec(code[code.index('def _edge('):code.index('def _edge_row(')],namespace)
exec(code[start:end],namespace)
launch=namespace['_launch_value']((1,1,(9,8,1,3,2,0,.03),95,(1.,2.,3.,.1,.2,.3),.4,.5,tuple(bytearray('SPG')),(4.,5.,6.),(7.,8.,9.),(9.81,1200.,8000),1,15,(1,2,4,2,9,4,5,6,.4,.5,500,9.81,1200,2.2)))
result=native.publish()
assert [r['type'] for r in result]==['bot_state','bot_ram','bot_ram','bot_observation']
state=result[0]
assert state['launches'][0]==launch,(state['launches'][0],launch)
assert state['rows']==[[1,2,3]] and state['source_batch_horizon_us']==100
assert state['push_contacts']==dict(contacts=[dict(seq=5,attacker_kind='human',attacker_id=2,target_kind='bot',target_id=1)],hazards=[1],falls=[[1,20]],critical_totals=[[1,'collision',4,2]],environment_fires=[2])
assert result[1]==dict(type='bot_ram',bot_id=1,target_kind='bot',target_id=2,ram_seq=8,damage_to_bot=4,damage_to_target=6,track_contacts_bot=['leftTrackHealth'],track_contacts_target=[],track_impact_speed=7.,contact_positions=[1.,2.,3.,4.])
assert result[2]['ram_contact_player_id']==2 and result[2]['ram_contact_seq']==12
assert 'contact_positions' not in result[2]
obs=result[3]
assert obs['player_vision_ranges']==[dict(id=2,radius=350.)]
assert obs['radio_links']==[dict(kind='human',id=2,allies=[dict(kind='bot',id=1)])]
assert obs['contacts'][0]==dict(observing_team=1,target_team=2,target_kind='human',target_id=2,visible=True,fresh=True,time_left=.5,radio_recipients=[dict(kind='bot',id=1,time_left=.4)],visible_by_player_ids=[],visible_by_bot_ids=[1],shootable_by_bot_ids=[1],threatened_bot_ids=[],x=10.,y=20.,z=30.,health=50,max_health=100,class_tag='heavyTank',armor=80.)
print('native publication boundary passed')
'''
class PublicationTests(unittest.TestCase):
    def test_fixed_launch_and_complete_event_shapes(self):
        source=os.environ.get('WOT_FORK_0101_SOURCE')
        if not source:self.skipTest('fixed gameplay source is required')
        py27=subprocess.check_output(['pyenv','which','python2.7'],text=True,env=dict(os.environ,PYENV_VERSION='2.7.18')).strip()
        prefix=subprocess.check_output([py27,'-c','import sys;print(sys.prefix)'],text=True).strip()
        with tempfile.TemporaryDirectory(prefix='bot-publication-') as directory:
            folder=Path(directory);script=folder/'test.py';script.write_text(FIXTURE)
            law=(Path(source)/'native/offline_release_drive_python.inc').read_text().split('PyObject *WOT_CDECL release_drive(')[0]
            (folder/'offline_release_drive_law.hpp').write_text('#pragma once\n#include <algorithm>\n#include <cmath>\n'+law)
            command=['c++','-std=c++17','-O1','-Wall','-Wextra','-Werror','-bundle','-undefined','dynamic_lookup','-isystem',str(Path(prefix)/'include/python2.7'),'-I'+str(folder),'-I'+str(Path(source)/'native'),'-I'+str(ROOT/'bigworld'),str(ROOT/'bigworld/stock_native_bot_publication_python.cpp'),str(ROOT/'tests/stock_native_bot_publication_python_host.cpp'),'-o',str(folder/'publication_test.so')]
            built=subprocess.run(command,text=True,capture_output=True);self.assertEqual(built.returncode,0,built.stderr)
            run=subprocess.run([py27,'-B',str(script),source],text=True,capture_output=True);self.assertEqual(run.returncode,0,run.stderr)
            self.assertIn('native publication boundary passed',run.stdout)
if __name__=='__main__':unittest.main()
