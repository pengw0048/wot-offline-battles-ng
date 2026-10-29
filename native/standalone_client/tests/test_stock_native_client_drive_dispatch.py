"""Whole-frame publication order and accumulator match the fixed caller."""
import os
from pathlib import Path
import subprocess
from test_stock_native_client_drive_contacts import ContactDifferential,ROOT,SOURCE

class PlayerDispatchDifferential(ContactDifferential):
 def test_outer_publication_order(self):
  folder=Path(self.folder.name);py27=subprocess.check_output(['pyenv','which','python2.7'],env=dict(os.environ,PYENV_VERSION='2.7.18'),text=True).strip();prefix=subprocess.check_output([py27,'-c','import sys;print(sys.prefix)'],text=True).strip()
  command=['c++','-std=c++17','-bundle','-undefined','dynamic_lookup','-ffp-contract=off','-Wall','-Wextra','-Werror','-Wno-unused-parameter','-Wno-register','-isystem',str(Path(prefix)/'include/python2.7'),'-I'+str(folder),'-I'+str(ROOT/'bigworld'),'-I'+str(ROOT/'tests'),'-I'+str(Path(SOURCE)/'native'),str(ROOT/'tests/stock_native_client_drive_dispatch_host.cpp'),str(ROOT/'bigworld/stock_native_client_drive_runtime.cpp'),str(ROOT/'bigworld/stock_native_bot_motion.cpp'),'-o',str(folder/'client_dispatch_test.so')]
  compiled=subprocess.run(command,capture_output=True,text=True);self.assertEqual(compiled.returncode,0,compiled.stderr)
  fixture=r'''
import ast,math
import client_dispatch_test as native
source=SOURCE+'/src/res/scripts/client/gui/mods/offline_lan_0922/battle_runtime.py';tree=ast.parse(open(source).read());outer=next(n for n in tree.body if isinstance(n,ast.ClassDef)and n.name=='BattleRuntime');names=('_drive_local','_flush_landing_observation','_send_local_siege_request');outer.body=[n for n in outer.body if isinstance(n,ast.FunctionDef)and n.name in names]
for node in outer.body:node.decorator_list=[]
module=ast.Module(body=[outer]);ast.fix_missing_locations(module);ns=dict(NETWORK_INPUT_SECONDS=.1,_INTEGER_TYPES=(int,long));exec(compile(module,source,'exec'),ns)
class O(object):
 def __init__(self,**kw):self.__dict__.update(kw)
count=0
for elapsed in (0.,.02,.09,.15,.31,.99):
 for siege in (-1,0,1):
  for landing in (0,1):
   for barrier in (0,1):
    for stopped in (0,1):
     for mask in (0,1,2,3,7,15,63):
      # New durable barriers intentionally fence later inputs when admission
      # fails. Compare the source's successful barrier path independently.
      if barrier and (stopped or not(mask&1)):continue
      old=ns['BattleRuntime']();log=[];index=[0];old._input_accumulator=.07;old._local_siege_braking=None if siege<0 else bool(siege);old._local_siege_pending=None;old._local_speed=old._local_turn_speed=0.;old._pending_landing_impacts=[12.]if landing else [];old._local_damage_report=None;old._drown_level=old._overturn_level=0;old._server=O(vehicle_id=3);old._server_entity=lambda value:O(siegeState=0);old.client=O(_input_seq=10)
      def operation(value):
       log.append(value);accepted=bool(mask&(1<<index[0]));index[0]+=1;return accepted
      def send(siege_enabled=None):
       accepted=operation(siege_enabled)
       if accepted:old.client._input_seq+=1
       return accepted
      old._sender=O(send_current=send);old.client.send_landing_observation=lambda *args:operation('landing');old._report_local_siege_edge=lambda *args,**kwargs:None;first=[True]
      def step(dt):
       if first[0] and barrier:old._local_input_sent_during_drive=send()
       first[0]=False
       return bool(stopped)
      old._drive_local_step=step;old._drive_local(elapsed)
      result=native.run(elapsed,siege,landing,barrier,stopped,mask);pending=old._local_siege_pending[0]if old._local_siege_pending is not None else None
      assert result[0]==log,(elapsed,siege,landing,barrier,stopped,mask,result,log)
      assert abs(result[1]-old._input_accumulator)<1e-12,(elapsed,siege,landing,barrier,stopped,mask,result,old._input_accumulator)
      assert result[2] is pending and result[3] is old._local_siege_braking,(result,old._local_siege_pending,old._local_siege_braking)
      count+=1
# An unadmitted barrier cannot be overtaken by a mode/current/landing input.
failed=native.run(.3,1,1,1,0,0);assert failed[0]==[None] and failed[4]==1,failed
print('%d whole-frame publication cases and durable barrier failure passed'%count)
'''
  fixture=fixture.replace('SOURCE',repr(str(SOURCE)));result=subprocess.run([py27,'-B','-c',fixture],cwd=folder,capture_output=True,text=True);self.assertEqual(result.returncode,0,result.stdout+result.stderr)
