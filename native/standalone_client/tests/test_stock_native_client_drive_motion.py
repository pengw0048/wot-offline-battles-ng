"""Player proposal merging, native recasts and reliable producing poses."""
import os
from pathlib import Path
import subprocess
from test_stock_native_client_drive_contacts import ContactDifferential,ROOT,SOURCE
class PlayerMotionIntegration(ContactDifferential):
 def test_component_precedence_and_scene_barriers(self):
  folder=Path(self.folder.name);py27=subprocess.check_output(['pyenv','which','python2.7'],env=dict(os.environ,PYENV_VERSION='2.7.18'),text=True).strip();prefix=subprocess.check_output([py27,'-c','import sys;print(sys.prefix)'],text=True).strip();big=ROOT/'bigworld';native=Path(SOURCE)/'native'
  fixture=(ROOT/'tests/test_stock_native_world_pipeline.cpp').read_text().split('\nint main(',1)[0];(folder/'pipeline_fixture.hpp').write_text(fixture)
  command=['c++','-std=c++17','-bundle','-undefined','dynamic_lookup','-ffp-contract=off','-Wall','-Wextra','-Werror','-Wno-unused-function','-Wno-unused-parameter','-Wno-register','-isystem',str(Path(prefix)/'include/python2.7'),'-I'+str(folder),'-I'+str(big),'-I'+str(native),str(ROOT/'tests/stock_native_client_drive_motion_host.cpp'),str(big/'stock_native_world_pipeline.cpp'),str(big/'stock_motion_rotation_batch.cpp'),str(big/'stock_offline_query_skin.cpp'),str(native/'offline_destructible_geometry.cpp'),str(native/'world_stage.cpp'),'-o',str(folder/'client_motion_test.so')]
  compiled=subprocess.run(command,capture_output=True,text=True);self.assertEqual(compiled.returncode,0,compiled.stderr)
  script=r'''
import ast
import client_motion_test as native
path=SOURCE+'/src/res/scripts/client/gui/mods/offline_lan_0922/battle_runtime.py';tree=ast.parse(open(path).read());outer=next(n for n in tree.body if isinstance(n,ast.ClassDef)and n.name=='BattleRuntime');outer.body=[n for n in outer.body if isinstance(n,ast.FunctionDef)and n.name in ('_merge_destructible_motion_proposals','_consumed_local_tree_detail')];module=ast.Module(body=[outer]);ast.fix_missing_locations(module);ns=dict(DESTRUCTIBLE_CONTACT_TOKEN_LIMIT=64);exec(compile(module,path,'exec'),ns);old=ns['BattleRuntime']();old._destructible_contact_token=lambda rows:tuple(tuple(v)for v in rows)
names=('clear','crushed','approach','kinetic','hard','pending');count=0
for a in range(6):
 for b in range(6):
  for required in (False,True):
   consumed=b==1
   catalog=dict(status=names[a],requires_commit=required,token=((1,7,None),),accepted_now=False,kinds='fragile')
   tree=dict(status=names[b],requires_commit=b==1,token=((1,8,None),),accepted_now=False,kinds='tree')
   sanitized=old._consumed_local_tree_detail(tree,{}if consumed else None);result=old._merge_destructible_motion_proposals(catalog,sanitized);actual=native.merge(a,b,required,consumed)
   expected=(names.index(result['status']),int(result['requires_commit']),int(result['accepted_now']),len(result['token']or()))
   assert actual==expected,(a,b,required,actual,expected)
   count+=1
assert native.integration()is True
print('%d fixed proposal combinations, native backing walls, failed effects, pending stream and frozen barriers passed'%count)
'''.replace('SOURCE',repr(str(SOURCE)))
  result=subprocess.run([py27,'-B','-c',script],cwd=folder,capture_output=True,text=True);self.assertEqual(result.returncode,0,result.stdout+result.stderr)
