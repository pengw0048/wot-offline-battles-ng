"""Production native relays must preserve the exact Python 2 stock chat contract."""
import os
from pathlib import Path
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
SOURCE = os.environ.get('WOT_FORK_0101_SOURCE')
FIXTURE = r'''
# -*- coding: utf-8 -*-
from __future__ import unicode_literals
import imp,sys,types,struct
import battle_radio_test as native
for name in ('gui','gui.mods','gui.mods.offline_lan_0922'):
 package=types.ModuleType(str(name));package.__path__=[];sys.modules[name]=package
radio=imp.load_source(str('gui.mods.offline_lan_0922.tactical_radio'),sys.argv[1])
class O(object):
 def __init__(self,**kw):self.__dict__.update(kw)
actions=[]
avatar=O(team=1,playerVehicleID=101,arena=O(vehicles={101:dict(team=1,isAlive=True,accountDBID=9001)}),messenger_onActionByServer_chat2=lambda *args:actions.append(args))
adapter=radio.BattleRadioAdapter(avatar,None,player_getter=lambda:avatar,users_ready_notifier=lambda:None)
assert adapter.start_team_chat()
actions[:]=[]
def chat(seq,text,**kw):
 row=dict(type='team_chat',round_id=1,team=1,issuer_id=1,chat_seq=seq,text=text);row.update(kw);return row
texts=['ASCII hello','注意 E5，跟我来。','中文 <tag> & English','emoji \U0001f600']
rows=[chat(i+1,text) for i,text in enumerate(texts)]
rows += [chat(4,'duplicate'),chat(5,'old round',round_id=2),chat(5,'enemy team',team=2),chat(5,'unknown issuer',issuer_id=99),chat(5,b'\xff'),chat(5,34),chat(5,'   '),chat(5,'x'*251),chat(5,'valid after rejection')]
result=native.relay(avatar,adapter,rows)
assert result==[True]*4+[False]*8+[True],result
messages=[args['strArg1'] for action,request,args in actions if action==radio.TEAM_CHAT_RECEIVE_ACTION_ID]
assert messages==texts+['valid after rejection'],messages
assert all(isinstance(text,unicode) for text in messages)
actions[:]=[]
assert native.relay(avatar,adapter,[chat(1,'worker')],1)==[False]
assert actions==[]
# Commands retain their numeric/byte contracts; no broad Unicode conversion is
# applied to enums or the SPG struct payload used by the exact stock reader.
rows=[dict(type='team_command',round_id=1,team=1,issuer_id=1,command_seq=1,command='ATTENTIONTOCELL',cell_index=45),dict(type='team_command',round_id=1,team=1,issuer_id=1,command_seq=2,command='POSITIVE',cell_index=None)]
rows += [dict(type='team_command',round_id=1,team=1,issuer_id=1,command_seq=3,command='ATTENTIONTOCELL',cell_index=100),dict(type='team_command',round_id=1,team=1,issuer_id=1,command_seq=3,command='SPG_AIM_AREA',cell_index=45,aim_point=[1.0,2.0,3.0],reload_time=4.5)]
assert native.relay(avatar,adapter,rows)==[True,True,False,True]
assert actions[0][0]==radio.COMMAND_IDS_BY_NAME['ATTENTIONTOCELL'] and actions[0][2]['int32Arg1']==45
assert actions[1][0]==radio.COMMAND_IDS_BY_NAME['POSITIVE']
assert actions[2][0]==radio.COMMAND_IDS_BY_NAME['SPG_AIM_AREA']
assert isinstance(actions[2][2]['strArg1'],str)
assert struct.unpack('<fffif',actions[2][2]['strArg1'])==(1.0,2.0,3.0,45,4.5)
# Execute the exact #1513 command formatter and MO translations through the
# real relay's received action arguments, then the production Flash converter.
if len(sys.argv)>2 and sys.argv[2]:
 import marshal,zipfile,gettext
 from os.path import join
 archive=zipfile.ZipFile(join(sys.argv[2],'res/packages/scripts.pkg'))
 def function(member,owner,name,namespace):
  root=marshal.loads(archive.read(member)[8:])
  cls=next(item for item in root.co_consts if isinstance(item,types.CodeType) and item.co_name==owner) if owner else root
  code=next(item for item in cls.co_consts if isinstance(item,types.CodeType) and item.co_name==name)
  namespace.setdefault('__builtins__',__builtins__)
  return types.FunctionType(code,namespace)
 translator=gettext.GNUTranslations(open(join(sys.argv[2],'res/text/LC_MESSAGES/ingame_gui.mo'),'rb'))
 namespace=dict(g_translators={'ingame_gui':translator},LOG_CURRENT_EXCEPTION=lambda:None,LOG_WARNING=lambda *args:None)
 make_string=function('scripts/client/helpers/i18n.pyc',None,'makeString',namespace)
 get_text=function('scripts/client/messenger/proto/bw_chat2/battle_chat_cmd.pyc','_ReceivedCmdDecorator','getCommandText',dict(
  _ACTIONS=O(battleChatCommandFromActionID=lambda identity:O(msgText='attention_to_cell' if identity==radio.COMMAND_IDS_BY_NAME['ATTENTIONTOCELL'] else 'positive')),
  I18N_INGAME_GUI=O(chat_shortcuts=lambda name:'#ingame_gui:chat_shortcuts/'+name),i18n=O(makeString=make_string),LOG_ERROR=lambda *args:None))
 rendered=[]
 colors=O(BLACK=1,RED=2,BROWN=3,GREEN=4)
 add_message=function('scripts/client/messenger/gui/Scaleform/view/battle/messenger_view.pyc','BattleMessengerView','addMessage',dict(FILL_COLORS=colors))
 self_message=function('scripts/client/gui/Scaleform/daapi/view/meta/BattleMessengerMeta.pyc','BattleMessengerMeta','as_showSelfMessageS',{})
 black_message=function('scripts/client/gui/Scaleform/daapi/view/meta/BattleMessengerMeta.pyc','BattleMessengerMeta','as_showBlackMessageS',{})
 def flash_message(text,identity):
  rendered.append((native.flash_utf8(text).decode('utf-8'),identity))
 view=O(_BattleMessengerView__isInTimeWarp=False,_accDbID=9001,_addedMsgIDs=set(),_isDAAPIInited=lambda:True,flashObject=O(as_showSelfMessage=flash_message,as_showBlackMessage=flash_message))
 view.as_showSelfMessageS=lambda text,identity:self_message(view,text,identity)
 view.as_showBlackMessageS=lambda text,identity:black_message(view,text,identity)
 for action,request,args in actions[:2]:
  instance=O(_commandID=action,_protoData=args,isOnMinimap=lambda:action==radio.COMMAND_IDS_BY_NAME['ATTENTIONTOCELL'],isSPGAimCommand=lambda:False,_getCellName=lambda unused=None:b'E5',hasTarget=lambda:False)
  text=get_text(instance)
  assert isinstance(text,unicode)
  own=action==radio.COMMAND_IDS_BY_NAME['ATTENTIONTOCELL']
  add_message(view,text,colors.BROWN if own else colors.BLACK,9001 if own else 9002)
 assert rendered==[('注意E5！',0),('遵命！',9002)],rendered
 assert view._addedMsgIDs=={9002}
 print('Exact #1513 ping/command formatting, BattleMessenger view/meta and Flash UTF-8 passed')
print('Native chat Unicode relay, exclusions, rejection recovery and stock commands passed')
'''


