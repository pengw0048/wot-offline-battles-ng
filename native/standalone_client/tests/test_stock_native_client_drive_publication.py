"""Durable local contact queues and exact CPython 2.7 decimal receipts."""
import os
from pathlib import Path
import subprocess
import unittest
from test_stock_native_client_drive_contacts import ContactDifferential,ROOT,SOURCE

FIXTURE=r'''
import ast,math,random,sys,collections
import client_drive_publication_test as native
source=sys.argv[1]+'/src/res/scripts/client/gui/mods/offline_lan_0922/battle_runtime.py';tree=ast.parse(open(source).read());names=set(('_destructible_contact_token','_queue_local_destructible_contact','local_destructible_contacts','_ack_local_destructible_contacts'))
outer=next(node for node in tree.body if isinstance(node,ast.ClassDef)and node.name=='BattleRuntime');outer.body=[node for node in outer.body if isinstance(node,ast.FunctionDef)and node.name in names];module=ast.Module(body=[outer]);ast.fix_missing_locations(module);ns=dict(math=math,DESTRUCTIBLE_CONTACT_TOKEN_LIMIT=64,DESTRUCTIBLE_CONTACT_WINDOW=16,DESTRUCTIBLE_CONTACT_INFLIGHT_LIMIT=64);exec(compile(module,source,'exec'),ns)
class O(object):pass
old=ns['BattleRuntime']();old._local_destructible_contacts=collections.OrderedDict();old._local_destructible_safe_poses={};old._local_destructible_contact_seq=0;old._local_destructible_admitted_seq=0;old.client=O();old.client.player_id=1;old._apply_local_destructible_rejection=lambda seq:True
owner=native.create();rng=random.Random(144619)
for i in range(400):
 start=tuple(rng.uniform(-800.,800.)for _ in range(3));end=tuple(v+rng.uniform(-.3,.3)for v in start);yaw=rng.uniform(-math.pi,math.pi);end_yaw=yaw+rng.uniform(-.1,.1);speed=rng.choice([2.675,1.23445,-1.23445,199.99995,0.,rng.uniform(-400.,400.)]);dt=rng.choice([.0000005,.0123455,.1,0.,-.1,.2,rng.uniform(0.,.1)]);token=[[i%40,i%13,None if i%2 else 0],[i%40,i%13,None if i%2 else 0]]
 expected=old._queue_local_destructible_contact(dict(requires_commit=True,token=token),start,yaw,speed,dt,end_position=end,end_yaw=end_yaw);accepted,rows=native.queue(owner,start,yaw,end,end_yaw,speed,dt,token)
 assert accepted==expected,(i,accepted,expected)
 assert rows==old.local_destructible_contacts(),(i,rows,old.local_destructible_contacts())
 if i%11==0:
  admitted=old._local_destructible_contact_seq;resolved=max(0,admitted-5);exact=[admitted]if admitted>resolved else [];rejected=exact
  old._ack_local_destructible_contacts(dict(players=[dict(id=1,destructible_contact_admitted_seq=admitted,destructible_contact_resolved_seq=resolved,destructible_contact_resolved_seqs=exact,destructible_contact_rejected_seqs=rejected)]))
  rows=native.ack(owner,admitted,resolved,exact,rejected);assert rows==old.local_destructible_contacts(),(i,rows,old.local_destructible_contacts())
# A bad frontier cannot consume valid pending events.
before=rows
for admitted,resolved,exact,rejected in [(-1,0,[],[]),(10000,0,[],[]),(1,2,[],[]),(old._local_destructible_contact_seq,0,[1,1],[])]:
 assert native.ack(owner,admitted,resolved,exact,rejected)==before
print('native durable contacts and CPython2 receipt rounding differential passed: 400 events')
'''
class PlayerPublicationDifferential(ContactDifferential):
 def test_durable_contact_original_law(self):
  folder=Path(self.folder.name);py27=subprocess.check_output(['pyenv','which','python2.7'],env=dict(os.environ,PYENV_VERSION='2.7.18'),text=True).strip();prefix=subprocess.check_output([py27,'-c','import sys;print(sys.prefix)'],text=True).strip()
  result=subprocess.run(['c++','-std=c++17','-Wno-register','-Wall','-Wextra','-Werror','-Wno-unused-parameter','-bundle','-undefined','dynamic_lookup','-isystem',str(Path(prefix)/'include/python2.7'),'-I'+str(folder),'-I'+str(ROOT/'bigworld'),'-I'+str(Path(SOURCE)/'native'),str(ROOT/'tests/stock_native_client_drive_publication_host.cpp'),'-o',str(folder/'client_drive_publication_test.so')],capture_output=True,text=True);self.assertEqual(result.returncode,0,result.stderr)
  (folder/'publication_fixture.py').write_text(FIXTURE);result=subprocess.run([py27,'-B',str(folder/'publication_fixture.py'),SOURCE],cwd=folder,capture_output=True,text=True);self.assertEqual(result.returncode,0,result.stdout+result.stderr)
if __name__=='__main__':unittest.main()