@unittest.skipUnless(SOURCE, 'set WOT_FORK_0101_SOURCE to the fixed gameplay checkout')
class NativeBattleRadioTests(unittest.TestCase):
    def test_native_radio_uses_the_python27_plaintext_contract(self):
        py27 = subprocess.check_output(['pyenv', 'which', 'python2.7'], text=True,
            env=dict(os.environ, PYENV_VERSION='2.7.18')).strip()
        prefix = subprocess.check_output([py27, '-c', 'import sys;print(sys.prefix)'], text=True).strip()
        with tempfile.TemporaryDirectory(prefix='battle-radio-') as directory:
            folder = Path(directory)
            command = ['c++', '-std=c++17', '-O1', '-DWOT_NATIVE_BATTLE_HOST_BUILD',
                '-DWOT_CLIENT_REPLICA_HOST_BUILD', '-Wno-register', '-bundle', '-undefined',
                'dynamic_lookup', '-Wl,-dead_strip', '-Wl,-exported_symbol,_initbattle_radio_test',
                '-isystem', str(Path(prefix) / 'include/python2.7'), '-I' + str(ROOT / 'bigworld'),
                '-I' + str(Path(SOURCE) / 'native'), str(ROOT / 'tests/stock_native_battle_radio_host.cpp'),
                str(ROOT / 'bigworld/stock_native_battle_startup_session.cpp'),
                str(ROOT / 'bigworld/stock_native_client_state.cpp'), '-o', str(folder / 'battle_radio_test.so')]
            result = subprocess.run(command, capture_output=True, text=True)
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
            tactical = Path(SOURCE) / 'src/res/scripts/client/gui/mods/offline_lan_0922/tactical_radio.py'
            result = subprocess.run([py27, '-B', '-c', FIXTURE, str(tactical), os.environ.get('WOT_0922_CLIENT', '')], cwd=folder,
                                    capture_output=True, text=True)
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
